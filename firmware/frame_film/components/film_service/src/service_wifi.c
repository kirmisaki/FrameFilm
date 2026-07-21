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

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"

#include "sys_log.h"
#include "service_param.h"
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

/*********************************************************************
 * GLOBAL VARIABLES
 */


/*********************************************************************
 * LOCAL FUNCTIONS
 */
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data);


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
