/*
 * SPDX-License-Identifier: MIT
 *
 * Internal interfaces shared by the ST25R3916 modules.
 */
#pragma once

#include "st25r3916_nfc.h"
#include "st25r3916_regs.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "soc/spi_struct.h"

/* Emulation states, mirroring the ISO14443-3 passive target state machine. */
typedef enum {
    ST25R_PICC_OFF = 0,
    ST25R_PICC_IDLE,
    ST25R_PICC_READY,
    ST25R_PICC_ACTIVE,
    ST25R_PICC_HALT,
} st25r_picc_state_t;

struct st25r_dev {
    st25r_config_t cfg;
    SemaphoreHandle_t lock;
    spi_dev_t *hw;
    spi_device_handle_t spi_dev;
    bool bus_owned; /* true: this library initialized the SPI bus and must free it */
    bool ready;
    bool reader_active;
    uint32_t clock_hz;

    struct {
        bool running;
        TaskHandle_t task;
        volatile bool stop;
        bool external_step; /* no internal task, st25r_picc_update() drives it */
        st25r_card_t card;
        st25r_picc_cb_t cb;
        void *ctx;
        st25r_picc_state_t state;
        uint32_t irq_pending;
        uint32_t bitrate; /* 0:106k 1:212k 2:424k */
        bool wakeup;
        uint8_t rx[ST25R_MAX_FRAME];
        uint16_t rx_len;
    } emu;

    struct {
        bool active;
        st25r_ntag_type_t type;
        uint8_t *mem;       /* image in use: cfg->memory or storage[] */
        uint16_t size;      /* image size in bytes */
        uint8_t pages;      /* total pages */
        uint8_t last_user;  /* last writable user page */
        bool writable;
        st25r_ntag_cb_t cb;
        void *ctx;
        uint8_t storage[ST25R_NTAG_MAX_MEMORY];
    } ntag;
};

/* ------------------------------------------------------------------------- */
/* Locking                                                                   */
/* ------------------------------------------------------------------------- */
void st25r_lock(st25r_handle_t h);
void st25r_unlock(st25r_handle_t h);

/* ------------------------------------------------------------------------- */
/* Register / FIFO / IRQ primitives (lock must be held)                      */
/* ------------------------------------------------------------------------- */
bool st25r_spi_write(st25r_handle_t h, const uint8_t *tx, uint16_t len);
bool st25r_spi_read(st25r_handle_t h, uint8_t *rx, uint16_t len);
bool st25r_reg_write8(st25r_handle_t h, uint16_t reg, uint8_t v);
bool st25r_reg_read8(st25r_handle_t h, uint16_t reg, uint8_t *v);
bool st25r_reg_write16(st25r_handle_t h, uint16_t reg, uint16_t v);
bool st25r_reg_read16(st25r_handle_t h, uint16_t reg, uint16_t *v);
bool st25r_reg_write32(st25r_handle_t h, uint16_t reg, uint32_t v);
bool st25r_reg_read32(st25r_handle_t h, uint16_t reg, uint32_t *v);
bool st25r_reg_modify(st25r_handle_t h, uint16_t reg, uint8_t set, uint8_t clear);
bool st25r_reg_set(st25r_handle_t h, uint16_t reg, uint8_t bits);
bool st25r_reg_clear(st25r_handle_t h, uint16_t reg, uint8_t bits);

bool st25r_cmd(st25r_handle_t h, uint8_t cmd);
bool st25r_cmd_write(st25r_handle_t h, uint8_t cmd, const uint8_t *data, uint16_t len);

bool st25r_fifo_write(st25r_handle_t h, const uint8_t *data, uint16_t len);
bool st25r_fifo_read(st25r_handle_t h, uint8_t *data, uint16_t len);
bool st25r_fifo_size(st25r_handle_t h, uint16_t *bytes, uint8_t *bits);
bool st25r_tx_bytes(st25r_handle_t h, uint16_t bytes, uint8_t bits);

bool st25r_irq_read(st25r_handle_t h, uint32_t *irq);
bool st25r_irq_read_active(st25r_handle_t h, uint32_t *irq);
bool st25r_irq_clear(st25r_handle_t h);
bool st25r_irq_mask_all(st25r_handle_t h, uint32_t mask);
bool st25r_irq_enable(st25r_handle_t h, uint32_t bits);
bool st25r_irq_disable(st25r_handle_t h, uint32_t bits);

/* Wait for any of @p bits; returns the accumulated IRQ value (0 on timeout). */
uint32_t st25r_wait_irq(st25r_handle_t h, uint32_t bits, uint32_t timeout_ms);
/* Blocking wait that also returns timeout through @p timed_out (may be NULL). */
uint32_t st25r_wait_irq_ex(st25r_handle_t h, uint32_t bits, uint32_t timeout_ms, bool *timed_out);

bool st25r_enable_osc(st25r_handle_t h);
bool st25r_field_off(st25r_handle_t h);
bool st25r_irq_asserted(st25r_handle_t h);

/* Chip bring-up and mode selection (called by st25r_init / the mode modules). */
esp_err_t st25r_chip_init(st25r_handle_t h);
esp_err_t st25r_pcd_configure(st25r_handle_t h);
esp_err_t st25r_picc_configure(st25r_handle_t h, const st25r_card_t *card);

/* Emulation state machine step (lock must be held). */
void st25r_picc_step(st25r_handle_t h);

/* Reader internals shared with the emulation module. */
bool st25r_pcd_request(st25r_handle_t h, bool wakeup, uint16_t *atqa);
bool st25r_pcd_anticollision(st25r_handle_t h, uint8_t lv, uint8_t out[5]);
bool st25r_pcd_select_level(st25r_handle_t h, uint8_t lv, uint8_t out[5], uint8_t *sak);
esp_err_t st25r_pcd_transceive(st25r_handle_t h, const uint8_t *tx, uint16_t tx_len, uint8_t *rx,
                               uint16_t *rx_len, uint32_t timeout_ms);
