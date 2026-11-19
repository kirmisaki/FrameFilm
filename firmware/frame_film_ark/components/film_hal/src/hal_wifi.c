#include <string.h>

#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "hal_wifi.h"
#include <stdio.h>
#include "esp_mac.h"
#include "sys_log.h"

#define HAL_WIFI_TAG            "HAL_WIFI"
#define HAL_WIFI_SCAN_MAX_AP    (16)

static bool s_netif_ready;
static bool s_initialized;
static volatile bool s_connected;
static esp_event_handler_instance_t s_wifi_handler;
static esp_event_handler_instance_t s_ip_handler;

/* 临时诊断：把 STA 断开原因翻成可读字符串，用来区分"收不到路由器"和"发不出去"。 */
static const char *wifi_reason_name(uint8_t reason)
{
    switch(reason)
    {
    case 1:   return "UNSPECIFIED";
    case 2:   return "AUTH_EXPIRE";
    case 3:   return "AUTH_LEAVE";
    case 4:   return "ASSOC_EXPIRE";
    case 5:   return "ASSOC_TOOMANY";
    case 6:   return "NOT_AUTHED";
    case 7:   return "NOT_ASSOCED";
    case 8:   return "ASSOC_LEAVE";
    case 9:   return "ASSOC_NOT_AUTHED";
    case 15:  return "4WAY_HANDSHAKE_TIMEOUT";
    case 200: return "BEACON_TIMEOUT";
    case 201: return "NO_AP_FOUND";
    case 202: return "AUTH_FAIL";
    case 203: return "ASSOC_FAIL";
    case 204: return "HANDSHAKE_TIMEOUT";
    case 205: return "CONNECTION_FAIL";
    default:  return "OTHER";
    }
}

static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if(base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED)
    {
        s_connected = false;
        const wifi_event_sta_disconnected_t *d = (const wifi_event_sta_disconnected_t *)data;
        uint8_t reason = d ? d->reason : 0;
        sys_logw(HAL_WIFI_TAG, "STA disconnected: reason=%u (%s), rssi=%d",
                 (unsigned)reason, wifi_reason_name(reason), d ? d->rssi : 0);
    }
    if(base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) s_connected = true;
}

esp_err_t hal_wifi_init(bool ram_storage)
{
    if(s_initialized) return ESP_OK;
    if(!s_netif_ready)
    {
        esp_err_t err = esp_netif_init();
        if(err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
        err = esp_event_loop_create_default();
        if(err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
        if(esp_netif_create_default_wifi_sta() == NULL) return ESP_ERR_NO_MEM;
        s_netif_ready = true;
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t err = esp_wifi_init(&cfg);
    if(err != ESP_OK) return err;
    err = esp_wifi_set_storage(ram_storage ? WIFI_STORAGE_RAM : WIFI_STORAGE_FLASH);
    if(err == ESP_OK) err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                                   wifi_event, NULL, &s_wifi_handler);
    if(err == ESP_OK) err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                                   wifi_event, NULL, &s_ip_handler);
    if(err == ESP_OK) err = esp_wifi_set_mode(WIFI_MODE_STA);
    if(err == ESP_OK) err = esp_wifi_start();
    if(err != ESP_OK)
    {
        if(s_ip_handler) esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, s_ip_handler);
        if(s_wifi_handler) esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, s_wifi_handler);
        s_ip_handler = NULL;
        s_wifi_handler = NULL;
        esp_wifi_deinit();
        return err;
    }
    s_initialized = true;
    s_connected = false;

    /* 临时诊断：打印当前最大发射功率（单位 0.25dBm），用于判断 PA 是否被限幅。 */
    {
        int8_t txp = 0;
        if(esp_wifi_get_max_tx_power(&txp) == ESP_OK)
            sys_logi(HAL_WIFI_TAG, "max tx power: %d (0.25dBm) = %d dBm", (int)txp, (int)txp / 4);
    }
    return ESP_OK;
}

void hal_wifi_deinit(void)
{
    if(!s_initialized) return;
    esp_wifi_disconnect();
    esp_wifi_stop();
    esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, s_ip_handler);
    esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, s_wifi_handler);
    s_ip_handler = NULL;
    s_wifi_handler = NULL;
    esp_wifi_deinit();
    s_connected = false;
    s_initialized = false;
}

esp_err_t hal_wifi_set_ram_storage(bool ram_storage)
{
    return s_initialized ? esp_wifi_set_storage(ram_storage ? WIFI_STORAGE_RAM : WIFI_STORAGE_FLASH)
                         : ESP_ERR_INVALID_STATE;
}

esp_err_t hal_wifi_get_power_save(wifi_ps_type_t *type)
{
    if(!s_initialized || !type) return ESP_ERR_INVALID_STATE;
    return esp_wifi_get_ps(type);
}

esp_err_t hal_wifi_set_power_save(wifi_ps_type_t type)
{
    return s_initialized ? esp_wifi_set_ps(type) : ESP_ERR_INVALID_STATE;
}

esp_err_t hal_wifi_get_sta_config(wifi_config_t *config)
{
    return s_initialized ? esp_wifi_get_config(WIFI_IF_STA, config) : ESP_ERR_INVALID_STATE;
}

esp_err_t hal_wifi_set_sta_config(const wifi_config_t *config)
{
    if(!s_initialized || !config) return ESP_ERR_INVALID_STATE;
    wifi_config_t copy = *config;
    return esp_wifi_set_config(WIFI_IF_STA, &copy);
}

esp_err_t hal_wifi_connect(const char *ssid, const char *password)
{
    if(!s_initialized || !ssid || !password) return ESP_ERR_INVALID_STATE;
    size_t ssid_len = strnlen(ssid, 33);
    size_t password_len = strnlen(password, 64);
    if(ssid_len == 0 || ssid_len > 32 || password_len > 63) return ESP_ERR_INVALID_ARG;
    wifi_config_t config = {0};
    memcpy(config.sta.ssid, ssid, ssid_len);
    memcpy(config.sta.password, password, password_len);
    esp_wifi_disconnect();
    s_connected = false;
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &config);
    return err == ESP_OK ? esp_wifi_connect() : err;
}

esp_err_t hal_wifi_reconnect(void)
{
    if(!s_initialized) return ESP_ERR_INVALID_STATE;
    esp_wifi_disconnect();
    s_connected = false;
    return esp_wifi_connect();
}

void hal_wifi_disconnect(void)
{
    if(!s_initialized) return;
    esp_wifi_disconnect();
    s_connected = false;
}

bool hal_wifi_initialized(void) { return s_initialized; }
bool hal_wifi_connected(void) { return s_connected; }

/* One netif is retained for reuse, like the existing default STA netif. */
static esp_netif_t *s_direct_ap_netif;

esp_err_t hal_wifi_softap_identity(char *ssid, size_t size)
{
    if(!ssid || size < 24) return ESP_ERR_INVALID_ARG;
    uint8_t mac[6];
    esp_err_t err = esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    if(err == ESP_OK) snprintf(ssid, size, "ARK-TRANSFER-%02X%02X%02X", mac[3], mac[4], mac[5]);
    return err;
}

esp_err_t hal_wifi_softap_start(const char *ssid, const char *password)
{
    if(!ssid || !password || strlen(ssid) > 32 || strlen(password) < 8 || strlen(password) > 63)
        return ESP_ERR_INVALID_ARG;
    esp_err_t err = hal_wifi_init(true);
    if(err != ESP_OK) return err;
    err = esp_wifi_stop();
    if(err != ESP_OK) return err;
    s_connected = false;
    if(!s_direct_ap_netif) s_direct_ap_netif = esp_netif_create_default_wifi_ap();
    if(!s_direct_ap_netif) return ESP_ERR_NO_MEM;
    wifi_config_t config = {0};
    strlcpy((char *)config.ap.ssid, ssid, sizeof(config.ap.ssid));
    config.ap.ssid_len = strlen(ssid);
    strlcpy((char *)config.ap.password, password, sizeof(config.ap.password));
    config.ap.channel = 11;
    config.ap.max_connection = 1;
    config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    config.ap.pmf_cfg.capable = true;
    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if(err == ESP_OK) err = esp_wifi_set_mode(WIFI_MODE_AP);
    if(err == ESP_OK) err = esp_wifi_set_config(WIFI_IF_AP, &config);
    if(err == ESP_OK) err = esp_wifi_set_bandwidth(WIFI_IF_AP, WIFI_BW_HT20);
    return err == ESP_OK ? esp_wifi_start() : err;
}

esp_err_t hal_wifi_softap_stop(void)
{
    if(!s_initialized) return ESP_ERR_INVALID_STATE;
    esp_err_t err = esp_wifi_stop();
    if(err == ESP_OK) err = esp_wifi_set_mode(WIFI_MODE_STA);
    return err == ESP_OK ? esp_wifi_start() : err;
}

/* 临时诊断：打印 SoftAP 当前信道/带宽/已连 station 数。
   用来确认 AP 是否真的在发、手机有没有关联上来。定位完删除。 */
void hal_wifi_softap_status(void)
{
    if(!s_initialized) return;

    uint8_t primary = 0;
    wifi_second_chan_t second = WIFI_SECOND_CHAN_NONE;
    wifi_bandwidth_t bw = WIFI_BW_HT20;
    wifi_sta_list_t list = {0};

    esp_wifi_get_channel(&primary, &second);
    esp_wifi_get_bandwidth(WIFI_IF_AP, &bw);
    esp_wifi_ap_get_sta_list(&list);
    sys_logi(HAL_WIFI_TAG, "softAP status: ch=%u bw=%d stations=%d",
             (unsigned)primary, (int)bw, (int)list.num);
}

/* 临时诊断：阻塞扫描一次并把 SSID/信道/RSSI 打到串口。
   用于判断 2.4G 接收链路是否正常（扫不到任何 AP = 收侧有问题）。定位完删除。 */
esp_err_t hal_wifi_scan_dump(void)
{
    if(!s_initialized) return ESP_ERR_INVALID_STATE;

    static wifi_ap_record_t s_records[HAL_WIFI_SCAN_MAX_AP];
    wifi_scan_config_t scan = {0};
    scan.show_hidden = true;

    esp_err_t err = esp_wifi_scan_start(&scan, true);
    if(err != ESP_OK)
    {
        sys_logw(HAL_WIFI_TAG, "scan start failed: %s", esp_err_to_name(err));
        return err;
    }

    uint16_t ap_num = 0;
    esp_wifi_scan_get_ap_num(&ap_num);

    uint16_t want = ap_num > HAL_WIFI_SCAN_MAX_AP ? HAL_WIFI_SCAN_MAX_AP : ap_num;
    if(want > 0)
    {
        err = esp_wifi_scan_get_ap_records(&want, s_records);
        if(err != ESP_OK)
        {
            sys_logw(HAL_WIFI_TAG, "scan get records failed: %s", esp_err_to_name(err));
            esp_wifi_clear_ap_list();
            return err;
        }
    }

    sys_logi(HAL_WIFI_TAG, "scan done: %u AP(s) found, listing %u", (unsigned)ap_num, (unsigned)want);
    for(uint16_t i = 0; i < want; i++)
    {
        sys_logi(HAL_WIFI_TAG, "  [%02u] %-32s ch=%2u rssi=%4d auth=%u",
                 (unsigned)i, (const char *)s_records[i].ssid, (unsigned)s_records[i].primary,
                 s_records[i].rssi, (unsigned)s_records[i].authmode);
    }
    esp_wifi_clear_ap_list();
    return ESP_OK;
}

