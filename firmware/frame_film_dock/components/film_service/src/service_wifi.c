/*********************************************************************
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Copyright (c) 2026 kiritro
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * 
 * FileName : /film_service/src/service_wifi.c
 * Author: Kiritro  Version: v0.1  Date: 2026/7/20
 * Description: WiFi服务
 * ChangeLog: Change Notes
 *
 *********************************************************************/

/*********************************************************************
 * INCLUDES
 */
#include <string.h>
#include <stdio.h>
#include <sys/time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "cJSON.h"

#include "sys_log.h"
#include "sys_com.h"
#include "service_param.h"
#include "service_file.h"
#include "service_wifi.h"

/*********************************************************************
 * MACROS
 */
#define WIFI_SERVICE_TAG        "wifi_service"

/*********************************************************************
* TYPEDEFS
*/


/*********************************************************************
 * CONSTANTS
 */


/*********************************************************************
 * LOCAL VARIABLES
 */
static bool g_wifi_initialized = false;
static bool g_wifi_connected = false;
static bool g_wifi_netif_ready = false;

static wifi_download_state_t g_download_state = WIFI_DOWNLOAD_IDLE;
static uint8_t g_download_progress = 0;
static int g_download_content_length = 0;
static int g_download_received = 0;
static char g_download_filename[256] = {0};
static uint8_t *g_download_buffer = NULL;

/*********************************************************************
 * GLOBAL VARIABLES
 */


/*********************************************************************
 * LOCAL FUNCTIONS
 */
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data);
static void wifi_download_task(void *pvParameters);
static esp_err_t wifi_http_event_handler(esp_http_client_event_t *evt);


/*********************************************************************
 * GLOBAL FUNCTIONS
 */

/**
 * [service_wifi_init 初始化WiFi]
 */
void service_wifi_init(void)
{
    if(g_wifi_initialized)
    {
        sys_logi(WIFI_SERVICE_TAG, "WiFi already initialized");
        return;
    }

    if(!g_service_param.network.wifi_enable)
    {
        sys_logi(WIFI_SERVICE_TAG, "WiFi disabled, skip init");
        return;
    }

    if(!g_wifi_netif_ready)
    {
        ESP_ERROR_CHECK(esp_netif_init());
        esp_event_loop_create_default();
        esp_netif_create_default_wifi_sta();
        g_wifi_netif_ready = true;
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    g_wifi_initialized = true;

    sys_logi(WIFI_SERVICE_TAG, "WiFi initialized");

    // 启动心跳任务（内部等待 WiFi 连接后再上报）
    service_wifi_heartbeat_start();

    if(strlen(g_service_param.network.wifi_ssid) > 0)
    {
        service_wifi_connect();
    }
}

/**
 * [service_wifi_deinit 反初始化WiFi]
 */
void service_wifi_deinit(void)
{
    if(!g_wifi_initialized)
    {
        return;
    }

    esp_wifi_disconnect();
    esp_wifi_stop();
    esp_wifi_deinit();

    g_wifi_connected = false;
    g_wifi_initialized = false;

    sys_logi(WIFI_SERVICE_TAG, "WiFi deinitialized");
}

/**
 * [service_wifi_connect 连接WiFi]
 */
void service_wifi_connect(void)
{
    if(g_wifi_connected)
    {
        sys_logi(WIFI_SERVICE_TAG, "Disconnecting before reconnect");
        esp_wifi_disconnect();
        g_wifi_connected = false;
    }

    if(!g_wifi_initialized)
    {
        service_wifi_init();
    }

    if(!g_wifi_initialized)
    {
        sys_logw(WIFI_SERVICE_TAG, "WiFi init failed");
        return;
    }

    if(strlen(g_service_param.network.wifi_ssid) == 0)
    {
        sys_logw(WIFI_SERVICE_TAG, "WiFi SSID is empty");
        return;
    }

    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid, g_service_param.network.wifi_ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, g_service_param.network.wifi_password, sizeof(wifi_config.sta.password) - 1);

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_connect());

    sys_logi(WIFI_SERVICE_TAG, "Connecting to SSID: %s", g_service_param.network.wifi_ssid);
}

/**
 * [service_wifi_disconnect 断开WiFi]
 */
void service_wifi_disconnect(void)
{
    if(!g_wifi_initialized)
    {
        return;
    }

    esp_wifi_disconnect();
    g_wifi_connected = false;

    sys_logi(WIFI_SERVICE_TAG, "WiFi disconnected");
}

/**
 * [service_wifi_get_connect_status 获取WiFi连接状态]
 * @return 0：未连接 1：已连接
 */
uint8_t service_wifi_get_connect_status(void)
{
    return g_wifi_connected ? 1 : 0;
}

/**
 * [service_wifi_clear_config 清除网络配置信息]
 */
void service_wifi_clear_config(void)
{
    if(g_wifi_initialized)
    {
        service_wifi_disconnect();
        service_wifi_deinit();
    }

    g_service_param.network.wifi_enable = 0;
    memset(g_service_param.network.wifi_ssid, 0, sizeof(g_service_param.network.wifi_ssid));
    memset(g_service_param.network.wifi_password, 0, sizeof(g_service_param.network.wifi_password));
    memset(g_service_param.network.film_api_url, 0, sizeof(g_service_param.network.film_api_url));

    service_param_save();

    sys_logi(WIFI_SERVICE_TAG, "WiFi config cleared");
}

/*********************************************************************
 * LOCAL FUNCTIONS
 */

/**
 * [wifi_http_event_handler HTTP事件处理]
 */
static esp_err_t wifi_http_event_handler(esp_http_client_event_t *evt)
{
    switch(evt->event_id)
    {
    case HTTP_EVENT_ON_CONNECTED:
        g_download_content_length = esp_http_client_get_content_length(evt->client);
        g_download_received = 0;
        g_download_progress = 0;
        if(g_download_buffer)
        {
            heap_caps_free(g_download_buffer);
            g_download_buffer = NULL;
        }
        sys_logi(WIFI_SERVICE_TAG, "HTTP connected, content length: %d", g_download_content_length);
        break;

    case HTTP_EVENT_ON_DATA:
        if(g_download_state != WIFI_DOWNLOAD_DOWNLOADING)
        {
            break;
        }
        if(evt->data_len > 0)
        {
            uint8_t *new_buf = (uint8_t *)heap_caps_realloc(g_download_buffer, g_download_received + evt->data_len, MALLOC_CAP_SPIRAM);
            if(new_buf)
            {
                g_download_buffer = new_buf;
                memcpy(g_download_buffer + g_download_received, evt->data, evt->data_len);
                g_download_received += evt->data_len;
                if(g_download_content_length > 0)
                {
                    g_download_progress = (uint8_t)(((uint32_t)g_download_received * 100) / g_download_content_length);
                }
            }
            else
            {
                sys_loge(WIFI_SERVICE_TAG, "Download buffer realloc failed");
                g_download_state = WIFI_DOWNLOAD_ERROR;
            }
        }
        break;

    case HTTP_EVENT_ON_FINISH:
        sys_logi(WIFI_SERVICE_TAG, "HTTP download finished, received: %d bytes", g_download_received);
        if(g_download_buffer && g_download_received > 0)
        {
            if(service_file_save_start(g_download_filename, g_download_received) == 0)
            {
                service_file_save_data(g_download_filename, g_download_received,
                                       g_download_buffer, g_download_received);
                service_file_save_stop(1);
                /* save_data takes ownership, file task will free */
            }
            else
            {
                heap_caps_free(g_download_buffer);
            }
            g_download_buffer = NULL;
            g_download_state = WIFI_DOWNLOAD_DONE;
            g_download_progress = 100;
        }
        else
        {
            g_download_state = WIFI_DOWNLOAD_DONE;
            g_download_progress = 100;
        }
        break;

    case HTTP_EVENT_DISCONNECTED:
        sys_logi(WIFI_SERVICE_TAG, "HTTP disconnected");
        if(g_download_state == WIFI_DOWNLOAD_DOWNLOADING)
        {
            g_download_state = WIFI_DOWNLOAD_ERROR;
        }
        if(g_download_buffer)
        {
            heap_caps_free(g_download_buffer);
            g_download_buffer = NULL;
        }
        break;

    case HTTP_EVENT_ERROR:
        sys_loge(WIFI_SERVICE_TAG, "HTTP error");
        g_download_state = WIFI_DOWNLOAD_ERROR;
        if(g_download_buffer)
        {
            heap_caps_free(g_download_buffer);
            g_download_buffer = NULL;
        }
        break;

    default:
        break;
    }
    return ESP_OK;
}

/**
 * [wifi_download_task 下载任务]
 * @param pvParameters 下载 URL（strdup 拷贝，任务结束负责释放）
 */
static void wifi_download_task(void *pvParameters)
{
    const char *url = (const char *)pvParameters;

    if(url == NULL || strlen(url) == 0)
    {
        sys_loge(WIFI_SERVICE_TAG, "Download URL is empty");
        g_download_state = WIFI_DOWNLOAD_ERROR;
        free((void *)url);
        vTaskDelete(NULL);
        return;
    }

    // 从 URL 提取文件名（截断查询参数 ?...，如 latest.film?device_id=... 取 latest.film）
    const char *last_slash = strrchr(url, '/');
    const char *filename = last_slash ? (last_slash + 1) : "download.film";
    const char *query = strchr(filename, '?');
    size_t fname_len = query ? (size_t)(query - filename) : strlen(filename);
    if(fname_len <= 0 || fname_len >= sizeof(g_download_filename))
    {
        fname_len = strlen("download.film");
        filename = "download.film";
    }
    memcpy(g_download_filename, filename, fname_len);
    g_download_filename[fname_len] = '\0';

    esp_http_client_config_t config = {
        .url = url,
        .event_handler = wifi_http_event_handler,
        .timeout_ms = 30000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);

    g_download_state = WIFI_DOWNLOAD_DOWNLOADING;
    g_download_progress = 0;
    g_download_content_length = 0;
    g_download_received = 0;

    sys_logi(WIFI_SERVICE_TAG, "Starting download: %s -> %s", url, g_download_filename);

    esp_err_t err = esp_http_client_perform(client);

    if(err != ESP_OK)
    {
        sys_loge(WIFI_SERVICE_TAG, "HTTP download failed: %d", err);
        if(g_download_state == WIFI_DOWNLOAD_DOWNLOADING)
        {
            g_download_state = WIFI_DOWNLOAD_ERROR;
        }
    }

    esp_http_client_cleanup(client);
    free((void *)url);
    vTaskDelete(NULL);
}

/**
 * [service_wifi_download_start 开始下载film文件（使用配置的 film_api_url）]
 */
void service_wifi_download_start(void)
{
    if(!g_service_param.network.wifi_enable)
    {
        sys_logw(WIFI_SERVICE_TAG, "WiFi disabled, cannot download");
        return;
    }

    if(!g_wifi_connected)
    {
        sys_logw(WIFI_SERVICE_TAG, "WiFi not connected, cannot download");
        return;
    }

    if(g_download_state == WIFI_DOWNLOAD_DOWNLOADING)
    {
        sys_logw(WIFI_SERVICE_TAG, "Download already in progress");
        return;
    }

    if(strlen(g_service_param.network.film_api_url) == 0)
    {
        sys_logw(WIFI_SERVICE_TAG, "Film API URL is empty");
        return;
    }

    service_wifi_download_url(g_service_param.network.film_api_url);
}

/**
 * [service_wifi_download_url 按指定 URL 开始下载film文件]
 */
void service_wifi_download_url(const char *url)
{
    if(!g_service_param.network.wifi_enable)
    {
        sys_logw(WIFI_SERVICE_TAG, "WiFi disabled, cannot download");
        return;
    }

    if(!g_wifi_connected)
    {
        sys_logw(WIFI_SERVICE_TAG, "WiFi not connected, cannot download");
        return;
    }

    if(g_download_state == WIFI_DOWNLOAD_DOWNLOADING)
    {
        sys_logw(WIFI_SERVICE_TAG, "Download already in progress");
        return;
    }

    if(url == NULL || strlen(url) == 0)
    {
        sys_logw(WIFI_SERVICE_TAG, "Download URL is empty");
        return;
    }

    char *url_copy = strdup(url);
    if(url_copy == NULL)
    {
        sys_loge(WIFI_SERVICE_TAG, "Failed to alloc download URL");
        g_download_state = WIFI_DOWNLOAD_ERROR;
        return;
    }

    // 创建下载任务
    if(pdPASS != xTaskCreate(wifi_download_task, "wifi_dl", 4096, url_copy, 5, NULL))
    {
        sys_loge(WIFI_SERVICE_TAG, "Failed to create download task");
        free(url_copy);
        g_download_state = WIFI_DOWNLOAD_ERROR;
    }
}

/**
 * [service_wifi_download_get_progress 获取下载进度]
 */
uint8_t service_wifi_download_get_progress(void)
{
    return g_download_progress;
}

/**
 * [service_wifi_download_get_state 获取下载状态]
 */
wifi_download_state_t service_wifi_download_get_state(void)
{
    return g_download_state;
}

/**
 * [wifi_event_handler WiFi事件处理]
 */
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if(event_base == WIFI_EVENT)
    {
        switch(event_id)
        {
        case WIFI_EVENT_STA_START:
            sys_logi(WIFI_SERVICE_TAG, "WiFi STA started");
            break;
        case WIFI_EVENT_STA_CONNECTED:
            sys_logi(WIFI_SERVICE_TAG, "WiFi STA connected");
            break;
        case WIFI_EVENT_STA_DISCONNECTED:
            g_wifi_connected = false;
            sys_logw(WIFI_SERVICE_TAG, "WiFi STA disconnected");
            break;
        default:
            break;
        }
    }
    else if(event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        sys_logi(WIFI_SERVICE_TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        g_wifi_connected = true;
    }
}

/*********************************************************************
 * 心跳（film-hub 服务端适配：定时上报 + 指令下发执行）
 *********************************************************************/
#define HEARTBEAT_RESP_MAX  2048
#define HEARTBEAT_URL_MAX   512

static TaskHandle_t g_heartbeat_task_hdl = NULL;
static char g_heartbeat_resp[HEARTBEAT_RESP_MAX];
static int g_heartbeat_resp_len = 0;

/**
 * [heartbeat_http_event_handler 心跳响应收集]
 */
static esp_err_t heartbeat_http_event_handler(esp_http_client_event_t *evt)
{
    switch(evt->event_id)
    {
    case HTTP_EVENT_ON_DATA:
        if(evt->data_len > 0 && g_heartbeat_resp_len + evt->data_len < (int)sizeof(g_heartbeat_resp))
        {
            memcpy(g_heartbeat_resp + g_heartbeat_resp_len, evt->data, evt->data_len);
            g_heartbeat_resp_len += evt->data_len;
        }
        break;
    case HTTP_EVENT_ON_FINISH:
    case HTTP_EVENT_DISCONNECTED:
        g_heartbeat_resp[g_heartbeat_resp_len] = '\0';
        break;
    default:
        break;
    }
    return ESP_OK;
}

/**
 * [wifi_heartbeat_exec_cmd 执行服务端下发的单条指令]
 */
static void wifi_heartbeat_exec_cmd(cJSON *cmd)
{
    cJSON *cmd_name = cJSON_GetObjectItem(cmd, "cmd");
    cJSON *params = cJSON_GetObjectItem(cmd, "params");
    if(!cJSON_IsString(cmd_name) || cmd_name->valuestring == NULL)
    {
        return;
    }
    const char *name = cmd_name->valuestring;

    if(strcmp(name, "download_film") == 0)
    {
        cJSON *url = params ? cJSON_GetObjectItem(params, "url") : NULL;
        if(cJSON_IsString(url) && url->valuestring != NULL && url->valuestring[0] != '\0')
        {
            sys_logi(WIFI_SERVICE_TAG, "Heartbeat cmd: download_film");
            service_wifi_download_url(url->valuestring);
        }
    }
    else if(strcmp(name, "set_config") == 0)
    {
        bool changed = false;
        if(cJSON_IsObject(params))
        {
            cJSON *item = NULL;
            item = cJSON_GetObjectItem(params, "play_mode");
            if(cJSON_IsNumber(item) && (item->valueint == 0 || item->valueint == 1 || item->valueint == 2))
            {
                if(g_service_param.film.play_mode != (uint8_t)item->valueint)
                {
                    g_service_param.film.play_mode = (uint8_t)item->valueint;
                    changed = true;
                }
            }
            item = cJSON_GetObjectItem(params, "wifi_enable");
            if(cJSON_IsNumber(item) && (item->valueint == 0 || item->valueint == 1))
            {
                if(g_service_param.network.wifi_enable != (uint8_t)item->valueint)
                {
                    g_service_param.network.wifi_enable = (uint8_t)item->valueint;
                    changed = true;
                    if(g_service_param.network.wifi_enable)
                    {
                        service_wifi_init();
                    }
                    else
                    {
                        service_wifi_disconnect();
                    }
                }
            }
            item = cJSON_GetObjectItem(params, "sleep_mode");
            if(cJSON_IsNumber(item) && (item->valueint == 0 || item->valueint == 1))
            {
                if(g_service_param.sleep.sleep_mode != (uint8_t)item->valueint)
                {
                    g_service_param.sleep.sleep_mode = (uint8_t)item->valueint;
                    changed = true;
                }
            }
            item = cJSON_GetObjectItem(params, "sleep_auto");
            if(cJSON_IsNumber(item) && (item->valueint == 0 || item->valueint == 1))
            {
                if(g_service_param.sleep.sleep_auto != (uint8_t)item->valueint)
                {
                    g_service_param.sleep.sleep_auto = (uint8_t)item->valueint;
                    changed = true;
                }
            }
            item = cJSON_GetObjectItem(params, "sleep_time");
            if(cJSON_IsNumber(item) && item->valueint >= 10 && item->valueint <= 2880)
            {
                if(g_service_param.sleep.sleep_time != (uint16_t)item->valueint)
                {
                    g_service_param.sleep.sleep_time = (uint16_t)item->valueint;
                    changed = true;
                }
            }
            item = cJSON_GetObjectItem(params, "ble_enable");
            if(cJSON_IsNumber(item) && (item->valueint == 0 || item->valueint == 1))
            {
                if(g_service_param.ble.ble_enable != (uint8_t)item->valueint)
                {
                    g_service_param.ble.ble_enable = (uint8_t)item->valueint;
                    changed = true;
                }
            }
        }
        if(changed)
        {
            sys_logi(WIFI_SERVICE_TAG, "Heartbeat cmd: set_config applied");
            service_param_save();
        }
    }
    else if(strcmp(name, "set_heartbeat") == 0)
    {
        cJSON *interval = params ? cJSON_GetObjectItem(params, "interval") : NULL;
        if(cJSON_IsNumber(interval) && interval->valueint >= 5 && interval->valueint <= 180)
        {
            sys_logi(WIFI_SERVICE_TAG, "Heartbeat cmd: set_heartbeat %d", interval->valueint);
            g_service_param.network.film_heartbeat_interval = (uint8_t)interval->valueint;
            service_param_save();
        }
    }
    else if(strcmp(name, "sync_time") == 0)
    {
        cJSON *ts = params ? cJSON_GetObjectItem(params, "timestamp") : NULL;
        if(cJSON_IsNumber(ts) && ts->valuedouble > 0)
        {
            struct timeval tv = { .tv_sec = (time_t)ts->valuedouble, .tv_usec = 0 };
            settimeofday(&tv, NULL);
            sys_logi(WIFI_SERVICE_TAG, "Heartbeat cmd: sync_time");
        }
    }
    else if(strcmp(name, "reboot") == 0)
    {
        sys_logi(WIFI_SERVICE_TAG, "Heartbeat cmd: reboot");
        vTaskDelay(pdMS_TO_TICKS(100));
        sys_reboot();
    }
    // clear_files 等其它指令：暂不支持，忽略
}

/**
 * [wifi_heartbeat_parse 解析心跳响应：token / 间隔 / 指令]
 */
static void wifi_heartbeat_parse(const char *body)
{
    cJSON *root = cJSON_Parse(body);
    if(root == NULL)
    {
        sys_logw(WIFI_SERVICE_TAG, "Heartbeat response parse failed");
        return;
    }

    cJSON *data = cJSON_GetObjectItem(root, "data");
    if(!cJSON_IsObject(data))
    {
        cJSON_Delete(root);
        return;
    }

    // 首次心跳（或 token 轮换）下发 token
    cJSON *token = cJSON_GetObjectItem(data, "token");
    if(cJSON_IsString(token) && token->valuestring != NULL && token->valuestring[0] != '\0')
    {
        if(strcmp(token->valuestring, g_service_param.network.film_token) != 0)
        {
            strncpy(g_service_param.network.film_token, token->valuestring,
                    sizeof(g_service_param.network.film_token) - 1);
            sys_logi(WIFI_SERVICE_TAG, "Heartbeat token updated");
            service_param_save();
        }
    }

    // 服务端可动态调整心跳间隔
    cJSON *interval = cJSON_GetObjectItem(data, "heartbeat_interval");
    if(cJSON_IsNumber(interval) && interval->valueint >= 5 && interval->valueint <= 180)
    {
        if(g_service_param.network.film_heartbeat_interval != (uint8_t)interval->valueint)
        {
            g_service_param.network.film_heartbeat_interval = (uint8_t)interval->valueint;
            service_param_save();
        }
    }

    // 指令下发
    cJSON *cmds = cJSON_GetObjectItem(data, "commands");
    if(cJSON_IsArray(cmds))
    {
        int count = cJSON_GetArraySize(cmds);
        for(int i = 0; i < count; i++)
        {
            cJSON *cmd = cJSON_GetArrayItem(cmds, i);
            wifi_heartbeat_exec_cmd(cmd);
        }
    }

    cJSON_Delete(root);
}

/**
 * [wifi_heartbeat_once 单次心跳请求]
 */
static void wifi_heartbeat_once(void)
{
    char url[HEARTBEAT_URL_MAX];
    char auto_hb[192] = "";
    const char *hb_base = g_service_param.network.film_heartbeat_url;

    // 心跳地址：优先使用已配置的完整心跳接口；否则由服务器 API 地址自动拼接
    // （只配置一个 API 地址即可，如 http://192.168.1.219:8000）
    if(strstr(hb_base, "/api/v1/device/heartbeat") == NULL)
    {
        const char *api = g_service_param.network.film_api_url;
        int alen = (int)strlen(api);
        while(alen > 0 && api[alen - 1] == '/') alen--;  // 去掉末尾斜杠
        if(alen <= 0)
        {
            sys_logw(WIFI_SERVICE_TAG, "No API URL configured, skip heartbeat");
            return;
        }
        snprintf(auto_hb, sizeof(auto_hb), "%.*s/api/v1/device/heartbeat", alen, api);
        hb_base = auto_hb;
    }

    const char *token = g_service_param.network.film_token;

    int n = snprintf(url, sizeof(url),
        "%s?device_id=%s&token=%s&battery=%d&play_mode=%d&wifi_enable=%d"
        "&sleep_mode=%d&sleep_auto=%d&sleep_time=%d&ble_enable=%d"
        "&current_file_id=%lu&state=idle&heartbeat_interval=%d",
        hb_base,
        g_service_param.network.film_device_id,
        (token != NULL && token[0] != '\0') ? token : "",
        100,
        g_service_param.film.play_mode,
        g_service_param.network.wifi_enable,
        g_service_param.sleep.sleep_mode,
        g_service_param.sleep.sleep_auto,
        g_service_param.sleep.sleep_time,
        g_service_param.ble.ble_enable,
        g_service_param.film.current_file_id,
        g_service_param.network.film_heartbeat_interval);

    if(n <= 0 || n >= (int)sizeof(url))
    {
        sys_logw(WIFI_SERVICE_TAG, "Heartbeat URL too long");
        return;
    }

    g_heartbeat_resp_len = 0;
    g_heartbeat_resp[0] = '\0';

    esp_http_client_config_t config = {
        .url = url,
        .event_handler = heartbeat_http_event_handler,
        .timeout_ms = 10000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if(client == NULL)
    {
        sys_loge(WIFI_SERVICE_TAG, "Heartbeat http client init failed");
        return;
    }

    esp_err_t err = esp_http_client_perform(client);
    if(err == ESP_OK)
    {
        wifi_heartbeat_parse(g_heartbeat_resp);
    }
    else
    {
        sys_logw(WIFI_SERVICE_TAG, "Heartbeat request failed: %d", err);
    }

    esp_http_client_cleanup(client);
}

/**
 * [wifi_heartbeat_task 心跳任务：WiFi 连接后定时上报]
 */
static void wifi_heartbeat_task(void *pvParameters)
{
    for(;;)
    {
        // 有 API 地址（用于拼接心跳接口）即视为已配置
        if(g_wifi_connected && strlen(g_service_param.network.film_api_url) > 0)
        {
            wifi_heartbeat_once();
        }

        uint32_t interval = g_service_param.network.film_heartbeat_interval;
        if(interval < 5) interval = 5;
        if(interval > 180) interval = 180;
        vTaskDelay(pdMS_TO_TICKS(interval * 1000));
    }
    vTaskDelete(NULL);
}

/**
 * [service_wifi_heartbeat_start 启动心跳任务]
 */
void service_wifi_heartbeat_start(void)
{
    if(g_heartbeat_task_hdl != NULL)
    {
        return;
    }

    if(pdPASS != xTaskCreate(wifi_heartbeat_task, "wifi_hb", 4096, NULL, 4, &g_heartbeat_task_hdl))
    {
        sys_loge(WIFI_SERVICE_TAG, "Failed to create heartbeat task");
    }
    else
    {
        sys_logi(WIFI_SERVICE_TAG, "Heartbeat task started");
    }
}
