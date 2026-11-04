#ifndef __HAL_NFC_H__
#define __HAL_NFC_H__

/*********************************************************************
 * INCLUDES
 */
#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"

#include "sys_cfg.h"

#ifdef __cplusplus
extern "C" {
#endif

/*********************************************************************
 * MACROS
 */
#define HAL_NFC_TAG                     "HAL_NFC"

/* 与 nfc_test / M5 Unit NFC 实测接线一致；只有 n16r8 有 NFC。
   NFC 单独占用 SPI3（EPD 用 SPI2），互不干扰。 */
#define HAL_NFC_SPI_HOST                SPI3_HOST
#define HAL_NFC_PIN_SCLK                GPIO_NUM_10
#define HAL_NFC_PIN_MOSI                GPIO_NUM_19
#define HAL_NFC_PIN_MISO                GPIO_NUM_9
#define HAL_NFC_PIN_CS                  GPIO_NUM_20
#define HAL_NFC_PIN_IRQ                 GPIO_NUM_7
#define HAL_NFC_SPI_CLOCK_HZ            (10000000)

#define HAL_NFC_WAKE_ENABLE             (1)
#define HAL_NFC_PICC_TASK_PRIO          (6)
#define HAL_NFC_PICC_TASK_CORE          (1)
#define HAL_NFC_PICC_TASK_STACK         (4096)

#define HAL_NFC_MAX_CARDS               (16)
#define HAL_NFC_MAX_CARD_BYTES          (4096)
#define HAL_NFC_SCAN_TIMEOUT_MS         (30000)

/*********************************************************************
 * TYPEDEFS
 */
typedef enum {
    HAL_NFC_KIND_M1_UID = 0,
    HAL_NFC_KIND_NTAG213 = 1,
    HAL_NFC_KIND_NTAG215 = 2,
    HAL_NFC_KIND_NTAG216 = 3,
    HAL_NFC_KIND_M1_1K = 4,
    HAL_NFC_KIND_M1_2K = 5,
    HAL_NFC_KIND_M1_4K = 6,
} hal_nfc_kind_t;

typedef enum {
    HAL_NFC_DATA_NONE = 0,
    HAL_NFC_DATA_NTAG_PAGES = 1,
    HAL_NFC_DATA_M1_BLOCKS = 2,
} hal_nfc_data_format_t;

typedef struct {
    hal_nfc_kind_t kind;
    hal_nfc_data_format_t data_format;
    uint8_t atqa[2];        /* 空中顺序 */
    uint8_t sak;
    uint8_t uid_len;        /* 4 / 7 */
    uint8_t uid[10];
    const uint8_t *mem;     /* NTAG 页镜像 / M1 块镜像，可为 NULL */
    uint16_t mem_len;
} hal_nfc_card_t;

/*********************************************************************
 * GLOBAL FUNCTIONS
 */
esp_err_t hal_nfc_init(void);
void hal_nfc_deinit(void);
bool hal_nfc_ready(void);
esp_err_t hal_nfc_chip_info(uint8_t *ic_type, uint8_t *rev);

esp_err_t hal_nfc_reader_begin(void);
esp_err_t hal_nfc_reader_end(void);
esp_err_t hal_nfc_reader_poll(hal_nfc_card_t *card, uint32_t timeout_ms);
esp_err_t hal_nfc_reader_transceive(const uint8_t *tx, uint16_t tx_len,
                                    uint8_t *rx, uint16_t *rx_len, uint32_t timeout_ms);
esp_err_t hal_nfc_reader_halt(void);

esp_err_t hal_nfc_emu_start(const hal_nfc_card_t *card);
esp_err_t hal_nfc_emu_stop(void);
esp_err_t hal_nfc_emu_set_card(const hal_nfc_card_t *card);
bool hal_nfc_emu_running(void);

bool hal_nfc_irq_active(void);
esp_err_t hal_nfc_sleep_arm(void);
void hal_nfc_sleep_disarm(void);
bool hal_nfc_sleep_armed(void);

#ifdef __cplusplus
}
#endif

#endif /* __HAL_NFC_H__ */
