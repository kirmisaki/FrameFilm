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
#include "service_wifi.h"
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

// 时间基准：1 tick = 100ms
#define MONITOR_TIMER_BASE_INTERVAL_MS           (100)

#define MONITOR_LED_BRIGHTNESS                   (30)     // LED亮度值

// 状态轮询 / 屏幕插拔检测周期（500ms）
#define MONITOR_POLL_PERIOD_TICKS                (5)

// 无操作（按键/蓝牙连断/屏幕插拔/WiFi连断）30s 后息屏
#define MONITOR_IDLE_TIMEOUT_TICKS               (300)

// 屏幕插拔提示：紫色/红色快闪 5s
#define MONITOR_SCREEN_FLASH_TICKS               (50)
#define MONITOR_FLASH_ON_TICKS                   (2)      // 快闪点亮 200ms
#define MONITOR_FLASH_OFF_TICKS                  (2)      // 快闪熄灭 200ms

// EPD刷新中 LED 闪烁（500ms 亮 / 500ms 灭）
#define MONITOR_REFRESH_BLINK_ON_TICKS           (5)
#define MONITOR_REFRESH_BLINK_OFF_TICKS          (5)

/*********************************************************************
* TYPEDEFS
*/
typedef struct
{
    uint32_t tick_counter;          // 100ms tick 计数
    uint32_t idle_tick;             // 距上次操作的空闲 tick（BLE连接时不累计）
    uint32_t flash_left_tick;       // 屏幕插拔快闪剩余 tick
    uint16_t blink_phase;           // 闪烁相位计数
    uint8_t  awake;                 // LED 是否点亮（息屏状态）
    uint8_t  activity_pending;      // 有待处理的操作事件（按键等异步置位）
    uint8_t  led_mode;              // LED 工作模式（MONITOR_LED_MODE_*）
    uint8_t  screen_prev;           // 上次屏幕插入状态
    uint8_t  ble_prev;              // 上次 BLE 连接状态
    uint8_t  wifi_prev;             // 上次 WiFi 连接状态
    uint8_t  flash_active;          // 屏幕插拔 5s 快闪进行中
    uint32_t flash_color;           // 快闪颜色
    uint32_t last_color;            // 当前实际输出颜色
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

static void monitor_button_press_cb(void);
static void monitor_mark_activity(void);
static void monitor_poll_state(void);
static void monitor_led_manage_event(void);

/*********************************************************************
 * GLOBAL FUNCTIONS
 */


void service_monitor_init(void)
{
    memset(&m_monitor_state, 0, sizeof(monitor_state_t));
    m_monitor_state.awake = 1;
    m_monitor_state.last_color = LED_COLOR_BLACK;
    m_monitor_state.led_mode = MONITOR_LED_MODE_NORMAL;

    // 按键（短按/长按）也属于"操作"，用于唤醒与刷新空闲计时
    hal_input_register_cb(INPUT_PRESS_SHORT, monitor_button_press_cb);
    hal_input_register_cb(INPUT_PRESS_LONG, monitor_button_press_cb);

    // 先同步一次状态，避免把开机时已插着的屏幕误判为插入事件触发闪灯
    hal_epd_detect_insert();
    m_monitor_state.screen_prev = hal_epd_is_inserted();
    m_monitor_state.ble_prev = service_ble_gatts_get_connect();
    m_monitor_state.wifi_prev = service_wifi_get_connect_status();

    // 开机亮白色常亮
    hal_led_set_brightness(MONITOR_LED_BRIGHTNESS);
    hal_led_set_color(LED_COLOR_WHITE);
    m_monitor_state.last_color = LED_COLOR_WHITE;
    sys_logi(MONITOR_TAG, "LED on (boot, white)");

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

    // 每 100ms 触发一次 LED 管理，保证插拔"快闪"有足够的时间分辨率
    msg.ID = MSG_LED_MANAGER;
    monitor_msg_send(&msg, 0);
}

static void monitor_button_press_cb(void)
{
    monitor_mark_activity();
    sys_logi(MONITOR_TAG, "button press activity");
}

/* 记录一次操作：点亮 LED 并重置空闲计时（按键等异步事件置位，由 LED 任务消费） */
static void monitor_mark_activity(void)
{
    m_monitor_state.activity_pending = 1;
}

/* 每 500ms 轮询一次：屏幕插拔检测 + BLE/WiFi 连接状态（连/断边沿均视为操作） */
static void monitor_poll_state(void)
{
    monitor_state_t *st = &m_monitor_state;
    uint8_t screen;
    uint8_t ble;
    uint8_t wifi;

    // 屏幕插拔检测：插入紫色快闪 / 拔出红色快闪，持续 5s
    hal_epd_detect_insert();
    screen = hal_epd_is_inserted();
    if (screen != st->screen_prev)
    {
        st->screen_prev = screen;
        st->flash_active = 1;
        st->flash_color = screen ? LED_COLOR_PURPLE : LED_COLOR_RED;
        st->flash_left_tick = MONITOR_SCREEN_FLASH_TICKS;
        st->blink_phase = 0;
        monitor_mark_activity();
        sys_logi(MONITOR_TAG, "screen %s, LED %s flash", screen ? "inserted" : "removed",
                 screen ? "purple" : "red");
    }

    // 蓝牙 / WiFi 连接状态（连接或断开都算操作）
    ble = service_ble_gatts_get_connect();
    wifi = service_wifi_get_connect_status();
    if ((ble != st->ble_prev) || (wifi != st->wifi_prev))
    {
        st->ble_prev = ble;
        st->wifi_prev = wifi;
        monitor_mark_activity();
    }
}

static void monitor_led_manage_event(void)
{
    monitor_state_t *st = &m_monitor_state;
    uint32_t color = LED_COLOR_WHITE;
    uint32_t blink_cycle = 1;
    uint32_t blink_on_ticks = 1;
    uint32_t out_color;

    st->tick_counter++;

    // 状态轮询放在事件处理前，保证本次 tick 能立即响应插拔/连接边沿
    if ((st->tick_counter % MONITOR_POLL_PERIOD_TICKS) == 0)
    {
        monitor_poll_state();
    }

    // 消费操作事件：点亮 LED 并重置空闲计时
    if (st->activity_pending)
    {
        st->activity_pending = 0;
        if (!st->awake)
        {
            st->blink_phase = 0;
        }
        st->idle_tick = 0;
        st->awake = 1;
    }

    // 空闲计时：BLE 连接中不休眠，其余情况无操作 30s 后息屏
    if (st->awake && !st->ble_prev)
    {
        st->idle_tick++;
        if (st->idle_tick >= MONITOR_IDLE_TIMEOUT_TICKS)
        {
            st->awake = 0;
            sys_logi(MONITOR_TAG, "LED off (idle 30s)");
        }
    }

    // 插拔快闪计时（5s）
    if (st->flash_active)
    {
        if (st->flash_left_tick > 0)
        {
            st->flash_left_tick--;
        }
        if (st->flash_left_tick == 0)
        {
            st->flash_active = 0;
        }
    }

    if (!st->awake)
    {
        out_color = LED_COLOR_BLACK;
    }
    else
    {
        // 基础色：蓝牙+WiFi=黄，仅蓝牙=浅绿，仅WiFi=蓝，无连接=白
        if (st->ble_prev && st->wifi_prev)
        {
            color = LED_COLOR_YELLOW;
        }
        else if (st->ble_prev)
        {
            color = LED_COLOR_GREEN;
        }
        else if (st->wifi_prev)
        {
            color = LED_COLOR_BLUE;
        }

        // 闪烁策略：插拔快闪 > 刷新闪烁 > 常亮
        if (st->flash_active)
        {
            color = st->flash_color;
            blink_cycle = MONITOR_FLASH_ON_TICKS + MONITOR_FLASH_OFF_TICKS;
            blink_on_ticks = MONITOR_FLASH_ON_TICKS;
        }
        else if (st->led_mode == MONITOR_LED_MODE_REFRESH)
        {
            blink_cycle = MONITOR_REFRESH_BLINK_ON_TICKS + MONITOR_REFRESH_BLINK_OFF_TICKS;
            blink_on_ticks = MONITOR_REFRESH_BLINK_ON_TICKS;
        }

        st->blink_phase = (st->blink_phase + 1) % blink_cycle;
        out_color = (st->blink_phase < blink_on_ticks) ? color : LED_COLOR_BLACK;
    }

    if (out_color != st->last_color)
    {
        hal_led_set_color(out_color);
        st->last_color = out_color;
    }
}

void service_monitor_set_film_refresh_state(uint8_t refreshing)
{
    m_monitor_state.led_mode = refreshing ? MONITOR_LED_MODE_REFRESH : MONITOR_LED_MODE_NORMAL;
    // 切换模式时归零相位，避免由熄灭相位切入导致瞬间熄灭
    m_monitor_state.blink_phase = 0;
    // 刷新开始/结束都视为一次操作（点亮并重置空闲计时）
    monitor_mark_activity();
}
