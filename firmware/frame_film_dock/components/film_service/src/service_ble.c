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
 * Description: ble gatt服务（仅做链路收发，命令解析与业务由 service_cmd 统一处理）
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

#include "sys_log.h"

#include "service_ble_gatts.h"
#include "service_ble.h"
#include "service_cmd.h"

/*********************************************************************
 * MACROS
 */
#define BEL_SERVICE_TAG            "ble_service"

#define BLE_MSG_QUEUE_LENGTH       100
#define BLE_MSG_QUEUE_ITEM_SIZE    sizeof( ble_msg_t )

/*********************************************************************
 * TYPEDEFS
 */

/*********************************************************************
 * CONSTANTS
 */

/*********************************************************************
 * LOCAL VARIABLES
 */
static TaskHandle_t m_ble_task_hdl = NULL;
static QueueHandle_t m_ble_msg_hdl = NULL;

/*********************************************************************
 * GLOBAL VARIABLES
 */

/*********************************************************************
 * LOCAL FUNCTIONS
 */
static void ble_task_handle(void *pvParameters);
static void ble_cmd_out(service_cmd_src_t src, const uint8_t *p_data, uint16_t len);

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
    // 注册BLE链路输出，命令处理的应答经此回到手机/上位机
    service_cmd_src_register(SERVICE_CMD_SRC_BLE, ble_cmd_out);

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
 * [service_ble_msg_gatts_cmd_send GATT收到命令回调，转交命令服务]
 * @param p_data [命令buf]
 * @param len    [长度]
 */
void service_ble_msg_gatts_cmd_send( uint8_t const *p_data, uint16_t len )
{
    // 命令解析/业务处理与链路无关，统一由 service_cmd 处理
    service_cmd_input(SERVICE_CMD_SRC_BLE, p_data, len);
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

/**
 * [ble_cmd_out 命令服务输出回调：把应答帧交给BLE任务通知]
 */
static void ble_cmd_out(service_cmd_src_t src, const uint8_t *p_data, uint16_t len)
{
    (void)src;
    service_ble_msg_gatts_data_send(p_data, len, MSG_BLE_CH1_OUT_DATA);
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
