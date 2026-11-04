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
#include <stdatomic.h>

#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "sys_log.h"
#include "hal_api.h"
#include "hal_nfc.h"
#include "sys_cfg.h"
#include "service_ble_gatts.h"
#include "service_ble.h"
#include "service_monitor.h"
#include "service_param.h"
#include "service_wifi.h"

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
#define MONITOR_LED_UPDATE_INTERVAL_MS           (500)    // LED状态更新间隔 500ms（只决定"颜色多久复核一次"，呼吸节拍在 hal_led 里）
// 呼吸亮度区间（0~100）。两端不取 0/100：不熄灭（最暗仍有底光）也不会太刺眼。
// 换算成 WS2812 通道值 = 255 × 该值 / 100，当前 1~10 即 2~25。
// 暗端发涩可以把 MIN 往上提：通道差 1 级在暗端的**相对**变化很大（2→3 是 50% 亮度），
// MIN 提到 3 后单级变化降到 ~8%，代价是最暗不再那么暗。
#define MONITOR_LED_BREATH_MIN                   (1)
#define MONITOR_LED_BREATH_MAX                   (10)
// 呼吸周期（毫秒）：只决定快慢，跟平滑度无关（抖动节拍固定 200Hz，在 hal_led 里）。
// 觉得太快就调大这个值，不会变粗糙。
#define MONITOR_LED_BREATH_PERIOD_MS             (3000)
// "自动"模式（SERVICE_LED_MODE_AUTO）：这么久没有操作就熄灭。
// 有操作（按键 / 蓝牙连接状态变化）即重新计时，见 monitor_encoder_activity_cb()。
#define MONITOR_LED_AUTO_OFF_SEC                 (20)
// 电池管理参数     
#define MONITOR_BAT_CHECK_INTERVAL_MS            (30000)  // 电池检测间隔 30s
#define MONITOR_BAT_LOW_THRESHOLD                (10)     // 低电量阈值 10%
#define MONITOR_BAT_CRITICAL_THRESHOLD           (5)      // 极低电量阈值 5%
// 自动休眠管理参数     
#define MONITOR_SLEEP_CHECK_INTERVAL_MS          (200)    // 休眠检测间隔 200ms
#define MONITOR_AUTO_SLEEP_TIMEOUT_SEC           (60)     // 自动休眠超时时间(手动唤醒) 1min (60s)
#define MONITOR_AUTO_SLEEP_TIMEOUT_SEC_LOW       (30)     // 自动休眠超时时间(自动唤醒) 30s (30s)

#define MONITOR_TIMER_BASE_INTERVAL_MS           (100)
#define MONITOR_LED_TICK_COUNT                   (MONITOR_LED_UPDATE_INTERVAL_MS / MONITOR_TIMER_BASE_INTERVAL_MS)
#define MONITOR_LED_AUTO_OFF_TICKS               (MONITOR_LED_AUTO_OFF_SEC * 1000 / MONITOR_LED_UPDATE_INTERVAL_MS)
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
    uint8_t ble_connected_prev;  // 用于把"连接状态变化"也算成一次操作
    uint8_t bat_level;
    uint8_t led_on;              // LED 当前是否点着（关闭模式 / 自动熄灯都会置 0）
    uint32_t led_color;          // 当前呼吸的颜色（0 = 未点着）
    uint32_t led_idle_ticks;     // "自动"模式下距上次操作的节拍数
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
static atomic_bool m_sleep_pending = false;
static service_monitor_sleep_prepare_cb_t m_sleep_prepare_cb = NULL;

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
static void monitor_manual_sleep_event(void);
static void monitor_enter_low_power(void);
static void monitor_encoder_activity_cb(void);

/*********************************************************************
 * GLOBAL FUNCTIONS
 */


void service_monitor_init(void)
{
    memset(&m_monitor_state, 0, sizeof(monitor_state_t));
    m_monitor_state.bat_level = 100;
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

void service_monitor_request_sleep(void)
{
    monitor_msg_t msg;

    if(m_monitor_msg_hdl == NULL)
    {
        /* monitor 任务还没起来（或创建失败）：不能静默 —— 调用方以为睡了，其实没睡 */
        sys_loge(MONITOR_TAG, "sleep request dropped: monitor task not ready");
        return;
    }

    msg.ID = MSG_ENTER_SLEEP;
    monitor_msg_send(&msg, 0);
    sys_logi(MONITOR_TAG, "manual sleep requested");
}

bool service_monitor_sleep_pending(void)
{
    return atomic_load(&m_sleep_pending);
}

void service_monitor_set_sleep_prepare_cb(service_monitor_sleep_prepare_cb_t cb)
{
    m_sleep_prepare_cb = cb;
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
        case MSG_ENTER_SLEEP :
            monitor_manual_sleep_event();
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
        if(m_monitor_msg_hdl) (void)xQueueSend(m_monitor_msg_hdl, &msg, 0);
    }

    if((m_monitor_state.tick_counter % MONITOR_BAT_TICK_COUNT) == 0)
    {
        msg.ID = MSG_BATTERY_MANAGER;
        if(m_monitor_msg_hdl) (void)xQueueSend(m_monitor_msg_hdl, &msg, 0);
    }

    if((m_monitor_state.tick_counter % MONITOR_SLEEP_TICK_COUNT) == 0)
    {
        msg.ID = MSG_AUTO_SLEEP_MANAGER;
        if(m_monitor_msg_hdl) (void)xQueueSend(m_monitor_msg_hdl, &msg, 0);
    }
}

static void monitor_led_manage_event(void)
{
    uint8_t mode = g_service_param.sys.led_mode;
    uint32_t led_color;
    uint8_t want_on;

    m_monitor_state.ble_connected = service_ble_gatts_get_connect();

    /* 连接状态变化也算一次"操作"：插上手机时灯要立刻亮起来报状态 */
    if(m_monitor_state.ble_connected != m_monitor_state.ble_connected_prev)
    {
        m_monitor_state.ble_connected_prev = m_monitor_state.ble_connected;
        m_monitor_state.led_idle_ticks = 0;
    }

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

    /* ---- 先判"该不该亮"（关闭模式 / 自动模式闲置超时） ---- */
    want_on = 1;
    if(mode == SERVICE_LED_MODE_OFF)
    {
        want_on = 0;
    }
    else if(mode == SERVICE_LED_MODE_AUTO
            && m_monitor_state.led_idle_ticks >= MONITOR_LED_AUTO_OFF_TICKS)
    {
        want_on = 0;
    }

    if(!want_on)
    {
        if(m_monitor_state.led_on)
        {
            hal_led_breath_stop();
            hal_led_set_color(LED_COLOR_BLACK);
            m_monitor_state.led_on = 0;
            m_monitor_state.led_color = 0;
        }
    }
    /* 这里只决定"亮什么色"，呼吸的明暗节拍由 hal_led 自己跑。
       颜色没变就不重启：重启会把相位拉回 0，看着像闪了一下。 */
    else if(!m_monitor_state.led_on || led_color != m_monitor_state.led_color)
    {
        m_monitor_state.led_on = 1;
        m_monitor_state.led_color = led_color;
        hal_led_breath_start(led_color, MONITOR_LED_BREATH_MIN, MONITOR_LED_BREATH_MAX,
                             MONITOR_LED_BREATH_PERIOD_MS);
    }

    /* 自动模式：没操作就把闲置计数往前推（其它模式不需要计时） */
    if(mode == SERVICE_LED_MODE_AUTO && m_monitor_state.led_idle_ticks < MONITOR_LED_AUTO_OFF_TICKS)
    {
        m_monitor_state.led_idle_ticks++;
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
    if(m_sleep_pending)
    {
        if(!service_wifi_direct_busy()) monitor_enter_low_power();
        return;
    }
    if(service_wifi_direct_busy())
    {
        m_monitor_state.sleep_counter = 0;
        return;
    }
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

/**
 * @brief 手动休眠（主菜单长按）：直接进低功耗
 *
 * **不看休眠模式开关**：用户明确按下的动作就该执行，那个开关管的是自动休眠。
 * 定时唤醒仍按既有条件跟随参数（与自动休眠路径同一判据），
 * 休眠卡页脚那句 AUTO WAKE 用的也是这个条件，两边必须一致。
 */
static void monitor_manual_sleep_event(void)
{
    if(g_service_param.sleep.sleep_auto && g_service_param.sleep.sleep_time > 0)
    {
        hal_pwr_set_timer_wakeup(g_service_param.sleep.sleep_time);
        sys_logi(MONITOR_TAG, "manual sleep, timer wake in %d min",
                 g_service_param.sleep.sleep_time);
    }
    else
    {
        sys_logi(MONITOR_TAG, "manual sleep, no timer wake");
    }

    monitor_enter_low_power();
}

static void monitor_enter_low_power(void)
{
    m_sleep_pending = true;
    if(service_wifi_direct_busy())
    {
        service_wifi_direct_cancel();
        return;
    }
    /* ext0 是电平唤醒。覆盖自动/低电入睡及页面等待超时的路径，
       按键仍按下时保留 pending，由 200ms 心跳重试，不阻塞 monitor。 */
    if(hal_pwr_wake_condition_met())
    {
        return;
    }
    /* 唤醒源必须先配好：hal_pwr_enter_sleep() 在 ext0 配置失败时会直接返回，
       而下面几步（停定时器 / deinit 外设）都不可逆 —— 先卸后睡不下去就成了
       "外设全卸、心跳也停了"的僵尸态。配不上就保持 pending，等下次心跳重试。 */
    if(!hal_pwr_wakeup_prepare())
    {
        sys_logw(MONITOR_TAG, "sleep deferred: wakeup source not ready");
        return;
    }
    if(m_sleep_prepare_cb == NULL || !m_sleep_prepare_cb())
    {
        sys_logw(MONITOR_TAG, "sleep deferred: app/UI not stopped");
        return;
    }
#if (SYS_NFC_ENABLE == 1)
    /* 卡包已在先停 app 前启动模拟；这里武装 IRQ 并决定是否保留 NFC。
       如果此时场还在（IRQ 高），先别睡，等场消失后由心跳重试。 */
    bool nfc_armed = (hal_nfc_sleep_arm() == ESP_OK);
    if(!nfc_armed)
    {
        hal_nfc_deinit();
    }
    else if(hal_nfc_irq_active())
    {
        sys_logi(MONITOR_TAG, "sleep deferred: NFC field present");
        return;
    }
#endif
    if(!service_ble_prepare_sleep())
    {
        sys_logw(MONITOR_TAG, "sleep deferred: BLE file transfer not closed");
        return;
    }
    /* 网络写入和会话恢复完成后才允许卸载 SD、切断外设供电。 */
    service_wifi_deinit();
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
    m_monitor_state.led_idle_ticks = 0;   // 有操作 → "自动"模式的熄灯计时重新开始
}
