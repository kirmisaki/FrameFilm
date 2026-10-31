// SPDX-License-Identifier: GPL-3.0-or-later
#include "hal_api.h"
#include "esp_sleep.h"
#include "esp_log.h"
static uint64_t wake_us;
static uint32_t led;
extern void c3_storage_lock(void);
extern void c3_storage_unlock(void);
extern void c3_raw_display_film(const unsigned char *data);
extern void c3_raw_epd_deinit(void);
extern uint8_t *service_file_get_buffer(void);
void hal_epd_display_film(const unsigned char *data) {
    (void)data;
    c3_storage_lock();
    // Resolve the single logical file while locked, avoiding a stale bank
    // pointer if an upload committed between the classic load and display.
    c3_raw_display_film(service_file_get_buffer());
    c3_storage_unlock();
}
void hal_epd_deinit(void) {
    c3_storage_lock();
    c3_raw_epd_deinit();
    c3_storage_unlock();
}
void film_hal_init(void) {
    // Bare C3 devboard: no assumed PCB latch, battery divider, LED or buttons.
    hal_epd_init();
    ESP_LOGW("C3", "devboard profile: battery/LED/buttons absent; use reset to wake");
}
void hal_sd_init(void) {}
int hal_sd_get_status(void) { return 0; }
void hal_sd_deinit(void) {}
int hal_sd_format(void) { return -1; }
void hal_bat_init(void) {}
int hal_bat_get_level(void) { return 255; }
int hal_bat_get_percent(void) { return 255; }
void hal_bat_deinit(void) {}
void hal_led_init(void) {}
void hal_led_deinit(void) {}
void hal_led_set_color(uint32_t color) { led = color; }
uint32_t hal_led_get_color(void) { return led; }
void hal_led_set_brightness(uint32_t brightness) { (void)brightness; }
uint32_t hal_led_get_brightness(void) { return 0; }
void hal_input_init(void) {}
void hal_input_deinit(void) {}
int hal_input_register_cb(input_press_type_t type,input_callback_t cb) { (void)type;(void)cb;return -1; }
int hal_input_unregister_cb(input_press_type_t type,input_callback_t cb) { (void)type;(void)cb;return -1; }
void hal_pwr_init(void) {}
bool hal_pwr_check_wakeup(void) { return esp_sleep_get_wakeup_cause()!=ESP_SLEEP_WAKEUP_TIMER; }
void hal_pwr_set_timer_wakeup(uint32_t minutes) { wake_us=(uint64_t)minutes*60000000; }
void hal_pwr_enter_sleep(void) {
    if(wake_us) ESP_ERROR_CHECK(esp_sleep_enable_timer_wakeup(wake_us));
    esp_deep_sleep_start();
}
