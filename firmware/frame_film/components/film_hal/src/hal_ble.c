/***********************************************************
 *
 * MIT License
 *
 * Copyright (c) 2026 kiritro
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
 * FileName : /film_hal/src/hal_ble.c
 * Author: Kiritro  Version: v0.1  Date: 2026/4/5
 * Description: ble gatt服务
 * ChangeLog: Change Notes
 *
***********************************************************/

/*********************************************************************
 * INCLUDES
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_mac.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_bt.h"

#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_bt_defs.h"
#include "esp_bt_main.h"
#include "esp_gatt_common_api.h"
#include "sdkconfig.h"

#include "sys_log.h"
#include "hal_ble.h"

/*********************************************************************
 * MACROS
 */
// #define CONFIG_SET_RAW_ADV_DATA

#define GATTS_TAG "GATTS_SERVICE"

#define TEST_MANUFACTURER_DATA_LEN        6

#define PROFILE_NUM                       2
#define PROFILE_A_APP_ID                  0
#define PROFILE_B_APP_ID                  1

#define GATTS_CHAR_VAL_LEN_MAX            200
#define CHAR_DECLARATION_SIZE             (sizeof(uint8_t))


/*********************************************************************
 * TYPEDEFS
 */
typedef struct
{
    uint8_t *prepare_buf;
    int prepare_len;
} prepare_type_env_t;


/*********************************************************************
 * CONSTANTS
 */
static const uint16_t character_client_config_uuid     = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;
static const uint16_t primary_service_uuid             = ESP_GATT_UUID_PRI_SERVICE;
static const uint16_t character_declaration_uuid       = ESP_GATT_UUID_CHAR_DECLARE;
static const uint8_t char_prop_read                    = ESP_GATT_CHAR_PROP_BIT_READ;
static const uint8_t char_prop_notify                  = ESP_GATT_CHAR_PROP_BIT_NOTIFY;
static const uint8_t char_prop_write_notify            = ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_NOTIFY | ESP_GATT_CHAR_PROP_BIT_WRITE_NR;

static const uint8_t m_measurement_ccc[2]              = {0x00, 0x00};

static const uint16_t REBOLOR_SERVICE_UUID             = 0x2000;
static const uint16_t REBOLOR_SERVICE_CHAR1_UUID       = 0x2001;
static const uint16_t REBOLOR_SERVICE_CHAR2_UUID       = 0x2002;
static const uint16_t REBOLOR_SERVICE_CHAR3_UUID       = 0x2003;

static const uint16_t DEVICE_INFO_SERVICE_UUID         = 0x180A;
static const uint16_t DEVICE_NAME_CHAR_UUID            = 0x2A00;
static const uint16_t MANUFACTURER_NAME_CHAR_UUID      = 0x2A29;
static const uint16_t MODEL_NUMBER_CHAR_UUID           = 0x2A24;
static const uint16_t SERIAL_NUMBER_CHAR_UUID          = 0x2A25;
static const uint16_t HARDWARE_REVISION_CHAR_UUID      = 0x2A27;
static const uint16_t FIRMWARE_REVISION_CHAR_UUID      = 0x2A26;
static const uint16_t SYSTEM_ID_CHAR_UUID              = 0x2A23;

static const esp_gatts_attr_db_t m_service_gatt_db[DEV_M_IDX_NB] =
{
    // Service Declaration
    [IDX_M_SVC]     =
    {   {ESP_GATT_AUTO_RSP}, {
            ESP_UUID_LEN_16, (uint8_t *) &primary_service_uuid, ESP_GATT_PERM_READ,
            sizeof(uint16_t), sizeof(REBOLOR_SERVICE_UUID), (uint8_t *) &REBOLOR_SERVICE_UUID
        }
    },

    /* Characteristic Declaration */
    [IDX_M_CHAR_1]     =
    {   {ESP_GATT_AUTO_RSP}, {
            ESP_UUID_LEN_16, (uint8_t *) &character_declaration_uuid, ESP_GATT_PERM_READ,
            CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *) &char_prop_write_notify
        }
    },

    /* Characteristic Value */
    [IDX_M_CHAR_VAL_1] =
    {   {ESP_GATT_AUTO_RSP}, {
            ESP_UUID_LEN_16, (uint8_t *) &REBOLOR_SERVICE_CHAR1_UUID, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
            GATTS_CHAR_VAL_LEN_MAX, 0, NULL
        }
    },

    /* Client Characteristic Configuration Descriptor */
    [IDX_M_CHAR_CFG_1]  =
    {   {ESP_GATT_AUTO_RSP}, {
            ESP_UUID_LEN_16, (uint8_t *) &character_client_config_uuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
            sizeof(uint16_t), sizeof(m_measurement_ccc), (uint8_t *)m_measurement_ccc
        }
    },

    /* Characteristic Declaration */
    [IDX_M_CHAR_2]     =
    {   {ESP_GATT_AUTO_RSP}, {
            ESP_UUID_LEN_16, (uint8_t *) &character_declaration_uuid, ESP_GATT_PERM_READ,
            CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *) &char_prop_notify
        }
    },

    /* Characteristic Value */
    [IDX_M_CHAR_VAL_2] =
    {   {ESP_GATT_AUTO_RSP}, {
            ESP_UUID_LEN_16, (uint8_t *) &REBOLOR_SERVICE_CHAR2_UUID, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
            GATTS_CHAR_VAL_LEN_MAX, 0, NULL
        }
    },

    /* Client Characteristic Configuration Descriptor */
    [IDX_M_CHAR_CFG_2]  =
    {   {ESP_GATT_AUTO_RSP}, {
            ESP_UUID_LEN_16, (uint8_t *) &character_client_config_uuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
            sizeof(uint16_t), sizeof(m_measurement_ccc), (uint8_t *)m_measurement_ccc
        }
    },

    /* Characteristic Declaration */
    [IDX_M_CHAR_3]     =
    {   {ESP_GATT_AUTO_RSP}, {
            ESP_UUID_LEN_16, (uint8_t *) &character_declaration_uuid, ESP_GATT_PERM_READ,
            CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *) &char_prop_notify
        }
    },

    /* Characteristic Value */
    [IDX_M_CHAR_VAL_3] =
    {   {ESP_GATT_AUTO_RSP}, {
            ESP_UUID_LEN_16, (uint8_t *) &REBOLOR_SERVICE_CHAR3_UUID, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
            GATTS_CHAR_VAL_LEN_MAX, 0, NULL
        }
    },

    /* Client Characteristic Configuration Descriptor */
    [IDX_M_CHAR_CFG_3]  =
    {   {ESP_GATT_AUTO_RSP}, {
            ESP_UUID_LEN_16, (uint8_t *) &character_client_config_uuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
            sizeof(uint16_t), sizeof(m_measurement_ccc), (uint8_t *)m_measurement_ccc
        }
    },

};

static const esp_gatts_attr_db_t dev_info_gatt_db[DEV_IDX_NB] =
{
    // Service Declaration
    [IDX_SVC_DEV]     =
    {   {ESP_GATT_AUTO_RSP}, {
            ESP_UUID_LEN_16, (uint8_t *) &primary_service_uuid, ESP_GATT_PERM_READ,
            sizeof(uint16_t), sizeof(DEVICE_INFO_SERVICE_UUID), (uint8_t *) &DEVICE_INFO_SERVICE_UUID
        }
    },

    /* Characteristic Declaration */
    [IDX_CHAR_1]     =
    {   {ESP_GATT_AUTO_RSP}, {
            ESP_UUID_LEN_16, (uint8_t *) &character_declaration_uuid, ESP_GATT_PERM_READ,
            CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *) &char_prop_read
        }
    },

    /* Characteristic Value */
    [IDX_CHAR_VAL_1] =
    {   {ESP_GATT_AUTO_RSP}, {
            ESP_UUID_LEN_16, (uint8_t *) &DEVICE_NAME_CHAR_UUID, ESP_GATT_PERM_READ,
            GATTS_CHAR_VAL_LEN_MAX, sizeof(SYS_DEVICE_NAME), (uint8_t *)SYS_DEVICE_NAME
        }
    },

    /* Characteristic Declaration */
    [IDX_CHAR_2]      =
    {   {ESP_GATT_AUTO_RSP}, {
            ESP_UUID_LEN_16, (uint8_t *) &character_declaration_uuid, ESP_GATT_PERM_READ,
            CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *) &char_prop_read
        }
    },

    /* Characteristic Value */
    [IDX_CHAR_VAL_2]  =
    {   {ESP_GATT_AUTO_RSP}, {
            ESP_UUID_LEN_16, (uint8_t *) &MANUFACTURER_NAME_CHAR_UUID, ESP_GATT_PERM_READ,
            GATTS_CHAR_VAL_LEN_MAX, sizeof(SYS_MANUFACTURER_NAME), (uint8_t *)SYS_MANUFACTURER_NAME
        }
    },

    /* Characteristic Declaration */
    [IDX_CHAR_3]      =
    {   {ESP_GATT_AUTO_RSP}, {
            ESP_UUID_LEN_16, (uint8_t *) &character_declaration_uuid, ESP_GATT_PERM_READ,
            CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *) &char_prop_read
        }
    },

    /* Characteristic Value */
    [IDX_CHAR_VAL_3]  =
    {   {ESP_GATT_AUTO_RSP}, {
            ESP_UUID_LEN_16, (uint8_t *) &MODEL_NUMBER_CHAR_UUID, ESP_GATT_PERM_READ,
            GATTS_CHAR_VAL_LEN_MAX, sizeof(SYS_MODEL_NUMBER), (uint8_t *)SYS_MODEL_NUMBER
        }
    },

    /* Characteristic Declaration */
    [IDX_CHAR_4]      =
    {   {ESP_GATT_AUTO_RSP}, {
            ESP_UUID_LEN_16, (uint8_t *) &character_declaration_uuid, ESP_GATT_PERM_READ,
            CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *) &char_prop_read
        }
    },

    /* Characteristic Value */
    [IDX_CHAR_VAL_4]  =
    {   {ESP_GATT_AUTO_RSP}, {
            ESP_UUID_LEN_16, (uint8_t *) &SERIAL_NUMBER_CHAR_UUID, ESP_GATT_PERM_READ,
            GATTS_CHAR_VAL_LEN_MAX, sizeof(SYS_SERIAL_NUMBER), (uint8_t *)SYS_SERIAL_NUMBER
        }
    },

    /* Characteristic Declaration */
    [IDX_CHAR_5]      =
    {   {ESP_GATT_AUTO_RSP}, {
            ESP_UUID_LEN_16, (uint8_t *) &character_declaration_uuid, ESP_GATT_PERM_READ,
            CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *) &char_prop_read
        }
    },

    /* Characteristic Value */
    [IDX_CHAR_VAL_5]  =
    {   {ESP_GATT_AUTO_RSP}, {
            ESP_UUID_LEN_16, (uint8_t *) &HARDWARE_REVISION_CHAR_UUID, ESP_GATT_PERM_READ,
            GATTS_CHAR_VAL_LEN_MAX, sizeof(SYS_HAREWARE_VERSION), (uint8_t *)SYS_HAREWARE_VERSION
        }
    },

    /* Characteristic Declaration */
    [IDX_CHAR_6]      =
    {   {ESP_GATT_AUTO_RSP}, {
            ESP_UUID_LEN_16, (uint8_t *) &character_declaration_uuid, ESP_GATT_PERM_READ,
            CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *) &char_prop_read
        }
    },

    /* Characteristic Value */
    [IDX_CHAR_VAL_6]  =
    {   {ESP_GATT_AUTO_RSP}, {
            ESP_UUID_LEN_16, (uint8_t *) &FIRMWARE_REVISION_CHAR_UUID, ESP_GATT_PERM_READ,
            GATTS_CHAR_VAL_LEN_MAX, sizeof(SYS_FIRMWARE_VERSION), (uint8_t *)SYS_FIRMWARE_VERSION
        }
    },

    /* Characteristic Declaration */
    [IDX_CHAR_7]      =
    {   {ESP_GATT_AUTO_RSP}, {
            ESP_UUID_LEN_16, (uint8_t *) &character_declaration_uuid, ESP_GATT_PERM_READ,
            CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *) &char_prop_read
        }
    },

    /* Characteristic Value */
    [IDX_CHAR_VAL_7]  =
    {   {ESP_GATT_AUTO_RSP}, {
            ESP_UUID_LEN_16, (uint8_t *) &SYSTEM_ID_CHAR_UUID, ESP_GATT_PERM_READ,
            GATTS_CHAR_VAL_LEN_MAX, sizeof(SYS_SYSTEM_ID), (uint8_t *)SYS_SYSTEM_ID
        }
    },
};


/*********************************************************************
 * LOCAL VARIABLES
 */
static uint16_t m_service_handle_table[DEV_M_IDX_NB];

static hal_ble_gatts_cmd_cb_t m_gatts_cmd_cb = NULL;

static esp_ble_conn_update_params_t conn_params = {0};

static uint8_t adv_config_done = 0;

static uint8_t ble_gatts_notify_mask = 0;
static uint8_t ble_gatt_mode = BLE_GATT_MODE_S;
static uint8_t ble_gatts_init = BLE_GATTS_UNINIT;
static uint8_t ble_gatts_connect = BLE_GATTS_DISCONNECT;
static uint8_t sys_ble_mac[6] = {0};

#ifdef CONFIG_SET_RAW_ADV_DATA
static uint8_t raw_adv_data[] =
{
    0x02, 0x01, 0x06,
    0x02, 0x0a, 0xeb, 0x03, 0x03, 0xab, 0xcd
};
static uint8_t raw_scan_rsp_data[] =
{
    0x0f, 0x09, 0x45, 0x53, 0x50, 0x5f, 0x47, 0x41, 0x54, 0x54, 0x53, 0x5f, 0x44,
    0x45, 0x4d, 0x4f
};
#else

static uint8_t adv_service_uuid128[16] =
{
    /* LSB <--------------------------------------------------------------------------------> MSB */
    //first uuid, 16bit, [12],[13] is the value
    0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00,
};

// The length of adv data must be less than 31 bytes
//static uint8_t test_manufacturer[TEST_MANUFACTURER_DATA_LEN] =  {0x12, 0x23, 0x45, 0x56};
//adv data
static esp_ble_adv_data_t adv_data =
{
    .set_scan_rsp = false,
    .include_name = true,
    .include_txpower = false,
    .min_interval = BLE_DEFAULT_MIN_INT, //slave connection min interval, Time = min_interval * 1.25 msec
    .max_interval = BLE_DEFAULT_MAX_INT, //slave connection max interval, Time = max_interval * 1.25 msec
    .appearance = 0x00,
    .manufacturer_len = TEST_MANUFACTURER_DATA_LEN,
    .p_manufacturer_data =  (uint8_t *)sys_ble_mac,
    .service_data_len = 0,
    .p_service_data = NULL,
    .service_uuid_len = sizeof(adv_service_uuid128),
    .p_service_uuid = adv_service_uuid128,
    .flag = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT),
};
// scan response data
static esp_ble_adv_data_t scan_rsp_data =
{
    .set_scan_rsp = true,
    .include_name = true,
    .include_txpower = true,
    //.min_interval = 0x0006,
    //.max_interval = 0x0010,
    .appearance = 0x00,
    .manufacturer_len = 0, //TEST_MANUFACTURER_DATA_LEN,
    .p_manufacturer_data =  NULL, //&test_manufacturer[0],
    .service_data_len = 0,
    .p_service_data = NULL,
    .service_uuid_len = sizeof(adv_service_uuid128),
    .p_service_uuid = adv_service_uuid128,
    .flag = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT),
};

#endif /* CONFIG_SET_RAW_ADV_DATA */

static esp_ble_adv_params_t adv_params =
{
    .adv_int_min        = BLE_ADV_MIN_INT,
    .adv_int_max        = BLE_ADV_MAX_INT,
    .adv_type           = ADV_TYPE_IND,
    .own_addr_type      = BLE_ADDR_TYPE_PUBLIC,
    //.peer_addr            =
    //.peer_addr_type       =
    .channel_map        = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

struct gatts_profile_inst
{
    esp_gatts_cb_t gatts_cb;
    uint16_t gatts_if;
    uint16_t app_id;
    uint16_t conn_id;
    uint16_t service_handle;
    esp_gatt_srvc_id_t service_id;
    uint16_t char_handle;
    esp_bt_uuid_t char_uuid;
    esp_gatt_perm_t perm;
    esp_gatt_char_prop_t property;
    uint16_t descr_handle;
    esp_bt_uuid_t descr_uuid;
};

static void esp_gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param);
static void gatts_profile_a_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param);
static void gatts_profile_b_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param);

/* One gatt-based profile one app_id and one gatts_if, this array will store the gatts_if returned by ESP_GATTS_REG_EVT */
static struct gatts_profile_inst gl_profile_tab[PROFILE_NUM] =
{
    [PROFILE_A_APP_ID] = {
        .gatts_cb = gatts_profile_a_event_handler,
        .gatts_if = ESP_GATT_IF_NONE,       /* Not get the gatt_if, so initial is ESP_GATT_IF_NONE */
    },
    [PROFILE_B_APP_ID] = {
        .gatts_cb = gatts_profile_b_event_handler,                   /* This demo does not implement, similar as profile A */
        .gatts_if = ESP_GATT_IF_NONE,       /* Not get the gatt_if, so initial is ESP_GATT_IF_NONE */
    },
};

static prepare_type_env_t a_prepare_write_env;


/*********************************************************************
 * GLOBAL VARIABLES
 */


/*********************************************************************
 * LOCAL FUNCTIONS
 */
static void dev_exec_write_event_env(prepare_type_env_t *prepare_write_env, esp_ble_gatts_cb_param_t *param);
static void gatts_notift_bit_set(uint16_t ret, uint8_t ch_bit);

/*********************************************************************
 * GLOBAL FUNCTIONS
 */


static void dev_exec_write_event_env(prepare_type_env_t *prepare_write_env, esp_ble_gatts_cb_param_t *param)
{
    if (param->exec_write.exec_write_flag == ESP_GATT_PREP_WRITE_EXEC)
    {
        // esp_log_buffer_hex(GATTS_TAG, prepare_write_env->prepare_buf, prepare_write_env->prepare_len);
    }
    else
    {
        sys_logi(GATTS_TAG, "ESP_GATT_PREP_WRITE_CANCEL");
    }
    if (prepare_write_env->prepare_buf)
    {
        free(prepare_write_env->prepare_buf);
        prepare_write_env->prepare_buf = NULL;
    }
    prepare_write_env->prepare_len = 0;
}

static void gatts_notift_bit_set(uint16_t ret, uint8_t ch_bit)
{
    if(ret == 0x0000)
    {
        ble_gatts_notify_mask &= ~(ch_bit);
    }
    else if(ret == 0x0001)
    {
        ble_gatts_notify_mask |= (ch_bit);
    }
}


void hal_ble_send_notify_data(uint8_t ch, uint8_t *buf, uint16_t len)
{
    if((ch == BLE_NOTIFY_SEND_CH1 || ch == BLE_NOTIFY_SEND_CH2 || ch == BLE_NOTIFY_SEND_CH3) && len > 0)
    {
        if(ble_gatts_connect == BLE_GATTS_CONNECT && ble_gatts_init == BLE_GATTS_INIT)
        {
            if(len <= GATTS_CHAR_VAL_LEN_MAX)
            {
                if(ch == BLE_NOTIFY_SEND_CH1 && (ble_gatts_notify_mask & GATTS_CH1_NOTIFY_ENABLE_BIT))
                {
                    esp_ble_gatts_send_indicate(gl_profile_tab[PROFILE_A_APP_ID].gatts_if, gl_profile_tab[PROFILE_A_APP_ID].conn_id,  m_service_handle_table[IDX_M_CHAR_VAL_1], len, buf, false);
                    // sys_logi(GATTS_TAG, "chid_%d send notify len %d gatts_if %d conn_id %d", ch, len, gl_profile_tab[PROFILE_A_APP_ID].gatts_if, gl_profile_tab[PROFILE_A_APP_ID].conn_id);
                }
                else if(ch == BLE_NOTIFY_SEND_CH2 && (ble_gatts_notify_mask & GATTS_CH2_NOTIFY_ENABLE_BIT))
                {
                    esp_ble_gatts_send_indicate(gl_profile_tab[PROFILE_A_APP_ID].gatts_if, gl_profile_tab[PROFILE_A_APP_ID].conn_id,  m_service_handle_table[IDX_M_CHAR_VAL_2], len, buf, false);
                    // sys_logi(GATTS_TAG, "chid_%d send notify len %d gatts_if %d conn_id %d", ch, len, gl_profile_tab[PROFILE_A_APP_ID].gatts_if, gl_profile_tab[PROFILE_A_APP_ID].conn_id);
                }
                else if(ch == BLE_NOTIFY_SEND_CH3 && (ble_gatts_notify_mask & GATTS_CH3_NOTIFY_ENABLE_BIT))
                {
                    esp_ble_gatts_send_indicate(gl_profile_tab[PROFILE_A_APP_ID].gatts_if, gl_profile_tab[PROFILE_A_APP_ID].conn_id,  m_service_handle_table[IDX_M_CHAR_VAL_3], len, buf, false);
                    // sys_logi(GATTS_TAG, "chid_%d send notify len %d gatts_if %d conn_id %d", ch, len, gl_profile_tab[PROFILE_A_APP_ID].gatts_if, gl_profile_tab[PROFILE_A_APP_ID].conn_id);
                }
            }
            else
            {
                sys_loge(GATTS_TAG, "notify out of length max %d", GATTS_CHAR_VAL_LEN_MAX);
            }
        }
    }
}

void hal_ble_gatts_cmd_register_cb(hal_ble_gatts_cmd_cb_t cb)
{
    m_gatts_cmd_cb = cb;
}

static void gatts_profile_a_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param)
{
    switch (event)
    {
    case ESP_GATTS_REG_EVT:
        sys_logi(GATTS_TAG, "REGISTER_APP_EVT, status %d, app_id %d", param->reg.status, param->reg.app_id);

        esp_err_t set_dev_name_ret = esp_ble_gap_set_device_name(SYS_DEVICE_NAME);
        if (set_dev_name_ret)
        {
            sys_loge(GATTS_TAG, "set device name failed, error code = %x", set_dev_name_ret);
        }
#ifdef CONFIG_SET_RAW_ADV_DATA
        esp_err_t raw_adv_ret = esp_ble_gap_config_adv_data_raw(raw_adv_data, sizeof(raw_adv_data));
        if (raw_adv_ret)
        {
            sys_loge(GATTS_TAG, "config raw adv data failed, error code = %x ", raw_adv_ret);
        }
        adv_config_done |= ADV_CONGIG_FLAG;
        esp_err_t raw_scan_ret = esp_ble_gap_config_scan_rsp_data_raw(raw_scan_rsp_data, sizeof(raw_scan_rsp_data));
        if (raw_scan_ret)
        {
            sys_loge(GATTS_TAG, "config raw scan rsp data failed, error code = %x", raw_scan_ret);
        }
        adv_config_done |= SCAN_RSP_CONFIG_FLAG;
#else
        //config adv data
        esp_err_t ret = esp_ble_gap_config_adv_data(&adv_data);
        if (ret)
        {
            sys_loge(GATTS_TAG, "config adv data failed, error code = %x", ret);
        }
        adv_config_done |= ADV_CONGIG_FLAG;
        //config scan response data
        ret = esp_ble_gap_config_adv_data(&scan_rsp_data);
        if (ret)
        {
            sys_loge(GATTS_TAG, "config scan response data failed, error code = %x", ret);
        }
        adv_config_done |= SCAN_RSP_CONFIG_FLAG;

#endif
        sys_logi(GATTS_TAG, "REGISTER_APP_EVT_A, status %d, app_id %d, gatts_if %d", param->reg.status, param->reg.app_id, gatts_if);
        esp_err_t create_attr_ret = esp_ble_gatts_create_attr_tab(m_service_gatt_db, gatts_if, DEV_M_IDX_NB, PROFILE_A_APP_ID);
        if (create_attr_ret)
        {
            sys_loge(GATTS_TAG, "create attr a table failed, error code = %x", create_attr_ret);
        }
        break;
    case ESP_GATTS_READ_EVT:
    {
        sys_logi(GATTS_TAG, "GATT_READ_EVT, conn_id %d, trans_id %" PRIu32 ", handle %d", param->read.conn_id, param->read.trans_id, param->read.handle);
        break;
    }
    case ESP_GATTS_WRITE_EVT:
    {
        // sys_logi(GATTS_TAG, "GATT_WRITE_EVT, conn_id %d, trans_id %" PRIu32 ", handle %d", param->write.conn_id, param->write.trans_id, param->write.handle);
        if (!param->write.is_prep)
        {
            if(param->write.handle == m_service_handle_table[IDX_M_CHAR_CFG_1] && param->write.len == 2)
            {
                uint16_t descr_value = param->write.value[1] << 8 | param->write.value[0];
                gatts_notift_bit_set(descr_value, GATTS_CH1_NOTIFY_ENABLE_BIT);
                if(ble_gatts_notify_mask & GATTS_CH1_NOTIFY_ENABLE_BIT)
                {
                    sys_logi(GATTS_TAG, "notify ch1 enable");
                }
                else
                {
                    sys_logi(GATTS_TAG, "notify ch1 disable");
                }
            }
            else if(param->write.handle == m_service_handle_table[IDX_M_CHAR_CFG_2] && param->write.len == 2)
            {
                uint16_t descr_value = param->write.value[1] << 8 | param->write.value[0];
                gatts_notift_bit_set(descr_value, GATTS_CH2_NOTIFY_ENABLE_BIT);
                if(ble_gatts_notify_mask & GATTS_CH2_NOTIFY_ENABLE_BIT)
                {
                    sys_logi(GATTS_TAG, "notify ch2 enable");
                }
                else
                {
                    sys_logi(GATTS_TAG, "notify ch2 disable");  
                }
            }
            else if(param->write.handle == m_service_handle_table[IDX_M_CHAR_CFG_3] && param->write.len == 2)
            {
                uint16_t descr_value = param->write.value[1] << 8 | param->write.value[0];
                gatts_notift_bit_set(descr_value, GATTS_CH3_NOTIFY_ENABLE_BIT);
                if(ble_gatts_notify_mask & GATTS_CH3_NOTIFY_ENABLE_BIT)
                {
                    sys_logi(GATTS_TAG, "notify ch3 enable");
                }
                else
                {
                    sys_logi(GATTS_TAG, "notify ch3 disable");
                }
            }
            else if(param->write.handle == m_service_handle_table[IDX_M_CHAR_VAL_1])
            {
                if(m_gatts_cmd_cb != NULL)
                {
                    m_gatts_cmd_cb(param->write.value, param->write.len);
                    // sys_logi(GATTS_TAG, "ch1 value: %02x", param->write.value[0]);
                }
            }
            else
            {
                sys_loge(GATTS_TAG, "unknown descr value");
            }
        }
        break;
    }
    case ESP_GATTS_EXEC_WRITE_EVT:
        sys_logi(GATTS_TAG, "ESP_GATTS_EXEC_WRITE_EVT");
        esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, ESP_GATT_OK, NULL);
        dev_exec_write_event_env(&a_prepare_write_env, param);
        break;
    case ESP_GATTS_MTU_EVT:

        sys_logi(GATTS_TAG, "ESP_GATTS_MTU_EVT, MTU %d", param->mtu.mtu);
        break;
    case ESP_GATTS_UNREG_EVT:
        break;
    case ESP_GATTS_CREATE_EVT:
        sys_logi(GATTS_TAG, "CREATE_SERVICE_EVT, status %d,  service_handle %d", param->create.status, param->create.service_handle);
        break;
    case ESP_GATTS_ADD_INCL_SRVC_EVT:
        break;
    case ESP_GATTS_ADD_CHAR_EVT:
    {
        break;
    }
    case ESP_GATTS_ADD_CHAR_DESCR_EVT:
        gl_profile_tab[PROFILE_A_APP_ID].descr_handle = param->add_char_descr.attr_handle;
        sys_logi(GATTS_TAG, "ADD_DESCR_EVT, status %d, attr_handle %d, service_handle %d",
                 param->add_char_descr.status, param->add_char_descr.attr_handle, param->add_char_descr.service_handle);
        break;
    case ESP_GATTS_DELETE_EVT:
        break;
    case ESP_GATTS_START_EVT:
        sys_logi(GATTS_TAG, "SERVICE_START_EVT, status %d, service_handle %d",
                 param->start.status, param->start.service_handle);
        break;
    case ESP_GATTS_STOP_EVT:
        break;
    case ESP_GATTS_CONNECT_EVT:
    {
        sys_logi(GATTS_TAG, "link_role:%d", param->connect.link_role);
        if(param->connect.link_role == 1)  //master role = 0  ; slave role = 1
        {
            memcpy(conn_params.bda, param->connect.remote_bda, sizeof(esp_bd_addr_t));
            /* For the IOS system, please reference the apple official documents about the ble connection parameters restrictions. */
            conn_params.latency = BLE_DAFAULT_LATENCY;
            conn_params.max_int = BLE_DEFAULT_MAX_INT;    // max_int = 0x20*1.25ms = 40ms
            conn_params.min_int = BLE_DEFAULT_MIN_INT;    // min_int = 0x10*1.25ms = 20ms
            conn_params.timeout = BLE_DEFAULT_TIMEOUT;    // timeout = 400*10ms = 4000ms
            sys_logi(GATTS_TAG, "ESP_GATTS_CONNECT_EVT, conn_id %d, remote %02x:%02x:%02x:%02x:%02x:%02x:",
                     param->connect.conn_id,
                     param->connect.remote_bda[0], param->connect.remote_bda[1], param->connect.remote_bda[2],
                     param->connect.remote_bda[3], param->connect.remote_bda[4], param->connect.remote_bda[5]);
            gl_profile_tab[PROFILE_A_APP_ID].conn_id = param->connect.conn_id;
            //start sent the update connection parameters to the peer device.
            esp_ble_gap_update_conn_params(&conn_params);
            ble_gatts_connect = BLE_GATTS_CONNECT;
            ble_gatts_notify_mask = 0x00;
        }
        break;
    }
    case ESP_GATTS_DISCONNECT_EVT:
    {
        if(memcmp(param->disconnect.remote_bda, conn_params.bda, 6) == 0)
        {
            ble_gatts_connect = BLE_GATTS_DISCONNECT;
            ble_gatts_notify_mask = 0x00;
            sys_logi(GATTS_TAG, "ESP_GATTS_DISCONNECT_EVT, disconnect reason 0x%x", param->disconnect.reason);
            esp_ble_gap_start_advertising(&adv_params);
        }
        break;
    }
    case ESP_GATTS_CONF_EVT:
        // sys_logi(GATTS_TAG, "ESP_GATTS_CONF_EVT, status %d attr_handle %d", param->conf.status, param->conf.handle);
        if (param->conf.status != ESP_GATT_OK)
        {
            // esp_log_buffer_hex(GATTS_TAG, param->conf.value, param->conf.len);
        }
        break;
    case ESP_GATTS_CREAT_ATTR_TAB_EVT:
    {
        if (param->add_attr_tab.status != ESP_GATT_OK)
        {
            sys_loge(GATTS_TAG, "create attribute a table failed, error code=0x%x", param->add_attr_tab.status);
        }
        else if (param->add_attr_tab.num_handle != DEV_M_IDX_NB)
        {
            sys_loge(GATTS_TAG, "create attribute a table abnormally, num_handle (%d) doesn't equal to DEV_M_IDX_NB(%d)", param->add_attr_tab.num_handle, DEV_M_IDX_NB);
        }
        else
        {
            sys_logi(GATTS_TAG, "create attribute a table successfully, the number handle = %d", param->add_attr_tab.num_handle);
            memcpy(m_service_handle_table, param->add_attr_tab.handles, sizeof(m_service_handle_table));
            gl_profile_tab[PROFILE_A_APP_ID].service_handle = param->add_attr_tab.handles[0];
            esp_ble_gatts_start_service(m_service_handle_table[0]);
        }
        break;
    }
    case ESP_GATTS_OPEN_EVT:
    case ESP_GATTS_CANCEL_OPEN_EVT:
    case ESP_GATTS_CLOSE_EVT:
    case ESP_GATTS_LISTEN_EVT:
    case ESP_GATTS_CONGEST_EVT:
    default:
        break;
    }
}

static void gatts_profile_b_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param)
{
    switch (event)
    {
    case ESP_GATTS_REG_EVT:
        sys_logi(GATTS_TAG, "REGISTER_APP_EVT, status %d, app_id %d", param->reg.status, param->reg.app_id);
        esp_err_t create_attr_ret = esp_ble_gatts_create_attr_tab(dev_info_gatt_db, gatts_if, DEV_IDX_NB, PROFILE_B_APP_ID);
        if (create_attr_ret)
        {
            sys_loge(GATTS_TAG, "create attr table failed, error code = %x", create_attr_ret);
        }
        break;
    case ESP_GATTS_READ_EVT:
    {
        sys_logi(GATTS_TAG, "GATT_READ_EVT, conn_id %d, trans_id %" PRIu32 ", handle %d", param->read.conn_id, param->read.trans_id, param->read.handle);
        break;
    }
    case ESP_GATTS_WRITE_EVT:
    {
        sys_logi(GATTS_TAG, "GATT_WRITE_EVT, conn_id %d, trans_id %" PRIu32 ", handle %d", param->write.conn_id, param->write.trans_id, param->write.handle);
        break;
    }
    case ESP_GATTS_EXEC_WRITE_EVT:
        break;
    case ESP_GATTS_MTU_EVT:
        break;
    case ESP_GATTS_UNREG_EVT:
        break;
    case ESP_GATTS_CREATE_EVT:
        sys_logi(GATTS_TAG, "CREATE_SERVICE_EVT1, status %d,  service_handle %d", param->create.status, param->create.service_handle);
        break;
    case ESP_GATTS_CREAT_ATTR_TAB_EVT:
    {
        if (param->add_attr_tab.status != ESP_GATT_OK)
        {
            sys_loge(GATTS_TAG, "create attribute table failed, error code=0x%x", param->add_attr_tab.status);
        }
        else if (param->add_attr_tab.num_handle != DEV_IDX_NB)
        {
            sys_loge(GATTS_TAG, "create attribute table abnormally, num_handle (%d) \
doesn't equal to HRS_IDX_NB(%d)", param->add_attr_tab.num_handle, DEV_IDX_NB);
        }
        else
        {
            sys_logi(GATTS_TAG, "create attribute table successfully, the number handle = %d", param->add_attr_tab.num_handle);
            gl_profile_tab[PROFILE_B_APP_ID].service_handle = param->add_attr_tab.handles[0];
            esp_ble_gatts_start_service(gl_profile_tab[PROFILE_B_APP_ID].service_handle);
        }
        break;
    }
    case ESP_GATTS_ADD_INCL_SRVC_EVT:
        break;
    case ESP_GATTS_ADD_CHAR_EVT:
        sys_logi(GATTS_TAG, "ADD_CHAR_EVT, status %d,  attr_handle %d, service_handle %d",
                 param->add_char.status, param->add_char.attr_handle, param->add_char.service_handle);

        gl_profile_tab[PROFILE_B_APP_ID].char_handle = param->add_char.attr_handle;
        esp_ble_gatts_add_char_descr(gl_profile_tab[PROFILE_B_APP_ID].service_handle, &gl_profile_tab[PROFILE_B_APP_ID].descr_uuid,
                                     ESP_GATT_PERM_READ,
                                     NULL, NULL);
        break;
    case ESP_GATTS_ADD_CHAR_DESCR_EVT:
        gl_profile_tab[PROFILE_B_APP_ID].descr_handle = param->add_char_descr.attr_handle;
        sys_logi(GATTS_TAG, "ADD_DESCR_EVT, status %d, attr_handle %d, service_handle %d",
                 param->add_char_descr.status, param->add_char_descr.attr_handle, param->add_char_descr.service_handle);
        break;
    case ESP_GATTS_DELETE_EVT:
        break;
    case ESP_GATTS_START_EVT:
        sys_logi(GATTS_TAG, "SERVICE_START_EVT, status %d, service_handle %d",
                 param->start.status, param->start.service_handle);
        break;
    case ESP_GATTS_STOP_EVT:
        break;
    case ESP_GATTS_CONNECT_EVT:
    {
        if(param->connect.link_role == 1)  //master role = 0  ; slave role = 1
        {
            sys_logi(GATTS_TAG, "CONNECT_EVT, conn_id %d, remote %02x:%02x:%02x:%02x:%02x:%02x:",
                     param->connect.conn_id,
                     param->connect.remote_bda[0], param->connect.remote_bda[1], param->connect.remote_bda[2],
                     param->connect.remote_bda[3], param->connect.remote_bda[4], param->connect.remote_bda[5]);
            gl_profile_tab[PROFILE_B_APP_ID].conn_id = param->connect.conn_id;
        }
        break;
    }
    case ESP_GATTS_CONF_EVT:
        sys_logi(GATTS_TAG, "ESP_GATTS_CONF_EVT status %d attr_handle %d", param->conf.status, param->conf.handle);
        if (param->conf.status != ESP_GATT_OK)
        {
            // esp_log_buffer_hex(GATTS_TAG, param->conf.value, param->conf.len);
        }
        break;
    case ESP_GATTS_DISCONNECT_EVT:
    case ESP_GATTS_OPEN_EVT:
    case ESP_GATTS_CANCEL_OPEN_EVT:
    case ESP_GATTS_CLOSE_EVT:
    case ESP_GATTS_LISTEN_EVT:
    case ESP_GATTS_CONGEST_EVT:
    default:
        break;
    }
}

static void gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param)
{
    /* If event is register event, store the gatts_if for each profile */
    if (event == ESP_GATTS_REG_EVT)
    {
        if (param->reg.status == ESP_GATT_OK)
        {
            gl_profile_tab[param->reg.app_id].gatts_if = gatts_if;
        }
        else
        {
            sys_logi(GATTS_TAG, "Reg app failed, app_id %04x, status %d",
                     param->reg.app_id,
                     param->reg.status);
            return;
        }
    }

    /* If the gatts_if equal to profile A, call profile A cb handler,
     * so here call each profile's callback */
    do
    {
        int idx;
        for (idx = 0; idx < PROFILE_NUM; idx++)
        {
            if (gatts_if == ESP_GATT_IF_NONE || /* ESP_GATT_IF_NONE, not specify a certain gatt_if, need to call every profile cb function */
                    gatts_if == gl_profile_tab[idx].gatts_if)
            {
                if (gl_profile_tab[idx].gatts_cb)
                {
                    gl_profile_tab[idx].gatts_cb(event, gatts_if, param);
                }
            }
        }
    }
    while (0);
}

void hal_ble_gatt_server_init(void)
{
    esp_err_t ret;
    esp_err_t p_ret = 0;

    ble_gatt_mode = BLE_GATT_MODE_S;
    ble_gatts_init = BLE_GATTS_UNINIT;
    ble_gatts_connect = BLE_GATTS_DISCONNECT;
    ble_gatts_notify_mask = 0x00;

    esp_read_mac((uint8_t *)sys_ble_mac, ESP_MAC_BT);
    sys_logi(GATTS_TAG, "ble mac:%02x %02x %02x %02x %02x %02x", sys_ble_mac[0], sys_ble_mac[1],   
             sys_ble_mac[2], sys_ble_mac[3],   
             sys_ble_mac[4], sys_ble_mac[5]);

    SYS_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret)
    {
        sys_loge(GATTS_TAG, "initialize controller failed: %s", esp_err_to_name(ret));
    }
    p_ret += ret;

    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret)
    {
        sys_loge(GATTS_TAG, "enable controller failed: %s", esp_err_to_name(ret));
    }
    p_ret += ret;

    ret = esp_bluedroid_init();
    if (ret)
    {
        sys_loge(GATTS_TAG, "init bluetooth failed: %s", esp_err_to_name(ret));
    }
    p_ret += ret;

    ret = esp_bluedroid_enable();
    if (ret)
    {
        sys_loge(GATTS_TAG, "enable bluetooth failed: %s", esp_err_to_name(ret));
    }
    p_ret += ret;

    //gap 初始化
    ret = esp_ble_gap_register_callback(esp_gap_cb);
    if (ret)
    {
        sys_loge(GATTS_TAG, "gap register error, error code = %x", ret);
    }
    p_ret += ret;

    //gatts 初始化
    p_ret = 0;
    ret = esp_ble_gatts_register_callback(gatts_event_handler);
    if (ret)
    {
        sys_loge(GATTS_TAG, "gatts register error, error code = %x", ret);
    }
    p_ret += ret;

    ret = esp_ble_gatts_app_register(PROFILE_A_APP_ID);
    if (ret)
    {
        sys_loge(GATTS_TAG, "gatts app register error, error code = %x", ret);
    }
    p_ret += ret;

    ret = esp_ble_gatts_app_register(PROFILE_B_APP_ID);
    if (ret)
    {
        sys_loge(GATTS_TAG, "gatts app register error, error code = %x", ret);
    }
    p_ret += ret;

    esp_err_t local_mtu_ret = esp_ble_gatt_set_local_mtu(GATTS_CHAR_VAL_LEN_MAX);
    if (local_mtu_ret)
    {
        sys_loge(GATTS_TAG, "set local  MTU failed, error code = %x", local_mtu_ret);
    }
    p_ret += ret;

    if (p_ret)
    {
        sys_loge(GATTS_TAG, "gatts init error!");
    }
    else
    {
        ble_gatts_init = BLE_GATTS_INIT;
    }
}


void hal_ble_gatt_server_uninit(void)
{
    esp_err_t ret;

    if(ble_gatts_connect == BLE_GATTS_DISCONNECT)
    {
        ret = esp_ble_gap_stop_advertising();
        if (ret)
        {
            sys_loge(GATTS_TAG, "gatts adv stop error, error code = %x", ret);
        }
    }
    else
    {
        ble_gatts_connect = BLE_GATTS_DISCONNECT;
        ret = esp_ble_gap_disconnect(conn_params.bda);
        if (ret)
        {
            sys_loge(GATTS_TAG, "gatts dev disconnect error, error code = %x", ret);
        }

        ret = esp_ble_gap_stop_advertising();
        if (ret)
        {
            sys_loge(GATTS_TAG, "gatts adv stop error, error code = %x", ret);
        }
    }

    ble_gatt_mode = BLE_GATT_MODE_S;
    ble_gatts_init = BLE_GATTS_UNINIT;
    ble_gatts_connect = BLE_GATTS_DISCONNECT;
    ble_gatts_notify_mask = 0x00;

    ret = esp_ble_gatts_stop_service(gl_profile_tab[PROFILE_A_APP_ID].service_handle);
    if (ret)
    {
        sys_loge(GATTS_TAG, "gatts a stop error, error code = %x", ret);
    }

    ret = esp_ble_gatts_stop_service(gl_profile_tab[PROFILE_B_APP_ID].service_handle);
    if (ret)
    {
        sys_loge(GATTS_TAG, "gatts b stop error, error code = %x", ret);
    }

    ret = esp_ble_gatts_delete_service(gl_profile_tab[PROFILE_A_APP_ID].service_handle);
    if (ret)
    {
        sys_loge(GATTS_TAG, "gatts a delete error, error code = %x", ret);
    }

    ret = esp_ble_gatts_delete_service(gl_profile_tab[PROFILE_B_APP_ID].service_handle);
    if (ret)
    {
        sys_loge(GATTS_TAG, "gatts b delete error, error code = %x", ret);
    }

    ret = esp_ble_gatts_app_unregister(gl_profile_tab[PROFILE_A_APP_ID].gatts_if);
    if (ret)
    {
        sys_loge(GATTS_TAG, "gatts a app unregister, error code = %x", ret);
    }

    ret = esp_ble_gatts_app_unregister(gl_profile_tab[PROFILE_B_APP_ID].gatts_if);
    if (ret)
    {
        sys_loge(GATTS_TAG, "gatts b app unregister, error code = %x", ret);
    }
}


void hal_ble_gatt_server_reinit(void)
{
    esp_err_t ret;
    esp_err_t p_ret = 0;

    ble_gatt_mode = BLE_GATT_MODE_S;
    ble_gatts_init = BLE_GATTS_UNINIT;
    ble_gatts_connect = BLE_GATTS_DISCONNECT;
    ble_gatts_notify_mask = 0x00;

    //gatts 初始化
    p_ret = 0;
    ret = esp_ble_gatts_register_callback(gatts_event_handler);
    if (ret)
    {
        sys_loge(GATTS_TAG, "gatts register error, error code = %x", ret);
    }
    p_ret += ret;

    //gap 初始化
    ret = esp_ble_gap_register_callback(esp_gap_cb);
    if (ret)
    {
        sys_loge(GATTS_TAG, "gap register error, error code = %x", ret);
    }
    p_ret += ret;

    ret = esp_ble_gatts_app_register(PROFILE_A_APP_ID);
    if (ret)
    {
        sys_loge(GATTS_TAG, "gatts app register error, error code = %x", ret);
    }
    p_ret += ret;

    ret = esp_ble_gatts_app_register(PROFILE_B_APP_ID);
    if (ret)
    {
        sys_loge(GATTS_TAG, "gatts app register error, error code = %x", ret);
    }
    p_ret += ret;

    esp_err_t local_mtu_ret = esp_ble_gatt_set_local_mtu(GATTS_CHAR_VAL_LEN_MAX);
    if (local_mtu_ret)
    {
        sys_loge(GATTS_TAG, "set local  MTU failed, error code = %x", local_mtu_ret);
    }
    p_ret += ret;

    if (p_ret)
    {
        sys_loge(GATTS_TAG, "gatts init error!");
    }
    else
    {
        ble_gatts_init = BLE_GATTS_INIT;
    }
}

void hal_ble_gatts_dev_disconnect(void)
{
    sys_logi(GATTS_TAG, "Invalid device disconnect now!");

    esp_err_t ret = esp_ble_gap_disconnect(conn_params.bda);
    if (ret)
    {
        sys_loge(GATTS_TAG, "gatts dev disconnect error, error code = %x", ret);
    }
}

static void esp_gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    switch (event)
    {
    case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
        adv_config_done &= (~ADV_CONGIG_FLAG);
        if (adv_config_done == 0)
        {
            esp_ble_gap_start_advertising(&adv_params);
        }
        break;
    case ESP_GAP_BLE_SCAN_RSP_DATA_SET_COMPLETE_EVT:
        adv_config_done &= (~SCAN_RSP_CONFIG_FLAG);
        if (adv_config_done == 0)
        {
            esp_ble_gap_start_advertising(&adv_params);
        }
        break;
    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
        //advertising start complete event to indicate advertising start successfully or failed
        if (param->adv_start_cmpl.status != ESP_BT_STATUS_SUCCESS)
        {
            sys_loge(GATTS_TAG, "Advertising start failed");
        }
        break;
    case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
        if (param->adv_stop_cmpl.status != ESP_BT_STATUS_SUCCESS)
        {
            sys_loge(GATTS_TAG, "Advertising stop failed");
        }
        else
        {
            sys_logi(GATTS_TAG, "Stop adv successfully");
        }
        break;
    case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT:
    {
        //the unit of the duration is second
        // sys_logi( "gattc set start scan");
        break;
    }
    case ESP_GAP_BLE_SCAN_START_COMPLETE_EVT:
        //scan start complete event to indicate scan start successfully or failed
        if (param->scan_start_cmpl.status != ESP_BT_STATUS_SUCCESS)
        {
            sys_loge(GATTS_TAG, "scan start failed, error status = %x", param->scan_start_cmpl.status);
            break;
        }
        // sys_logi( "scan start success");
        break;
    case ESP_GAP_BLE_SCAN_RESULT_EVT:
    {
        esp_ble_gap_cb_param_t *scan_result = (esp_ble_gap_cb_param_t *)param;
        switch (scan_result->scan_rst.search_evt)
        {
        case ESP_GAP_SEARCH_INQ_RES_EVT:
            break;
        case ESP_GAP_SEARCH_INQ_CMPL_EVT:
        {
            sys_logi(GATTS_TAG, "ESP_GAP_SEARCH_INQ_CMPL_EVT, scan stop");
            break;
        }
        default:
            break;
        }
        break;
    }
    case ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT:
        if (param->scan_stop_cmpl.status != ESP_BT_STATUS_SUCCESS)
        {
            sys_loge(GATTS_TAG, "scan stop failed, error status = %x", param->scan_stop_cmpl.status);
            break;
        }
        sys_logi(GATTS_TAG, "stop scan successfully");
        break;
    case ESP_GAP_BLE_UPDATE_CONN_PARAMS_EVT:
        sys_logi(GATTS_TAG, "update connection params status = %d, min_int = %d, max_int = %d,conn_int = %d,latency = %d, timeout = %d",
                  param->update_conn_params.status,
                  param->update_conn_params.min_int,
                  param->update_conn_params.max_int,
                  param->update_conn_params.conn_int,
                  param->update_conn_params.latency,
                  param->update_conn_params.timeout);
        break;
    default:
        break;
    }
}
