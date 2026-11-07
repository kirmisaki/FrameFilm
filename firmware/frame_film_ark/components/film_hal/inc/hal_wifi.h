#ifndef __HAL_WIFI_H__
#define __HAL_WIFI_H__

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "esp_wifi_types.h"

/* WiFi driver and runtime STA configuration stay in HAL. */
esp_err_t hal_wifi_init(bool ram_storage);
void hal_wifi_deinit(void);
esp_err_t hal_wifi_connect(const char *ssid, const char *password);
esp_err_t hal_wifi_reconnect(void);
void hal_wifi_disconnect(void);
bool hal_wifi_initialized(void);
bool hal_wifi_connected(void);
esp_err_t hal_wifi_get_sta_config(wifi_config_t *config);
esp_err_t hal_wifi_set_sta_config(const wifi_config_t *config);
esp_err_t hal_wifi_set_ram_storage(bool ram_storage);
esp_err_t hal_wifi_get_power_save(wifi_ps_type_t *type);
esp_err_t hal_wifi_set_power_save(wifi_ps_type_t type);
/* Temporary direct-transfer AP; credentials are RAM-only. */
esp_err_t hal_wifi_softap_identity(char *ssid, size_t size);
esp_err_t hal_wifi_softap_start(const char *ssid, const char *password);
esp_err_t hal_wifi_softap_stop(void);
#ifdef ARK_AP_FILE_BENCH
esp_err_t hal_wifi_ap_file_bench_start(const char *password, char *ssid, size_t ssid_size);
#endif

#endif /* __HAL_WIFI_H__ */
