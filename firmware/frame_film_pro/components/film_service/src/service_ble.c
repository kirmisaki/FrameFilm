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
 * FileName : /film_service/src/service_ble.c
 * Author: Kiritro  Version: v0.1  Date: 2026/4/5
 * Description: ble gatt服务
 * ChangeLog: Change Notes
 *
 *********************************************************************/

/*********************************************************************
 * INCLUDES
 */
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "esp_system.h"

#include "sys_log.h"

#include "service_ble_gatts.h"
#include "service_ble.h"
#include "service_file.h"
#include "service_film.h"
#include "service_ota.h"
#include "service_param.h"
#include "service_wifi.h"

#include "hal_bat.h"
#include "hal_sd.h"

/*********************************************************************
 * MACROS
 */


/*********************************************************************
 * TYPEDEFS
 */
#define BEL_SERVICE_TAG            "ble_service"

#define BLE_MSG_QUEUE_LENGTH       50
#define BLE_MSG_QUEUE_ITEM_SIZE    sizeof( ble_msg_t )

#define BLE_FILM_TRANS_IDLE       0
#define BLE_FILM_TRANS_STARTED    1
#define BLE_FILM_TRANS_RECV_NAME  2
#define BLE_FILM_TRANS_RECV_LEN   3
#define BLE_FILM_TRANS_RECV_DATA  4
#define BLE_FILM_TRANS_STOPPED    5

#define BLE_OTA_TRANS_IDLE        0
#define BLE_OTA_TRANS_STARTED     1
#define BLE_OTA_TRANS_RECV_LEN    2
#define BLE_OTA_TRANS_RECV_DATA   3
#define BLE_OTA_TRANS_STOPPED     4


/*********************************************************************
 * CONSTANTS
 */


/*********************************************************************
 * LOCAL VARIABLES
 */
static TaskHandle_t m_ble_task_hdl = NULL;
static QueueHandle_t m_ble_msg_hdl = NULL;
static uint8_t m_film_trans_state = BLE_FILM_TRANS_IDLE;
static uint8_t m_film_trans_filename[256];
static uint32_t m_film_trans_file_size = 0;
static uint32_t m_film_trans_received = 0;
static uint8_t m_ota_trans_state = BLE_OTA_TRANS_IDLE;
static uint32_t m_ota_trans_file_size = 0;
static uint32_t m_ota_trans_received = 0;


/*********************************************************************
 * GLOBAL VARIABLES
 */


/*********************************************************************
 * LOCAL FUNCTIONS
 */
static void ble_task_handle(void *pvParameters);
static uint8_t ble_checksum(uint8_t arr[], int len);
static void ble_cmd_process(ble_cmd_t *cmd);

/*********************************************************************
 * GLOBAL FUNCTIONS
 */



/**
 * [service_ble_init 初始化ble服务]
 */
void service_ble_init(void)
{
    // 初始化ble服务
    service_ble_gatt_server_init();
    service_ble_gatts_cmd_register_cb(service_ble_msg_gatts_cmd_send);

    if(m_ble_task_hdl == NULL)
    {
        if ( pdPASS != xTaskCreate( ble_task_handle, SYS_OS_NAME_BLE_TASK, SYS_OS_SIZE_BLE_TASK, NULL, SYS_OS_PRI_BLE_TASK, &m_ble_task_hdl ))
        {
            sys_loge(BEL_SERVICE_TAG, "ble task create error!");
        }
    }
}

/**
 * [service_ble_msg_send ble事件msg发送]
 * @param p_msg  [msg]
 * @param in_isr [is in interrupt]
 */
void service_ble_msg_send(void *p_msg, bool in_isr)
{
    /* The queue could not be created. */
    if(m_ble_msg_hdl != NULL)
    {
        if(in_isr == 0)
        {
            SYS_ERROR_CHECK((xQueueSend(m_ble_msg_hdl, p_msg, portMAX_DELAY) != pdPASS));
        }
        else /* Is In interrupt.*/
        {
            BaseType_t xHigherPriorityTaskWoken;
            /* No tasks have yet been unblocked. */
            xHigherPriorityTaskWoken = pdFALSE;

            /* Write the byte to the queue. xHigherPriorityTaskWoken will get set to
            pdTRUE if writing to the queue causes a task to leave the Blocked state,
            and the task leaving the Blocked state has a priority higher than the
            currently executing task (the task that was interrupted). */
            xQueueSendFromISR( m_ble_msg_hdl, p_msg, &xHigherPriorityTaskWoken );
            /* Now the buffer is empty, and the interrupt source has been cleared, a context
            switch should be performed if xHigherPriorityTaskWoken is equal to pdTRUE.
            NOTE: The syntax required to perform a context switch from an ISR varies from
            port to port, and from compiler to compiler. Check the web documentation and
            examples for the port being used to find the syntax required for your
            application. */
            portYIELD_FROM_ISR( xHigherPriorityTaskWoken );
        }
    }
}

/**
 * [service_ble_msg_gatts_cmd_send 发送命令获取msg]
 * @param p_data [命令buf]
 * @param len    [长度]
 */
void service_ble_msg_gatts_cmd_send( uint8_t const *p_data, uint16_t len )
{
    /* The queue could not be created. */
    if(m_ble_msg_hdl != NULL)
    {
        BaseType_t pxHigherPriorityTaskWoken = pdFALSE;

        ble_msg_t msg = {0};

        msg.ID = MSG_BLE_CH1_IN_CMD;
        msg.pdata = pvPortMalloc(len);
        msg.len = len;

        if(msg.pdata)
        {
            memcpy(msg.pdata, p_data, len);
            msg.len = len;
        }
        xQueueSendFromISR( m_ble_msg_hdl, &msg, &pxHigherPriorityTaskWoken );
        portYIELD_FROM_ISR( pxHigherPriorityTaskWoken );
    }
}

/**
 * [service_ble_msg_gatts_data_send 发送数据msg]
 * @param p_data [命令buf]
 * @param len    [长度]
 * @param ch     [val MSG_BLE_CH1_OUT_DATA MSG_BLE_CH2_OUT_DATA MSG_BLE_CH3_OUT_DATA]
 */
void service_ble_msg_gatts_data_send( uint8_t const *p_data, uint16_t len, uint8_t ch)
{
    /* The queue could not be created. */
    if(m_ble_msg_hdl != NULL)
    {
        BaseType_t pxHigherPriorityTaskWoken = pdFALSE;

        ble_msg_t msg = {0};

        msg.ID = ch;
        msg.pdata = pvPortMalloc(len);
        msg.len = len;

        if(msg.pdata)
        {
            memcpy(msg.pdata, p_data, len);
            msg.len = len;
        }
        xQueueSendFromISR( m_ble_msg_hdl, &msg, &pxHigherPriorityTaskWoken );
        portYIELD_FROM_ISR( pxHigherPriorityTaskWoken );
    }
}

static void ble_task_handle(void *pvParameters)
{
    m_ble_msg_hdl = xQueueCreate( BLE_MSG_QUEUE_LENGTH, BLE_MSG_QUEUE_ITEM_SIZE );
    SYS_ERROR_CHECK(m_ble_msg_hdl == NULL);

    for(;;)
    {
        ble_msg_t msg;
        SYS_ERROR_CHECK( xQueueReceive( m_ble_msg_hdl, (void *const)&msg, portMAX_DELAY ) != pdPASS );

        switch(msg.ID)
        {
        case MSG_BLE_CH1_IN_CMD :
        {
            if( msg.len )
            {
                if(msg.pdata[0] ==  BLE_CMD_HEAD)
                {
                    uint8_t sum = ble_checksum(msg.pdata, msg.len - 1);
                    // sys_logi(BEL_SERVICE_TAG, "sum:0x%02x", sum);

                    if(sum == msg.pdata[msg.len - 1])
                    {
                        ble_cmd_t cmd = {0};

                        cmd.ch = msg.pdata[1];
                        cmd.len = msg.pdata[2];
                        if(cmd.len == msg.len - 4)
                        {
                            cmd.pdata = msg.pdata + 3;
                            if(cmd.pdata)
                            {
                                ble_cmd_process(&cmd);
                            }
                        }
                    }
                }
                vPortFree(msg.pdata);
            }
            break;
        }
        case MSG_BLE_CH1_OUT_DATA :
        {
            if( msg.len )
            {
                service_ble_send_notify_data(BLE_NOTIFY_SEND_CH1, msg.pdata, msg.len);
                vPortFree(msg.pdata);
            }
            break;
        }
        case MSG_BLE_CH2_OUT_DATA :
        {
            if( msg.len )
            {
                service_ble_send_notify_data(BLE_NOTIFY_SEND_CH2, msg.pdata, msg.len);
                vPortFree(msg.pdata);
            }
            break;
        }
        case MSG_BLE_CH3_OUT_DATA :
        {
            if( msg.len )
            {
                service_ble_send_notify_data(BLE_NOTIFY_SEND_CH3, msg.pdata, msg.len);
                vPortFree(msg.pdata);
            }
            break;
        }
        case MSG_BLE_GAP_DISCONNECT:
            service_ble_gatts_dev_disconnect();
            break;
        default :
        {
            if( msg.len )
            {
                vPortFree(msg.pdata);
            }
            break;
        }
        }
    }
}

static void ble_cmd_process(ble_cmd_t *cmd)
{
    if(cmd == NULL)
    {
        sys_logw(BEL_SERVICE_TAG, "cmd is NULL");
        return;
    }

    switch(cmd->ch)
    {
        case BLE_FILM_TRANS_CH_FILE_START :
        {
            if(m_film_trans_state == BLE_FILM_TRANS_IDLE || m_film_trans_state == BLE_FILM_TRANS_STOPPED)
            {
                m_film_trans_state = BLE_FILM_TRANS_STARTED;
                m_film_trans_received = 0;
                memset(m_film_trans_filename, 0, sizeof(m_film_trans_filename));
                m_film_trans_file_size = 0;
                sys_logi(BEL_SERVICE_TAG, "Film transfer started");
            }
            else
            {
                sys_logw(BEL_SERVICE_TAG, "Invalid state for FILE_START: %d", m_film_trans_state);
            }
            break;
        }
        case BLE_FILM_TRANS_CH_FILE_NAME :
        {
            if(m_film_trans_state == BLE_FILM_TRANS_STARTED && cmd->len > 0)
            {
                memcpy(m_film_trans_filename, cmd->pdata, cmd->len);
                m_film_trans_filename[cmd->len] = '\0';
                m_film_trans_state = BLE_FILM_TRANS_RECV_NAME;
                sys_logi(BEL_SERVICE_TAG, "Received filename: %s", m_film_trans_filename);
            }
            else
            {
                sys_logw(BEL_SERVICE_TAG, "Invalid state or length for FILE_NAME: state=%d, len=%d", m_film_trans_state, cmd->len);
            }
            break;
        }
        case BLE_FILM_TRANS_CH_FILE_LEN :
        {
            if(m_film_trans_state == BLE_FILM_TRANS_RECV_NAME && cmd->len == 4)
            {
                m_film_trans_file_size = (cmd->pdata[0] << 24) | (cmd->pdata[1] << 16) | (cmd->pdata[2] << 8) | cmd->pdata[3];
                m_film_trans_state = BLE_FILM_TRANS_RECV_LEN;
                sys_logi(BEL_SERVICE_TAG, "Received file size: %d", m_film_trans_file_size);

                service_file_save_start((const char*)m_film_trans_filename, m_film_trans_file_size);
            }
            else
            {
                sys_logw(BEL_SERVICE_TAG, "Invalid state or length for FILE_LEN: state=%d, len=%d", m_film_trans_state, cmd->len);
            }
            break;
        }
        case BLE_FILM_TRANS_CH_FILE_DATA :
        {
            if((m_film_trans_state == BLE_FILM_TRANS_RECV_LEN || m_film_trans_state == BLE_FILM_TRANS_RECV_DATA) && cmd->len > 0)
            {
                uint8_t *pdata_copy = (uint8_t*)pvPortMalloc(cmd->len);
                if(pdata_copy)
                {
                    memcpy(pdata_copy, cmd->pdata, cmd->len);
                    service_file_save_data((const char*)m_film_trans_filename, m_film_trans_file_size, pdata_copy, cmd->len);
                    m_film_trans_received += cmd->len;
                    m_film_trans_state = BLE_FILM_TRANS_RECV_DATA;
                }
            }
            else
            {
                sys_logw(BEL_SERVICE_TAG, "Invalid state or length for FILE_DATA: state=%d, len=%d", m_film_trans_state, cmd->len);
            }
            break;
        }
        case BLE_FILM_TRANS_CH_FILE_STOP :
        {
            if(m_film_trans_state == BLE_FILM_TRANS_RECV_DATA)
            {
                service_file_save_stop();
                sys_logi(BEL_SERVICE_TAG, "Film transfer stopped, received: %d/%d bytes", m_film_trans_received, m_film_trans_file_size);
                m_film_trans_state = BLE_FILM_TRANS_STOPPED;
            }
            else
            {
                sys_logw(BEL_SERVICE_TAG, "Invalid state for FILE_STOP: %d", m_film_trans_state);
            }
            break;
        }
        case BLE_FILM_TRANS_CH_FILE_DELETE : // 删除文件
        {
            if(cmd->len == 1)
            {
                uint8_t file_id = cmd->pdata[0];
                sys_logi(BEL_SERVICE_TAG, "Delete file id: %d", file_id);
                service_file_delete(file_id);
            }
            break;
        }
        case BLE_FILM_TRANS_CH_FILE_LIST : // 查询文件列表
        {
            uint32_t file_count = service_file_get_count();
            sys_logi(BEL_SERVICE_TAG, "File list count: %d", file_count);

            uint8_t *cmd_buf = pvPortMalloc(150);
            if(cmd_buf == NULL) break;

            for(uint32_t i = 0; i < file_count; i++)
            {
                memset(cmd_buf, 0, 150);

                const char *filename = service_file_get_filename(i);
                uint8_t name_len = strlen(filename) + 1;

                cmd_buf[0] = BLE_CMD_HEAD;
                cmd_buf[1] = BLE_FILM_TRANS_CH_FILE_LIST;
                cmd_buf[2] = 2 + name_len;
                cmd_buf[3] = i & 0xff;
                cmd_buf[4] = name_len;

                memcpy(&cmd_buf[5], filename, name_len);

                uint8_t checksum = ble_checksum(cmd_buf, 5 + name_len);
                cmd_buf[5 + name_len] = checksum;

                service_ble_send_notify_data(BLE_NOTIFY_SEND_CH1, cmd_buf, 6 + name_len);

                vTaskDelay(50 / portTICK_PERIOD_MS);
            }

            vPortFree(cmd_buf);
            break;
        }
        case BLE_FILM_TRANS_CH_FILE_DISPLAY : // 显示id对应的文件
        {
            if(cmd->len == 1)
            {
                uint8_t file_id = cmd->pdata[0];
                sys_logi(BEL_SERVICE_TAG, "Display file id: %d", file_id);
                service_film_display(file_id);
            }
            break;
        }
        case BLE_FILM_TRANS_CH_FILE_DISPLAY_GET : // 查询当前显示的文件id
        {
            uint32_t current_id = service_film_get_current_id();
            sys_logi(BEL_SERVICE_TAG, "Current display file id: %d", current_id);

            uint8_t cmd_buf[6];
            cmd_buf[0] = BLE_CMD_HEAD;
            cmd_buf[1] = BLE_FILM_TRANS_CH_FILE_DISPLAY_GET;
            cmd_buf[2] = 1;
            cmd_buf[3] = current_id & 0xFF;
            cmd_buf[4] = ble_checksum(cmd_buf, 4);
            service_ble_msg_gatts_data_send(cmd_buf, sizeof(cmd_buf), MSG_BLE_CH1_OUT_DATA);
            break;
        }
        case BLE_FILM_TRANS_CH_OTA_LEN :
        {
            if(m_ota_trans_state == BLE_OTA_TRANS_IDLE && cmd->len == 4)
            {
                m_ota_trans_file_size = (cmd->pdata[0] << 24) | (cmd->pdata[1] << 16) | (cmd->pdata[2] << 8) | cmd->pdata[3];
                m_ota_trans_received = 0;
                m_ota_trans_state = BLE_OTA_TRANS_RECV_LEN;
                sys_logi(BEL_SERVICE_TAG, "OTA file size: %d bytes", m_ota_trans_file_size);
                service_ota_start();
                service_set_length(m_ota_trans_file_size);
            }
            else
            {
                sys_logw(BEL_SERVICE_TAG, "Invalid state or length for OTA_LEN: state=%d, len=%d", m_ota_trans_state, cmd->len);
            }
            break;
        }
        case BLE_FILM_TRANS_CH_OTA_DATA :
        {
            if((m_ota_trans_state == BLE_OTA_TRANS_RECV_LEN || m_ota_trans_state == BLE_OTA_TRANS_RECV_DATA) && cmd->len > 0)
            {
                service_ota_write(cmd->pdata, cmd->len);
                m_ota_trans_received += cmd->len;
                m_ota_trans_state = BLE_OTA_TRANS_RECV_DATA;
            }
            else
            {
                sys_logw(BEL_SERVICE_TAG, "Invalid state or length for OTA_DATA: state=%d, len=%d", m_ota_trans_state, cmd->len);
            }
            break;
        }
        case BLE_FILM_TRANS_CH_OTA_START :
        {
            if(m_ota_trans_state == BLE_OTA_TRANS_IDLE)
            {
                m_ota_trans_state = BLE_OTA_TRANS_STARTED;
                m_ota_trans_received = 0;
                service_ota_start();
                sys_logi(BEL_SERVICE_TAG, "OTA started (without length)");
            }
            else
            {
                sys_logw(BEL_SERVICE_TAG, "Invalid state for OTA_START: %d", m_ota_trans_state);
            }
            break;
        }
        case BLE_FILM_TRANS_CH_OTA_STOP :
        {
            if(m_ota_trans_state == BLE_OTA_TRANS_RECV_DATA || m_ota_trans_state == BLE_OTA_TRANS_RECV_LEN)
            {
                service_ota_stop();
                sys_logi(BEL_SERVICE_TAG, "OTA stopped, received: %d/%d bytes", m_ota_trans_received, m_ota_trans_file_size);
                m_ota_trans_state = BLE_OTA_TRANS_STOPPED;
            }
            else
            {
                sys_logw(BEL_SERVICE_TAG, "Invalid state for OTA_STOP: %d", m_ota_trans_state);
            }
            break;
        }
        case BLE_FILM_TRANS_CH_CTRL_MODE : // Film模式切换
        {
            if(cmd->len == 1 && (cmd->pdata[0] == 0 || cmd->pdata[0] == 1))
            {
                uint8_t mode = cmd->pdata[0];
                sys_logi(BEL_SERVICE_TAG, "Set play mode: %d", mode);
                g_service_param.film.play_mode = mode;
                service_param_save();
            }
            break;
        }
        case BLE_FILM_TRANS_CH_CTRL_MODE_GET : // Film模式查询
        {
            sys_logi(BEL_SERVICE_TAG, "Current play mode: %d", g_service_param.film.play_mode);
            uint8_t cmd_buf[6];
            cmd_buf[0] = BLE_CMD_HEAD;
            cmd_buf[1] = BLE_FILM_TRANS_CH_CTRL_MODE_GET;
            cmd_buf[2] = 1;
            cmd_buf[3] = g_service_param.film.play_mode & 0xFF;
            cmd_buf[4] = ble_checksum(cmd_buf, 4);
            service_ble_msg_gatts_data_send(cmd_buf, sizeof(cmd_buf), MSG_BLE_CH1_OUT_DATA);
            break;
        }
        case BLE_FILM_TRANS_CH_CTRL_RESET : // 重置
        {
            sys_logi(BEL_SERVICE_TAG, "Resetting parameters...");
            service_param_reset();
            vTaskDelay(100 / portTICK_PERIOD_MS);
            sys_reboot();
            break;
        }
        case BLE_FILM_TRANS_CH_CTRL_PWRREAD : // 读取电池电压
        {
            uint8_t *cmd = NULL;
            cmd = pvPortMalloc(BLE_CMD_LEN_MIN + 1);
            if(cmd)
            {
                cmd[0] = BLE_CMD_HEAD;
                cmd[1] = BLE_FILM_TRANS_CH_CTRL_PWRREAD;
                cmd[2] = 1;
                cmd[3] = hal_bat_get_percent();
                cmd[4] = ble_checksum(cmd, BLE_CMD_LEN_MIN);
                service_ble_msg_gatts_data_send(cmd, BLE_CMD_LEN_MIN + 1, MSG_BLE_CH1_OUT_DATA);
                vPortFree(cmd);
                cmd = NULL;
                sys_logi(BEL_SERVICE_TAG, "Battery level: %d%%", hal_bat_get_percent());
            }
            break;
        }
        case BLE_FILM_TRANS_CH_CTRL_REBOOT : // 重启
        {
            sys_logi(BEL_SERVICE_TAG, "Rebooting...");
            sys_reboot();
            break;
        }
        case BLE_FILM_TRANS_CH_CTRL_SLEEPONOFF : // 休眠模式开关
        {
            if(cmd->len == 1 && (cmd->pdata[0] == 0 || cmd->pdata[0] == 1))
            {
                uint8_t mode = cmd->pdata[0];
                sys_logi(BEL_SERVICE_TAG, "Set sleep mode: %s", mode ? "ON" : "OFF");
                g_service_param.sleep.sleep_mode = mode;
                service_param_save();
            }
            break;
        }         
        case BLE_FILM_TRANS_CH_CTRL_SLEEPONOFF_GET : // 休眠模式开关查询
        {
            sys_logi(BEL_SERVICE_TAG, "Sleep mode: %s", g_service_param.sleep.sleep_mode ? "ON" : "OFF");
            uint8_t resp_buf[6];
            resp_buf[0] = BLE_CMD_HEAD;
            resp_buf[1] = BLE_FILM_TRANS_CH_CTRL_SLEEPONOFF_GET;
            resp_buf[2] = 1;
            resp_buf[3] = g_service_param.sleep.sleep_mode & 0xFF;
            resp_buf[4] = ble_checksum(resp_buf, 4);
            service_ble_msg_gatts_data_send(resp_buf, sizeof(resp_buf), MSG_BLE_CH1_OUT_DATA);
            break;
        }
        case BLE_FILM_TRANS_CH_CTRL_SLEEPMODE : // 定时唤醒开关
        {
            if(cmd->len == 1 && (cmd->pdata[0] == 0 || cmd->pdata[0] == 1))
            {
                uint8_t auto_wake = cmd->pdata[0];
                sys_logi(BEL_SERVICE_TAG, "Set auto wake: %s", auto_wake ? "ON" : "OFF");
                g_service_param.sleep.sleep_auto = auto_wake;
                service_param_save();
            }
            break;
        }
        case BLE_FILM_TRANS_CH_CTRL_SLEEPMODE_GET : // 定时唤醒开关查询
        {
            sys_logi(BEL_SERVICE_TAG, "Auto wake: %s", g_service_param.sleep.sleep_auto ? "ON" : "OFF");
            uint8_t resp_buf[6];
            resp_buf[0] = BLE_CMD_HEAD;
            resp_buf[1] = BLE_FILM_TRANS_CH_CTRL_SLEEPMODE_GET;
            resp_buf[2] = 1;
            resp_buf[3] = g_service_param.sleep.sleep_auto & 0xFF;
            resp_buf[4] = ble_checksum(resp_buf, 4);
            service_ble_msg_gatts_data_send(resp_buf, sizeof(resp_buf), MSG_BLE_CH1_OUT_DATA);
            break;
        }
        case BLE_FILM_TRANS_CH_CTRL_SLEEPMODE_TIME : // 定时唤醒时间（单位分钟）
        {
            if(cmd->len == 2)
            {
                uint16_t time_min = (cmd->pdata[0] << 8) | cmd->pdata[1];
                if(time_min >= 10 && time_min <= 2880)
                {
                    sys_logi(BEL_SERVICE_TAG, "Set sleep wake time: %d min", time_min);
                    g_service_param.sleep.sleep_time = time_min;
                    service_param_save();
                }
            }
            break;
        }   
        case BLE_FILM_TRANS_CH_CTRL_SLEEPMODE_TIME_GET : // 定时唤醒时间查询（单位分钟）
        {
            sys_logi(BEL_SERVICE_TAG, "Sleep wake time: %d min", g_service_param.sleep.sleep_time);
            uint8_t resp_buf[7];
            resp_buf[0] = BLE_CMD_HEAD;
            resp_buf[1] = BLE_FILM_TRANS_CH_CTRL_SLEEPMODE_TIME_GET;
            resp_buf[2] = 2;
            resp_buf[3] = (g_service_param.sleep.sleep_time >> 8) & 0xFF;
            resp_buf[4] = g_service_param.sleep.sleep_time & 0xFF;
            resp_buf[5] = ble_checksum(resp_buf, 5);
            service_ble_msg_gatts_data_send(resp_buf, sizeof(resp_buf), MSG_BLE_CH1_OUT_DATA);
            break;
        }
        case BLE_FILM_TRANS_CH_CTRL_SDRESET : // SD卡格式化
        {
            sys_logi(BEL_SERVICE_TAG, "SD card format requested");
            int ret = hal_sd_format();
            uint8_t resp_buf[6];
            resp_buf[0] = BLE_CMD_HEAD;
            resp_buf[1] = BLE_FILM_TRANS_CH_CTRL_SDRESET;
            resp_buf[2] = 1;
            resp_buf[3] = (ret == 0) ? 0 : 1;
            resp_buf[4] = ble_checksum(resp_buf, 4);
            service_ble_msg_gatts_data_send(resp_buf, sizeof(resp_buf), MSG_BLE_CH1_OUT_DATA);
            if(ret == 0)
            {
                sys_logi(BEL_SERVICE_TAG, "SD card formatted, rebooting...");
                vTaskDelay(500 / portTICK_PERIOD_MS);
                sys_reboot();
            }
            break;
        }
        case BLE_FILM_TRANS_CH_CTRL_WIFI_ENABLE : // WiFi开关设置
        {
            if(cmd->len == 1 && (cmd->pdata[0] == 0 || cmd->pdata[0] == 1))
            {
                uint8_t enable = cmd->pdata[0];
                sys_logi(BEL_SERVICE_TAG, "Set WiFi enable: %s", enable ? "ON" : "OFF");
                g_service_param.network.wifi_enable = enable;
                service_param_save();
                if(enable)
                {
                    service_wifi_init();
                }
                else
                {
                    service_wifi_disconnect();
                    service_wifi_deinit();
                }
            }
            break;
        }
        case BLE_FILM_TRANS_CH_CTRL_WIFI_ENABLE_GET : // WiFi开关查询
        {
            sys_logi(BEL_SERVICE_TAG, "WiFi enable: %s", g_service_param.network.wifi_enable ? "ON" : "OFF");
            uint8_t resp_buf[6];
            resp_buf[0] = BLE_CMD_HEAD;
            resp_buf[1] = BLE_FILM_TRANS_CH_CTRL_WIFI_ENABLE_GET;
            resp_buf[2] = 1;
            resp_buf[3] = g_service_param.network.wifi_enable & 0xFF;
            resp_buf[4] = ble_checksum(resp_buf, 4);
            service_ble_msg_gatts_data_send(resp_buf, sizeof(resp_buf), MSG_BLE_CH1_OUT_DATA);
            break;
        }
        case BLE_FILM_TRANS_CH_CTRL_WIFI_SSID : // WiFi SSID设置
        {
            if(cmd->len > 0 && cmd->len < sizeof(g_service_param.network.wifi_ssid))
            {
                memset(g_service_param.network.wifi_ssid, 0, sizeof(g_service_param.network.wifi_ssid));
                memcpy(g_service_param.network.wifi_ssid, cmd->pdata, cmd->len);
                sys_logi(BEL_SERVICE_TAG, "Set WiFi SSID: %s", g_service_param.network.wifi_ssid);
                service_param_save();
            }
            break;
        }
        case BLE_FILM_TRANS_CH_CTRL_WIFI_SSID_GET : // WiFi SSID查询
        {
            uint8_t ssid_len = strlen(g_service_param.network.wifi_ssid);
            sys_logi(BEL_SERVICE_TAG, "WiFi SSID: %s", g_service_param.network.wifi_ssid);
            uint8_t resp_ssid_len = 4 + ssid_len; // HEAD + CH + LEN + DATA
            uint8_t *resp_buf = pvPortMalloc(resp_ssid_len);
            if(resp_buf)
            {
                resp_buf[0] = BLE_CMD_HEAD;
                resp_buf[1] = BLE_FILM_TRANS_CH_CTRL_WIFI_SSID_GET;
                resp_buf[2] = ssid_len;
                if(ssid_len > 0)
                {
                    memcpy(&resp_buf[3], g_service_param.network.wifi_ssid, ssid_len);
                }
                resp_buf[3 + ssid_len] = ble_checksum(resp_buf, 3 + ssid_len);
                service_ble_msg_gatts_data_send(resp_buf, resp_ssid_len, MSG_BLE_CH1_OUT_DATA);
                vPortFree(resp_buf);
            }
            break;
        }
        case BLE_FILM_TRANS_CH_CTRL_WIFI_PASSWORD : // WiFi 密码设置
        {
            if(cmd->len > 0 && cmd->len < sizeof(g_service_param.network.wifi_password))
            {
                memset(g_service_param.network.wifi_password, 0, sizeof(g_service_param.network.wifi_password));
                memcpy(g_service_param.network.wifi_password, cmd->pdata, cmd->len);
                sys_logi(BEL_SERVICE_TAG, "Set WiFi password");
                service_param_save();
            }
            break;
        }
        case BLE_FILM_TRANS_CH_CTRL_WIFI_PASSWORD_GET : // WiFi 密码查询
        {
            uint8_t pwd_len = strlen(g_service_param.network.wifi_password);
            sys_logi(BEL_SERVICE_TAG, "WiFi password query");
            uint8_t resp_pwd_len = 4 + pwd_len;
            uint8_t *resp_buf = pvPortMalloc(resp_pwd_len);
            if(resp_buf)
            {
                resp_buf[0] = BLE_CMD_HEAD;
                resp_buf[1] = BLE_FILM_TRANS_CH_CTRL_WIFI_PASSWORD_GET;
                resp_buf[2] = pwd_len;
                if(pwd_len > 0)
                {
                    memcpy(&resp_buf[3], g_service_param.network.wifi_password, pwd_len);
                }
                resp_buf[3 + pwd_len] = ble_checksum(resp_buf, 3 + pwd_len);
                service_ble_msg_gatts_data_send(resp_buf, resp_pwd_len, MSG_BLE_CH1_OUT_DATA);
                vPortFree(resp_buf);
            }
            break;
        }
        case BLE_FILM_TRANS_CH_CTRL_FILM_API_URL : // HTTP下载film文件的API地址设置
        {
            if(cmd->len > 0 && cmd->len < sizeof(g_service_param.network.film_api_url))
            {
                memset(g_service_param.network.film_api_url, 0, sizeof(g_service_param.network.film_api_url));
                memcpy(g_service_param.network.film_api_url, cmd->pdata, cmd->len);
                sys_logi(BEL_SERVICE_TAG, "Set film API URL: %s", g_service_param.network.film_api_url);
                service_param_save();
            }
            break;
        }
        case BLE_FILM_TRANS_CH_CTRL_FILM_API_URL_GET : // HTTP下载film文件的API地址查询
        {
            uint8_t url_len = strlen(g_service_param.network.film_api_url);
            sys_logi(BEL_SERVICE_TAG, "Film API URL: %s", g_service_param.network.film_api_url);
            uint8_t resp_url_len = 4 + url_len;
            uint8_t *resp_buf = pvPortMalloc(resp_url_len);
            if(resp_buf)
            {
                resp_buf[0] = BLE_CMD_HEAD;
                resp_buf[1] = BLE_FILM_TRANS_CH_CTRL_FILM_API_URL_GET;
                resp_buf[2] = url_len;
                if(url_len > 0)
                {
                    memcpy(&resp_buf[3], g_service_param.network.film_api_url, url_len);
                }
                resp_buf[3 + url_len] = ble_checksum(resp_buf, 3 + url_len);
                service_ble_msg_gatts_data_send(resp_buf, resp_url_len, MSG_BLE_CH1_OUT_DATA);
                vPortFree(resp_buf);
            }
            break;
        }
        case BLE_FILM_TRANS_CH_CTRL_WIFI_CONNECT : // 连接WiFi
        {
            sys_logi(BEL_SERVICE_TAG, "WiFi connect requested");
            service_wifi_connect();
            break;
        }
        case BLE_FILM_TRANS_CH_CTRL_WIFI_DISCONNECT : // 断开WiFi连接
        {
            sys_logi(BEL_SERVICE_TAG, "WiFi disconnect requested");
            service_wifi_disconnect();
            break;
        }
        case BLE_FILM_TRANS_CH_CTRL_WIFI_CONNECT_GET : // 查询WiFi连接状态
        {
            uint8_t status = service_wifi_get_connect_status();
            sys_logi(BEL_SERVICE_TAG, "WiFi connect status: %s", status ? "Connected" : "Disconnected");
            uint8_t resp_buf[6];
            resp_buf[0] = BLE_CMD_HEAD;
            resp_buf[1] = BLE_FILM_TRANS_CH_CTRL_WIFI_CONNECT_GET;
            resp_buf[2] = 1;
            resp_buf[3] = status;
            resp_buf[4] = ble_checksum(resp_buf, 4);
            service_ble_msg_gatts_data_send(resp_buf, sizeof(resp_buf), MSG_BLE_CH1_OUT_DATA);
            break;
        }
        case BLE_FILM_TRANS_CH_CTRL_WIFI_CLEAR : // 清除网络配置信息
        {
            sys_logi(BEL_SERVICE_TAG, "WiFi config clear requested");
            service_wifi_clear_config();
            break;
        }
        case BLE_FILM_TRANS_CH_CTRL_FILM_DOWNLOAD : // 开始下载film文件
        {
            sys_logi(BEL_SERVICE_TAG, "Film download requested");
            service_wifi_download_start();
            break;
        }
        case BLE_FILM_TRANS_CH_CTRL_FILM_DOWNLOAD_STATE : // 查询下载状态
        {
            wifi_download_state_t state = service_wifi_download_get_state();
            uint8_t progress = service_wifi_download_get_progress();
            sys_logi(BEL_SERVICE_TAG, "Download state: %d, progress: %d%%", state, progress);
            uint8_t resp_buf[7];
            resp_buf[0] = BLE_CMD_HEAD;
            resp_buf[1] = BLE_FILM_TRANS_CH_CTRL_FILM_DOWNLOAD_STATE;
            resp_buf[2] = 2;
            resp_buf[3] = (uint8_t)state;
            resp_buf[4] = progress;
            resp_buf[5] = ble_checksum(resp_buf, 5);
            service_ble_msg_gatts_data_send(resp_buf, sizeof(resp_buf), MSG_BLE_CH1_OUT_DATA);
            break;
        }
        default :
        {
            break;
        }
    }
}

/**
 * [ble_checksum 和校验]
 * @param  arr [校验函数]
 * @param  len [校验长度]
 * @return     [校验值]
 */
static uint8_t ble_checksum(uint8_t arr[], int len)
{
    uint8_t sum = 0;
    for (int i = 0; i < len; i++)
    {
        sum += arr[i];
    }
    return sum;
}
