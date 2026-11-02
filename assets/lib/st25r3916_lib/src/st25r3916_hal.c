/*
 * SPDX-License-Identifier: MIT
 *
 * Low level ST25R3916 transport: raw SPI, registers, FIFO, interrupts,
 * chip bring-up and the IRQ wake-up source.
 */
#include "st25r3916_priv.h"

#include <string.h>

#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "hal/spi_ll.h"
#include "soc/soc_caps.h"
#include "soc/spi_struct.h"

static const char *TAG = "st25r3916";

/* The raw transfer path bypasses the SPI driver (one polling transaction costs
 * ~15-20us, which is significant in NFC timing windows), so the clock divider
 * and frame format are programmed once here. */
#define ST25R_DEFAULT_CLOCK_HZ 10000000u
#define ST25R_APB_HZ 80000000u

/* ------------------------------------------------------------------------- */
/* Helpers                                                                   */
/* ------------------------------------------------------------------------- */
static spi_dev_t *spi_hw_of(st25r_handle_t h)
{
    /* SPI_LL_GET_HW maps the host id (SPI2_HOST = 1, SPI3_HOST = 2) to the
     * peripheral instance and returns NULL where the target has no such port. */
    return SPI_LL_GET_HW((int)h->cfg.host);
}

static inline void cs_low(st25r_handle_t h)
{
    gpio_set_level(h->cfg.pin_cs, 0);
}

static inline void cs_high(st25r_handle_t h)
{
    gpio_set_level(h->cfg.pin_cs, 1);
}

/* One SPI transfer without touching CS. Caller keeps CS asserted across the
 * address byte and the payload. tx may be NULL (sends 0xFF). */
static bool spi_xfer(st25r_handle_t h, const uint8_t *tx, uint8_t *rx, uint16_t len)
{
    if (!len || len > 64) {
        return false;
    }
    spi_dev_t *hw = h->hw;
    while (hw->cmd.usr) {
    }
    hw->dma_int_clr.val = 0xFFFFFFFFu;
    hw->user.usr_dummy = 0;
    hw->user.usr_addr = 0;
    hw->user.usr_command = 0;
    hw->user.usr_mosi_highpart = 0;
    hw->user.usr_miso_highpart = 0;
    hw->user.usr_mosi = 1;
    hw->user.usr_miso = rx ? 1 : 0;
    hw->user1.usr_addr_bitlen = 0;
    hw->user2.usr_command_bitlen = 0;
    hw->ms_dlen.ms_data_bitlen = (uint32_t)len * 8u - 1u;

    uint8_t txbuf[68];
    memset(txbuf, 0xFF, sizeof(txbuf));
    if (tx) {
        memcpy(txbuf, tx, len);
    }
    for (uint16_t off = 0; off < len; off += 4) {
        uint32_t w = 0;
        memcpy(&w, &txbuf[off], 4);
        hw->data_buf[off / 4] = w;
    }
    hw->cmd.update = 1;
    while (hw->cmd.update) {
    }
    hw->cmd.usr = 1;
    while (!hw->dma_int_raw.trans_done) {
    }
    hw->dma_int_clr.val = 0xFFFFFFFFu;

    if (rx) {
        for (uint16_t off = 0; off < len; off += 4) {
            const uint32_t w = hw->data_buf[off / 4];
            const uint16_t n = (uint16_t)((len - off > 4) ? 4 : (len - off));
            memcpy(&rx[off], &w, n);
        }
    }
    return true;
}

bool st25r_spi_write(st25r_handle_t h, const uint8_t *tx, uint16_t len)
{
    uint16_t done = 0;
    while (done < len) {
        const uint16_t n = (uint16_t)((len - done > 64) ? 64 : (len - done));
        if (!spi_xfer(h, tx + done, NULL, n)) {
            return false;
        }
        done += n;
    }
    return true;
}

bool st25r_spi_read(st25r_handle_t h, uint8_t *rx, uint16_t len)
{
    uint16_t done = 0;
    while (done < len) {
        const uint16_t n = (uint16_t)((len - done > 64) ? 64 : (len - done));
        if (!spi_xfer(h, NULL, rx + done, n)) {
            return false;
        }
        done += n;
    }
    return true;
}

static void apply_raw_clock(st25r_handle_t h)
{
    spi_dev_t *hw = h->hw;
    hw->user.doutdin = 1;      /* full duplex */
    hw->misc.ck_idle_edge = 0; /* SPI mode 1: CPOL=0 ... */
    hw->user.ck_out_edge = 1;  /* ... CPHA=1 */

    const uint32_t hz = h->clock_hz;
    int best_n = 8, best_pre = 1;
    uint32_t best_err = 0xFFFFFFFFu;
    for (int n = 2; n <= 64; ++n) {
        int pre = (int)(((ST25R_APB_HZ / (uint32_t)n) + (hz / 2)) / hz);
        if (pre <= 0) {
            pre = 1;
        }
        if (pre > 16) {
            pre = 16;
        }
        const uint32_t freq = ST25R_APB_HZ / ((uint32_t)pre * (uint32_t)n);
        const uint32_t err = (freq > hz) ? (freq - hz) : (hz - freq);
        if (err < best_err || (err == best_err && n > best_n)) {
            best_err = err;
            best_n = n;
            best_pre = pre;
        }
    }
    int hdiv = (128 * best_n + 127) / 256;
    if (hdiv <= 0) {
        hdiv = 1;
    }
    hw->clock.clk_equ_sysclk = 0;
    hw->clock.clkdiv_pre = (uint32_t)(best_pre - 1);
    hw->clock.clkcnt_n = (uint32_t)(best_n - 1);
    hw->clock.clkcnt_h = (uint32_t)(hdiv - 1);
    hw->clock.clkcnt_l = (uint32_t)(best_n - 1);
}

/* ------------------------------------------------------------------------- */
/* Locking                                                                   */
/* ------------------------------------------------------------------------- */
void st25r_lock(st25r_handle_t h)
{
    if (h && h->lock) {
        xSemaphoreTakeRecursive(h->lock, portMAX_DELAY);
    }
}

void st25r_unlock(st25r_handle_t h)
{
    if (h && h->lock) {
        xSemaphoreGiveRecursive(h->lock);
    }
}

/* ------------------------------------------------------------------------- */
/* Registers                                                                 */
/* ------------------------------------------------------------------------- */
static bool reg_access(st25r_handle_t h, uint16_t reg, uint8_t op, const uint8_t *tx, uint8_t *rx, uint16_t len)
{
    const uint16_t word = (uint16_t)((reg & 0xFF00u) | ((reg & ST25R_REG_MASK) | op));
    const uint8_t addr[2] = {(uint8_t)(word >> 8), (uint8_t)(word & 0xFFu)};
    const uint8_t alen = (reg & 0xFF00u) ? 2 : 1;

    cs_low(h);
    bool ok = st25r_spi_write(h, addr, alen);
    if (ok && tx && len) {
        ok = st25r_spi_write(h, tx, len);
    }
    if (ok && rx && len) {
        ok = st25r_spi_read(h, rx, len);
    }
    cs_high(h);
    return ok;
}

bool st25r_reg_write8(st25r_handle_t h, uint16_t reg, uint8_t v)
{
    return reg_access(h, reg, ST25R_OP_WRITE_REG, &v, NULL, 1);
}

bool st25r_reg_read8(st25r_handle_t h, uint16_t reg, uint8_t *v)
{
    return reg_access(h, reg, ST25R_OP_READ_REG, NULL, v, 1);
}

bool st25r_reg_write16(st25r_handle_t h, uint16_t reg, uint16_t v)
{
    const uint8_t b[2] = {(uint8_t)(v >> 8), (uint8_t)(v & 0xFF)};
    return reg_access(h, reg, ST25R_OP_WRITE_REG, b, NULL, 2);
}

bool st25r_reg_read16(st25r_handle_t h, uint16_t reg, uint16_t *v)
{
    uint8_t b[2] = {0, 0};
    if (!reg_access(h, reg, ST25R_OP_READ_REG, NULL, b, 2)) {
        return false;
    }
    *v = (uint16_t)(((uint16_t)b[0] << 8) | b[1]);
    return true;
}

bool st25r_reg_write32(st25r_handle_t h, uint16_t reg, uint32_t v)
{
    const uint8_t b[4] = {(uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v};
    return reg_access(h, reg, ST25R_OP_WRITE_REG, b, NULL, 4);
}

bool st25r_reg_read32(st25r_handle_t h, uint16_t reg, uint32_t *v)
{
    uint8_t b[4] = {0, 0, 0, 0};
    if (!reg_access(h, reg, ST25R_OP_READ_REG, NULL, b, 4)) {
        return false;
    }
    *v = ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | b[3];
    return true;
}

bool st25r_reg_modify(st25r_handle_t h, uint16_t reg, uint8_t set, uint8_t clear)
{
    uint8_t v = 0;
    if (!st25r_reg_read8(h, reg, &v)) {
        return false;
    }
    const uint8_t w = (uint8_t)((v & ~clear) | set);
    return (w == v) ? true : st25r_reg_write8(h, reg, w);
}

bool st25r_reg_set(st25r_handle_t h, uint16_t reg, uint8_t bits)
{
    return st25r_reg_modify(h, reg, bits, 0);
}

bool st25r_reg_clear(st25r_handle_t h, uint16_t reg, uint8_t bits)
{
    return st25r_reg_modify(h, reg, 0, bits);
}

/* ------------------------------------------------------------------------- */
/* Commands and FIFO                                                         */
/* ------------------------------------------------------------------------- */
static bool cmd_xfer(st25r_handle_t h, uint8_t cmd, const uint8_t *data, uint16_t len)
{
    cs_low(h);
    bool ok = st25r_spi_write(h, &cmd, 1);
    if (ok && data && len) {
        ok = st25r_spi_write(h, data, len);
    }
    cs_high(h);
    return ok;
}

bool st25r_cmd(st25r_handle_t h, uint8_t cmd)
{
    return cmd_xfer(h, cmd, NULL, 0);
}

bool st25r_cmd_write(st25r_handle_t h, uint8_t cmd, const uint8_t *data, uint16_t len)
{
    return cmd_xfer(h, cmd, data, len);
}

bool st25r_fifo_write(st25r_handle_t h, const uint8_t *data, uint16_t len)
{
    return reg_access(h, 0, ST25R_OP_LOAD_FIFO, data, NULL, len);
}

bool st25r_fifo_read(st25r_handle_t h, uint8_t *data, uint16_t len)
{
    return reg_access(h, 0, ST25R_OP_READ_FIFO, NULL, data, len);
}

bool st25r_fifo_size(st25r_handle_t h, uint16_t *bytes, uint8_t *bits)
{
    uint16_t s = 0;
    if (!st25r_reg_read16(h, ST25R_REG_FIFO_STATUS1, &s)) {
        return false;
    }
    *bytes = (uint16_t)((s >> 8) | ((s & 0x00C0u) << 2));
    *bits = (uint8_t)((s >> 1) & 0x0007u);
    return true;
}

bool st25r_tx_bytes(st25r_handle_t h, uint16_t bytes, uint8_t bits)
{
    const uint16_t ntx = (uint16_t)(((bytes & 0x01FFu) << 3) | (bits & 0x07u));
    return st25r_reg_write16(h, ST25R_REG_NUM_TX_BYTES1, ntx);
}

/* ------------------------------------------------------------------------- */
/* Interrupts                                                                */
/* ------------------------------------------------------------------------- */
bool st25r_irq_read(st25r_handle_t h, uint32_t *irq)
{
    uint8_t err = 0;
    uint16_t main_nfc = 0;
    uint8_t passive = 0;
    if (!st25r_reg_read8(h, ST25R_REG_ERR_WUP_IRQ, &err) ||
        !st25r_reg_read16(h, ST25R_REG_MAIN_IRQ, &main_nfc) ||
        !st25r_reg_read8(h, ST25R_REG_PT_IRQ, &passive)) {
        return false;
    }
    *irq = ((uint32_t)main_nfc << 16) | ((uint32_t)err << 8) | passive;
    return true;
}

bool st25r_irq_read_active(st25r_handle_t h, uint32_t *irq)
{
    uint8_t err = 0;
    uint16_t main_nfc = 0;
    if (!st25r_reg_read8(h, ST25R_REG_ERR_WUP_IRQ, &err) ||
        !st25r_reg_read16(h, ST25R_REG_MAIN_IRQ, &main_nfc)) {
        return false;
    }
    *irq = ((uint32_t)main_nfc << 16) | ((uint32_t)err << 8);
    return true;
}

bool st25r_irq_clear(st25r_handle_t h)
{
    uint32_t discard = 0;
    return st25r_irq_read(h, &discard);
}

bool st25r_irq_mask_all(st25r_handle_t h, uint32_t mask)
{
    return st25r_reg_write32(h, ST25R_REG_MASK_MAIN_IRQ, mask);
}

bool st25r_irq_enable(st25r_handle_t h, uint32_t bits)
{
    uint32_t mask = 0;
    if (!st25r_reg_read32(h, ST25R_REG_MASK_MAIN_IRQ, &mask)) {
        return false;
    }
    return st25r_irq_mask_all(h, mask & ~bits);
}

bool st25r_irq_disable(st25r_handle_t h, uint32_t bits)
{
    uint32_t mask = 0;
    if (!st25r_reg_read32(h, ST25R_REG_MASK_MAIN_IRQ, &mask)) {
        return false;
    }
    return st25r_irq_mask_all(h, mask | bits);
}

bool st25r_irq_asserted(st25r_handle_t h)
{
    if (h->cfg.pin_irq == GPIO_NUM_NC) {
        return true; /* no IRQ line: always poll the registers */
    }
    return gpio_get_level(h->cfg.pin_irq) != 0;
}

uint32_t st25r_wait_irq_ex(st25r_handle_t h, uint32_t bits, uint32_t timeout_ms, bool *timed_out)
{
    const int64_t start = esp_timer_get_time();
    const int64_t budget = (int64_t)timeout_ms * 1000;
    uint32_t acc = 0;
    if (timed_out) {
        *timed_out = false;
    }
    for (;;) {
        if (st25r_irq_asserted(h)) {
            uint32_t v = 0;
            if (st25r_irq_read(h, &v)) {
                acc |= v;
                if (acc & bits) {
                    return acc;
                }
            }
        }
        if ((esp_timer_get_time() - start) >= budget) {
            if (timed_out) {
                *timed_out = true;
            }
            return acc;
        }
        vTaskDelay(0);
    }
}

uint32_t st25r_wait_irq(st25r_handle_t h, uint32_t bits, uint32_t timeout_ms)
{
    return st25r_wait_irq_ex(h, bits, timeout_ms, NULL);
}

/* ------------------------------------------------------------------------- */
/* Oscillator / field                                                        */
/* ------------------------------------------------------------------------- */
bool st25r_enable_osc(st25r_handle_t h)
{
    uint8_t v = 0;
    if (!st25r_reg_read8(h, ST25R_REG_OP_CONTROL, &v)) {
        return false;
    }
    if ((v & ST25R_OP_EN) == 0) {
        if (!st25r_irq_enable(h, ST25R_IRQ_OSC) || !st25r_irq_clear(h)) {
            return false;
        }
        if (!st25r_reg_set(h, ST25R_REG_OP_CONTROL, ST25R_OP_EN)) {
            return false;
        }
        const uint32_t irq = st25r_wait_irq(h, ST25R_IRQ_OSC, 50);
        st25r_irq_disable(h, ST25R_IRQ_OSC);
        if ((irq & ST25R_IRQ_OSC) == 0) {
            uint8_t aux = 0;
            if (!st25r_reg_read8(h, ST25R_REG_AUX_DISPLAY, &aux) || (aux & ST25R_AUX_DISP_OSC_OK) == 0) {
                return false;
            }
        }
    }
    uint8_t aux = 0;
    return st25r_reg_read8(h, ST25R_REG_AUX_DISPLAY, &aux) && (aux & ST25R_AUX_DISP_OSC_OK);
}

bool st25r_field_off(st25r_handle_t h)
{
    return st25r_cmd(h, ST25R_CMD_STOP_ALL_ACTIVITIES) &&
           st25r_reg_modify(h, ST25R_REG_OP_CONTROL, 0, ST25R_OP_RX_EN | ST25R_OP_TX_EN);
}

/* ------------------------------------------------------------------------- */
/* Chip bring-up                                                             */
/* ------------------------------------------------------------------------- */
esp_err_t st25r_chip_init(st25r_handle_t h)
{
    /* The chip needs a moment after power-on (a warm boot through USB can
     * leave it in a transient state). */
    vTaskDelay(pdMS_TO_TICKS(50));

    uint8_t ident = 0;
    bool detected = false;
    for (int attempt = 0; attempt < 5 && !detected; ++attempt) {
        if (st25r_reg_read8(h, ST25R_REG_IC_IDENTITY, &ident)) {
            detected = (((ident >> 3) & 0x1Fu) == 0x05u);
        }
        if (!detected) {
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }
    if (!detected) {
        ESP_LOGE(TAG, "ST25R3916 not detected (identity 0x%02X)", ident);
        return ESP_ERR_NOT_FOUND;
    }
    ESP_LOGI(TAG, "ST25R3916 detected (identity 0x%02X, rev %u)", ident, ident & 0x07u);

    /* Drop anything a previous run left behind. */
    st25r_cmd(h, ST25R_CMD_STOP_ALL_ACTIVITIES);
    st25r_reg_modify(h, ST25R_REG_OP_CONTROL, 0, ST25R_OP_TX_EN | ST25R_OP_RX_EN);
    vTaskDelay(pdMS_TO_TICKS(2));

    if (!st25r_cmd(h, ST25R_CMD_SET_DEFAULT)) {
        return ESP_FAIL;
    }

    /* Overheat protection must be configured after power-on / set default. */
    const uint8_t protection[2] = {0x04, 0x10};
    if (!st25r_cmd_write(h, ST25R_CMD_TEST_ACCESS, protection, sizeof(protection))) {
        return ESP_FAIL;
    }

    /* IO_CONF1 = 0x00 (MCU_CLK off), IO_CONF2 = sup3v | MISO pull-downs | io_drv_lvl */
    const uint16_t io_conf = (uint16_t)(ST25R_IO2_SUP3V | ST25R_IO2_MISO_PD1 | ST25R_IO2_MISO_PD2 | ST25R_IO2_IO_DRV_LVL);
    if (!st25r_reg_write16(h, ST25R_REG_IO_CONF1, io_conf)) {
        return ESP_FAIL;
    }
    /* TX driver: AM modulation depth index 0xD, d_res = 0 (1 ohm). */
    st25r_reg_write8(h, ST25R_REG_TX_DRIVER, 0xD0);
    st25r_reg_modify(h, ST25R_REG_IO_CONF1, 0x07, 0x07); /* no MCU_CLK, no LF clock */

    /* Antenna driver non-overlap calibration. */
    st25r_reg_write8(h, ST25R_B(ST25R_REG_B_RES_AM_MOD), 0x80);
    st25r_reg_set(h, ST25R_REG_IO_CONF2, ST25R_IO2_AAT_EN);
    st25r_reg_write8(h, ST25R_B(ST25R_REG_B_RES_AM_MOD), 0x00);

    /* External field detector thresholds (activation/deactivation). */
    st25r_reg_write8(h, ST25R_REG_EFD_ACT_THRESHOLD, 0x13);
    st25r_reg_write8(h, ST25R_REG_EFD_DEACT_THRESHOLD, 0x02);

    /* Passive target frame delay compensation and modulation depth. */
    st25r_reg_modify(h, ST25R_REG_NFCIP1_PT, (uint8_t)(0x05u << 4), 0xF0);
    st25r_reg_write8(h, ST25R_REG_PT_MOD, 0x5F);
    st25r_reg_write8(h, ST25R_B(ST25R_REG_B_EMD_SUPPRESSION), 0x40);

    st25r_reg_write8(h, ST25R_REG_ANT_TUNE1, 0x82);
    st25r_reg_write8(h, ST25R_REG_ANT_TUNE2, 0x82);

    /* Automatic external field detector. */
    st25r_reg_set(h, ST25R_REG_OP_CONTROL, ST25R_OP_EN_FD_MASK);
    st25r_cmd(h, ST25R_CMD_CLEAR_FIFO);

    /* Keep only the error interrupts unmasked while the oscillator starts. */
    if (!st25r_irq_mask_all(h, 0xFFFF00FFu) || !st25r_irq_clear(h)) {
        return ESP_FAIL;
    }
    if (!st25r_enable_osc(h)) {
        ESP_LOGE(TAG, "Oscillator did not start");
        return ESP_FAIL;
    }
    st25r_irq_mask_all(h, 0);

    if (!st25r_cmd(h, ST25R_CMD_ADJUST_REGULATORS)) {
        return ESP_FAIL;
    }
    vTaskDelay(pdMS_TO_TICKS(5));

    uint8_t reg_display = 0;
    if (st25r_reg_read8(h, ST25R_B(0x2C), &reg_display)) {
        ESP_LOGD(TAG, "Regulator display 0x%02X", reg_display);
    }
    h->ready = true;
    return ESP_OK;
}

/* ------------------------------------------------------------------------- */
/* Public lifecycle                                                          */
/* ------------------------------------------------------------------------- */
esp_err_t st25r_init(const st25r_config_t *cfg, st25r_handle_t *out)
{
    if (!cfg || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    if (cfg->pin_sclk == GPIO_NUM_NC || cfg->pin_mosi == GPIO_NUM_NC || cfg->pin_miso == GPIO_NUM_NC ||
        cfg->pin_cs == GPIO_NUM_NC) {
        return ESP_ERR_INVALID_ARG;
    }

    st25r_handle_t h = (st25r_handle_t)calloc(1, sizeof(*h));
    if (!h) {
        return ESP_ERR_NO_MEM;
    }
    h->cfg = *cfg;
    h->clock_hz = cfg->clock_hz ? cfg->clock_hz : ST25R_DEFAULT_CLOCK_HZ;
    h->lock = xSemaphoreCreateRecursiveMutex();
    if (!h->lock) {
        free(h);
        return ESP_ERR_NO_MEM;
    }

    /* CS idles high before the bus is up. */
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << cfg->pin_cs,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&io);
    if (err != ESP_OK) {
        goto fail;
    }
    gpio_set_level(cfg->pin_cs, 1);

    if (cfg->pin_irq != GPIO_NUM_NC) {
        io.pin_bit_mask = 1ULL << cfg->pin_irq;
        io.mode = GPIO_MODE_INPUT;
        err = gpio_config(&io);
        if (err != ESP_OK) {
            goto fail;
        }
    }

    spi_bus_config_t bus = {
        .mosi_io_num = cfg->pin_mosi,
        .miso_io_num = cfg->pin_miso,
        .sclk_io_num = cfg->pin_sclk,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4096,
    };
    err = spi_bus_initialize(cfg->host, &bus, SPI_DMA_DISABLED);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_initialize failed: %s", esp_err_to_name(err));
        goto fail;
    }

    h->hw = spi_hw_of(h);
    if (!h->hw) {
        ESP_LOGE(TAG, "SPI host %d is not supported by the raw register path", (int)cfg->host);
        err = ESP_ERR_NOT_SUPPORTED;
        spi_bus_free(cfg->host);
        goto fail;
    }
    apply_raw_clock(h);

    err = st25r_chip_init(h);
    if (err != ESP_OK) {
        spi_bus_free(cfg->host);
        goto fail;
    }

    ESP_LOGI(TAG, "ready: SCLK=%d MOSI=%d MISO=%d CS=%d IRQ=%d %lu Hz", (int)cfg->pin_sclk, (int)cfg->pin_mosi,
             (int)cfg->pin_miso, (int)cfg->pin_cs, (int)cfg->pin_irq, (unsigned long)h->clock_hz);
    *out = h;
    return ESP_OK;

fail:
    if (h->lock) {
        vSemaphoreDelete(h->lock);
    }
    free(h);
    return err;
}

esp_err_t st25r_deinit(st25r_handle_t h)
{
    if (!h) {
        return ESP_ERR_INVALID_ARG;
    }
    st25r_picc_end(h);

    st25r_lock(h);
    st25r_field_off(h);
    st25r_unlock(h);

    spi_bus_free(h->cfg.host);
    vSemaphoreDelete(h->lock);
    free(h);
    return ESP_OK;
}

esp_err_t st25r_chip_info(st25r_handle_t h, uint8_t *ic_type, uint8_t *rev)
{
    if (!h || !h->ready || !ic_type || !rev) {
        return ESP_ERR_INVALID_ARG;
    }
    st25r_lock(h);
    uint8_t v = 0;
    const bool ok = st25r_reg_read8(h, ST25R_REG_IC_IDENTITY, &v);
    st25r_unlock(h);
    if (!ok) {
        return ESP_FAIL;
    }
    *ic_type = (uint8_t)((v >> 3) & 0x1Fu);
    *rev = (uint8_t)(v & 0x07u);
    return ESP_OK;
}

/* ------------------------------------------------------------------------- */
/* IRQ wake-up                                                               */
/* ------------------------------------------------------------------------- */
esp_err_t st25r_wakeup_enable(st25r_handle_t h, st25r_wake_mode_t mode)
{
    if (!h || h->cfg.pin_irq == GPIO_NUM_NC) {
        return ESP_ERR_INVALID_ARG;
    }
    if (mode == ST25R_WAKE_LIGHT_SLEEP) {
        esp_err_t err = gpio_wakeup_enable(h->cfg.pin_irq, GPIO_INTR_HIGH_LEVEL);
        if (err != ESP_OK) {
            return err;
        }
        return esp_sleep_enable_gpio_wakeup();
    }
#if SOC_PM_SUPPORT_EXT1_WAKEUP
    return esp_sleep_enable_ext1_wakeup_io(1ULL << (uint32_t)h->cfg.pin_irq, ESP_EXT1_WAKEUP_ANY_HIGH);
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t st25r_wakeup_disable(st25r_handle_t h, st25r_wake_mode_t mode)
{
    if (!h || h->cfg.pin_irq == GPIO_NUM_NC) {
        return ESP_ERR_INVALID_ARG;
    }
    if (mode == ST25R_WAKE_LIGHT_SLEEP) {
        gpio_wakeup_disable(h->cfg.pin_irq);
        return esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO);
    }
#if SOC_PM_SUPPORT_EXT1_WAKEUP
    return esp_sleep_disable_ext1_wakeup_io(1ULL << (uint32_t)h->cfg.pin_irq);
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}
