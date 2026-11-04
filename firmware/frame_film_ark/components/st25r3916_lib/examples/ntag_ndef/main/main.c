/*
 * ST25R3916 NTAG21x emulation example.
 *
 * Presents an NTAG215 whose memory lives inside the library. A phone can read
 * the NDEF text record below, and - because writable = true - can also write a
 * new NDEF message; every written page is reported through the callback so the
 * application can persist it in its own card wallet storage.
 */
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "st25r3916_nfc.h"

static const char *TAG = "ntag_ndef";

/* M5 Unit NFC wiring on ESP32-S3, adjust to your board. */
#define PIN_SCLK 10
#define PIN_MOSI 19
#define PIN_MISO 9
#define PIN_CS   20
#define PIN_IRQ  7

/* Minimal NDEF TLV dump: find 0x03, then the record inside. */
static void log_ndef(st25r_handle_t h)
{
    uint8_t page4[64];
    if (st25r_ntag_read_memory(h, 4, page4, sizeof(page4) / 4) != ESP_OK) {
        return;
    }
    if (page4[0] != 0x03) {
        ESP_LOGI(TAG, "tag holds no NDEF message");
        return;
    }
    uint16_t len = page4[1];
    uint16_t off = 2;
    if (len == 0xFF) {
        len = (uint16_t)((page4[2] << 8) | page4[3]);
        off = 4;
    }
    ESP_LOGI(TAG, "NDEF message: %u bytes, first type 0x%02X", len, page4[off + 1]);
}

/* The callback runs inside the NFC task, where a UART log would break the
 * reader's timing window: only raise a flag here and print from app_main(). */
static volatile bool s_page_written;
static volatile uint8_t s_page;

static void on_ntag_event(st25r_handle_t h, st25r_ntag_event_t evt, uint8_t page, uint16_t len, void *ctx)
{
    (void)h;
    (void)ctx;
    if (evt == ST25R_NTAG_EVT_PAGE_WRITTEN) {
        s_page = page;
        s_page_written = true;
        (void)len;
    } else if (evt == ST25R_NTAG_EVT_NDEF_CHANGED) {
        ESP_LOGD(TAG, "NDEF replaced locally (%u bytes)", len);
    }
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

    st25r_ntag_config_t ntag = {
        .type = ST25R_NTAG_215,
        .uid = {0x04, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66},
        .memory = NULL, /* use the image inside the handle */
        .writable = true,
        .callback = on_ntag_event,
        .task_stack_size = 4096,
        .task_priority = 5,
        .task_core = tskNO_AFFINITY,
    };
    ESP_ERROR_CHECK(st25r_ntag_begin(nfc, &ntag));
    ESP_ERROR_CHECK(st25r_ntag_set_ndef_text(nfc, "Hello from ST25R3916"));

    ESP_LOGI(TAG, "NTAG215 ready - tap a phone (read and write both work)");
    for (;;) {
        if (s_page_written) {
            s_page_written = false;
            ESP_LOGI(TAG, "reader wrote page %u", s_page);
            log_ndef(nfc);
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
