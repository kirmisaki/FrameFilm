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
 * FileName : /film_service/src/service_ota.c
 * Author: Kiritro  Version: v0.1  Date: 2026/4/30
 * Description: OTA service
 * ChangeLog: Change Notes
 *
 *********************************************************************/

/*********************************************************************
 * INCLUDES
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_system.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_app_format.h"
#include "esp_app_desc.h"
#include "mbedtls/sha256.h"

#include "sys_log.h"
#include "sys_event.h"
#include "sys_com.h"
#include "service_ota.h"
#include "hal_hardware.h"

/*********************************************************************
 * MACROS
 */
#define OTA_TAG                         "ota_service"

#define OTA_STATE_IDLE                  (0)
#define OTA_STATE_STARTED               (1)
#define OTA_STATE_RECEIVING             (2)
#define OTA_STATE_COMPLETED             (3)
#define OTA_STATE_FAILED                (4)

/*********************************************************************
 * TYPEDEFS
 */
typedef struct {
    uint8_t state;
    uint8_t last_progress;              // 上次广播的进度百分比（按 10% 分档去重）
    uint32_t total_size;
    uint32_t received_size;
    const esp_partition_t *update_partition;
    esp_ota_handle_t update_handle;
} ota_service_state_t;

/*********************************************************************
 * CONSTANTS
 */

/*********************************************************************
 * LOCAL VARIABLES
 */
static ota_service_state_t m_ota_state;
static uint8_t m_ota_init = 0;

/*********************************************************************
 * GLOBAL VARIABLES
 */

/*********************************************************************
 * LOCAL FUNCTIONS
 */
static void ota_internal_init(void);
static esp_err_t ota_validate_target_partition(const esp_partition_t *part);

/*********************************************************************
 * GLOBAL FUNCTIONS
 */

static void ota_internal_init(void)
{
    if(m_ota_init == 0)
    {
        memset(&m_ota_state, 0, sizeof(ota_service_state_t));
        m_ota_state.state = OTA_STATE_IDLE;
        m_ota_state.total_size = 0;
        m_ota_state.received_size = 0;
        m_ota_state.update_partition = NULL;
        m_ota_init = 1;
        sys_logi(OTA_TAG, "OTA service initialized");
    }
}

void service_ota_start(void)
{
    ota_internal_init();

    if(m_ota_state.state != OTA_STATE_IDLE)
    {
        sys_loge(OTA_TAG, "OTA is already in progress, state: %d", m_ota_state.state);
        return;
    }

    m_ota_state.update_partition = esp_ota_get_next_update_partition(NULL);
    if(m_ota_state.update_partition == NULL)
    {
        sys_loge(OTA_TAG, "No OTA partition found");
        m_ota_state.state = OTA_STATE_FAILED;
        return;
    }

    sys_logi(OTA_TAG, "Starting OTA, partition: %s, size: %d bytes",
             m_ota_state.update_partition->label,
             m_ota_state.update_partition->size);

    esp_err_t err = esp_ota_begin(m_ota_state.update_partition, OTA_SIZE_UNKNOWN, &m_ota_state.update_handle);
    if(err != ESP_OK)
    {
        sys_loge(OTA_TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        m_ota_state.state = OTA_STATE_FAILED;
        return;
    }

    m_ota_state.state = OTA_STATE_STARTED;
    m_ota_state.received_size = 0;
    m_ota_state.last_progress = 0;   // 每次 OTA 会话复位进度去重基准
    sys_logi(OTA_TAG, "OTA begin successful, waiting for data...");
}

void service_set_length(uint32_t len)
{
    if(m_ota_init == 0)
    {
        sys_loge(OTA_TAG, "OTA service not initialized");
        return;
    }

    if(m_ota_state.state != OTA_STATE_STARTED)
    {
        sys_loge(OTA_TAG, "OTA not started, cannot set length, state: %d", m_ota_state.state);
        return;
    }

    if(len > m_ota_state.update_partition->size)
    {
        sys_loge(OTA_TAG, "OTA size %d exceeds partition size %d", len, m_ota_state.update_partition->size);
        m_ota_state.state = OTA_STATE_FAILED;
        return;
    }

    m_ota_state.total_size = len;
    sys_logi(OTA_TAG, "OTA total length set: %d bytes", len);
}

void service_ota_write(uint8_t *data, uint16_t len)
{
    if(m_ota_init == 0)
    {
        sys_loge(OTA_TAG, "OTA service not initialized");
        return;
    }

    if(m_ota_state.state != OTA_STATE_STARTED && m_ota_state.state != OTA_STATE_RECEIVING)
    {
        sys_loge(OTA_TAG, "OTA not started, cannot write, state: %d", m_ota_state.state);
        return;
    }

    if(data == NULL || len == 0)
    {
        sys_loge(OTA_TAG, "Invalid data or length");
        return;
    }

    esp_err_t err = esp_ota_write(m_ota_state.update_handle, (const void *)data, len);
    if(err != ESP_OK)
    {
        sys_loge(OTA_TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
        m_ota_state.state = OTA_STATE_FAILED;
        return;
    }

    m_ota_state.received_size += len;
    m_ota_state.state = OTA_STATE_RECEIVING;

    if(m_ota_state.total_size > 0)
    {
        uint8_t progress = (uint8_t)((m_ota_state.received_size * 100) / m_ota_state.total_size);
        /* 每跨 10% 广播一次进度，避免逐包刷屏，payload: u8 percent */
        if((progress / 10) != (m_ota_state.last_progress / 10))
        {
            m_ota_state.last_progress = progress;
            sys_event_publish(SYS_EVT_OTA_PROGRESS, &progress, sizeof(progress));
        }
    }
    else
    {
        if(m_ota_state.received_size % (50 * 1024) == 0)
        {
            sys_logi(OTA_TAG, "OTA received: %d bytes", m_ota_state.received_size);
        }
    }
}

void service_ota_stop(void)
{
    if(m_ota_init == 0)
    {
        sys_loge(OTA_TAG, "OTA service not initialized");
        sys_reboot();
        return;
    }

    if(m_ota_state.state != OTA_STATE_RECEIVING)
    {
        sys_loge(OTA_TAG, "OTA not in receiving state, state: %d", m_ota_state.state);
        sys_reboot();
        return;
    }

    sys_logi(OTA_TAG, "OTA writing completed, received: %d bytes", m_ota_state.received_size);

    esp_err_t err = esp_ota_end(m_ota_state.update_handle);
    if(err != ESP_OK)
    {
        if(err == ESP_ERR_OTA_VALIDATE_FAILED)
        {
            sys_loge(OTA_TAG, "OTA image validation failed");
        }
        else
        {
            sys_loge(OTA_TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
        }
        m_ota_state.state = OTA_STATE_FAILED;

        sys_reboot();
        return;
    }

    err = ota_validate_target_partition(m_ota_state.update_partition);
    if(err != ESP_OK)
    {
        sys_loge(OTA_TAG, "OTA hardware target mismatch or missing descriptor; current image retained");
        m_ota_state.state = OTA_STATE_FAILED;
        sys_reboot();
        return;
    }
    err = esp_ota_set_boot_partition(m_ota_state.update_partition);
    if(err != ESP_OK)
    {
        sys_loge(OTA_TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
        m_ota_state.state = OTA_STATE_FAILED;

        sys_reboot();
        return;
    }

    m_ota_state.state = OTA_STATE_COMPLETED;
    sys_logi(OTA_TAG, "OTA completed successfully, rebooting...");

    sys_reboot();
}

/* Staged WiFi OTA. Boot rollback is not enabled. */
/* Shared final gate for legacy BLE and staged HTTP OTA, before boot selection. */
static esp_err_t ota_validate_target_partition(const esp_partition_t *part)
{
    esp_image_header_t header;
    esp_app_desc_t desc;
    ArkHardwareDescriptor_t hardware;
    if(esp_partition_read(part, 0, &header, sizeof(header)) != ESP_OK ||
       header.chip_id != ESP_CHIP_ID_ESP32S3 ||
       esp_ota_get_partition_description(part, &desc) != ESP_OK ||
       strncmp(desc.project_name, "frame_film_ark", sizeof(desc.project_name)) ||
       esp_partition_read(part, ARK_HARDWARE_DESCRIPTOR_OFFSET, &hardware, sizeof(hardware)) != ESP_OK ||
       !hal_hardware_descriptor_matches(&hardware))
        return ESP_ERR_INVALID_ARG;
    return ESP_OK;
}

static struct
{
    esp_ota_handle_t handle;
    const esp_partition_t *part;
    uint32_t size, received;
    uint8_t sha[32];
    mbedtls_sha256_context hash;
    bool writing, hashing, ready;
} direct_ota;
uint32_t service_ota_direct_max(void)
{
    const esp_partition_t *p = esp_ota_get_next_update_partition(NULL);
    return p ? p->size : 0;
}
void service_ota_direct_abort(void)
{
    if(direct_ota.writing)
        esp_ota_abort(direct_ota.handle);
    if(direct_ota.hashing)
        mbedtls_sha256_free(&direct_ota.hash);
    memset(&direct_ota, 0, sizeof(direct_ota));
}
esp_err_t service_ota_direct_begin(uint32_t size, const uint8_t sha[32])
{
    if(direct_ota.writing || direct_ota.ready)
        return ESP_ERR_INVALID_STATE;
    const esp_partition_t *p = esp_ota_get_next_update_partition(NULL);
    if(!p || p->address == esp_ota_get_running_partition()->address || !size || size > p->size)
        return ESP_ERR_INVALID_SIZE;
    service_ota_direct_abort();
    esp_err_t e = esp_ota_begin(p, size, &direct_ota.handle);
    if(e != ESP_OK)
        return e;
    direct_ota.writing = true;
    direct_ota.part = p;
    direct_ota.size = size;
    memcpy(direct_ota.sha, sha, 32);
    mbedtls_sha256_init(&direct_ota.hash);
    direct_ota.hashing = true;
    if(mbedtls_sha256_starts(&direct_ota.hash, 0))
    {
        service_ota_direct_abort();
        return ESP_FAIL;
    }
    return ESP_OK;
}
esp_err_t service_ota_direct_write(const uint8_t *data, size_t len)
{
    if(!direct_ota.writing || !data || !len)
        return ESP_ERR_INVALID_STATE;
    if(len > direct_ota.size - direct_ota.received)
        return ESP_ERR_INVALID_SIZE;
    esp_err_t e = esp_ota_write(direct_ota.handle, data, len);
    if(e != ESP_OK)
        return e;
    if(mbedtls_sha256_update(&direct_ota.hash, data, len))
        return ESP_FAIL;
    direct_ota.received += len;
    return ESP_OK;
}
esp_err_t service_ota_direct_finish(void)
{
    if(!direct_ota.writing)
        return ESP_ERR_INVALID_STATE;
    uint8_t sha[32];
    if(direct_ota.received != direct_ota.size || mbedtls_sha256_finish(&direct_ota.hash, sha) ||
       memcmp(sha, direct_ota.sha, 32))
        return ESP_ERR_INVALID_CRC;
    mbedtls_sha256_free(&direct_ota.hash);
    direct_ota.hashing = false;
    esp_err_t e = esp_ota_end(direct_ota.handle);
    direct_ota.writing = false;
    if(e != ESP_OK)
        return e;
    e = ota_validate_target_partition(direct_ota.part);
    if(e != ESP_OK) return e;
    direct_ota.ready = true;
    sys_logi(OTA_TAG, "WiFi OTA verified: %lu bytes, awaiting activation",
             (unsigned long)direct_ota.received);
    return ESP_OK;
}
esp_err_t service_ota_direct_activate(void)
{
    if(!direct_ota.ready)
    {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t result = esp_ota_set_boot_partition(direct_ota.part);
    if(result == ESP_OK)
    {
        sys_logi(OTA_TAG, "WiFi OTA activated; restart pending");
    }
    return result;
}
