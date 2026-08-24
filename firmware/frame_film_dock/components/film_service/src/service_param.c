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
 * FileName : /film_service/src/service_param.c
 * Author: Kiritro  Version: v0.1  Date: 2026/4/7
 * Description: 服务参数初始化
 * ChangeLog: Change Notes
 *
 *********************************************************************/

/*********************************************************************
 * INCLUDES
 */
#include <string.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "nvs_flash.h"
#include "esp_mac.h"

#include "sys_log.h"
#include "service_param.h"

/*********************************************************************
 * MACROS
 */
#define SERVICE_FACTORY_DEFAULT_FLAG                            (0x22)

/*********************************************************************
* TYPEDEFS
*/


/*********************************************************************
 * CONSTANTS
 */


/*********************************************************************
 * LOCAL VARIABLES
 */
static void nvs_init(void);
static void nvs_param_save(void);
static void service_param_set_default(void);

/*********************************************************************
 * GLOBAL VARIABLES
 */
ServiceParam_Def_t g_service_param = {0};

/*********************************************************************
 * LOCAL FUNCTIONS
 */


/*********************************************************************
 * GLOBAL FUNCTIONS
 */


/**
 * [service_param_init 初始化服务参数]
 */
void service_param_init(void)
{
    nvs_init();
    service_param_ensure_device_id();
}

/**
 * [service_param_ensure_device_id 确保设备唯一ID存在（无则用 MAC 生成并保存）]
 */
void service_param_ensure_device_id(void)
{
    if(g_service_param.network.film_device_id[0] != '\0')
    {
        return;
    }

    uint8_t mac[6];
    if(esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK)
    {
        sys_loge("param", "read mac failed");
        return;
    }

    snprintf(g_service_param.network.film_device_id,
             sizeof(g_service_param.network.film_device_id),
             "%02x%02x%02x%02x%02x%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    nvs_param_save();
}

/**
 * [service_param_save 保存服务参数]
 */
void service_param_save(void)
{
    nvs_param_save();
}

/**
 * [service_param_reset 重置服务参数]
 */
void service_param_reset(void)
{
    service_param_set_default();
    nvs_param_save();
}


/**
 * [service_param_set_default 设置服务参数默认值]
 */
static void service_param_set_default(void)
{
    // 设置服务参数默认值
    g_service_param.factory_flag = SERVICE_FACTORY_DEFAULT_FLAG;

    // 参数还原
    g_service_param.film.load_complete = 0;
    g_service_param.film.play_mode = 0;
    g_service_param.film.current_file_id = 0;

    g_service_param.sleep.sleep_mode = 1;  // 休眠模式默认开启
    g_service_param.sleep.sleep_auto = 0;  // 自动唤醒默认关闭
    g_service_param.sleep.sleep_time = 10; // 默认10分钟

    // 网络参数重置
    g_service_param.network.wifi_enable = 0; // WiFi默认关闭
    g_service_param.network.film_heartbeat_interval = 5; // 默认5秒心跳间隔
    memset(g_service_param.network.wifi_ssid, 0, sizeof(g_service_param.network.wifi_ssid));
    memset(g_service_param.network.wifi_password, 0, sizeof(g_service_param.network.wifi_password));
    memset(g_service_param.network.film_api_url, 0, sizeof(g_service_param.network.film_api_url));
    memset(g_service_param.network.film_heartbeat_url, 0, sizeof(g_service_param.network.film_heartbeat_url));
    memset(g_service_param.network.film_device_id, 0, sizeof(g_service_param.network.film_device_id));
    memset(g_service_param.network.film_token, 0, sizeof(g_service_param.network.film_token));

    // BLE参数重置
    g_service_param.ble.ble_enable = 1; // BLE默认开启
    g_service_param.ble.ble_mode = 0; // BLE默认常开
}

/**
 * [nvs_init 初始化nvs]
 */
static void nvs_init(void)
{
    esp_err_t err;
    nvs_handle_t my_nvs_handle;

    err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        SYS_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    SYS_ERROR_CHECK(err);

    err = nvs_open(SYS_M_NVS_NAMESPACE, NVS_READWRITE, &my_nvs_handle);
    SYS_ERROR_CHECK(err);

    int retry = 6;
    while(retry--)
    {
        size_t required_size = sizeof(g_service_param);
        err = nvs_get_blob(my_nvs_handle, SYS_M_NVS_KEY_NAME, &g_service_param, &required_size);

        if(err == ESP_ERR_NVS_NOT_FOUND || g_service_param.factory_flag != SERVICE_FACTORY_DEFAULT_FLAG) //FACTORY RESET
        {
            service_param_set_default();

            err = nvs_set_blob(my_nvs_handle, SYS_M_NVS_KEY_NAME, &g_service_param, sizeof(g_service_param));
            SYS_ERROR_CHECK(err);
            err = nvs_commit(my_nvs_handle);
            SYS_ERROR_CHECK(err);
        }
        else
        {
            SYS_ERROR_CHECK(err);
            if(err == ESP_OK)
            {
                break;
            }
        }
        if(retry == 1)
        {
            SYS_ERROR_CHECK(nvs_flash_erase());
            err = nvs_flash_init();
        }
    }

    nvs_close(my_nvs_handle);
}

/**
 * [nvs_param_save 保存nvs参数]
 */
static void nvs_param_save(void)
{
    esp_err_t err;
    nvs_handle_t my_nvs_handle;

    err = nvs_open(SYS_M_NVS_NAMESPACE, NVS_READWRITE, &my_nvs_handle);
    SYS_ERROR_CHECK(err);

    if(err == ESP_OK)
    {
        err = nvs_set_blob(my_nvs_handle, SYS_M_NVS_KEY_NAME, &g_service_param, sizeof(g_service_param));
        SYS_ERROR_CHECK(err);

        if(err == ESP_OK)
        {
            err = nvs_commit(my_nvs_handle);
            SYS_ERROR_CHECK(err);
        }
    }

    nvs_close(my_nvs_handle);
}
