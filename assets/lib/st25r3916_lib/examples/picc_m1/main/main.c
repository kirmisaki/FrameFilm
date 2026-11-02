/*
 * ST25R3916 card emulation example: MIFARE Classic 1K presented as an ID card.
 *
 * The reader sees UID/ATQA/SAK and selects the card; data commands (0x30 ...)
 * are not answered, which is exactly the "UID only" behaviour this example
 * wants to show. Any NFC-A identity can be presented the same way (Ultralight,
 * NTAG, DESFire as long as only the identity is needed).
 */
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "st25r3916_nfc.h"

static const char *TAG = "picc_m1";

/* M5 Unit NFC wiring on ESP32-S3, adjust to your board. */
#define PIN_SCLK 10
#define PIN_MOSI 19
#define PIN_MISO 9
#define PIN_CS   20
#define PIN_IRQ  7

static bool on_picc_event(st25r_handle_t h, st25r_picc_event_t evt, const uint8_t *data, uint16_t len, void *ctx)
{
    (void)h;
    (void)ctx;
    switch (evt) {
        case ST25R_PICC_EVT_FIELD_ON:
            ESP_LOGI(TAG, "reader field detected");
            break;
        case ST25R_PICC_EVT_FIELD_OFF:
            ESP_LOGI(TAG, "field gone, waiting for the next reader");
            break;
        case ST25R_PICC_EVT_SELECTED:
            ESP_LOGI(TAG, "selected by the reader (UID exchange done)");
            break;
        case ST25R_PICC_EVT_FRAME: {
            char hex[3 * 16 + 1] = {0};
            const uint16_t shown = (len > 16) ? 16 : len;
            for (uint16_t i = 0; i < shown; ++i) {
                snprintf(hex + i * 3, 4, "%02X ", data[i]);
            }
            ESP_LOGI(TAG, "frame %u bytes: %s", len, hex);
            break;
        }
        default:
            break;
    }
    /* false: UID-only card, do not keep the session selected. */
    return false;
}

void app_main(void)
{
    st25r_config_t cfg = ST25R_CONFIG_DEFAULT();
    cfg.pin_sclk = PIN_SCLK;
    cfg.pin_mosi = PIN_MOSI;
    cfg.pin_miso = PIN_MISO;
    cfg.pin_cs = PIN_CS;
    cfg.pin_irq = PIN_IRQ;

    st25r_handle_t nfc = NULL;
    ESP_ERROR_CHECK(st25r_init(&cfg, &nfc));

    uint8_t type = 0, rev = 0;
    ESP_ERROR_CHECK(st25r_chip_info(nfc, &type, &rev));
    ESP_LOGI(TAG, "chip type 0x%02X rev %u", type, rev);

    st25r_picc_config_t picc = {
        .card =
            {
                .tech = ST25R_TECH_NFC_A,
                .atqa = 0x0004,
                .sak = 0x08,
                .uid_len = 4,
                .uid = {0x11, 0x22, 0x33, 0x44},
            },
        .callback = on_picc_event,
        .user_ctx = NULL,
        .task_stack_size = 4096,
        .task_priority = 5,
        .task_core = tskNO_AFFINITY,
    };
    ESP_ERROR_CHECK(st25r_picc_begin(nfc, &picc));
    ESP_LOGI(TAG, "presenting MIFARE Classic 1K UID 11223344 - tap a reader");

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
