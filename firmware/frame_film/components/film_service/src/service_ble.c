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
 * FileName : /film_service/src/service_ble.c
 * Author: Kiritro  Version: v0.1  Date: 2026/4/5
 * Description: ble gatt服务
 * ChangeLog: Change Notes
 *
***********************************************************/

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

#include "hal_ble.h"
#include "service_ble.h"


/*********************************************************************
 * MACROS
 */


/*********************************************************************
 * TYPEDEFS
 */
#define BEL_SERVICE_TAG            "ble_service"

#define BLE_MSG_QUEUE_LENGTH       50
#define BLE_MSG_QUEUE_ITEM_SIZE    sizeof( ble_msg_t )


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


/*********************************************************************
 * GLOBAL FUNCTIONS
 */



/**
 * [service_ble_init 初始化ble服务]
 */
void service_ble_init(void)
{
    // 初始化蓝牙GATT服务
    hal_ble_gatt_server_init();

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
            // if( msg.len )
            // {
            //     if(msg.pdata[0] ==  CMD_HEAD)
            //     {
            //         uint8_t sum = sys_checksum(msg.pdata, msg.len - 1);
            //         // sys_logi("sum:0x%02x", sum);

            //         if(sum == msg.pdata[msg.len - 1])
            //         {
            //             sys_db_msg_t msg_db = {0};

            //             msg_db.ID = MSG_SYS_DB_DATA_PROCESS;
            //             msg_db.subID = MSG_SYS_DATA_BLE;
            //             msg_db.packageID = MSG_SYS_PACKAGE_CMD;
            //             msg_db.isresp = (msg.pdata[1] & 0x01);
            //             msg_db.ch = msg.pdata[2];
            //             msg_db.plen = msg.len - 4;
            //             if(msg_db.plen)
            //             {
            //                 msg_db.pdata = pvPortMalloc(msg_db.plen);
            //                 if(msg_db.pdata)
            //                 {
            //                     memcpy(msg_db.pdata, msg.pdata + 3, msg_db.plen);
            //                     sys_task_msg_send(&msg_db, 1);
            //                 }
            //             }
            //         }
            //     }
            //     vPortFree(msg.pdata);
            // }
            break;
        }
        case MSG_BLE_CH1_OUT_DATA :
        {
            if( msg.len )
            {
                hal_ble_send_notify_data(BLE_NOTIFY_SEND_CH1, msg.pdata, msg.len);
                vPortFree(msg.pdata);
            }
            break;
        }
        case MSG_BLE_CH2_OUT_DATA :
        {
            if( msg.len )
            {
                hal_ble_send_notify_data(BLE_NOTIFY_SEND_CH2, msg.pdata, msg.len);
                vPortFree(msg.pdata);
            }
            break;
        }
        case MSG_BLE_CH3_OUT_DATA :
        {
            if( msg.len )
            {
                hal_ble_send_notify_data(BLE_NOTIFY_SEND_CH3, msg.pdata, msg.len);
                vPortFree(msg.pdata);
            }
            break;
        }
        case MSG_BLE_GAP_DISCONNECT:
            hal_ble_gatts_dev_disconnect();
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
