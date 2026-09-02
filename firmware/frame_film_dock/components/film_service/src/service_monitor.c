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
 * FileName : /film_service/src/service_monitor.c
 * Author: Kiritro  Version: v0.1  Date: 2026/4/16
 * Description: 监控服务
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

#include "sys_log.h"
#include "hal_api.h"
#include "service_ble_gatts.h"
#include "service_monitor.h"

/*********************************************************************
 * MACROS
 */
#define MONITOR_TAG                              "MONITOR"

#define MONITOR_MSG_QUEUE_LENGTH                 30
#define MONITOR_MSG_QUEUE_ITEM_SIZE              sizeof( monitor_msg_t )

#define SYS_OS_PRI_MONITOR_TASK                  (7)
#define SYS_OS_SIZE_MONITOR_TASK                 (4096)
#define SYS_OS_NAME_MONITOR_TASK                 "monitor_task"

// LED管理参数
#define MONITOR_LED_UPDATE_INTERVAL_MS           (500)    // LED状态更新间隔 500ms
#define MONITOR_LED_BLINK_ON_TICKS               (1)      // LED闪烁点亮tick数 (1 * 500ms = 500ms亮)
#define MONITOR_LED_BLINK_OFF_TICKS              (0)      // LED闪烁熄灭tick数 (3 * 500ms = 1500ms灭)
#define MONITOR_LED_BRIGHTNESS                   (30)     // LED亮度值

#define MONITOR_TIMER_BASE_INTERVAL_MS           (100)
#define MONITOR_LED_TICK_COUNT                   (MONITOR_LED_UPDATE_INTERVAL_MS / MONITOR_TIMER_BASE_INTERVAL_MS)

/*********************************************************************
* TYPEDEFS
*/
typedef struct
{
    uint8_t ble_connected;
    uint8_t led_state;
    uint32_t tick_counter;
} monitor_state_t;

/*********************************************************************
 * CONSTANTS
 */


/*********************************************************************
 * LOCAL VARIABLES
 */
static TaskHandle_t m_monitor_task_hdl = NULL;
static QueueHandle_t m_monitor_msg_hdl = NULL;
static TimerHandle_t m_monitor_timer = NULL;
static monitor_state_t m_monitor_state;

/*********************************************************************
 * GLOBAL VARIABLES
 */


/*********************************************************************
 * LOCAL FUNCTIONS
 */
static void monitor_task_handle(void *pvParameters);
static void monitor_msg_send(void *p_msg, bool in_isr);
static void monitor_timer_callback(TimerHandle_t xTimer);

static void monitor_led_manage_event(void);

/*********************************************************************
 * GLOBAL FUNCTIONS
 */


void service_monitor_init(void)
{
    memset(&m_monitor_state, 0, sizeof(monitor_state_t));
    m_monitor_state.led_state = 0;
    m_monitor_state.tick_counter = 0;

    if(m_monitor_task_hdl == NULL)
    {
        if ( pdPASS != xTaskCreate( monitor_task_handle, SYS_OS_NAME_MONITOR_TASK, SYS_OS_SIZE_MONITOR_TASK, NULL, SYS_OS_PRI_MONITOR_TASK, NULL ))
        {
            sys_loge(MONITOR_TAG, "monitor task create error!");
        }
    }

    if(m_monitor_timer == NULL)
    {
        m_monitor_timer = xTimerCreate( "monitor_timer", pdMS_TO_TICKS(MONITOR_TIMER_BASE_INTERVAL_MS), pdTRUE, NULL, monitor_timer_callback );
        SYS_ERROR_CHECK(m_monitor_timer == NULL);
    }
    xTimerStart(m_monitor_timer, 0);
}

static void monitor_task_handle(void *pvParameters)
{
    m_monitor_msg_hdl = xQueueCreate( MONITOR_MSG_QUEUE_LENGTH, MONITOR_MSG_QUEUE_ITEM_SIZE );
    SYS_ERROR_CHECK(m_monitor_msg_hdl == NULL);

    for(;;)
    {
        monitor_msg_t msg;
        SYS_ERROR_CHECK( xQueueReceive( m_monitor_msg_hdl, (void *const)&msg, portMAX_DELAY ) != pdPASS );

        switch(msg.ID)
        {
        case MSG_LED_MANAGER :
            monitor_led_manage_event();
            break;
        default :
            break;
        }
    }
}

static void monitor_msg_send(void *p_msg, bool in_isr)
{
    if(m_monitor_msg_hdl != NULL)
    {
        if(in_isr == 0)
        {
            SYS_ERROR_CHECK((xQueueSend(m_monitor_msg_hdl, p_msg, portMAX_DELAY) != pdPASS));
        }
        else
        {
            BaseType_t xHigherPriorityTaskWoken;
            xHigherPriorityTaskWoken = pdFALSE;
            xQueueSendFromISR( m_monitor_msg_hdl, p_msg, &xHigherPriorityTaskWoken );
            portYIELD_FROM_ISR( xHigherPriorityTaskWoken );
        }
    }
}

static void monitor_timer_callback(TimerHandle_t xTimer)
{
    monitor_msg_t msg;

    m_monitor_state.tick_counter++;

    if((m_monitor_state.tick_counter % MONITOR_LED_TICK_COUNT) == 0)
    {
        msg.ID = MSG_LED_MANAGER;
        monitor_msg_send(&msg, 0);
    }
}

static void monitor_led_manage_event(void)
{
    uint32_t led_color;
    uint32_t blink_cycle = MONITOR_LED_BLINK_ON_TICKS + MONITOR_LED_BLINK_OFF_TICKS;

    m_monitor_state.ble_connected = service_ble_gatts_get_connect();

    if(m_monitor_state.ble_connected)
    {
        led_color = LED_COLOR_GREEN;
    }
    else
    {
        led_color = LED_COLOR_WHITE;
    }

    // 闪烁控制: led_state作为周期计数器
    m_monitor_state.led_state = (m_monitor_state.led_state + 1) % blink_cycle;
    if(m_monitor_state.led_state < MONITOR_LED_BLINK_ON_TICKS)
    {
        hal_led_set_color(led_color);
        hal_led_set_brightness(MONITOR_LED_BRIGHTNESS);
    }
    else
    {
        hal_led_set_color(LED_COLOR_BLACK);
    }
}
