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

#include "hal_bat.h"

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

            uint8_t *cmd_buf = pvPortMalloc(70);
            if(cmd_buf == NULL) break;

            for(uint32_t i = 0; i < file_count; i++)
            {
                memset(cmd_buf, 0, 70);

                const char *filename = service_file_get_filename(i);
                uint8_t name_len = strlen(filename) + 1;

                cmd_buf[0] = BLE_CMD_HEAD;
                cmd_buf[1] = BLE_FILM_TRANS_CH_FILE_LIST;
                cmd_buf[2] = 2 + name_len;
                cmd_buf[3] = i;
                cmd_buf[4] = name_len;

                memcpy(&cmd_buf[5], filename, name_len);

                uint8_t checksum = ble_checksum(cmd_buf, 5 + name_len);
                cmd_buf[5 + name_len] = checksum;

                service_ble_msg_gatts_data_send(cmd_buf, 6 + name_len, MSG_BLE_CH1_OUT_DATA);

                vTaskDelay(10 / portTICK_PERIOD_MS);
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
            break;
        }         
        case BLE_FILM_TRANS_CH_CTRL_SLEEPONOFF_GET : // 休眠模式开关查询
        {
            break;
        }
        case BLE_FILM_TRANS_CH_CTRL_SLEEPMODE : // 休眠模式 定时唤醒开关
        {
            break;
        }
        case BLE_FILM_TRANS_CH_CTRL_SLEEPMODE_GET : // 休眠模式 定时唤醒开关查询
        {
            break;
        }
        case BLE_FILM_TRANS_CH_CTRL_SLEEPMODE_TIME : // 定时唤醒开关时间（单位分钟）
        {
            break;
        }   
        case BLE_FILM_TRANS_CH_CTRL_SLEEPMODE_TIME_GET : // 定时唤醒开关时间查询（单位分钟）
        {
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
