/***********************************************************
 *
 * MIT License
 *
 * Copyright (c) 2025 kiritro
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 *
 * FileName : /film_service/src/service_ota.c
 * Author: Kiritro  Version: v0.1  Date: 2026/4/30
 * Description: OTA service
 * ChangeLog: Change Notes
 *
***********************************************************/

/*********************************************************************
 * INCLUDES
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_system.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_app_format.h"

#include "sys_log.h"
#include "sys_com.h"
#include "service_ota.h"

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
        uint8_t progress = (m_ota_state.received_size * 100) / m_ota_state.total_size;
        if(progress % 10 == 0)
        {
            // sys_logi(OTA_TAG, "OTA progress: %d%% (%d/%d bytes)", progress, m_ota_state.received_size, m_ota_state.total_size);
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
