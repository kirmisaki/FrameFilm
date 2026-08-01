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

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

#include "sys_log.h"
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
 */
static void wifi_download_task(void *pvParameters)
{
    const char *url = g_service_param.network.film_api_url;

    if(strlen(url) == 0)
    {
        sys_loge(WIFI_SERVICE_TAG, "Film API URL is empty");
        g_download_state = WIFI_DOWNLOAD_ERROR;
        vTaskDelete(NULL);
        return;
    }

    // 从 URL 提取文件名
    const char *last_slash = strrchr(url, '/');
    const char *filename = last_slash ? (last_slash + 1) : "download.film";
    strncpy(g_download_filename, filename, sizeof(g_download_filename) - 1);

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
    vTaskDelete(NULL);
}

/**
 * [service_wifi_download_start 开始下载film文件]
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

    // 创建下载任务
    if(pdPASS != xTaskCreate(wifi_download_task, "wifi_dl", 4096, NULL, 5, NULL))
    {
        sys_loge(WIFI_SERVICE_TAG, "Failed to create download task");
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
