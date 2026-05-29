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
#include "hal_led.h"
#include "hal_bat.h"
#include "hal_encoder.h"
#include "hal_epd.h"
#include "hal_pwr.h"
#include "service_ble_gatts.h"
#include "service_monitor.h"
#include "service_param.h"

/*********************************************************************
 * MACROS
 */
#define MONITOR_TAG                    "monitor"

#define MONITOR_MSG_QUEUE_LENGTH       30
#define MONITOR_MSG_QUEUE_ITEM_SIZE    sizeof( monitor_msg_t )

#define SYS_OS_PRI_MONITOR_TASK        (7)
#define SYS_OS_SIZE_MONITOR_TASK       (4096)
#define SYS_OS_NAME_MONITOR_TASK       "monitor_task"

#define MONITOR_TIMER_BASE_INTERVAL_MS (100)

#define MONITOR_LED_TICK_COUNT         (MONITOR_LED_UPDATE_INTERVAL_MS / MONITOR_TIMER_BASE_INTERVAL_MS)
#define MONITOR_BAT_TICK_COUNT         (MONITOR_BAT_CHECK_INTERVAL_MS / MONITOR_TIMER_BASE_INTERVAL_MS)
#define MONITOR_SLEEP_TICK_COUNT       (MONITOR_SLEEP_CHECK_INTERVAL_MS / MONITOR_TIMER_BASE_INTERVAL_MS)
#define MONITOR_AUTO_SLEEP_TICK_COUNT  (MONITOR_AUTO_SLEEP_TIMEOUT_SEC * 1000 / MONITOR_SLEEP_CHECK_INTERVAL_MS)

/*********************************************************************
* TYPEDEFS
*/
typedef struct
{
    uint8_t ble_connected;
    uint8_t bat_level;
    uint8_t led_state;
    uint32_t sleep_counter;
    uint8_t last_encoder_state;
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
static void monitor_battery_manage_event(void);
static void monitor_auto_sleep_manage_event(void);
static void monitor_enter_low_power(void);

/*********************************************************************
 * GLOBAL FUNCTIONS
 */


void service_monitor_init(void)
{
    memset(&m_monitor_state, 0, sizeof(monitor_state_t));
    m_monitor_state.bat_level = 100;
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

    if((m_monitor_state.tick_counter % MONITOR_BAT_TICK_COUNT) == 0)
    {
        msg.ID = MSG_BATTERY_MANAGER;
        monitor_msg_send(&msg, 0);
    }

    if((m_monitor_state.tick_counter % MONITOR_SLEEP_TICK_COUNT) == 0)
    {
        msg.ID = MSG_AUTO_SLEEP_MANAGER;
        monitor_msg_send(&msg, 0);
    }
}

static void monitor_led_manage_event(void)
{
    m_monitor_state.ble_connected = service_ble_gatts_get_connect();

    if(m_monitor_state.bat_level < MONITOR_BAT_LOW_THRESHOLD)
    {
        hal_led_set_color(LED_COLOR_RED);
    }
    else if(m_monitor_state.ble_connected)
    {
        hal_led_set_color(LED_COLOR_GREEN);
    }
    else
    {
        hal_led_set_color(LED_COLOR_BLUE);
    }
}

static void monitor_battery_manage_event(void)
{
    m_monitor_state.bat_level = (uint8_t)hal_bat_get_level();
    sys_logi(MONITOR_TAG, "battery level: %d%%", m_monitor_state.bat_level);

    if(m_monitor_state.bat_level < MONITOR_BAT_CRITICAL_THRESHOLD)
    {
        sys_logi(MONITOR_TAG, "battery critical low, entering low power mode");
        monitor_enter_low_power();
    }
}

static void monitor_auto_sleep_manage_event(void)
{
    if(!g_service_param.sleep.sleep_mode)
    {
        m_monitor_state.sleep_counter = 0;
        return;
    }

    encoder_press_type_t encoder_state = hal_encoder_get_press();
    m_monitor_state.ble_connected = service_ble_gatts_get_connect();

    if(encoder_state != ENCODER_PRESS_NONE)
    {
        m_monitor_state.sleep_counter = 0;
    }
    else
    {
        m_monitor_state.sleep_counter++;
    }

    if(!m_monitor_state.ble_connected) // ble disconnected
    {
        if(m_monitor_state.sleep_counter >= MONITOR_AUTO_SLEEP_TICK_COUNT)
        {
            sys_logi(MONITOR_TAG, "auto sleep timeout, entering low power mode");
            if(g_service_param.sleep.sleep_auto && g_service_param.sleep.sleep_time > 0)
            {
                hal_pwr_set_timer_wakeup(g_service_param.sleep.sleep_time);
                sys_logi(MONITOR_TAG, "timer wake enabled: %d min", g_service_param.sleep.sleep_time);
            }
            monitor_enter_low_power();
        }
    }
    else // ble connected
    {
        m_monitor_state.sleep_counter = 0;
    }
}

static void monitor_enter_low_power(void)
{
    xTimerStop(m_monitor_timer, 0);

    service_ble_gatt_server_uninit();
    hal_led_deinit();
    hal_bat_deinit();
    hal_sd_deinit();
    hal_epd_deinit();

    hal_pwr_enter_sleep();
}
