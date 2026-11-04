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
 * FileName : /film_app/core/app_manager.c
 * Author: Kiritro  Version: v0.1  Date: 2026/9/9
 * Description: App 层调度器：注册/切换/事件路由/输入转发/切换状态机
 * ChangeLog: Change Notes
 *
 *********************************************************************/


/*********************************************************************
 * INCLUDES
 */
#include <string.h>
#include <stdio.h>
#include <stdatomic.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/timers.h"

#include "sys_log.h"
#include "sys_event.h"
#include "hal_input.h"
#include "hal_epd.h"
#include "service_file.h"
#include "service_ble.h"
#include "service_param.h"
#include "app_manager.h"
#include "app_render.h"
#include "app_sleep.h"  /* app_sleep_run：主菜单长按 = 手动休眠 */
#include "ui_conf.h"    /* UI_CALIB_FRAME：标定帧模式下的开机页行为 */

/*********************************************************************
 * MACROS
 */
#define APP_MANAGER_TAG         "app_manager"

#define APP_QUEUE_LENGTH        (16)
#define APP_QUEUE_ITEM_SIZE     sizeof(app_event_t)
#define APP_TASK_PRIO           (5)
#define APP_TASK_STACK          (4096)
#define APP_TASK_NAME           "app_task"

#define APP_TICK_MS             (100)    // 周期 on_tick 心跳（时钟 1s 去重、动图叠加 frame 推进）

#define APP_PARAM_GET_BUF_MAX   (60)     // 参数查询回包缓冲区（与 BLE 回包上限对齐）

/*********************************************************************
 * TYPEDEFS
 */
/**
 * @brief 输入路由结果（app_manager 内部使用）
 */
typedef enum {
    APP_INPUT_CONSUMED = 0,  // 输入已由调度器/主菜单消费，不再下发
    APP_INPUT_PASS,          // 输入未消费，放行给当前 app
} app_input_result_t;

/*********************************************************************
 * CONSTANTS
 */

/*********************************************************************
 * LOCAL VARIABLES
 */
static TaskHandle_t m_app_task_hdl = NULL;
static QueueHandle_t m_app_queue_hdl = NULL;
static TimerHandle_t m_app_timer = NULL;   // on_tick 心跳

static const app_entry_t *m_app_registry[APP_ID_MAX] = {0};
static app_id_t m_current_app = APP_ID_IMAGE;
static uint8_t m_app_running = 0;
static uint32_t m_tick_acc_ms = 0;         // 当前 app 的 on_tick 累计时长（按 tick_ms 分频）
static app_switch_mode_t m_switch_mode = APP_SWITCH_MODE_NONE;  // 生效模式（init 时解析）
static app_id_t m_last_guest_app = APP_ID_MAX;  // 最近一次切入的非图片 app（简易模式确认键切换目标）

/* 主菜单轮播表：下标即选择索引（UI 页据此显示"第 N 项"）。
   顺序必须与 app_menu.c 的 MENU_ITEMS 一致——那边是视觉表，这里是行为表。 */
static const app_id_t m_menu_entries[APP_MENU_ENTRY_NUM] = {
    APP_ID_IMAGE, APP_ID_PASS, APP_ID_TEMPLATE, APP_ID_CLOCK, APP_ID_ANIMATION, APP_ID_SETTINGS,
};
static uint8_t m_menu_sel = 0;   // 主菜单当前选中索引
static uint8_t m_boot_page = 0;  // 开机画面占屏中（此期间不进入任何 app，按键丢弃）
static app_id_t m_boot_next_app = APP_ID_MENU;  // 开机画面结束后切到哪（START APP 决定）
static uint8_t m_boot_ui_shown = 0;  // 开机画面的 UI 页仍在屏上（收尾的唯一依据，见 app_do_switch）
static uint8_t m_boot_evt_pending = 0;  // 开机画面期间欠着一次 APP_EVT_BOOT（见 app_do_switch）
static uint8_t m_sleep_page = 0; // 休眠卡占屏中（同上；此后设备就断电了）
static atomic_bool m_sleep_stop_sent = false;
static atomic_bool m_sleep_stopped = false;
#define APP_SLEEP_STOP_CMD 0xFFFFFFFEu

/* 参数通道反查表：param_ch / param_ch+1 → 归属 app。注册时构建，与“当前 app”无关，
   手机可在显示图片时预设动图参数，事件照样送达目标 app */
static const app_entry_t *m_param_owner[256] = {0};

/* 各 app 的 RAM 状态是否已载入（载入 NVS 或套用默认值后置位）。
   未置位说明该 app 从未进入过，其 state 仍是 BSS 零值，
   此时若直接接受 BLE 参数设置会把零值落盘（覆盖后续 load 的默认值回落）。 */
static uint8_t m_state_loaded[APP_ID_MAX] = {0};

/*********************************************************************
 * GLOBAL VARIABLES
 */

/*********************************************************************
 * LOCAL FUNCTIONS
 */
static void app_task_handle(void *pvParameters);
static void app_timer_callback(TimerHandle_t xTimer);
static void app_handle_event(const app_event_t *e);
static void app_do_switch(app_id_t id);
static uint8_t app_do_switch_target(app_id_t id, const char *filename);
static void app_menu_notice(const char *why);
static void app_manager_sleep_from_app(void);
static int app_param_ch_route(uint8_t ch, const uint8_t *data, uint8_t len);
static app_input_result_t app_manager_process_input(input_press_type_t key);
static void app_input_dispatch(input_press_type_t key);
static input_press_type_t ble_key_to_press(uint8_t key);
static int app_state_save_of(const app_entry_t *app);
static int app_state_load_of(const app_entry_t *app);
static void app_manager_notify_boot(void);

/*********************************************************************
 * LOCAL HELPERS
 */

/**
 * @brief BLE 遥控键值 → HAL 输入事件
 *
 * 与 hal_input 上报的事件同构，因此注入后能完整复用既有语义：
 * 上/下 = 菜单导航，单击 = 确认，双击 = 退回主菜单，长按 = 手动休眠。
 * 未知键值一律映射为 INPUT_PRESS_NONE（消费不掉任何东西）。
 */
static input_press_type_t ble_key_to_press(uint8_t key)
{
    switch(key)
    {
        case BLE_KEY_SHORT:  return INPUT_PRESS_SHORT;
        case BLE_KEY_LONG:   return INPUT_PRESS_LONG;
        case BLE_KEY_UP:     return INPUT_PRESS_UP;
        case BLE_KEY_DOWN:   return INPUT_PRESS_DOWN;
        case BLE_KEY_DOUBLE: return INPUT_PRESS_DOUBLE;
        default:             return INPUT_PRESS_NONE;
    }
}

/**
 * @brief 确保当前 app 已进入（on_enter）
 */
static void app_ensure_running(void)
{
    if(!m_app_running)
    {
        const app_entry_t *app = m_app_registry[m_current_app];
        /* UI 层 app 的"进入"动作是建页面，由 ui_core 在 ui_task 上下文完成；
           此处不调 on_enter——那样会在 app_task 里触碰 lv_*，违反 LVGL 单任务约束 */
        if(app && app->layer != APP_LAYER_UI && app->on_enter)
        {
            app->on_enter();
        }
        m_app_running = 1;
    }
}

/**
 * @brief 停掉当前 app（按显示层分流）
 *
 * UI 层：page_exit 会同步关掉输出闸门，随后 DIRECT 层 app 即可安全绘制面板。
 */
static void app_stop_current(void)
{
    const app_entry_t *old = m_app_registry[m_current_app];

    if(old != NULL && old->layer == APP_LAYER_UI)
    {
        ui_core_page_exit();
        return;
    }

    if(old && old->on_exit)
    {
        old->on_exit();
    }
}

/**
 * @brief 启动当前 app（按显示层分流）
 */
static void app_start_current(void)
{
    const app_entry_t *app = m_app_registry[m_current_app];

    if(app != NULL && app->layer == APP_LAYER_UI)
    {
        if(!ui_core_is_ready())
        {
            sys_logw(APP_MANAGER_TAG, "app %s needs ui layer but panel unsupported", app->name);
        }
        else
        {
            /* 异步：ui_task 建页面并开闸门 */
            ui_core_page_enter((uint8_t)m_current_app, app->ui_ops);
        }
        m_app_running = 1;   // 页面构建由 ui_task 负责，这里只标记已启动
        return;
    }

    /* 直绘层没有 UI 那样的"进页先清场"（ui_page_build → ui_clean_panel），
       但它接着上一屏（主菜单/别的 app）的画面继续做 mono 差分刷新 ——
       快刷波形没有彻底擦除的相位，"同色"的残留像素不会被驱动，会留在屏上（残影）。
       这里与 UI 层对称：切进直绘 app 前复位一次面板，让该 app 的首帧先做完整清场。
       复位本身不改画面，清场代价（约 3s）由首帧付。 */
    app_render_clean_panel();

    app_ensure_running();
}

/**
 * @brief 解析生效的按键切换模式
 *
 * FULL 需要屏幕能跑主菜单（UI 层页面 + 快刷 + 导航键），否则自动降级为
 * SIMPLE，避免 sys_cfg.h 的配置与硬件不一致时切换功能整体失效。
 */
static app_switch_mode_t app_switch_effective_mode(void)
{
    if(SYS_APP_SWITCH_MODE == SYS_APP_SWITCH_NONE)
    {
        return APP_SWITCH_MODE_NONE;
    }
    if(SYS_APP_SWITCH_MODE == SYS_APP_SWITCH_FULL)
    {
        return app_render_has_app_menu() ? APP_SWITCH_MODE_FULL : APP_SWITCH_MODE_SIMPLE;
    }
    return APP_SWITCH_MODE_SIMPLE;
}

/**
 * @brief app 是否已注册（未注册的 app 不可切换，避免切到空白画面）
 */
static int app_is_registered(app_id_t id)
{
    return (id < APP_ID_MAX) && (m_app_registry[id] != NULL);
}

/**
 * @brief 指定 app 是否运行在 UI 框架层
 */
static int app_is_ui_layer(app_id_t id)
{
    const app_entry_t *app = (id < APP_ID_MAX) ? m_app_registry[id] : NULL;
    return (app != NULL) && (app->layer == APP_LAYER_UI);
}

/**
 * @brief 主菜单是否可用
 *
 * 主菜单是 UI 层页面：需生效模式为 FULL（面板具备快刷能力）且该 app 已注册。
 * 不可用时按键交回各 app，避免"按了没反应"。
 */
static int app_menu_available(void)
{
    return (m_switch_mode == APP_SWITCH_MODE_FULL) && app_is_registered(APP_ID_MENU);
}

/**
 * @brief 进入主菜单
 *
 * 选择索引先对齐"刚离开的 app"，避免菜单高亮与当前画面脱节；随后切页并把索引
 * 同步给页面。ui_core_post 与 ui_core_page_enter 走同一 FIFO 队列，故该消息
 * 必然在页面 create 之后被处理。
 */
static void app_menu_open(void)
{
    uint8_t i;

    for(i = 0; i < APP_MENU_ENTRY_NUM; i++)
    {
        if(m_menu_entries[i] == m_current_app)
        {
            m_menu_sel = i;
            break;
        }
    }

    app_do_switch(APP_ID_MENU);
    (void)ui_core_post(APP_UI_MSG_MENU_SEL, &m_menu_sel, 1);
}

/**
 * @brief 当前 app 是否声明订阅了某全局事件（app_entry.events 列表，0 结尾）
 */
static int app_wants_sys_event(const app_entry_t *app, uint16_t id)
{
    if(app == NULL || app->events == NULL)
    {
        return 0;
    }
    for(const uint16_t *p = app->events; *p != 0; p++)
    {
        if(*p == id)
        {
            return 1;
        }
    }
    return 0;
}

/**
 * @brief 全局事件总线订阅回调
 *
 * 在发布方上下文（BLE/WiFi/HTTP/OTA 任务）同步执行，只做归一化 + 非阻塞入队，
 * 真正的业务处理在 app 任务中执行。
 */
static void app_manager_on_sys_event(const sys_event_t *e, void *ctx)
{
    (void)ctx;
    if(e == NULL || m_app_queue_hdl == NULL)
    {
        return;
    }

    app_event_t ae;
    memset(&ae, 0, sizeof(ae));
    ae.type   = APP_EVT_SYS;
    ae.input  = INPUT_PRESS_NONE;
    ae.cmd    = e->id;      /* sys_event_id_t，app 侧按此匹配 app_entry.events */
    ae.len    = (e->len > APP_EVENT_PAYLOAD_MAX) ? APP_EVENT_PAYLOAD_MAX : (uint8_t)e->len;
    if(ae.len > 0)
    {
        memcpy(ae.payload, e->payload, ae.len);
    }

    /* 非阻塞入队：app 任务刷屏耗时较长时宁可丢弃并告警，也不阻塞发布方 */
    if(xQueueSend(m_app_queue_hdl, &ae, 0) != pdPASS)
    {
        sys_logw(APP_MANAGER_TAG, "app queue full, drop evt=0x%04X", (unsigned)e->id);
        if(e->id == SYS_EVT_BLE_APP_CMD && e->len > 2 &&
           e->payload[0] == BLE_FILM_TRANS_CH_CTRL_APP_SWITCH)
        {
            uint8_t response[2] = {e->payload[1], 3};
            service_ble_send_resp(BLE_FILM_TRANS_CH_CTRL_APP_SWITCH, response, sizeof(response));
        }
    }
}

/**
 * @brief BLE app 参数通道路由（SET/GET）
 *
 * 按 ch 查归属 app，与“当前 app”无关：手机可在显示图片时预设动图参数。
 * SET 只调 on_param_set（app 仅改自己的结构体，不刷屏、不落盘），
 * 落盘由本函数按归属 app 完成（见下方 app_state_save_of）；
 * GET 先确保状态已载入，再调 on_param_get 组帧并经 service_ble_send_resp 回发。
 *
 * @return 1 已消费（是参数通道）；0 非参数通道，交回既有逻辑
 */
static int app_param_ch_route(uint8_t ch, const uint8_t *data, uint8_t len)
{
    const app_entry_t *owner = m_param_owner[ch];
    if(owner == NULL)
    {
        return 0;
    }

    if(ch == owner->param_ch)
    {
        if(owner->on_param_set != NULL)
        {
            /* 归属 app 可能从未进入过（state 仍为零值）：先把状态载入/套用默认值，
               再执行参数回调，最后按其归属落盘，避免零值落盘污染 NVS。 */
            if(owner->id >= 0 && owner->id < APP_ID_MAX && !m_state_loaded[owner->id])
            {
                app_state_load_of(owner);
            }
            owner->on_param_set(data, len);
            /* 参数回调可能在本 app 非当前 app 时执行，app_state_save() 存不到它，
               故由框架按归属 app 落盘（app 侧因此无需关心 NVS）。 */
            app_state_save_of(owner);
        }
    }
    else
    {
        uint8_t out[APP_PARAM_GET_BUF_MAX];
        uint8_t n = app_manager_param_get((uint8_t)owner->id, out, sizeof(out));
        service_ble_send_resp(ch, out, n);
    }
    return 1;
}

/**
 * @brief 读某个 app 的参数（TLV），供设置页等内部页面使用（不经 BLE）
 *
 * 与 BLE 参数通道共用同一套回调与状态载入规则：该 app 从未进入过时先把状态
 * 载入（无数据则套默认值），否则会读到 BSS 零值。
 *
 * @return TLV 字节数；0 = 该 app 不支持参数 / 缓冲不足
 */
uint8_t app_manager_param_get(uint8_t app_id, uint8_t *out, uint8_t max)
{
    const app_entry_t *app;

    if((app_id >= (uint8_t)APP_ID_MAX) || (out == NULL))
    {
        return 0;
    }
    app = m_app_registry[app_id];
    if((app == NULL) || (app->on_param_get == NULL))
    {
        return 0;
    }
    if(!m_state_loaded[app_id])
    {
        app_state_load_of(app);
    }
    return app->on_param_get(out, max);
}

/**
 * @brief 写某个 app 的参数（TLV），供设置页等内部页面使用（不经 BLE）
 *
 * 与 BLE 通道同款：先确保状态已载入 → on_param_set → 由框架按归属 app 落盘
 * （app 侧因此不碰 NVS）。刷屏与否由 app 自己按"是否当前 app"判断。
 */
void app_manager_param_set(uint8_t app_id, const uint8_t *tlv, uint8_t len)
{
    const app_entry_t *app;

    if((app_id >= (uint8_t)APP_ID_MAX) || (tlv == NULL) || (len == 0))
    {
        return;
    }
    app = m_app_registry[app_id];
    if((app == NULL) || (app->on_param_set == NULL))
    {
        return;
    }
    if(!m_state_loaded[app_id])
    {
        app_state_load_of(app);
    }
    app->on_param_set(tlv, len);
    app_state_save_of(app);
}

static void app_handle_event(const app_event_t *e)
{
    if(e->type == APP_EVT_UI_MSG && e->cmd == APP_SLEEP_STOP_CMD)
    {
        const bool was_sleep_page = m_sleep_page != 0;
        m_sleep_page = 1;
        if(!was_sleep_page && m_app_running)
        {
            const app_entry_t *app = m_app_registry[m_current_app];
            if(app && app->layer != APP_LAYER_UI && app->on_exit) app->on_exit();
            app_state_save();
        }
        if(ui_core_stop_for_sleep() == 0)
            atomic_store(&m_sleep_stopped, true);
        else
            atomic_store(&m_sleep_stop_sent, false);
        return;
    }
    /* Extended 0x4B is acknowledged after the app task has resolved the name.
       Busy gates reject it explicitly, so the client may retry without a timer. */
    if(e->type == APP_EVT_SYS && e->cmd == SYS_EVT_BLE_APP_CMD &&
       e->len > 2 && e->payload[0] == BLE_FILM_TRANS_CH_CTRL_APP_SWITCH)
    {
        uint8_t response[2] = {e->payload[1], 1};
        const char *filename = (const char *)&e->payload[2];
        size_t name_bytes = e->len - 2;
        if(e->len >= 4 && response[0] == APP_ID_IMAGE &&
           filename[name_bytes - 1] == '\0' &&
           memchr(filename, '\0', name_bytes - 1) == NULL &&
           strchr(filename, '/') == NULL && strchr(filename, '\\') == NULL)
        {
            response[1] = (m_sleep_page || m_boot_page) ? 3 :
                          app_do_switch_target((app_id_t)response[0], filename);
        }
        service_ble_send_resp(BLE_FILM_TRANS_CH_CTRL_APP_SWITCH, response, sizeof(response));
        return;
    }

    /* 入睡后可能还要等直传恢复网络；这期间不能被排队事件重新启动 app。 */
    if(m_sleep_page)
    {
        return;
    }

    /* BLE app 参数通道：先于菜单态闸门处理，保证目标 app 即便不是当前 app 也能收到 */
    if(e->type == APP_EVT_SYS && (uint16_t)e->cmd == SYS_EVT_BLE_APP_CMD && e->len >= 1)
    {
        if(app_param_ch_route(e->payload[0], &e->payload[1], (uint8_t)(e->len - 1)))
        {
            return;
        }
    }

    /* 切换事件：由调度器消费 */
    if(e->type == APP_EVT_SWITCH)
    {
        app_id_t target = (app_id_t)(uintptr_t)e->data;
        app_do_switch(target);
        return;
    }

    /* 开机占屏期间不驱动 app，避免与 ui_task 的 flush 抢 SPI。
       BLE 参数与切换仍可提前处理；休眠则已在函数入口拦截全部事件。 */
    if(m_boot_page || m_sleep_page)
    {
        if(m_boot_page)
        {
            /* 开机页走完进度后由**页面**上报可以切页（时序见 app_boot.h） */
            if(e->type == APP_EVT_UI_MSG && e->cmd == APP_UI_REQ_BOOT_DONE)
            {
                app_manager_boot_end();
            }
#if (UI_CALIB_FRAME == 1)
            /* 标定帧模式下开机页不自动前进，短按确认键手动进主菜单（见 ui_conf.h） */
            else if(e->type == APP_EVT_INPUT && e->input == INPUT_PRESS_SHORT)
            {
                app_manager_boot_end();
            }
#endif
        }
        return;
    }

    /* 周期心跳：只走 on_tick，按各 app 声明的 tick_ms 分频 */
    if(e->type == APP_EVT_TIMER)
    {
        const app_entry_t *app = m_app_registry[m_current_app];
        if(app && app->on_tick && app->tick_ms > 0)
        {
            m_tick_acc_ms += APP_TICK_MS;
            if(m_tick_acc_ms >= app->tick_ms)
            {
                m_tick_acc_ms = 0;
                app->on_tick();
            }
        }
        return;
    }

    /* 输入事件：主菜单 / 双击退出 / 长按休眠先由 app_manager 裁决 */
    if(e->type == APP_EVT_INPUT)
    {
        if(app_manager_process_input(e->input) == APP_INPUT_CONSUMED)
        {
            return;
        }

        /* 未消费的按键：当前 app 属 UI 层时交给 ui_core（LVGL 非线程安全，
           必须投递到 ui_task 处理），不透传给 on_event */
        if(app_is_ui_layer(m_current_app))
        {
            ui_core_post_key((uint8_t)e->input);
            return;
        }
    }

    /* 全局总线事件：统一走 APP_EVT_SYS，避免兼容路径绕过 app_entry.events 过滤 */
    if(e->type == APP_EVT_SYS)
    {
        /* 调度器级：BLE 请求切换 app（payload: [0]=命令通道，[1]=目标 app id） */
        if((uint16_t)e->cmd == SYS_EVT_BLE_APP_CMD)
        {
            if(e->len == 2 && e->payload[0] == BLE_FILM_TRANS_CH_CTRL_APP_SWITCH)
            {
                app_do_switch((app_id_t)e->payload[1]);
            }
            /* 远程按键注入（payload: [0]=命令通道，[1]=键值）：
               映射成与 HAL 上报同款事件入队，完整复用既有语义
               （菜单导航 / 单击确认 / 双击退回 / 长按休眠） */
            else if(e->len >= 2 && e->payload[0] == BLE_FILM_TRANS_CH_CTRL_KEY_INJECT)
            {
                app_input_dispatch(ble_key_to_press(e->payload[1]));
            }
            return;
        }

        /* 其余总线事件：仅当当前 app 声明关注该 ID 时才下发（避免无关事件打扰） */
        const app_entry_t *cur = m_app_registry[m_current_app];
        if(!app_wants_sys_event(cur, (uint16_t)e->cmd))
        {
            return;
        }
    }

    /* UI 页面请求只有"当前是 UI 层 app"才可能发出；迟到的请求（页面已随层切换销毁）
       不应落到 DIRECT 层 app 的 on_event 上 */
    if(e->type == APP_EVT_UI_MSG && !app_is_ui_layer(m_current_app))
    {
        return;
    }

    /* 进入当前 app 后转发给它的 on_event */
    app_ensure_running();
    const app_entry_t *app = m_app_registry[m_current_app];
    if(app && app->on_event)
    {
        app->on_event(e);
    }
}

/**
 * @brief 把"进不去"的原因显示在主菜单上
 *
 * 提示就画在菜单底部那块留白里（见 app_menu.c）。当前不是菜单时（例如 BLE 远程
 * 切到一个进不去的 app）没有页面可承载，只留日志 —— 不为了提示去切页，
 * 那样反而会造成"进去了"的错觉。
 */
static void app_menu_notice(const char *why)
{
    if(why == NULL)
    {
        return;
    }
    if(m_current_app != APP_ID_MENU || m_boot_page || m_sleep_page)
    {
        return;
    }

    (void)ui_core_post(APP_UI_MSG_MENU_NOTICE, why, (uint8_t)strlen(why));
}

static void app_do_switch(app_id_t id)
{
    (void)app_do_switch_target(id, NULL);
}

static uint8_t app_do_switch_target(app_id_t id, const char *filename)
{
    if(m_sleep_page)
    {
        return 3;
    }

    /* 未注册的 app 不可切换：BLE 传来的非法 id 或按模式裁剪掉的 app 会走到这里，
       若无此守卫会切到空注册项 → 无 on_enter → 屏幕停在上一帧，看起来像卡死 */
    if(!app_is_registered(id))
    {
        sys_logw(APP_MANAGER_TAG, "switch ignored: app id=%d not registered", (int)id);
        return 1;
    }
    if(filename == NULL && id == m_current_app && m_app_running)
    {
        /* 已是当前 app，正常无需动作。但若它是 UI 层且被直绘场景暂停过输出，
           必须恢复，否则页面会永远停在暂停态（屏上留着直绘内容）。
           ui_core_resume 只在 PAUSED 态生效，重复调用不额外付清场代价。 */
        if(app_is_ui_layer(id))
        {
            ui_core_resume();
        }
        return 0;   // 已是当前运行 app
    }

    const app_entry_t *app = m_app_registry[id];
    if(filename != NULL)
    {
        if(id != APP_ID_IMAGE || app->prepare_open == NULL) return 1;
        /* Invalid names must not replace the old app's file list/cache. The
           final ID still comes from the synchronized playback list below. */
        if(!service_file_has_film(app->data_dir, filename)) return 2;
    }
    char previous_dir[64];
    snprintf(previous_dir, sizeof(previous_dir), "%s", service_file_get_dir());

    /* ① 数据源切换 + 进入前预检
         目录列表是异步刷新的，"这个 app 有没有内容"只有等列表就绪才问得准，
         所以预检必须排在这一步之后；也只有在这一步退回是**无副作用**的 ——
         旧 app 尚未被停、状态尚未改，把目录还回去即可当作没发生过。
         预检不过就别切：切过去屏幕只会停在上一帧，用户会以为已经进到那个 app 了。 */
    if(app != NULL && app->data_dir != NULL)
    {
        sys_logi(APP_MANAGER_TAG, "switch data dir=%s count=%u",
                 app->data_dir, (unsigned)service_file_set_dir_sync(app->data_dir));
    }

    if(filename != NULL)
    {
        uint8_t status = !service_file_is_list_ready() ? 3 :
                         (app->prepare_open(filename) ? 0 : 2);
        if(status != 0)
        {
            (void)service_file_set_dir_sync(previous_dir);
            return status;
        }
        /* Same image app: consume the one-shot target without panel reset or
           changing ownership; an already executing waveform finishes normally. */
        if(id == m_current_app && m_app_running)
        {
            app->on_enter();
            return 0;
        }
    }

    if(filename == NULL && app != NULL && app->enter_block_reason != NULL)
    {
        const char *why = app->enter_block_reason();

        if(why != NULL)
        {
            const app_entry_t *cur = m_app_registry[m_current_app];

            if(cur != NULL && cur->data_dir != NULL)
            {
                (void)service_file_set_dir_sync(cur->data_dir);   // 还原旧 app 的数据源
            }
            sys_logw(APP_MANAGER_TAG, "switch to %s blocked: %s", app->name, why);
            app_menu_notice(why);
            return 2;
        }
    }

    /* 记录最近一次切入的非图片 app：简易模式下确认键在该 app 与图片之间互切 */
    if(id != APP_ID_IMAGE)
    {
        m_last_guest_app = id;
    }

    /* ② 先停旧层：UI 层会同步关掉输出闸门并删页面，
       避免切换途中 ui_task 把 UI 帧推上屏、覆盖正要绘制的 DIRECT 画面 */
    /* 开机页是 APP_ID_MAX 的托管页、不在 app 注册表里：app_stop_current() 按
       m_current_app 分流根本碰不到它（默认还是图片，DIRECT 层）。必须在这里显式收掉，
       否则 ui_page_teardown() 不会跑 —— 黑白快刷会话（hal_epd_mono_session_end）不关、
       输出闸门与显存一直占着，紧接着直绘的图片会以错误的刷新方式叠在开机帧上
       （表现为"图片没刷新"）。放在预检之后：切换被拦下时开机页要留着继续占屏。 */
    if(m_boot_ui_shown)
    {
        m_boot_ui_shown = 0;
        ui_core_page_exit();
    }
    app_stop_current();
    /* 切出兜底保存（幂等，覆盖 app 内部未显式保存的改动）。
       仅在旧 app 确实运行过时保存：启动首次切换时它尚未 on_enter/载入状态，
       直接保存会把空结构体写回 NVS，覆盖掉待载入的持久化数据。 */
    if(m_app_running)
    {
        app_state_save();
    }

    m_current_app = id;
    m_app_running = 0;
    m_tick_acc_ms = 0;
    m_boot_page = 0;   // 切到任何 app 即结束开机画面（含 BLE 远程切换）
    m_sleep_page = 0;  // 同理：切到 app 就意味着不再处于"占屏托管"状态

    app_state_load();   // ③ 切入先载入状态，再启动（按显示层分流）
    app_start_current();
    service_param_app_current_set((uint8_t)id);   // ④ 记录当前 app，供下次启动恢复

    /* 走过开机画面的启动路径欠着一次 APP_EVT_BOOT：此刻目标 app 才真正成为
       "首个 app"，在这里补投（不补的话图片自动切图 / 模板自动拉取永远不会触发）。
       直启路径由 app_init 在切换后自行投递，那边不受影响。 */
    if(m_boot_evt_pending)
    {
        m_boot_evt_pending = 0;
        app_manager_notify_boot();
    }
    return 0;
}

/**
 * @brief 从 app 内进入休眠（长按确认键）：**不出示休眠卡**，屏上保持当前 app 的画面
 *
 * 先停当前 app 有两层原因：
 *   ① 让它停止绘制 —— 动图在按帧率不停推帧、图片可能正在翻页；
 *   ② app_stop_current() 对 UI 层是**同步**等 page_exit 完成的，停完之后不可能有
 *      半帧在途。所以这条路径不必像"画休眠卡"那样按时间兜一帧的时长（见 app_sleep.h），
 *      长按松手就能立刻断电。
 * 停 app 不会清屏：UI 层只是关掉输出闸门（屏上留最后一帧），DIRECT 层 on_exit 也不绘制。
 */
static void app_manager_sleep_from_app(void)
{
    /* 即使不画休眠卡，也必须冻结后续事件直到 deep sleep。 */
    m_sleep_page = 1;
    app_stop_current();
    if(m_app_running)
    {
        app_state_save();   // 睡前的兜底落盘（幂等，与切页时同一套）
    }
    app_sleep_run(0);
}

static app_input_result_t app_manager_process_input(input_press_type_t key)
{
    if(key == INPUT_PRESS_NONE || m_switch_mode == APP_SWITCH_MODE_NONE)
    {
        return APP_INPUT_PASS;   // 未开启按键切换：全部按键放行给当前 app（BLE 远程切换不受影响）
    }

    /* 主菜单：上/下换选中项，确认键进入。菜单是调度器的"根"，
       按键一律不往下传（页面本身不接 on_key） */
    if(m_current_app == APP_ID_MENU)
    {
        if(key == INPUT_PRESS_UP || key == INPUT_PRESS_DOWN)
        {
            int step = (key == INPUT_PRESS_UP) ? 1 : -1;
            m_menu_sel = (uint8_t)((m_menu_sel + APP_MENU_ENTRY_NUM + step) % APP_MENU_ENTRY_NUM);
            (void)ui_core_post(APP_UI_MSG_MENU_SEL, &m_menu_sel, 1);
        }
        else if(key == INPUT_PRESS_SHORT)
        {
            app_id_t target = m_menu_entries[m_menu_sel];
            if(app_is_registered(target))
            {
                app_do_switch(target);
            }
            else
            {
                sys_logw(APP_MANAGER_TAG, "menu entry %u not registered", (unsigned)m_menu_sel);
            }
        }
        /* 长按 = 手动休眠：菜单里**画休眠卡**（这张卡会一直留在屏上，直到按下确认键唤醒）。
           请求后保持休眠门闸，直到 monitor 完成网络恢复并进入 deep sleep。
           注意**不看休眠模式开关** —— 那个开关管的是自动休眠，用户明确按下的动作就该执行。
           双击在菜单里没有语义：菜单就是调度器的"根"，没有上一层可退。 */
        else if(key == INPUT_PRESS_LONG)
        {
            app_sleep_run(1);
        }
        return APP_INPUT_CONSUMED;
    }

    /* 其余 app：**双击**确认键退回主菜单（原长按语义挪到这里）。
       主菜单不可用（无 UI 层的机型）时按键交回 app，避免"按了没反应" */
    if(key == INPUT_PRESS_DOUBLE && app_menu_available())
    {
        app_menu_open();
        return APP_INPUT_CONSUMED;
    }

    /* 长按确认键 = 手动休眠，且**不出示休眠卡**：屏上保持当前 app 的画面。
       请求返回后仍冻结 app，等待 monitor 进入 deep sleep。 */
    if(key == INPUT_PRESS_LONG)
    {
        app_manager_sleep_from_app();
        return APP_INPUT_CONSUMED;
    }

    /* SIMPLE：只在当前 app 未声明的按键上做切换，
       声明式按键掩码避免打断 app 自身的按键功能（图片翻页、动图播放模式等） */
    if(m_switch_mode == APP_SWITCH_MODE_SIMPLE)
    {
        const app_entry_t *cur = m_app_registry[m_current_app];
        uint8_t busy = cur ? cur->keys : 0;

        if(!APP_KEY_IS_BUSY(busy, key))
        {
            if(key == INPUT_PRESS_UP || key == INPUT_PRESS_DOWN)
            {
                /* 上/下：非图片 app 直接回图片；图片 app 内放行（按键已声明占用） */
                if(m_current_app != APP_ID_IMAGE)
                {
                    app_do_switch(APP_ID_IMAGE);
                    return APP_INPUT_CONSUMED;
                }
                return APP_INPUT_PASS;
            }
            if(key == INPUT_PRESS_SHORT)
            {
                /* 确认键：图片 <-> 最近推送的 app 互切 */
                if(m_current_app != APP_ID_IMAGE)
                {
                    app_do_switch(APP_ID_IMAGE);
                    return APP_INPUT_CONSUMED;
                }
                if(app_is_registered(m_last_guest_app))
                {
                    app_do_switch(m_last_guest_app);
                    return APP_INPUT_CONSUMED;
                }
                return APP_INPUT_PASS;   // 尚未推送过任何内容：不消费，避免盲切到空白
            }
        }
    }

    return APP_INPUT_PASS;   // 放行给当前 app
}

static void app_input_dispatch(input_press_type_t key)
{
    if(m_app_queue_hdl == NULL)
    {
        return;
    }

    app_event_t e;
    memset(&e, 0, sizeof(e));
    e.type = APP_EVT_INPUT;
    e.input = key;
    xQueueSend(m_app_queue_hdl, &e, portMAX_DELAY);
}

static void app_timer_callback(TimerHandle_t xTimer)
{
    if(m_app_queue_hdl == NULL)
    {
        return;
    }

    app_event_t e;
    memset(&e, 0, sizeof(e));
    e.type = APP_EVT_TIMER;
    e.input = INPUT_PRESS_NONE;
    /* 定时器回调在 FreeRTOS 定时器任务上下文，非阻塞发送，满则丢弃 */
    xQueueSend(m_app_queue_hdl, &e, 0);
}

static void app_task_handle(void *pvParameters)
{
    app_event_t evt;

    (void)pvParameters;

    for(;;)
    {
        if(xQueueReceive(m_app_queue_hdl, &evt, portMAX_DELAY) == pdPASS)
        {
            app_handle_event(&evt);
        }
    }
}

/*********************************************************************
 * GLOBAL FUNCTIONS
 */

void app_manager_init(void)
{
    if(m_app_queue_hdl != NULL)
    {
        return;   // 已初始化
    }

    m_app_queue_hdl = xQueueCreate( APP_QUEUE_LENGTH, APP_QUEUE_ITEM_SIZE );
    if(m_app_queue_hdl == NULL)
    {
        sys_loge(APP_MANAGER_TAG, "app queue create error!");
        return;
    }

    /* 注册表和开机配置尚未就绪：先占住启动门闸，再订阅事件/启动任务。
       首次显式切换会释放；开机页路径则保持到 BOOT_DONE，避免文件列表事件
       提前启动默认图片 app，与随后建立的 UI 页面并发操作 EPD。 */
    m_boot_page = 1;

    /* 输入回调由 app_init.c 的 app_init_register_inputs() 统一注册，
       经 app_manager_on_input() 转发为 APP_EVT_INPUT 事件。 */

    /* 订阅全局事件总线（通配）：服务层事件统一上浮到 app 任务执行，
       订阅回调只做归一化 + 非阻塞入队，不占用发布方上下文。 */
    if(sys_event_subscribe(SYS_EVT_ANY, app_manager_on_sys_event, NULL) == 0)
    {
        sys_logw(APP_MANAGER_TAG, "sys_event subscribe failed (table full)");
    }

    /* 周期心跳：供时钟/动图推进（图片 app 可不处理） */
    m_app_timer = xTimerCreate("app_timer", pdMS_TO_TICKS(APP_TICK_MS), pdTRUE, NULL, app_timer_callback);
    if(m_app_timer != NULL)
    {
        xTimerStart(m_app_timer, 0);
    }

    if(xTaskCreate(app_task_handle, APP_TASK_NAME, APP_TASK_STACK, NULL, APP_TASK_PRIO, &m_app_task_hdl) != pdPASS)
    {
        sys_loge(APP_MANAGER_TAG, "app task create error!");
    }

    /* 默认 app 为图片（照片墙） */
    m_current_app = APP_ID_IMAGE;
    m_app_running = 0;
    m_menu_sel = 0;
    m_sleep_page = 0;
    m_last_guest_app = APP_ID_MAX;

    /* 解析生效的按键切换模式：FULL 在跑不动主菜单的屏上自动降级为 SIMPLE */
    m_switch_mode = app_switch_effective_mode();
    sys_logi(APP_MANAGER_TAG, "app switch mode cfg=%d effective=%d app_menu=%d",
             (int)SYS_APP_SWITCH_MODE, (int)m_switch_mode, app_render_has_app_menu());
}

bool app_manager_prepare_sleep(void)
{
    if(m_app_queue_hdl == NULL || m_app_task_hdl == NULL) return false;
    if(atomic_load(&m_sleep_stopped)) return true;
    if(!atomic_exchange(&m_sleep_stop_sent, true))
    {
        app_event_t e = {0};
        e.type = APP_EVT_UI_MSG;
        e.cmd = APP_SLEEP_STOP_CMD;
        /* 抢在普通输入/切页前面，当前 app 回调仍须先自然返回。 */
        if(xQueueSendToFront(m_app_queue_hdl, &e, 0) != pdPASS)
        {
            atomic_store(&m_sleep_stop_sent, false);
            return false;
        }
    }
    for(unsigned i = 0; i < 750 && atomic_load(&m_sleep_stop_sent) &&
        !atomic_load(&m_sleep_stopped); i++)
        vTaskDelay(pdMS_TO_TICKS(20));
    return atomic_load(&m_sleep_stopped);
}

void app_manager_register(const app_entry_t *app)
{
    if(app == NULL || app->id >= APP_ID_MAX)
    {
        sys_loge(APP_MANAGER_TAG, "invalid register app id=%d", app ? (int)app->id : -1);
        return;
    }
    m_app_registry[app->id] = app;

    /* 构建参数通道反查表：设置通道与查询通道（param_ch+1）都指向该 app */
    if(app->param_ch != 0)
    {
        m_param_owner[app->param_ch] = app;
        m_param_owner[(uint8_t)(app->param_ch + 1)] = app;
    }

    sys_logi(APP_MANAGER_TAG, "register app id=%d name=%s", (int)app->id, app->name);
}

void app_manager_switch(app_id_t id)
{
    if(id >= APP_ID_MAX)
    {
        return;
    }

    if(m_app_queue_hdl != NULL)
    {
        /* 异步切换：投递 SWITCH 事件，on_exit/on_enter 在 app 任务上下文执行 */
        app_event_t e;
        memset(&e, 0, sizeof(e));
        e.type = APP_EVT_SWITCH;
        e.input = INPUT_PRESS_NONE;
        e.data = (void *)(uintptr_t)id;
        xQueueSend(m_app_queue_hdl, &e, portMAX_DELAY);
    }
    else
    {
        /* 初始化早期队列未就绪：同步切换 */
        app_do_switch(id);
    }
}

void app_manager_on_input(input_press_type_t key)
{
    /* hal_input 回调上下文（中断/任务）统一转发为 APP_EVT_INPUT 事件 */
    app_input_dispatch(key);
}

app_id_t app_manager_get_current(void)
{
    return m_current_app;
}

app_switch_mode_t app_manager_get_switch_mode(void)
{
    return m_switch_mode;
}

int app_manager_is_registered(app_id_t id)
{
    return app_is_registered(id);
}

void app_manager_boot_mark(void)
{
    m_boot_evt_pending = 1;
}

int app_manager_boot_advance_pending(void)
{
    return m_boot_evt_pending ? 1 : 0;
}

static void app_manager_notify_boot(void)
{
    if(m_app_queue_hdl == NULL)
    {
        return;
    }

    /* 启动后仅投递一次：首个 app 据此执行“开机自动”行为（自动切图 / 自动拉取） */
    app_event_t e;
    memset(&e, 0, sizeof(e));
    e.type = APP_EVT_BOOT;
    e.input = INPUT_PRESS_NONE;
    xQueueSend(m_app_queue_hdl, &e, portMAX_DELAY);
}

void app_manager_boot_show(const app_ui_ops_t *ops, app_id_t next)
{
    if(ops == NULL)
    {
        return;
    }

    /* 开机画面结束后去哪个 app 由调用方定（见 app_boot_cfg 的 START APP） */
    m_boot_next_app = next;
    /* 开机画面期间没有任何 app 在运行，APP_EVT_BOOT 无处可送：
       欠着它，等真正切到目标 app 时在 app_do_switch() 末尾补投 */
    app_manager_boot_mark();
    /* 页面 id 用 APP_ID_MAX：开机画面不是 app，不在注册表里也不参与切换 */
    m_boot_page = 1;
    m_boot_ui_shown = 1;
    ui_core_page_enter((uint8_t)APP_ID_MAX, ops);
}

void app_manager_boot_end(void)
{
    if(!m_boot_page)
    {
        return;   // 已在开机画面期间被切走了（如 BLE 远程切换）：不覆盖用户的选择
    }

    /* 门闸由 app_do_switch() 提交目标时释放；SWITCH 尚在队列里时，
       仍不能让先排队的文件列表/按键事件启动默认图片 app。 */
    /* 开机页的 UI 收尾不在这里做：要等 app_do_switch() 过了进入预检、确实要切走时
       才拆（否则切换被 enter_block_reason 拦下时，开机页已被拆、又没有新页面顶上，
       屏上会永远停在开机帧）。见 app_do_switch() 里的 m_boot_ui_shown 分支。 */
    app_manager_switch(m_boot_next_app);
}

void app_manager_sleep_show(const app_ui_ops_t *ops)
{
    if(ops == NULL)
    {
        return;
    }

    /* 与开机画面同理用 APP_ID_MAX：休眠卡不是 app，不在注册表里也不参与切换。
       区别是它不再切回来 —— app_sleep_run() 画完这帧就进 deep sleep。 */
    ui_core_page_enter((uint8_t)APP_ID_MAX, ops);
    m_sleep_page = 1;
}

int app_manager_post_ui_msg(uint32_t cmd, const void *data, uint8_t len)
{
    app_event_t e;

    if(m_app_queue_hdl == NULL || cmd == 0)
    {
        return -1;
    }
    if(len > APP_EVENT_PAYLOAD_MAX)
    {
        sys_logw(APP_MANAGER_TAG, "ui msg cmd=%u len=%u exceeds %u, truncate",
                 (unsigned)cmd, (unsigned)len, (unsigned)APP_EVENT_PAYLOAD_MAX);
        len = APP_EVENT_PAYLOAD_MAX;
    }

    memset(&e, 0, sizeof(e));
    e.type  = APP_EVT_UI_MSG;
    e.input = INPUT_PRESS_NONE;
    e.cmd   = cmd;
    e.len   = len;
    if(len > 0 && data != NULL)
    {
        memcpy(e.payload, data, len);
    }

    /* 非阻塞：调用方是 ui_task，阻塞会让整个 UI 卡住 */
    if(xQueueSend(m_app_queue_hdl, &e, 0) != pdPASS)
    {
        sys_logw(APP_MANAGER_TAG, "ui msg cmd=%u dropped (queue full)", (unsigned)cmd);
        return -1;
    }
    return 0;
}

/**
 * @brief 保存指定 app 的状态到 NVS（无状态声明时为 no-op）
 */
static int app_state_save_of(const app_entry_t *app)
{
    if(app == NULL || app->state == NULL || app->state_size == 0)
    {
        return 0;   // 该 app 不持久化，no-op
    }
    return service_param_app_save((uint8_t)app->id, app->state, app->state_size, app->state_ver);
}

int app_state_save(void)
{
    return app_state_save_of(m_app_registry[m_current_app]);
}

/**
 * @brief 从 NVS 载入指定 app 的状态；无数据/校验失败则套用默认值
 *
 * 无论走哪条路径，完成后该 app 的 RAM 状态均为有效值，故置 m_state_loaded 标记。
 */
static int app_state_load_of(const app_entry_t *app)
{
    if(app == NULL || app->state == NULL || app->state_size == 0)
    {
        return 0;   // 该 app 不持久化，no-op
    }

    if(app->id >= 0 && app->id < APP_ID_MAX)
    {
        m_state_loaded[app->id] = 1;
    }

    if(service_param_app_load((uint8_t)app->id, app->state, app->state_size, app->state_ver) == 0)
    {
        return 0;
    }

    /* 无数据 / 头部校验失败 / 版本不符：静默回落到默认值（app 状态非关键数据） */
    if(app->state_default != NULL)
    {
        memcpy(app->state, app->state_default, app->state_size);
    }
    return -1;
}

int app_state_load(void)
{
    return app_state_load_of(m_app_registry[m_current_app]);
}
