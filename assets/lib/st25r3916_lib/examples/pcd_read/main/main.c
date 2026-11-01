/*
 * ST25R3916 reader example: poll NFC-A cards and hand the identity to the
 * application, which is where a "card wallet" would store or look it up.
 *
 * The library deliberately keeps no card database: the callback below is the
 * only place card data crosses from the NFC layer into the application.
 */
#include <inttypes.h>
#include <stdio.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "st25r3916_nfc.h"

static const char *TAG = "pcd_read";

/* M5 Unit NFC wiring on ESP32-S3, adjust to your board. */
#define PIN_SCLK 10
#define PIN_MOSI 19
#define PIN_MISO 9
#define PIN_CS   20
#define PIN_IRQ  7

/* Application side of the card wallet. Return true when the card is known. */
static bool wallet_lookup(const st25r_card_t *card)
{
    char uid[3 * 10 + 1] = {0};
    for (uint8_t i = 0; i < card->uid_len; ++i) {
        snprintf(uid + i * 3, 4, "%02X ", card->uid[i]);
    }
    ESP_LOGI(TAG, "card: %s  UID(%u): %s ATQA=0x%04X SAK=0x%02X", st25r_card_type_name(card->type), card->uid_len,
             uid, card->atqa, card->sak);
    /* Compare against the stored wallet here, e.g. memcmp(card->uid, ...). */
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
    ESP_ERROR_CHECK(st25r_reader_begin(nfc));
    ESP_LOGI(TAG, "polling for NFC-A cards");

    st25r_card_t card = {0};
    for (;;) {
        const esp_err_t err = st25r_reader_poll(nfc, &card, 500);
        if (err == ESP_OK) {
            (void)wallet_lookup(&card);
            /* One card per tap: halt it so the next poll finds a new one. */
            ESP_ERROR_CHECK(st25r_reader_halt(nfc));
            vTaskDelay(pdMS_TO_TICKS(500));
        } else if (err != ESP_ERR_NOT_FOUND) {
            ESP_LOGW(TAG, "poll failed: %s", esp_err_to_name(err));
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}
