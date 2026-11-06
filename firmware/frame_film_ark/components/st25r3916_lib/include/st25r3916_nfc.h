/*
 * SPDX-License-Identifier: MIT
 *
 * st25r3916_nfc - minimal ST25R3916 NFC library for ESP-IDF 5.5
 *
 * Features
 *  - chip bring-up (SPI + mandatory power-on sequence)
 *  - NFC-A card emulation (UID / ATQA / SAK identity, MIFARE Classic as an ID card)
 *  - NFC-A reader (REQA/WUPA -> ATQA -> anticollision -> SELECT -> UID)
 *  - IRQ pin as ESP light-sleep / deep-sleep wake-up source
 *
 * The library never stores card data: the application supplies the identity to
 * emulate and receives the identity that was read, so a "card wallet" lives
 * entirely on the application side.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Opaque device handle. One handle drives one ST25R3916. */
typedef struct st25r_dev *st25r_handle_t;

/** Maximum frame size the library will move through the FIFO. */
#define ST25R_MAX_FRAME 256

/* ------------------------------------------------------------------------- */
/* Configuration                                                             */
/* ------------------------------------------------------------------------- */

/**
 * @brief Hardware configuration.
 *
 * All pins are mandatory except @c pin_irq. The IRQ pin is only needed for
 * latency-sensitive use and for the wake-up feature.
 */
typedef struct {
    spi_host_device_t host; /**< SPI2_HOST (SPI3_HOST is supported when available) */
    gpio_num_t pin_sclk;    /**< SPI clock */
    gpio_num_t pin_mosi;    /**< SPI MOSI (chip data in) */
    gpio_num_t pin_miso;    /**< SPI MISO (chip data out) */
    gpio_num_t pin_cs;      /**< chip select, driven by this library */
    gpio_num_t pin_irq;     /**< IRQ output of the chip, GPIO_NUM_NC if unused */
    uint32_t clock_hz;      /**< SPI clock, 0 selects the default 10 MHz */
} st25r_config_t;

/** @brief Default configuration (pins left unset, fill them in). */
#define ST25R_CONFIG_DEFAULT()            \
    {                                     \
        .host = SPI2_HOST,                \
        .pin_sclk = GPIO_NUM_NC,          \
        .pin_mosi = GPIO_NUM_NC,          \
        .pin_miso = GPIO_NUM_NC,          \
        .pin_cs = GPIO_NUM_NC,            \
        .pin_irq = GPIO_NUM_NC,           \
        .clock_hz = 10000000,             \
    }

/* ------------------------------------------------------------------------- */
/* Card description                                                          */
/* ------------------------------------------------------------------------- */

/** @brief Card technology. Emulation supports NFC-A; B/F are reserved. */
typedef enum {
    ST25R_TECH_NFC_A = 0,
    ST25R_TECH_NFC_B,
    ST25R_TECH_NFC_F,
} st25r_tech_t;

/** @brief Coarse card type derived from the SAK byte / ATQA. */
typedef enum {
    ST25R_CARD_UNKNOWN = 0,
    ST25R_CARD_MIFARE_CLASSIC_1K,
    ST25R_CARD_MIFARE_CLASSIC_2K,
    ST25R_CARD_MIFARE_CLASSIC_4K,
    ST25R_CARD_MIFARE_ULTRALIGHT,
    ST25R_CARD_MIFARE_PLUS,
    ST25R_CARD_MIFARE_DESFIRE,
    ST25R_CARD_ISO14443_4,
} st25r_card_type_t;

/** @brief Card identity, used both for reading and for emulation. */
typedef struct {
    st25r_tech_t tech; /**< technology of the card */
    st25r_card_type_t type; /**< coarse type derived from SAK/ATQA */
    uint16_t atqa;     /**< ATQA, little endian as on air (e.g. 0x0004 for M1) */
    uint8_t sak;       /**< SAK of the first select level */
    uint8_t uid_len;   /**< 4 or 7 (single or double size UID) */
    uint8_t uid[10];   /**< UID, only the first uid_len bytes are meaningful */
} st25r_card_t;

/** @brief Human readable card type name. */
const char *st25r_card_type_name(st25r_card_type_t type);

/* ------------------------------------------------------------------------- */
/* Lifecycle                                                                 */
/* ------------------------------------------------------------------------- */

/**
 * @brief Bring up SPI, the chip and its mandatory power-on sequence.
 *
 * @param[in]  cfg hardware configuration
 * @param[out] out device handle
 */
esp_err_t st25r_init(const st25r_config_t *cfg, st25r_handle_t *out);

/** @brief Stop all activities, release SPI and free the handle. */
esp_err_t st25r_deinit(st25r_handle_t h);

/** @brief Read the IC identity register (type 0x05, revision 0x00..0x07). */
esp_err_t st25r_chip_info(st25r_handle_t h, uint8_t *ic_type, uint8_t *rev);

/* ------------------------------------------------------------------------- */
/* Reader (PCD)                                                              */
/* ------------------------------------------------------------------------- */

/** @brief Start the reader: NFC-A initiator configuration and RF field on. */
esp_err_t st25r_reader_begin(st25r_handle_t h);

/** @brief Stop the reader and switch the RF field off. */
esp_err_t st25r_reader_end(st25r_handle_t h);

/**
 * @brief Poll for one NFC-A card and read its identity.
 *
 * Runs REQA/WUPA, the anticollision loop and SELECT. On success @p card is
 * filled with UID/ATQA/SAK/type and ESP_OK is returned.
 *
 * @param[in]  h          device handle
 * @param[out] card       identity of the card that answered
 * @param[in]  timeout_ms overall budget for this call
 *
 * @return ESP_OK, ESP_ERR_NOT_FOUND (no card), ESP_ERR_INVALID_STATE or an
 *         SPI error.
 */
esp_err_t st25r_reader_poll(st25r_handle_t h, st25r_card_t *card, uint32_t timeout_ms);

/**
 * @brief Send an ISO14443-A frame (CRC appended by the chip) and wait for the answer.
 *
 * Intended for application-level protocols (e.g. reading NDEF pages) that run
 * after st25r_reader_poll() returned a card.
 *
 * @param[in]     tx        frame payload, no CRC
 * @param[in]     tx_len    payload length
 * @param[out]    rx        receive buffer
 * @param[in,out] rx_len    buffer size in, received length out (CRC stripped)
 * @param[in]     timeout_ms response timeout
 */
esp_err_t st25r_reader_transceive(st25r_handle_t h, const uint8_t *tx, uint16_t tx_len, uint8_t *rx,
                                  uint16_t *rx_len, uint32_t timeout_ms);

/** @brief Send HLTA to put the selected card into the halt state. */
esp_err_t st25r_reader_halt(st25r_handle_t h);

/* ------------------------------------------------------------------------- */
/* Card emulation (PICC)                                                     */
/* ------------------------------------------------------------------------- */

/** @brief Emulation events delivered to the application callback. */
typedef enum {
    ST25R_PICC_EVT_FIELD_ON = 0, /**< a reader field appeared */
    ST25R_PICC_EVT_FIELD_OFF,    /**< the field disappeared */
    ST25R_PICC_EVT_SELECTED,     /**< the reader selected our UID */
    ST25R_PICC_EVT_FRAME,        /**< application level frame (CRC already removed) */
} st25r_picc_event_t;

/**
 * @brief Emulation callback.
 *
 * Called from the emulation task (or from st25r_picc_update()). Keep it short:
 * a MIFARE FDT is only ~87 us, a long callback makes the card look absent.
 *
 * For ST25R_PICC_EVT_FRAME the application may answer with
 * st25r_picc_transmit() and return true to keep the session selected, or
 * return false to fall back to the idle state (an "ID card" that only
 * presents its UID does exactly that).
 *
 * @param[in] h    device handle
 * @param[in] evt  event kind
 * @param[in] data frame payload for ST25R_PICC_EVT_FRAME, otherwise NULL
 * @param[in] len  frame length for ST25R_PICC_EVT_FRAME, otherwise 0
 * @param[in] ctx  user context
 * @return true to stay in the active state, false to end the session
 */
typedef bool (*st25r_picc_cb_t)(st25r_handle_t h, st25r_picc_event_t evt, const uint8_t *data, uint16_t len,
                                void *ctx);

/** @brief Emulation configuration. */
typedef struct {
    st25r_card_t card;              /**< identity to present */
    st25r_picc_cb_t callback;       /**< optional event callback */
    void *user_ctx;                 /**< context passed to the callback */
    uint32_t task_stack_size;       /**< 0 selects 4096 bytes */
    UBaseType_t task_priority;      /**< 0 = no internal task, call st25r_picc_update() yourself */
    BaseType_t task_core;           /**< task affinity, tskNO_AFFINITY by default */
} st25r_picc_config_t;

/**
 * @brief Start NFC-A card emulation.
 *
 * Configures the passive target logic, loads UID/ATQA/SAK into the chip and
 * either spawns the emulation task or leaves the stepping to the caller.
 *
 * @note NFC-B and NFC-F emulation are not implemented; they return
 *       ESP_ERR_NOT_SUPPORTED.
 */
esp_err_t st25r_picc_begin(st25r_handle_t h, const st25r_picc_config_t *cfg);

/** @brief Stop emulation and release the RF front end. */
esp_err_t st25r_picc_end(st25r_handle_t h);

/** @brief True while emulation is running. */
bool st25r_picc_is_running(st25r_handle_t h);

/**
 * @brief Advance the emulation state machine once.
 *
 * Only needed when the configuration asked for no internal task
 * (task_priority == 0); must be called in a tight loop then.
 */
esp_err_t st25r_picc_update(st25r_handle_t h);

/**
 * @brief Replace the identity while emulation is running.
 *
 * Takes effect on the next RF cycle (field off/on).
 */
esp_err_t st25r_picc_set_card(st25r_handle_t h, const st25r_card_t *card);

/**
 * @brief Send a frame from the emulated card (CRC appended by the chip).
 *
 * Meant to be called from the ST25R_PICC_EVT_FRAME callback. Only valid in the
 * active state.
 */
esp_err_t st25r_picc_transmit(st25r_handle_t h, const uint8_t *data, uint16_t len);

/* ------------------------------------------------------------------------- */
/* NTAG21x emulation (NFC-A, read/write, NDEF)                               */
/* ------------------------------------------------------------------------- */

/** @brief NTAG21x variant and its memory size. */
typedef enum {
    ST25R_NTAG_213 = 0, /**< 45 pages / 180 B, 144 B user memory */
    ST25R_NTAG_215,     /**< 135 pages / 540 B, 504 B user memory */
    ST25R_NTAG_216,     /**< 231 pages / 924 B, 888 B user memory */
} st25r_ntag_type_t;

/** @brief Size of the largest NTAG21x image (NTAG216). */
#define ST25R_NTAG_MAX_MEMORY 924

/** @brief Events reported while the NTAG is emulated. */
typedef enum {
    ST25R_NTAG_EVT_PAGE_WRITTEN = 0, /**< a reader wrote one page (4 bytes) */
    ST25R_NTAG_EVT_NDEF_CHANGED,     /**< st25r_ntag_set_ndef*() replaced the NDEF message */
} st25r_ntag_event_t;

/**
 * @brief NTAG callback, called from the emulation task.
 *
 * @param[in] h    device handle
 * @param[in] evt  event kind
 * @param[in] page page address for ST25R_NTAG_EVT_PAGE_WRITTEN
 * @param[in] len  number of bytes written (4)
 * @param[in] ctx  user context
 */
typedef void (*st25r_ntag_cb_t)(st25r_handle_t h, st25r_ntag_event_t evt, uint8_t page, uint16_t len,
                                void *ctx);

/** @brief NTAG21x emulation configuration. */
typedef struct {
    st25r_ntag_type_t type;   /**< NTAG213 / 215 / 216 */
    uint8_t uid[7];           /**< 7-byte UID (NTAG21x always uses double size UID) */
    uint8_t *memory;          /**< optional external image, at least the size of the variant, NULL = internal */
    bool writable;            /**< allow WRITE/COMPAT_WRITE from the reader */
    st25r_ntag_cb_t callback; /**< optional page/NDEF event callback */
    void *user_ctx;           /**< context passed to the callback */
    uint32_t task_stack_size; /**< 0 selects 4096 bytes */
    UBaseType_t task_priority;/**< 0 = no internal task, call st25r_picc_update() yourself */
    BaseType_t task_core;     /**< task affinity, tskNO_AFFINITY by default */
} st25r_ntag_config_t;

/**
 * @brief Start NTAG21x emulation.
 *
 * Builds the tag image (UID pages, BCC, capability container, empty NDEF),
 * loads it into the passive target logic and starts the emulation task.
 * Supported commands: GET_VERSION, READ, FAST_READ, WRITE, COMPAT_WRITE,
 * READ_CNT, READ_SIG, HALT.
 *
 * @note The image is always formatted on begin; use st25r_ntag_write_memory()
 *       or st25r_ntag_set_ndef() to load application data afterwards.
 */
esp_err_t st25r_ntag_begin(st25r_handle_t h, const st25r_ntag_config_t *cfg);

/** @brief Stop NTAG emulation (same as st25r_picc_end()). */
esp_err_t st25r_ntag_end(st25r_handle_t h);

/** @brief True while NTAG emulation is running. */
bool st25r_ntag_is_running(st25r_handle_t h);

/**
 * @brief Put an NDEF message into the tag (TLV 0x03 + message + 0xFE).
 *
 * @param[in] message raw NDEF message (header + type + payload)
 * @param[in] len     message length; must fit into the user memory
 */
esp_err_t st25r_ntag_set_ndef(st25r_handle_t h, const uint8_t *message, uint16_t len);

/** @brief Store a well formed NDEF Text record (UTF-8, language "en"). */
esp_err_t st25r_ntag_set_ndef_text(st25r_handle_t h, const char *text);

/** @brief Read the emulated memory image. */
esp_err_t st25r_ntag_read_memory(st25r_handle_t h, uint8_t page, uint8_t *data, uint8_t pages);

/** @brief Write the emulated memory image (no reader involved). */
esp_err_t st25r_ntag_write_memory(st25r_handle_t h, uint8_t page, const uint8_t *data, uint8_t pages);

/* ------------------------------------------------------------------------- */
/* IRQ wake-up                                                               */
/* ------------------------------------------------------------------------- */

/** @brief Sleep mode the IRQ pin should wake the MCU from. */
typedef enum {
    ST25R_WAKE_LIGHT_SLEEP = 0, /**< esp_light_sleep_start(), GPIO wake source */
    ST25R_WAKE_DEEP_SLEEP,      /**< esp_deep_sleep_start(), EXT1 wake source */
} st25r_wake_mode_t;

/**
 * @brief Arm the chip IRQ pin as a wake-up source.
 *
 * While emulating, the chip raises IRQ when a reader field appears, so a card
 * tap resumes the MCU. The chip keeps its configuration in light sleep; after
 * deep sleep the caller must call st25r_init() again.
 *
 * @param[in] mode sleep mode that will be entered
 */
esp_err_t st25r_wakeup_enable(st25r_handle_t h, st25r_wake_mode_t mode);

/** @brief Disarm the wake-up source again. */
esp_err_t st25r_wakeup_disable(st25r_handle_t h, st25r_wake_mode_t mode);

#ifdef __cplusplus
}
#endif
