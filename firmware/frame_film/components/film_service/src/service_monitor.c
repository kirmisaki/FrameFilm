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
 * FileName : /film_service/src/service_monitor.c
 * Author: Kiritro  Version: v0.1  Date: 2026/4/16
 * Description: 监控服务
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

#include "sys_log.h"

#include "service_monitor.h"

/*********************************************************************
 * MACROS
 */
#define MONITOR_TAG                    "monitor"

#define MONITOR_MSG_QUEUE_LENGTH       30
#define MONITOR_MSG_QUEUE_ITEM_SIZE    sizeof( monitor_msg_t )

#define SYS_OS_PRI_MONITOR_TASK        (10)
#define SYS_OS_SIZE_MONITOR_TASK       (4096)
#define SYS_OS_NAME_MONITOR_TASK       "monitor_task"

#define MONITOR_TIMER_TICKS            pdMS_TO_TICKS(100)   //100ms

/*********************************************************************
* TYPEDEFS
*/


/*********************************************************************
 * CONSTANTS
 */


/*********************************************************************
 * LOCAL VARIABLES
 */
static TaskHandle_t m_monitor_task_hdl = NULL;
static QueueHandle_t m_monitor_msg_hdl = NULL;
static TimerHandle_t m_monitor_timer = NULL;

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
static void monitor_battery_manage_event(void);
static void monitor_auto_sleep_manage_event(void);

/*********************************************************************
 * GLOBAL FUNCTIONS
 */


void service_monitor_init(void)
{
    if(m_monitor_task_hdl == NULL)
    {
        if ( pdPASS != xTaskCreate( monitor_task_handle, SYS_OS_NAME_MONITOR_TASK, SYS_OS_SIZE_MONITOR_TASK, NULL, SYS_OS_PRI_MONITOR_TASK, NULL ))
        {
            sys_loge(MONITOR_TAG, "monitor task create error!");
        }
    }

    if(m_monitor_timer == NULL)
    {
        m_monitor_timer = xTimerCreate( "monitor_timer", MONITOR_TIMER_TICKS, pdTRUE, NULL, monitor_timer_callback );
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
        case MSG_BATTERY_MANAGER :
            monitor_battery_manage_event();
            break;
        case MSG_AUTO_SLEEP_MANAGER :
            monitor_auto_sleep_manage_event();
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
        else /* Is In interrupt.*/
        {
            BaseType_t xHigherPriorityTaskWoken;
            xHigherPriorityTaskWoken = pdFALSE;
            xQueueSendFromISR( m_monitor_msg_hdl, p_msg, &xHigherPriorityTaskWoken );
            portYIELD_FROM_ISR( xHigherPriorityTaskWoken );
        }
    }
}

static void monitor_led_manage_event(void)
{
    sys_logi(MONITOR_TAG, "led manage event");
}

static void monitor_battery_manage_event(void)
{
    sys_logi(MONITOR_TAG, "battery manage event");
}

static void monitor_auto_sleep_manage_event(void)
{
    sys_logi(MONITOR_TAG, "auto sleep manage event");
}

static void monitor_timer_callback(TimerHandle_t xTimer)
{
    
}
