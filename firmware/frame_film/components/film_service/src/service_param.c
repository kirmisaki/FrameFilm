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

/* app 状态 blob：6 字节头 + app 数据（app 数据上限见 service_param.h 的
 * SERVICE_PARAM_APP_DATA_MAX，各 app 以 _Static_assert 校验自身结构体不超限） */
#define SERVICE_PARAM_APP_MAGIC                                 (0xA55A)

/*********************************************************************
* TYPEDEFS
*/
typedef struct __attribute__((packed))
{
    uint16_t magic;    // 0xA55A，不匹配视为无数据
    uint16_t size;     // 后续 app 数据有效字节数
    uint8_t  version;  // app 状态结构体版本
    uint8_t  rsvd;
} service_param_app_hdr_t;

/*********************************************************************
 * CONSTANTS
 */


/*********************************************************************
 * LOCAL VARIABLES
 */
static void nvs_init(void);
static void nvs_param_save(void);
static void service_param_set_default(void);
static esp_err_t app_nvs_open(nvs_handle_t *handle);
static void app_key_build(char *key, size_t key_size, uint8_t app_id);

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
 *
 * 上下文例外：本函数会调用 service_param_app_erase_all()，而 app 状态接口的约定是
 * “只在 app 任务上下文串行调用”。此处由 BLE 任务调用是安全的：
 *   1) 仅擦除 NVS，不读写任何 app 的 RAM 状态，不存在与 app 任务的数据竞争；
 *   2) 调用方随后立即 vTaskDelay + sys_reboot()，不会与正在运行的 app 状态机交错。
 * 除本场景外，请勿在 app 任务之外调用 service_param_app_* 系列接口。
 */
void service_param_reset(void)
{
    service_param_set_default();
    nvs_param_save();
    service_param_app_erase_all();
}


/**
 * [service_param_set_default 设置服务参数默认值]
 */
static void service_param_set_default(void)
{
    // 设置服务参数默认值
    g_service_param.param_ver = SERVICE_PARAM_VER;
    g_service_param.factory_flag = SERVICE_FACTORY_DEFAULT_FLAG;

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

        if(err == ESP_ERR_NVS_NOT_FOUND ||
           g_service_param.factory_flag != SERVICE_FACTORY_DEFAULT_FLAG ||
           g_service_param.param_ver != SERVICE_PARAM_VER) //FACTORY RESET / 布局版本变更
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

/**
 * [app_nvs_open 打开 app 状态命名空间]
 */
static esp_err_t app_nvs_open(nvs_handle_t *handle)
{
    return nvs_open(SYS_M_NVS_APP_NAMESPACE, NVS_READWRITE, handle);
}

/**
 * [app_key_build 构造 app 状态 key：app<id>]
 */
static void app_key_build(char *key, size_t key_size, uint8_t app_id)
{
    snprintf(key, key_size, "%s%u", SYS_M_NVS_APP_KEY_PREFIX, (unsigned)app_id);
}

/**
 * [service_param_app_load 读取 app 状态；失败返回负值（调用方回落默认值）]
 */
int service_param_app_load(uint8_t app_id, void *buf, uint16_t size, uint8_t ver)
{
    if(app_id >= SERVICE_PARAM_APP_NUM || buf == NULL || size == 0 || size > SERVICE_PARAM_APP_DATA_MAX)
    {
        return -1;
    }

    nvs_handle_t handle;
    if(app_nvs_open(&handle) != ESP_OK)
    {
        return -1;
    }

    char key[16] = {0};
    app_key_build(key, sizeof(key), app_id);

    /* 先读进栈缓冲校验头部，避免校验失败时污染调用方的 app 状态 */
    uint8_t raw[sizeof(service_param_app_hdr_t) + SERVICE_PARAM_APP_DATA_MAX];
    size_t len = sizeof(raw);
    esp_err_t err = nvs_get_blob(handle, key, raw, &len);
    nvs_close(handle);

    if(err != ESP_OK)
    {
        sys_logi("param", "app%u state not found: %d", (unsigned)app_id, (int)err);
        return -1;
    }

    /* 长度不足以容纳头部时直接判为无数据：必须先判，再解头部，
       否则 len < sizeof(hdr) 时会读到 raw 中未初始化的字节 */
    if(len < sizeof(service_param_app_hdr_t))
    {
        sys_logw("param", "app%u state too short: len %u", (unsigned)app_id, (unsigned)len);
        return -1;
    }

    service_param_app_hdr_t hdr;
    memcpy(&hdr, raw, sizeof(hdr));

    if(hdr.magic != SERVICE_PARAM_APP_MAGIC || hdr.size != size || hdr.version != ver ||
       len < (sizeof(hdr) + size))
    {
        sys_logw("param", "app%u state invalid (magic %04x size %u/%u ver %u/%u len %u)",
                 (unsigned)app_id, hdr.magic, hdr.size, size, hdr.version, ver, (unsigned)len);
        return -1;
    }

    memcpy(buf, raw + sizeof(hdr), size);
    return 0;
}

/**
 * [service_param_app_save 保存 app 状态]
 */
int service_param_app_save(uint8_t app_id, const void *buf, uint16_t size, uint8_t ver)
{
    if(app_id >= SERVICE_PARAM_APP_NUM || buf == NULL || size == 0 || size > SERVICE_PARAM_APP_DATA_MAX)
    {
        return -1;
    }

    nvs_handle_t handle;
    if(app_nvs_open(&handle) != ESP_OK)
    {
        return -1;
    }

    uint8_t raw[sizeof(service_param_app_hdr_t) + SERVICE_PARAM_APP_DATA_MAX];
    service_param_app_hdr_t hdr = {
        .magic   = SERVICE_PARAM_APP_MAGIC,
        .size    = size,
        .version = ver,
        .rsvd    = 0,
    };
    memcpy(raw, &hdr, sizeof(hdr));
    memcpy(raw + sizeof(hdr), buf, size);

    char key[16] = {0};
    app_key_build(key, sizeof(key), app_id);

    esp_err_t err = nvs_set_blob(handle, key, raw, sizeof(hdr) + size);
    if(err == ESP_OK)
    {
        err = nvs_commit(handle);
    }
    nvs_close(handle);

    if(err != ESP_OK)
    {
        sys_logw("param", "app%u state save failed: %d", (unsigned)app_id, (int)err);
        return -1;
    }
    return 0;
}

/**
 * [service_param_app_erase 清除单个 app 状态]
 */
int service_param_app_erase(uint8_t app_id)
{
    if(app_id >= SERVICE_PARAM_APP_NUM)
    {
        return -1;
    }

    nvs_handle_t handle;
    if(app_nvs_open(&handle) != ESP_OK)
    {
        return -1;
    }

    char key[16] = {0};
    app_key_build(key, sizeof(key), app_id);

    esp_err_t err = nvs_erase_key(handle, key);
    if(err == ESP_OK || err == ESP_ERR_NVS_NOT_FOUND)
    {
        err = nvs_commit(handle);
    }
    nvs_close(handle);

    return (err == ESP_OK) ? 0 : -1;
}

/**
 * [service_param_app_erase_all 清除全部 app 状态（出厂重置）]
 */
void service_param_app_erase_all(void)
{
    for(uint8_t i = 0; i < SERVICE_PARAM_APP_NUM; i++)
    {
        service_param_app_erase(i);
    }

    nvs_handle_t handle;
    if(app_nvs_open(&handle) == ESP_OK)
    {
        if(nvs_erase_key(handle, SYS_M_NVS_APP_KEY_CURRENT) == ESP_OK)
        {
            nvs_commit(handle);
        }
        nvs_close(handle);
    }
}

/**
 * [service_param_app_current_get 读取上次运行的 app id；无记录返回 -1]
 */
int service_param_app_current_get(void)
{
    nvs_handle_t handle;
    if(app_nvs_open(&handle) != ESP_OK)
    {
        return -1;
    }

    uint8_t app_id = 0;
    esp_err_t err = nvs_get_u8(handle, SYS_M_NVS_APP_KEY_CURRENT, &app_id);
    nvs_close(handle);

    if(err != ESP_OK || app_id >= SERVICE_PARAM_APP_NUM)
    {
        return -1;
    }
    return (int)app_id;
}

/**
 * [service_param_app_current_set 记录当前运行的 app id]
 */
int service_param_app_current_set(uint8_t app_id)
{
    if(app_id >= SERVICE_PARAM_APP_NUM)
    {
        return -1;
    }

    nvs_handle_t handle;
    if(app_nvs_open(&handle) != ESP_OK)
    {
        return -1;
    }

    esp_err_t err = nvs_set_u8(handle, SYS_M_NVS_APP_KEY_CURRENT, app_id);
    if(err == ESP_OK)
    {
        err = nvs_commit(handle);
    }
    nvs_close(handle);

    return (err == ESP_OK) ? 0 : -1;
}
