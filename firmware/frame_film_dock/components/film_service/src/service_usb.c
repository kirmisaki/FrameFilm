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
 * FileName : /film_service/src/service_usb.c
 * Author: Kiritro  Version: v0.1  Date: 2026/9/11
 * Description: USB(CDC)服务：负责CDC数据的接收与发送，命令解析交由 service_cmd 统一处理
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

#include "hal_api.h"

#include "service_usb.h"
#include "service_cmd.h"

#if SYS_FUNC_USB_CDC_EN

/*********************************************************************
 * MACROS
 */
#define USB_SERVICE_TAG            "usb_service"

#define SERVICE_USB_QUEUE_LENGTH   (40)
#define SERVICE_USB_QUEUE_ITEM_SIZE  sizeof( usb_msg_t )

/*********************************************************************
* TYPEDEFS
*/
typedef struct
{
    uint16_t len;
    uint8_t *pdata;
} usb_msg_t;

/*********************************************************************
 * CONSTANTS
 */

/*********************************************************************
 * LOCAL VARIABLES
 */
static TaskHandle_t m_usb_task_hdl = NULL;
static QueueHandle_t m_usb_msg_hdl = NULL;

/*********************************************************************
 * GLOBAL VARIABLES
 */

/*********************************************************************
 * LOCAL FUNCTIONS
 */
static void usb_task_handle(void *pvParameters);
static void usb_rx_cb(const uint8_t *p_data, uint32_t len);
static void usb_cmd_out(service_cmd_src_t src, const uint8_t *p_data, uint16_t len);

/*********************************************************************
 * GLOBAL FUNCTIONS
 */

/**
 * [service_usb_init 初始化USB(CDC)服务]
 */
void service_usb_init(void)
{
    if(m_usb_task_hdl == NULL)
    {
        if(pdPASS != xTaskCreate(usb_task_handle, SYS_OS_NAME_USB_TASK, SYS_OS_SIZE_USB_TASK, NULL, SYS_OS_PRI_USB_TASK, &m_usb_task_hdl))
        {
            sys_loge(USB_SERVICE_TAG, "usb task create error!");
        }
    }

    // 注册USB链路输出，命令处理的应答经此由CDC发回上位机
    service_cmd_src_register(SERVICE_CMD_SRC_USB, usb_cmd_out);
    // 注册CDC接收回调
    hal_usb_cdc_register_rx_cb(usb_rx_cb);
}

/**
 * [usb_rx_cb CDC接收回调]
 *
 * 运行在TinyUSB任务上下文，仅做拷贝入队，不做耗时操作。
 */
static void usb_rx_cb(const uint8_t *p_data, uint32_t len)
{
    if(m_usb_msg_hdl == NULL || p_data == NULL || len == 0)
    {
        return;
    }

    usb_msg_t msg = {0};
    msg.len = (uint16_t)len;
    msg.pdata = pvPortMalloc(msg.len);
    if(msg.pdata == NULL)
    {
        sys_loge(USB_SERVICE_TAG, "usb msg malloc error!");
        return;
    }
    memcpy(msg.pdata, p_data, msg.len);

    // 不阻塞USB任务，队列满时丢弃该片数据
    if(xQueueSend(m_usb_msg_hdl, &msg, 0) != pdPASS)
    {
        sys_logw(USB_SERVICE_TAG, "usb msg queue full, drop %d bytes", msg.len);
        vPortFree(msg.pdata);
    }
}

static void usb_task_handle(void *pvParameters)
{
    m_usb_msg_hdl = xQueueCreate( SERVICE_USB_QUEUE_LENGTH, SERVICE_USB_QUEUE_ITEM_SIZE );
    if(m_usb_msg_hdl == NULL)
    {
        sys_loge(USB_SERVICE_TAG, "usb msg queue create error!");
        vTaskDelete(NULL);
        return;
    }

    for(;;)
    {
        usb_msg_t msg;
        if(xQueueReceive( m_usb_msg_hdl, (void *const)&msg, portMAX_DELAY ) == pdPASS)
        {
            service_cmd_input(SERVICE_CMD_SRC_USB, msg.pdata, msg.len);

            if(msg.pdata)
            {
                vPortFree(msg.pdata);
            }
        }
    }
}

/**
 * [usb_cmd_out 命令服务输出回调：把应答帧经CDC发出]
 */
static void usb_cmd_out(service_cmd_src_t src, const uint8_t *p_data, uint16_t len)
{
    (void)src;

    if(hal_usb_cdc_write(p_data, len) != ESP_OK)
    {
        sys_logw(USB_SERVICE_TAG, "cdc write failed");
    }
}

#endif /* SYS_FUNC_USB_CDC_EN */
