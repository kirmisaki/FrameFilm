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

#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "sys_log.h"
#include "hal_api.h"
#include "service_ble_gatts.h"
#include "service_monitor.h"
#include "service_param.h"

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
#define MONITOR_LED_BLINK_OFF_TICKS              (3)      // LED闪烁熄灭tick数 (3 * 500ms = 1500ms灭)
// 电池管理参数     
#define MONITOR_BAT_CHECK_INTERVAL_MS            (30000)  // 电池检测间隔 30s
#define MONITOR_BAT_LOW_THRESHOLD                (10)     // 低电量阈值 10%
#define MONITOR_BAT_CRITICAL_THRESHOLD           (5)      // 极低电量阈值 5%
// 自动休眠管理参数     
#define MONITOR_SLEEP_CHECK_INTERVAL_MS          (200)    // 休眠检测间隔 200ms
#define MONITOR_AUTO_SLEEP_TIMEOUT_SEC           (60)     // 自动休眠超时时间(手动唤醒) 1min (60s)
#if FRAMEFILM_STD == 1
#define MONITOR_AUTO_SLEEP_TIMEOUT_SEC_LOW       (20)     // 自动休眠超时时间(自动唤醒) 20s (20s)
#endif
#if FRAMEFILM_PRO == 1
#define MONITOR_AUTO_SLEEP_TIMEOUT_SEC_LOW       (30)     // 自动休眠超时时间(自动唤醒) 30s (30s)
#endif
#if FRAMEFILM_MAX == 1
#define MONITOR_AUTO_SLEEP_TIMEOUT_SEC_LOW       (50)     // 自动休眠超时时间(自动唤醒) 50s (50s)
#endif

#define MONITOR_TIMER_BASE_INTERVAL_MS           (100)
#define MONITOR_LED_TICK_COUNT                   (MONITOR_LED_UPDATE_INTERVAL_MS / MONITOR_TIMER_BASE_INTERVAL_MS)
#define MONITOR_BAT_TICK_COUNT                   (MONITOR_BAT_CHECK_INTERVAL_MS / MONITOR_TIMER_BASE_INTERVAL_MS)
#define MONITOR_SLEEP_TICK_COUNT                 (MONITOR_SLEEP_CHECK_INTERVAL_MS / MONITOR_TIMER_BASE_INTERVAL_MS)
#define MONITOR_AUTO_SLEEP_TICK_COUNT            (MONITOR_AUTO_SLEEP_TIMEOUT_SEC * 1000 / MONITOR_SLEEP_CHECK_INTERVAL_MS)
#define MONITOR_AUTO_SLEEP_TICK_COUNT_LOW        (MONITOR_AUTO_SLEEP_TIMEOUT_SEC_LOW * 1000 / MONITOR_SLEEP_CHECK_INTERVAL_MS)

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
    uint32_t wakeup_ticks;
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
static void monitor_encoder_activity_cb(void);

/*********************************************************************
 * GLOBAL FUNCTIONS
 */


void service_monitor_init(void)
{
    memset(&m_monitor_state, 0, sizeof(monitor_state_t));
    m_monitor_state.bat_level = 100;
    m_monitor_state.led_state = 0;
    m_monitor_state.tick_counter = 0;

    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
    if(wakeup_reason == ESP_SLEEP_WAKEUP_TIMER) // 定时唤醒
    {
        m_monitor_state.wakeup_ticks = MONITOR_AUTO_SLEEP_TICK_COUNT_LOW;
        // sys_logi(MONITOR_TAG, "wakeup by timer, wakeup_ticks: %d", m_monitor_state.wakeup_ticks);
    }
    else // 手动唤醒
    {
        m_monitor_state.wakeup_ticks = MONITOR_AUTO_SLEEP_TICK_COUNT;
        // sys_logi(MONITOR_TAG, "wakeup by manual, wakeup_ticks: %d", m_monitor_state.wakeup_ticks);
    }

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

    // 注册输入回调，用于重置休眠计数器
    hal_input_register_cb(INPUT_PRESS_SHORT, monitor_encoder_activity_cb);
    hal_input_register_cb(INPUT_PRESS_LONG, monitor_encoder_activity_cb);
    hal_input_register_cb(INPUT_PRESS_UP, monitor_encoder_activity_cb);
    hal_input_register_cb(INPUT_PRESS_DOWN, monitor_encoder_activity_cb);
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
    uint32_t led_color;
    uint32_t blink_cycle = MONITOR_LED_BLINK_ON_TICKS + MONITOR_LED_BLINK_OFF_TICKS;

    m_monitor_state.ble_connected = service_ble_gatts_get_connect();

    if(m_monitor_state.bat_level < MONITOR_BAT_LOW_THRESHOLD)
    {
        led_color = LED_COLOR_RED;
    }
    else if(m_monitor_state.ble_connected)
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
        hal_led_set_brightness(5);
    }
    else
    {
        hal_led_set_color(LED_COLOR_BLACK);
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

    m_monitor_state.sleep_counter++;

    m_monitor_state.ble_connected = service_ble_gatts_get_connect();
    if(!m_monitor_state.ble_connected) // ble disconnected
    {
        if(m_monitor_state.sleep_counter >= m_monitor_state.wakeup_ticks)
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
    hal_input_deinit();

    // 关闭外设供电并进入休眠
    hal_pwr_enter_sleep();
}

static void monitor_encoder_activity_cb(void)
{
    m_monitor_state.sleep_counter = 0;
}
