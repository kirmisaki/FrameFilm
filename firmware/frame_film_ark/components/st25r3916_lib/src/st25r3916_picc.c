/*
 * SPDX-License-Identifier: MIT
 *
 * NFC-A card emulation (PICC).
 *
 * The anticollision/selection is answered by the chip's passive target logic
 * (UID/ATQA/SAK live in PT memory). This module runs the ISO14443-3 listener
 * state machine around it: field on/off, halt/wake-up and the application
 * level frames after selection.
 *
 * A MIFARE Classic presented through this API behaves like an ID card: the
 * reader sees UID/ATQA/SAK, data commands are not answered unless the
 * application handles them in its callback.
 */
#include "st25r3916_priv.h"

#include <string.h>

#include "esp_intr_alloc.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "st25r3916/picc";

#define ST25R_IRQ_DEFAULT_MASK                                                                \
    (ST25R_IRQ_NFCT | ST25R_IRQ_RXS | ST25R_IRQ_EON | ST25R_IRQ_EOF | ST25R_IRQ_CRC |        \
     ST25R_IRQ_ERR1 | ST25R_IRQ_ERR2 | ST25R_IRQ_PAR)
#define ST25R_IRQ_MODE_MASK (ST25R_IRQ_WU_A | ST25R_IRQ_WU_AX | ST25R_IRQ_RXE_PTA)

/* ------------------------------------------------------------------------- */
/* Interrupt bookkeeping                                                     */
/* ------------------------------------------------------------------------- */
static void irq_refresh(st25r_handle_t h)
{
    if (!st25r_irq_asserted(h)) {
        return;
    }
    uint32_t v = 0;
    if (st25r_irq_read(h, &v)) {
        h->emu.irq_pending |= v;
    }
}

static uint32_t irq_take(st25r_handle_t h, uint32_t bits)
{
    const uint32_t got = h->emu.irq_pending & bits;
    h->emu.irq_pending &= ~got;
    return got;
}

static bool extra_field(st25r_handle_t h)
{
    uint8_t v = 0;
    return st25r_reg_read8(h, ST25R_REG_AUX_DISPLAY, &v) && (v & ST25R_AUX_DISP_EFD_O);
}

static void emit(st25r_handle_t h, st25r_picc_event_t evt, const uint8_t *data, uint16_t len)
{
    if (h->emu.cb) {
        (void)h->emu.cb(h, evt, data, len, h->emu.ctx);
    }
}

static void set_state(st25r_handle_t h, st25r_picc_state_t next)
{
    if (next == h->emu.state) {
        return;
    }
    const st25r_picc_state_t prev = h->emu.state;
    h->emu.state = next;
    if (next == ST25R_PICC_OFF && prev != ST25R_PICC_OFF) {
        emit(h, ST25R_PICC_EVT_FIELD_OFF, NULL, 0);
    }
    if (next == ST25R_PICC_IDLE && prev == ST25R_PICC_OFF) {
        emit(h, ST25R_PICC_EVT_FIELD_ON, NULL, 0);
    }
    ESP_LOGD(TAG, "state %d -> %d", (int)prev, (int)next);
}

/* ------------------------------------------------------------------------- */
/* State transitions                                                         */
/* ------------------------------------------------------------------------- */
static st25r_picc_state_t goto_off(st25r_handle_t h)
{
    h->emu.irq_pending = 0;
    h->emu.bitrate = 0; /* 106 kbit/s, the only rate this library emulates */
    h->emu.wakeup = false;

    st25r_cmd(h, ST25R_CMD_STOP_ALL_ACTIVITIES);
    st25r_reg_set(h, ST25R_REG_OP_CONTROL, ST25R_OP_RX_EN);

    /* Auto response on for NFC-A, target logic back to sense. */
    st25r_reg_clear(h, ST25R_REG_NFCIP1_PT, ST25R_PT_D_106_AC_A);
    st25r_cmd(h, ST25R_CMD_GO_TO_SENSE);
    st25r_reg_clear(h, ST25R_REG_ISO14443A, ST25R_ISO_NFC_F0);

    st25r_irq_mask_all(h, 0xFFFFFFFFu);
    st25r_irq_clear(h);
    st25r_irq_enable(h, ST25R_IRQ_OSC | ST25R_IRQ_DEFAULT_MASK | ST25R_IRQ_MODE_MASK);

    st25r_reg_set(h, ST25R_REG_AUX_DEF, ST25R_AUX_NO_CRC_RX);
    /* ISO14443A with bit rate detection while waiting for a field. */
    st25r_reg_modify(h, ST25R_REG_MODE, ST25R_MODE_BITRATE_DETECTION, ST25R_MODE_OM_MASK);

    if (extra_field(h)) {
        set_state(h, ST25R_PICC_IDLE);
        return ST25R_PICC_IDLE;
    }
    /* No external field: power the analog front end down. */
    st25r_reg_modify(h, ST25R_REG_OP_CONTROL, 0,
                     ST25R_OP_TX_EN | ST25R_OP_RX_EN | ST25R_OP_EN);
    set_state(h, ST25R_PICC_OFF);
    return ST25R_PICC_OFF;
}

static st25r_picc_state_t goto_idle(st25r_handle_t h)
{
    uint8_t v = 0;
    if (!st25r_reg_read8(h, ST25R_REG_OP_CONTROL, &v)) {
        return h->emu.state;
    }
    if ((v & ST25R_OP_EN) == 0) {
        st25r_reg_set(h, ST25R_REG_OP_CONTROL, ST25R_OP_EN | ST25R_OP_RX_EN);
        uint8_t aux = 0;
        if (st25r_reg_read8(h, ST25R_REG_AUX_DISPLAY, &aux) && (aux & ST25R_AUX_DISP_OSC_OK) == 0) {
            if ((st25r_wait_irq(h, ST25R_IRQ_OSC, 1000) & ST25R_IRQ_OSC) == 0) {
                ESP_LOGW(TAG, "oscillator not ready, back to off");
                return goto_off(h);
            }
        }
    } else {
        irq_take(h, ST25R_IRQ_OSC);
    }

    /* goto_off() 把 MODE 复位成"速率检测"并关掉前端；每次场到来都重新落一遍
       监听/调制参数，否则下一次读卡器场不会被自动应答。 */
    st25r_reg_write8(h, ST25R_REG_MODE, ST25R_MODE_LISTEN_NFC_A);

    st25r_reg_set(h, ST25R_REG_AUX_DEF, ST25R_AUX_NO_CRC_RX);

    if (h->emu.state == ST25R_PICC_ACTIVE && !h->emu.wakeup) {
        st25r_reg_clear(h, ST25R_REG_NFCIP1_PT, ST25R_PT_D_106_AC_A);
        st25r_cmd(h, ST25R_CMD_GO_TO_SENSE);
    }

    st25r_cmd(h, ST25R_CMD_CLEAR_FIFO);
    st25r_cmd(h, ST25R_CMD_UNMASK_RECEIVE_DATA);
    h->emu.wakeup = false;

    set_state(h, ST25R_PICC_IDLE);
    return ST25R_PICC_IDLE;
}

static st25r_picc_state_t goto_ready(st25r_handle_t h)
{
    if (irq_take(h, ST25R_IRQ_EOF)) {
        return goto_off(h);
    }

    st25r_reg_clear(h, ST25R_REG_AUX_DEF, ST25R_AUX_NO_CRC_RX);

    const uint8_t br = (uint8_t)h->emu.bitrate;
    st25r_reg_write8(h, ST25R_REG_BITRATE, (uint8_t)((br << 4) | br));

    st25r_reg_clear(h, ST25R_REG_OP_CONTROL, ST25R_OP_WU);
    /* Fixed 106 kbit/s listening mode, collision resolution by the chip. */
    st25r_reg_write8(h, ST25R_REG_MODE, ST25R_MODE_LISTEN_NFC_A);

    set_state(h, ST25R_PICC_READY);
    return ST25R_PICC_READY;
}

static st25r_picc_state_t goto_active(st25r_handle_t h)
{
    /* From here on the application answers, so disable the automatic response. */
    st25r_reg_set(h, ST25R_REG_NFCIP1_PT, ST25R_PT_D_106_AC_A);
    irq_take(h, ST25R_IRQ_PAR | ST25R_IRQ_CRC | ST25R_IRQ_ERR2 | ST25R_IRQ_ERR1);
    st25r_irq_enable(h, ST25R_IRQ_RXE);

    const bool was_selected = (h->emu.state == ST25R_PICC_READY);
    set_state(h, ST25R_PICC_ACTIVE);
    if (was_selected) {
        emit(h, ST25R_PICC_EVT_SELECTED, NULL, 0);
    }
    return ST25R_PICC_ACTIVE;
}

static st25r_picc_state_t goto_halt(st25r_handle_t h)
{
    /* Silent until a WUPA (or the field disappears). */
    st25r_reg_clear(h, ST25R_REG_NFCIP1_PT, ST25R_PT_D_106_AC_A);
    st25r_cmd(h, ST25R_CMD_GO_TO_SLEEP);
    st25r_reg_modify(h, ST25R_REG_MODE, ST25R_MODE_TARG_BITRATE, 0xFB);
    st25r_reg_clear(h, ST25R_REG_ISO14443A, ST25R_ISO_NFC_F0);
    st25r_cmd(h, ST25R_CMD_UNMASK_RECEIVE_DATA);
    st25r_irq_enable(h, ST25R_IRQ_NFCT | ST25R_IRQ_RXS | ST25R_IRQ_CRC | ST25R_IRQ_ERR1 | ST25R_IRQ_ERR2 |
                            ST25R_IRQ_PAR | ST25R_IRQ_EON | ST25R_IRQ_EOF | ST25R_IRQ_MODE_MASK);

    if (!extra_field(h)) {
        return goto_off(h);
    }
    set_state(h, ST25R_PICC_HALT);
    return ST25R_PICC_HALT;
}

/* ------------------------------------------------------------------------- */
/* State handlers                                                            */
/* ------------------------------------------------------------------------- */
static st25r_picc_state_t update_off(st25r_handle_t h)
{
    if (irq_take(h, ST25R_IRQ_EON)) {
        return goto_idle(h);
    }
    return ST25R_PICC_OFF;
}

static st25r_picc_state_t update_idle(st25r_handle_t h)
{
    const uint32_t irq = irq_take(h, ST25R_IRQ_NFCT | ST25R_IRQ_RXE | ST25R_IRQ_EOF | ST25R_IRQ_RXE_PTA);
    if (!irq) {
        return ST25R_PICC_IDLE;
    }

    if (irq & ST25R_IRQ_NFCT) {
        uint8_t br = 0;
        if (st25r_reg_read8(h, ST25R_REG_BITRATE_DISPLAY, &br)) {
            br = (uint8_t)((br >> 4) & 0x03u);
            h->emu.bitrate = (br > 2) ? 2 : br;
        }
    }
    if ((irq & ST25R_IRQ_EOF) && (irq & ST25R_IRQ_RXE) == 0) {
        return goto_off(h);
    }
    if (irq & ST25R_IRQ_RXE) {
        uint32_t errs = irq_take(h, ST25R_IRQ_PAR | ST25R_IRQ_CRC | ST25R_IRQ_ERR2 | ST25R_IRQ_ERR1);
        (void)errs;
        /* The chip answers REQA/WUPA/SDD itself while idle; anything that
         * reaches this point is not part of the UID exchange. */
        st25r_cmd(h, ST25R_CMD_CLEAR_FIFO);
        st25r_cmd(h, ST25R_CMD_UNMASK_RECEIVE_DATA);
        st25r_reg_clear(h, ST25R_REG_OP_CONTROL, ST25R_OP_TX_EN);
        return ST25R_PICC_IDLE;
    }
    if (irq & ST25R_IRQ_RXE_PTA) {
        uint8_t pta = 0;
        if (st25r_reg_read8(h, ST25R_REG_PT_DISPLAY, &pta) && (pta & 0x0Fu) > ST25R_PTA_IDLE) {
            return goto_ready(h);
        }
    }
    return ST25R_PICC_IDLE;
}

static st25r_picc_state_t update_ready(st25r_handle_t h)
{
    const uint32_t mask = ST25R_IRQ_EOF | (h->emu.wakeup ? ST25R_IRQ_WU_AX : ST25R_IRQ_WU_A);
    const uint32_t irq = irq_take(h, mask);
    if (!irq) {
        return ST25R_PICC_READY;
    }
    if (irq & ST25R_IRQ_EOF) {
        return goto_off(h);
    }
    if (irq & (ST25R_IRQ_WU_A | ST25R_IRQ_WU_AX)) {
        return goto_active(h);
    }
    return ST25R_PICC_READY;
}

static st25r_picc_state_t update_active(st25r_handle_t h)
{
    const uint32_t irq = irq_take(h, ST25R_IRQ_EOF | ST25R_IRQ_RXE);
    if (!irq) {
        return ST25R_PICC_ACTIVE;
    }
    if (irq & ST25R_IRQ_EOF) {
        return goto_off(h);
    }

    const uint32_t errs = irq_take(h, ST25R_IRQ_PAR | ST25R_IRQ_CRC | ST25R_IRQ_ERR2 | ST25R_IRQ_ERR1);
    uint16_t bytes = 0;
    uint8_t bits = 0;
    if (!st25r_fifo_size(h, &bytes, &bits)) {
        return goto_idle(h);
    }

    /* Short frames (REQA/WUPA) carry no CRC and are handled by the chip. */
    if (bits == 7 && bytes == 1) {
        uint8_t dummy = 0;
        st25r_fifo_read(h, &dummy, 1);
        st25r_cmd(h, ST25R_CMD_CLEAR_FIFO);
        st25r_cmd(h, ST25R_CMD_UNMASK_RECEIVE_DATA);
        return goto_idle(h);
    }
    if (errs || bytes <= 2) {
        /* 调试：说明读卡器发来的帧被芯片判成错误/短帧 */
        ESP_LOGD(TAG, "active drop errs=%08X bytes=%u bits=%u", (unsigned)errs, bytes, bits);
        st25r_cmd(h, ST25R_CMD_CLEAR_FIFO);
        st25r_cmd(h, ST25R_CMD_UNMASK_RECEIVE_DATA);
        return h->emu.wakeup ? goto_halt(h) : goto_idle(h);
    }

    uint16_t len = (bytes > sizeof(h->emu.rx)) ? (uint16_t)sizeof(h->emu.rx) : bytes;
    if (!st25r_fifo_read(h, h->emu.rx, len)) {
        return goto_idle(h);
    }
    len = (uint16_t)(len - 2); /* drop the CRC the receiver kept */
    h->emu.rx_len = len;

    const bool halt = (len >= 2 && h->emu.rx[0] == ST25R_AC_HLTA && h->emu.rx[1] == 0x00);
    bool keep = false;
    if (h->emu.cb && !halt) {
        keep = h->emu.cb(h, ST25R_PICC_EVT_FRAME, h->emu.rx, len, h->emu.ctx);
    }
    if (halt) {
        return goto_halt(h);
    }
    return keep ? ST25R_PICC_ACTIVE : goto_idle(h);
}

static st25r_picc_state_t update_halt(st25r_handle_t h)
{
    const uint32_t irq = irq_take(h, ST25R_IRQ_NFCT | ST25R_IRQ_RXE | ST25R_IRQ_EOF | ST25R_IRQ_RXE_PTA);
    if (!irq) {
        return ST25R_PICC_HALT;
    }
    if (irq & ST25R_IRQ_NFCT) {
        uint8_t br = 0;
        if (st25r_reg_read8(h, ST25R_REG_BITRATE_DISPLAY, &br)) {
            br = (uint8_t)((br >> 4) & 0x03u);
            h->emu.bitrate = (br > 2) ? 2 : br;
        }
    }
    if (irq & ST25R_IRQ_EOF) {
        return goto_off(h);
    }
    if (irq & ST25R_IRQ_RXE) {
        st25r_cmd(h, ST25R_CMD_CLEAR_FIFO);
        st25r_cmd(h, ST25R_CMD_UNMASK_RECEIVE_DATA);
        return ST25R_PICC_HALT;
    }
    if (irq & ST25R_IRQ_RXE_PTA) {
        uint8_t pta = 0;
        if (st25r_reg_read8(h, ST25R_REG_PT_DISPLAY, &pta) && (pta & 0x0Fu) > ST25R_PTA_HALT) {
            h->emu.wakeup = true;
            return goto_ready(h);
        }
    }
    return ST25R_PICC_HALT;
}

void st25r_picc_step(st25r_handle_t h)
{
    irq_refresh(h);
    switch (h->emu.state) {
        case ST25R_PICC_OFF:
            (void)update_off(h);
            break;
        case ST25R_PICC_IDLE:
            (void)update_idle(h);
            break;
        case ST25R_PICC_READY:
            (void)update_ready(h);
            break;
        case ST25R_PICC_ACTIVE:
            (void)update_active(h);
            break;
        case ST25R_PICC_HALT:
            (void)update_halt(h);
            break;
        default:
            set_state(h, ST25R_PICC_OFF);
            break;
    }
}

/* ------------------------------------------------------------------------- */
/* PT memory                                                                 */
/* ------------------------------------------------------------------------- */
static bool reg_access_pt(st25r_handle_t h, const uint8_t *data, uint16_t len)
{
    return st25r_cmd_write(h, ST25R_OP_LOAD_PT_A, data, len);
}

static bool load_pt_memory(st25r_handle_t h, const st25r_card_t *card)
{
    uint8_t w[15] = {0};
    const uint8_t uid_len = (card->uid_len <= 10) ? card->uid_len : 10;
    memcpy(w, card->uid, uid_len);

    w[10] = (uint8_t)(card->atqa & 0xFF);
    w[11] = (uint8_t)(card->atqa >> 8);

    const bool cascade = (card->uid_len == 7);
    w[12] = (uint8_t)(cascade ? (card->sak | 0x04) : (card->sak & ~0x04));
    w[13] = (uint8_t)(card->sak & ~0x04);
    w[14] = (uint8_t)(card->sak & ~0x04);

    if (!st25r_reg_modify(h, ST25R_REG_AUX_DEF, cascade ? ST25R_AUX_UID_7 : 0x00,
                          ST25R_AUX_UID_LEN_MASK)) {
        return false;
    }
    return reg_access_pt(h, w, sizeof(w));
}

/* ------------------------------------------------------------------------- */
/* Configuration                                                             */
/* ------------------------------------------------------------------------- */
esp_err_t st25r_picc_configure(st25r_handle_t h, const st25r_card_t *card)
{
    if (!h || !h->ready || !card) {
        return ESP_ERR_INVALID_ARG;
    }
    if (card->tech != ST25R_TECH_NFC_A) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (card->uid_len != 4 && card->uid_len != 7) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!st25r_enable_osc(h) || !st25r_cmd(h, ST25R_CMD_STOP_ALL_ACTIVITIES) ||
        !st25r_reg_clear(h, ST25R_REG_OP_CONTROL, ST25R_OP_WU)) {
        return ESP_FAIL;
    }

    /* Passive target, ISO14443A, bit rate detection. */
    if (!st25r_reg_write8(h, ST25R_REG_MODE, ST25R_MODE_TARG_BITRATE) ||
        !st25r_reg_write8(h, ST25R_REG_NFCIP1_PT, 0x5C) ||
        !st25r_reg_write8(h, ST25R_REG_MASK_PT_IRQ, 0x02) ||
        !st25r_reg_write8(h, ST25R_REG_TIMER_EMV, 0x08)) {
        return ESP_FAIL;
    }

    if (!load_pt_memory(h, card)) {
        return ESP_FAIL;
    }

    /* Energy saving: no automatic NFC-A response until the field is stable. */
    if (!st25r_reg_modify(h, ST25R_REG_NFCIP1_PT, ST25R_PT_D_AC_AP2P | ST25R_PT_D_212_424_1R,
                          ST25R_PT_D_AC_AP2P | ST25R_PT_D_212_424_1R | ST25R_PT_D_106_AC_A)) {
        return ESP_FAIL;
    }
    st25r_reg_clear(h, ST25R_REG_TIMER_EMV, 0xE0);
    st25r_reg_set(h, ST25R_REG_TIMER_EMV, ST25R_TIMER_MRT_STEP);
    st25r_reg_write8(h, ST25R_REG_MASK_RX_TIMER, st25r_calc_mrt(100, true));

    /* Standard ISO14443-A parity, no NFCIP-1 framing. */
    st25r_reg_clear(h, ST25R_REG_ISO14443A, ST25R_ISO_NO_TX_PAR | ST25R_ISO_NO_RX_PAR | ST25R_ISO_NFC_F0);

    st25r_reg_write8(h, ST25R_REG_ANT_TUNE1, 0x00);
    st25r_reg_write8(h, ST25R_REG_ANT_TUNE2, 0xFF);
    st25r_reg_write8(h, ST25R_B(ST25R_REG_B_OVERSHOOT_1), 0x00);
    st25r_reg_write8(h, ST25R_B(ST25R_REG_B_OVERSHOOT_2), 0x00);
    st25r_reg_write8(h, ST25R_B(ST25R_REG_B_UNDERSHOOT_1), 0x00);
    st25r_reg_write8(h, ST25R_B(ST25R_REG_B_UNDERSHOOT_2), 0x00);

    st25r_cmd(h, ST25R_CMD_UNMASK_RECEIVE_DATA);

    /* Start from the powered-off state and wait for a reader field.
       与 M5 参考实现的 start_emulation() 一致：不再额外覆盖调制寄存器，
       被动目标模式由芯片自行处理负载调制。 */
    (void)goto_off(h);
    set_state(h, ST25R_PICC_OFF);

    /* 一次性回读关键寄存器，便于现场确认配置是否真的落下 */
    uint8_t mode = 0, op = 0, a1 = 0, a2 = 0, auxm = 0, ptm = 0, mrt = 0, tx = 0, ram = 0;
    (void)st25r_reg_read8(h, ST25R_REG_MODE, &mode);
    (void)st25r_reg_read8(h, ST25R_REG_OP_CONTROL, &op);
    (void)st25r_reg_read8(h, ST25R_REG_ANT_TUNE1, &a1);
    (void)st25r_reg_read8(h, ST25R_REG_ANT_TUNE2, &a2);
    (void)st25r_reg_read8(h, ST25R_B(ST25R_REG_B_AUX_MODULATION), &auxm);
    (void)st25r_reg_read8(h, ST25R_REG_PT_MOD, &ptm);
    (void)st25r_reg_read8(h, ST25R_REG_MASK_RX_TIMER, &mrt);
    (void)st25r_reg_read8(h, ST25R_REG_TX_DRIVER, &tx);
    (void)st25r_reg_read8(h, ST25R_B(ST25R_REG_B_RES_AM_MOD), &ram);
    ESP_LOGD(TAG, "picc cfg: mode=%02X op=%02X ant=%02X/%02X aux=%02X ptmod=%02X mrt=%02X tx=%02X ram=%02X",
             mode, op, a1, a2, auxm, ptm, mrt, tx, ram);
    return ESP_OK;
}

/* ------------------------------------------------------------------------- */
/* Task and public API                                                       */
/* ------------------------------------------------------------------------- */
/* IRQ 脚是电平信号（高有效），上升沿唤醒模拟任务即可，不用轮询占 CPU。 */
static void IRAM_ATTR picc_irq_isr(void *arg)
{
    st25r_handle_t h = (st25r_handle_t)arg;
    BaseType_t woken = pdFALSE;
    if (h->emu.irq_sem) {
        xSemaphoreGiveFromISR(h->emu.irq_sem, &woken);
    }
    if (woken == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

static void picc_task(void *arg)
{
    st25r_handle_t h = (st25r_handle_t)arg;
    for (;;) {
        if (h->emu.stop) {
            break;
        }
        (void)st25r_picc_update(h);
        if (h->emu.stop) {
            break;
        }
        /* 电平信号：还有未处理的中断就立刻再跑一轮（任务处理期间可能又来了新帧） */
        if (h->cfg.pin_irq != GPIO_NUM_NC && st25r_irq_asserted(h)) {
            continue;
        }
        if (h->emu.irq_sem) {
            /* 阻塞等芯片拉高 IRQ；1s 超时只是兜底（ISR 漏边沿/未装时仍能自愈） */
            (void)xSemaphoreTake(h->emu.irq_sem, pdMS_TO_TICKS(1000));
        } else {
            vTaskDelay(1); /* 没有 IRQ 线或 ISR 安装失败：退回轮询 */
        }
    }
    h->emu.task = NULL;
    vTaskDelete(NULL);
}

esp_err_t st25r_picc_begin(st25r_handle_t h, const st25r_picc_config_t *cfg)
{
    if (!h || !h->ready || !cfg) {
        return ESP_ERR_INVALID_ARG;
    }
    if (h->emu.running || h->reader_active) {
        return ESP_ERR_INVALID_STATE;
    }

    st25r_lock(h);
    h->emu.card = cfg->card;
    h->emu.cb = cfg->callback;
    h->emu.ctx = cfg->user_ctx;
    h->emu.stop = false;
    h->emu.external_step = (cfg->task_priority == 0);
    h->emu.irq_pending = 0;
    h->emu.state = ST25R_PICC_OFF;

    esp_err_t err = st25r_picc_configure(h, &h->emu.card);
    st25r_unlock(h);
    if (err != ESP_OK) {
        return err;
    }

    h->emu.running = true;
    if (!h->emu.external_step) {
        /* IRQ 上升沿唤醒模拟任务；ISR 服务可能被 EPD/SD 装过，重复安装会返回
           ESP_ERR_INVALID_STATE，这里直接忽略。 */
        if (h->cfg.pin_irq != GPIO_NUM_NC) {
            h->emu.irq_sem = xSemaphoreCreateBinary();
            if (h->emu.irq_sem) {
                (void)gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
                if (gpio_set_intr_type(h->cfg.pin_irq, GPIO_INTR_POSEDGE) != ESP_OK ||
                    gpio_isr_handler_add(h->cfg.pin_irq, picc_irq_isr, h) != ESP_OK) {
                    vSemaphoreDelete(h->emu.irq_sem);
                    h->emu.irq_sem = NULL; /* 退回轮询 */
                }
            }
        }

        const uint32_t stack = cfg->task_stack_size ? cfg->task_stack_size : 4096;
        BaseType_t ok;
        if (cfg->task_core == tskNO_AFFINITY) {
            ok = xTaskCreate(picc_task, "st25r_picc", stack, h, cfg->task_priority, &h->emu.task);
        } else {
            ok = xTaskCreatePinnedToCore(picc_task, "st25r_picc", stack, h, cfg->task_priority, &h->emu.task,
                                         cfg->task_core);
        }
        if (ok != pdPASS) {
            if (h->emu.irq_sem) {
                gpio_isr_handler_remove(h->cfg.pin_irq);
                vSemaphoreDelete(h->emu.irq_sem);
                h->emu.irq_sem = NULL;
            }
            h->emu.running = false;
            return ESP_ERR_NO_MEM;
        }
    } else {
        ESP_LOGI(TAG, "emulation ready (external stepping)");
    }
    return ESP_OK;
}

esp_err_t st25r_picc_end(st25r_handle_t h)
{
    if (!h) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!h->emu.running) {
        return ESP_OK;
    }
    h->emu.stop = true;
    if (h->emu.irq_sem) {
        xSemaphoreGive(h->emu.irq_sem); /* 任务可能正阻塞在 IRQ 等待上 */
    }
    if (h->emu.task) {
        const int64_t start = esp_timer_get_time();
        while (h->emu.task && (esp_timer_get_time() - start) < 1000000) {
            vTaskDelay(1);
        }
    }
    if (h->emu.irq_sem) {
        gpio_isr_handler_remove(h->cfg.pin_irq);
        vSemaphoreDelete(h->emu.irq_sem);
        h->emu.irq_sem = NULL;
    }
    st25r_lock(h);
    st25r_field_off(h);
    /* 与 M5 的 stop_emulation() 对齐：离开监听模式时把 MODE 归零（退出 target 位），
       否则下一次 initiator 配置前的残留状态会让芯片停在被动目标逻辑上。 */
    st25r_reg_write8(h, ST25R_REG_MODE, 0x00);
    h->emu.state = ST25R_PICC_OFF;
    h->emu.running = false;
    st25r_unlock(h);
    return ESP_OK;
}

bool st25r_picc_is_running(st25r_handle_t h)
{
    return h && h->emu.running;
}

esp_err_t st25r_picc_update(st25r_handle_t h)
{
    if (!h || !h->ready) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!h->emu.running) {
        return ESP_ERR_INVALID_STATE;
    }
    st25r_lock(h);
    st25r_picc_step(h);
    st25r_unlock(h);
    return ESP_OK;
}

esp_err_t st25r_picc_set_card(st25r_handle_t h, const st25r_card_t *card)
{
    if (!h || !card) {
        return ESP_ERR_INVALID_ARG;
    }
    if (card->tech != ST25R_TECH_NFC_A || (card->uid_len != 4 && card->uid_len != 7)) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    st25r_lock(h);
    h->emu.card = *card;
    bool ok = true;
    if (h->emu.running) {
        ok = load_pt_memory(h, card);
    }
    st25r_unlock(h);
    return ok ? ESP_OK : ESP_FAIL;
}

esp_err_t st25r_picc_transmit(st25r_handle_t h, const uint8_t *data, uint16_t len)
{
    if (!h || !data || !len || len > ST25R_MAX_FRAME) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!h->emu.running || h->emu.state != ST25R_PICC_ACTIVE) {
        return ESP_ERR_INVALID_STATE;
    }
    st25r_lock(h);
    const bool ok = st25r_cmd(h, ST25R_CMD_CLEAR_FIFO) && st25r_fifo_write(h, data, len) &&
                    st25r_tx_bytes(h, len, 0) && st25r_cmd(h, ST25R_CMD_TRANSMIT_WITH_CRC);
    st25r_unlock(h);
    return ok ? ESP_OK : ESP_FAIL;
}

esp_err_t st25r_picc_transmit_no_crc(st25r_handle_t h, const uint8_t *data, uint16_t len)
{
    if (!h || !data || !len || len > ST25R_MAX_FRAME) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!h->emu.running || h->emu.state != ST25R_PICC_ACTIVE) {
        return ESP_ERR_INVALID_STATE;
    }
    st25r_lock(h);
    /* ISO14443-A 监听方向：芯片按字节自动补奇校验位，这里只要求不追加 CRC
       （MIFARE 认证第一帧 Nt 就是这种帧）。 */
    const bool ok = st25r_reg_write8(h, ST25R_REG_ISO14443A, 0x00) &&
                    st25r_cmd(h, ST25R_CMD_CLEAR_FIFO) && st25r_fifo_write(h, data, len) &&
                    st25r_tx_bytes(h, len, 0) && st25r_cmd(h, ST25R_CMD_TRANSMIT_WITHOUT_CRC);
    st25r_unlock(h);
    return ok ? ESP_OK : ESP_FAIL;
}
