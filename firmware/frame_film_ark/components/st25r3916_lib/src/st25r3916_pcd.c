/*
 * SPDX-License-Identifier: MIT
 *
 * NFC-A reader (PCD) side: REQA/WUPA, anticollision and SELECT.
 */
#include "st25r3916_priv.h"

#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "st25r3916/pcd";

/* ISO14443-3 timings, in milliseconds for the ST25R3916 no-response timer. */
#define ST25R_TIMEOUT_REQ_MS    4
#define ST25R_TIMEOUT_ANTICOLL  8
#define ST25R_TIMEOUT_SELECT    8
#define ST25R_TIMEOUT_HALT_MS   2

static inline uint8_t bcc8(const uint8_t *p)
{
    return (uint8_t)(p[0] ^ p[1] ^ p[2] ^ p[3]);
}

static bool write_fwt(st25r_handle_t h, uint32_t ms)
{
    uint8_t timer = 0;
    if (!st25r_reg_read8(h, ST25R_REG_TIMER_EMV, &timer)) {
        return false;
    }
    const uint16_t nrt = st25r_calc_nrt(ms, (timer & ST25R_TIMER_NRT_STEP) != 0);
    return st25r_reg_write16(h, ST25R_REG_NO_RESP_TIMER1, nrt);
}

static esp_err_t field_on(st25r_handle_t h)
{
    uint8_t v = 0;
    if (!st25r_reg_read8(h, ST25R_REG_OP_CONTROL, &v)) {
        ESP_LOGE(TAG, "field_on: read OP_CONTROL failed");
        return ESP_FAIL;
    }
    if (v & ST25R_OP_TX_EN) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!st25r_cmd(h, ST25R_CMD_NFC_INITIAL_FIELD_ON)) {
        ESP_LOGE(TAG, "field_on: NFC_INITIAL_FIELD_ON failed");
        return ESP_FAIL;
    }
    vTaskDelay(pdMS_TO_TICKS(5));
    /* 打开 TX/RX（set = tx_en|rx_en），写反成 clear 会把刚拉起的场立刻关掉。 */
    if (!st25r_reg_modify(h, ST25R_REG_OP_CONTROL, ST25R_OP_TX_EN | ST25R_OP_RX_EN, 0)) {
        ESP_LOGE(TAG, "field_on: enable TX/RX failed");
        return ESP_FAIL;
    }
    return ESP_OK;
}

/* ------------------------------------------------------------------------- */
/* Mode configuration                                                        */
/* ------------------------------------------------------------------------- */
esp_err_t st25r_pcd_configure(st25r_handle_t h)
{
    if (!h || !h->ready) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!st25r_cmd(h, ST25R_CMD_STOP_ALL_ACTIVITIES)) {
        ESP_LOGE(TAG, "configure: STOP_ALL_ACTIVITIES failed");
        return ESP_FAIL;
    }
    if (!st25r_reg_clear(h, ST25R_REG_OP_CONTROL, ST25R_OP_WU)) {
        ESP_LOGE(TAG, "configure: clear WU failed");
        return ESP_FAIL;
    }

    /* Initiator, ISO14443A, automatic anticollision (ar = auto). */
    if (!st25r_reg_write8(h, ST25R_REG_MODE, (uint8_t)(ST25R_MODE_OM_ISO14443A | 0x01))) {
        ESP_LOGE(TAG, "configure: write MODE failed");
        return ESP_FAIL;
    }
    if (!st25r_reg_write8(h, ST25R_REG_BITRATE, 0x00)) {
        ESP_LOGE(TAG, "configure: write BITRATE failed");
        return ESP_FAIL;
    }
    if (!st25r_reg_write8(h, ST25R_REG_ISO14443A, 0x00)) {
        ESP_LOGE(TAG, "configure: write ISO14443A failed");
        return ESP_FAIL;
    }

    st25r_reg_clear(h, ST25R_REG_AUX_DEF, ST25R_AUX_DIS_CORR);

    st25r_reg_write8(h, ST25R_B(ST25R_REG_B_OVERSHOOT_1), 0x40);
    st25r_reg_write8(h, ST25R_B(ST25R_REG_B_OVERSHOOT_2), 0x03);
    st25r_reg_write8(h, ST25R_B(ST25R_REG_B_UNDERSHOOT_1), 0x40);
    st25r_reg_write8(h, ST25R_B(ST25R_REG_B_UNDERSHOOT_2), 0x03);

    st25r_reg_write8(h, ST25R_B(0x0C), 0x47); /* correlator 1 */
    st25r_reg_write8(h, ST25R_B(0x0D), 0x00); /* correlator 2 */

    st25r_irq_enable(h, ST25R_IRQ_WL | ST25R_IRQ_TXE | ST25R_IRQ_RXS | ST25R_IRQ_RXE | ST25R_IRQ_PAR |
                            ST25R_IRQ_CRC | ST25R_IRQ_ERR1 | ST25R_IRQ_ERR2 | ST25R_IRQ_NRE | ST25R_IRQ_COL);

    /* Receiver: 600 kHz filter, dynamic squelch, AGC. */
    if (!st25r_reg_write8(h, ST25R_REG_RX_CONF1, 0x08) || /* z_600k */
        !st25r_reg_write8(h, ST25R_REG_RX_CONF2, 0x2D) || /* sqm_dyn|agc_en|agc_m|agc6_3 */
        !st25r_reg_write8(h, ST25R_REG_RX_CONF3, 0xD8) ||
        !st25r_reg_write8(h, ST25R_REG_RX_CONF4, 0x22) ||
        !st25r_cmd(h, ST25R_CMD_RESET_RX_GAIN)) {
        ESP_LOGE(TAG, "configure: receiver setup failed");
        return ESP_FAIL;
    }
    st25r_irq_mask_all(h, 0);

    const esp_err_t err = field_on(h);
    if (err == ESP_OK) {
        h->reader_active = true;
    }
    return err;
}

/* ------------------------------------------------------------------------- */
/* Low level NFC-A transactions                                              */
/* ------------------------------------------------------------------------- */
esp_err_t st25r_pcd_transceive(st25r_handle_t h, const uint8_t *tx, uint16_t tx_len, uint8_t *rx,
                               uint16_t *rx_len, uint32_t timeout_ms)
{
    if (!h || !h->ready || !tx || !tx_len || !rx || !rx_len) {
        return ESP_ERR_INVALID_ARG;
    }
    write_fwt(h, timeout_ms);
    if (!st25r_reg_write8(h, ST25R_REG_ISO14443A, 0x00) ||
        !st25r_reg_clear(h, ST25R_REG_AUX_DEF, ST25R_AUX_NO_CRC_RX) || !st25r_irq_clear(h) ||
        !st25r_cmd(h, ST25R_CMD_CLEAR_FIFO) || !st25r_fifo_write(h, tx, tx_len) ||
        !st25r_tx_bytes(h, tx_len, 0) || !st25r_cmd(h, ST25R_CMD_TRANSMIT_WITH_CRC)) {
        return ESP_FAIL;
    }

    bool timed_out = false;
    uint32_t irq = st25r_wait_irq_ex(h, ST25R_IRQ_RXE | ST25R_IRQ_RXS | ST25R_IRQ_PAR | ST25R_IRQ_CRC |
                                            ST25R_IRQ_ERR1 | ST25R_IRQ_ERR2,
                                      timeout_ms, &timed_out);
    if (timed_out) {
        return ESP_ERR_TIMEOUT;
    }
    if (irq & (ST25R_IRQ_PAR | ST25R_IRQ_CRC | ST25R_IRQ_ERR1 | ST25R_IRQ_ERR2)) {
        st25r_cmd(h, ST25R_CMD_CLEAR_FIFO);
        return ESP_ERR_INVALID_RESPONSE;
    }

    /* RXS only means the frame is still being received: wait for its end. */
    if (!(irq & ST25R_IRQ_RXE)) {
        bool to_rxe = false;
        irq |= st25r_wait_irq_ex(h, ST25R_IRQ_RXE | ST25R_IRQ_PAR | ST25R_IRQ_CRC | ST25R_IRQ_ERR1 | ST25R_IRQ_ERR2,
                                 timeout_ms, &to_rxe);
        if (irq & (ST25R_IRQ_PAR | ST25R_IRQ_CRC | ST25R_IRQ_ERR1 | ST25R_IRQ_ERR2)) {
            st25r_cmd(h, ST25R_CMD_CLEAR_FIFO);
            return ESP_ERR_INVALID_RESPONSE;
        }
        if (!(irq & ST25R_IRQ_RXE)) {
            return ESP_ERR_TIMEOUT;
        }
    }

    uint16_t bytes = 0;
    uint8_t bits = 0;
    if (!st25r_fifo_size(h, &bytes, &bits) || bytes == 0) {
        return ESP_ERR_TIMEOUT;
    }
    uint16_t len = (uint16_t)((bytes > *rx_len) ? *rx_len : bytes);
    if (!st25r_fifo_read(h, rx, len)) {
        return ESP_FAIL;
    }
    *rx_len = len;
    return ESP_OK;
}

bool st25r_pcd_request(st25r_handle_t h, bool wakeup, uint16_t *atqa)
{
    if (!atqa) {
        return false;
    }
    *atqa = 0;
    write_fwt(h, ST25R_TIMEOUT_REQ_MS);
    if (!st25r_reg_write8(h, ST25R_REG_ISO14443A, ST25R_ISO_ANTCL) ||
        !st25r_reg_set(h, ST25R_REG_AUX_DEF, ST25R_AUX_NO_CRC_RX) || !st25r_irq_clear(h) ||
        !st25r_cmd(h, ST25R_CMD_CLEAR_FIFO) ||
        !st25r_cmd(h, wakeup ? ST25R_CMD_TRANSMIT_WUPA : ST25R_CMD_TRANSMIT_REQA)) {
        return false;
    }

    bool timed_out = false;
    uint32_t irq = st25r_wait_irq_ex(h, ST25R_IRQ_RXE | ST25R_IRQ_RXS | ST25R_IRQ_COL, ST25R_TIMEOUT_REQ_MS,
                                     &timed_out);
    if ((irq & ST25R_IRQ_RXE) == 0) {
        /* Wait for the end of the frame answered by the chip's anticollision. */
        bool to_rxe = false;
        irq |= st25r_wait_irq_ex(h, ST25R_IRQ_RXE, ST25R_TIMEOUT_REQ_MS, &to_rxe);
        if ((irq & ST25R_IRQ_RXE) == 0) {
            return false;
        }
    }

    uint8_t rbuf[2] = {0, 0};
    if (!st25r_fifo_read(h, rbuf, sizeof(rbuf))) {
        return false;
    }
    *atqa = (uint16_t)(((uint16_t)rbuf[1] << 8) | rbuf[0]);
    return true;
}

bool st25r_pcd_anticollision(st25r_handle_t h, uint8_t lv, uint8_t out[5])
{
    if (!out || lv < 1 || lv > 3) {
        return false;
    }
    write_fwt(h, ST25R_TIMEOUT_ANTICOLL);
    if (!st25r_reg_write8(h, ST25R_REG_ISO14443A, ST25R_ISO_ANTCL) ||
        !st25r_reg_clear(h, ST25R_REG_AUX_DEF, ST25R_AUX_NO_CRC_RX)) {
        return false;
    }

    uint8_t frame[7] = {(uint8_t)(0x91 + lv * 2), 0x20, 0, 0, 0, 0, 0};
    uint32_t sbytes = 2, sbits = 0;
    uint8_t offset = 0;
    uint8_t coll_byte = 0;
    bool collision = false;
    uint32_t guard = 32;
    uint16_t actual = 0;

    do {
        if (!st25r_irq_clear(h) || !st25r_cmd(h, ST25R_CMD_CLEAR_FIFO) ||
            !st25r_fifo_write(h, frame, (uint16_t)(sbytes + (sbits ? 1 : 0))) ||
            !st25r_tx_bytes(h, (uint16_t)sbytes, (uint8_t)sbits) ||
            !st25r_cmd(h, ST25R_CMD_TRANSMIT_WITHOUT_CRC)) {
            return false;
        }

        bool timed_out = false;
        const uint32_t irq = st25r_wait_irq_ex(h, ST25R_IRQ_RXE | ST25R_IRQ_COL, ST25R_TIMEOUT_ANTICOLL,
                                               &timed_out);
        collision = (irq & ST25R_IRQ_COL) != 0;
        if (timed_out || (!collision && !(irq & ST25R_IRQ_RXE))) {
            return false;
        }

        uint8_t cd = 0;
        uint16_t avail = 0;
        uint8_t avail_bits = 0;
        if (!st25r_fifo_size(h, &avail, &avail_bits) || avail == 0) {
            return false;
        }
        actual = (uint16_t)((5 - offset) < avail ? (5 - offset) : avail);
        if (!st25r_fifo_read(h, out + offset, actual) || !st25r_reg_read8(h, ST25R_REG_COLLISION, &cd)) {
            return false;
        }

        if (collision) {
            const uint8_t cbytes = (uint8_t)((cd >> 4) & 0x0F);
            const uint8_t cbits = (uint8_t)((cd >> 1) & 0x07);
            coll_byte = out[offset + actual - 1];
            coll_byte |= (uint8_t)(1u << cbits);

            sbytes = (uint32_t)(cbytes + (cbits == 0x07 ? 1 : 0));
            sbits = (uint32_t)((cbits + 1) & 0x07);
            if (sbytes < 2 || sbytes >= sizeof(frame)) {
                ESP_LOGD(TAG, "collision display 0x%02X out of frame", cd);
                return false;
            }
            frame[1] = (uint8_t)((sbytes << 4) | sbits); /* NVB */
            memcpy(frame + 2 + offset, out + offset, actual);
            frame[sbytes] = coll_byte;
            offset = (uint8_t)(actual - 1);
        }

        if (sbits) {
            out[offset] = (uint8_t)((out[offset] >> sbits) << sbits);
            out[offset] |= coll_byte;
        }
        vTaskDelay(0);
    } while (collision && guard--);

    if (collision) {
        return false;
    }
    if (offset + actual != 5 || out[4] != bcc8(out)) {
        ESP_LOGD(TAG, "anticollision answer failed BCC");
        return false;
    }
    return true;
}

bool st25r_pcd_select_level(st25r_handle_t h, uint8_t lv, uint8_t out[5], uint8_t *sak)
{
    if (!st25r_pcd_anticollision(h, lv, out)) {
        return false;
    }

    uint8_t frame[7] = {(uint8_t)(0x91 + lv * 2), 0x70, 0, 0, 0, 0, 0};
    memcpy(frame + 2, out, 5);

    uint8_t rbuf[4] = {0, 0, 0, 0};
    uint16_t rx_len = sizeof(rbuf);
    const esp_err_t err = st25r_pcd_transceive(h, frame, sizeof(frame), rbuf, &rx_len, ST25R_TIMEOUT_SELECT);
    if (err != ESP_OK || rx_len < 1) {
        ESP_LOGD(TAG, "select lv%u failed (%s)", lv, esp_err_to_name(err));
        return false;
    }
    *sak = rbuf[0];
    return true;
}

/* ------------------------------------------------------------------------- */
/* Public reader API                                                         */
/* ------------------------------------------------------------------------- */
esp_err_t st25r_reader_begin(st25r_handle_t h)
{
    if (!h || !h->ready) {
        return ESP_ERR_INVALID_ARG;
    }
    st25r_lock(h);
    esp_err_t err = st25r_pcd_configure(h);
    if (err != ESP_OK) {
        /* 从模拟模式切回来时芯片可能停在目标/透明状态：重跑一次芯片初始化再配。 */
        ESP_LOGW(TAG, "configure failed (%s), re-init chip and retry", esp_err_to_name(err));
        if (st25r_chip_init(h) == ESP_OK) {
            err = st25r_pcd_configure(h);
        }
        ESP_LOGW(TAG, "retry result: %s", esp_err_to_name(err));
    }
    st25r_unlock(h);
    return err;
}

esp_err_t st25r_reader_end(st25r_handle_t h)
{
    if (!h || !h->ready) {
        return ESP_ERR_INVALID_ARG;
    }
    st25r_lock(h);
    st25r_field_off(h);
    h->reader_active = false;
    st25r_unlock(h);
    return ESP_OK;
}

esp_err_t st25r_reader_poll(st25r_handle_t h, st25r_card_t *card, uint32_t timeout_ms)
{
    if (!h || !h->ready || !card) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!h->reader_active) {
        return ESP_ERR_INVALID_STATE;
    }
    if (timeout_ms == 0) {
        timeout_ms = 50;
    }

    memset(card, 0, sizeof(*card));
    card->tech = ST25R_TECH_NFC_A;

    const int64_t start = esp_timer_get_time();
    while ((esp_timer_get_time() - start) < (int64_t)timeout_ms * 1000) {
        st25r_lock(h);

        uint16_t atqa = 0;
        /* WUPA instead of REQA so a card that was halted by a previous
         * transaction is woken up again on the next poll. */
        if (!st25r_pcd_request(h, true, &atqa)) {
            st25r_unlock(h);
            /* 连续轮询时让出 CPU，否则 IDLE1 长时间得不到调度会触发任务看门狗。 */
            vTaskDelay(1);
            continue;
        }

        uint8_t out[5] = {0, 0, 0, 0, 0};
        uint8_t sak = 0;
        bool complete = false;
        bool ok = true;
        uint8_t lv = 1;

        for (; lv <= 3; ++lv) {
            if (!st25r_pcd_select_level(h, lv, out, &sak)) {
                ok = false;
                break;
            }
            const uint8_t *u = out + ((out[0] == 0x88) ? 1 : 0);
            memcpy(card->uid + (lv - 1) * 3, u, (size_t)(out[0] == 0x88 ? 3 : 4));
            if ((sak & 0x04) == 0) {
                complete = true;
                break;
            }
        }
        st25r_unlock(h);

        if (!ok || !complete) {
            vTaskDelay(1);
            continue;
        }

        card->atqa = atqa;
        card->sak = sak;
        card->uid_len = (uint8_t)(lv * 3 + 1);

        /* ATQA/SAK to a coarse type, enough for a card wallet. */
        if (sak == 0x08 || sak == 0x88) {
            card->type = ST25R_CARD_MIFARE_CLASSIC_1K;
        } else if (sak == 0x18) {
            card->type = ST25R_CARD_MIFARE_CLASSIC_2K;
        } else if (sak == 0x38 || sak == 0x98) {
            card->type = ST25R_CARD_MIFARE_CLASSIC_4K;
        } else if (sak == 0x00) {
            card->type = ST25R_CARD_MIFARE_ULTRALIGHT;
        } else if (sak == 0x10 || sak == 0x11) {
            card->type = ST25R_CARD_MIFARE_PLUS;
        } else if (sak == 0x20 || sak == 0x28) {
            card->type = ST25R_CARD_MIFARE_DESFIRE;
        } else if (sak & 0x20) {
            card->type = ST25R_CARD_ISO14443_4;
        } else {
            card->type = ST25R_CARD_UNKNOWN;
        }
        return ESP_OK;
    }
    return ESP_ERR_NOT_FOUND;
}

esp_err_t st25r_reader_transceive(st25r_handle_t h, const uint8_t *tx, uint16_t tx_len, uint8_t *rx,
                                  uint16_t *rx_len, uint32_t timeout_ms)
{
    if (!h || !h->ready || !tx || !tx_len || !rx || !rx_len || !*rx_len) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!h->reader_active) {
        return ESP_ERR_INVALID_STATE;
    }
    if (timeout_ms == 0) {
        timeout_ms = 20;
    }

    uint16_t raw_len = (uint16_t)(*rx_len + 2);
    uint8_t raw[ST25R_MAX_FRAME];
    if (raw_len > sizeof(raw)) {
        raw_len = sizeof(raw);
    }

    st25r_lock(h);
    const esp_err_t err = st25r_pcd_transceive(h, tx, tx_len, raw, &raw_len, timeout_ms);
    st25r_unlock(h);
    if (err != ESP_OK) {
        return err;
    }

    /* A 4 bit ACK/NAK stays as it is, everything else carries a 2 byte CRC. */
    uint16_t len = raw_len;
    if (len > 2) {
        len = (uint16_t)(len - 2);
    }
    if (len > *rx_len) {
        len = *rx_len;
    }
    memcpy(rx, raw, len);
    *rx_len = len;
    return ESP_OK;
}

esp_err_t st25r_reader_halt(st25r_handle_t h)
{
    if (!h || !h->ready) {
        return ESP_ERR_INVALID_ARG;
    }
    const uint8_t hlt[2] = {ST25R_AC_HLTA, 0x00};

    st25r_lock(h);
    write_fwt(h, ST25R_TIMEOUT_HALT_MS);
    bool ok = st25r_reg_write8(h, ST25R_REG_ISO14443A, 0x00) &&
              st25r_reg_clear(h, ST25R_REG_AUX_DEF, ST25R_AUX_NO_CRC_RX) && st25r_irq_clear(h) &&
              st25r_cmd(h, ST25R_CMD_CLEAR_FIFO) && st25r_fifo_write(h, hlt, sizeof(hlt)) &&
              st25r_tx_bytes(h, sizeof(hlt), 0) && st25r_cmd(h, ST25R_CMD_TRANSMIT_WITH_CRC);
    if (ok) {
        const uint32_t irq = st25r_wait_irq(h, ST25R_IRQ_TXE, ST25R_TIMEOUT_HALT_MS);
        ok = (irq & ST25R_IRQ_TXE) != 0;
    }
    st25r_unlock(h);
    return ok ? ESP_OK : ESP_FAIL;
}

const char *st25r_card_type_name(st25r_card_type_t type)
{
    switch (type) {
        case ST25R_CARD_MIFARE_CLASSIC_1K:
            return "MIFARE Classic 1K";
        case ST25R_CARD_MIFARE_CLASSIC_2K:
            return "MIFARE Classic 2K";
        case ST25R_CARD_MIFARE_CLASSIC_4K:
            return "MIFARE Classic 4K";
        case ST25R_CARD_MIFARE_ULTRALIGHT:
            return "MIFARE Ultralight / NTAG";
        case ST25R_CARD_MIFARE_PLUS:
            return "MIFARE Plus";
        case ST25R_CARD_MIFARE_DESFIRE:
            return "MIFARE DESFire";
        case ST25R_CARD_ISO14443_4:
            return "ISO14443-4";
        default:
            return "unknown";
    }
}
