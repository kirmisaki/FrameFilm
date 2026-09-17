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
 * FileName : /film_app/src/app_manager.c
 * Author: Kiritro  Version: v0.1  Date: 2026/9/9
 * Description: App 层调度器：注册/切换/事件路由/输入转发/切换状态机
 * ChangeLog: Change Notes
 *
 *********************************************************************/


/*********************************************************************
 * INCLUDES
 */
#include <string.h>

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

/*********************************************************************
 * MACROS
 */
#define APP_MANAGER_TAG         "app_manager"

#define APP_QUEUE_LENGTH        (16)
#define APP_QUEUE_ITEM_SIZE     sizeof(app_event_t)
#define APP_TASK_PRIO           (5)
#define APP_TASK_STACK          (4096)
#define APP_TASK_NAME           "app_task"

#define APP_SWITCH_TIMEOUT_MS   (10000)  // 切换菜单无输入超时，回 RUN
#define APP_TICK_MS             (100)    // 周期 on_tick 心跳（时钟 1s 去重、动图叠加 frame 推进）

#define APP_PARAM_GET_BUF_MAX   (60)     // 参数查询回包缓冲区（与 BLE 回包上限对齐）

/*********************************************************************
 * TYPEDEFS
 */
/**
 * @brief 切换状态机
 */
typedef enum {
    APP_SWITCH_RUN = 0,      // 正常运行态
    APP_SWITCH_SELECT,       // 封面菜单态（FULL 模式专用）
} app_switch_state_t;

/**
 * @brief 输入路由结果（app_manager 内部使用）
 */
typedef enum {
    APP_INPUT_CONSUMED = 0,  // 输入已由调度器/切换菜单消费，不再下发
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
static app_switch_state_t m_switch_state = APP_SWITCH_RUN;
static app_id_t m_switch_highlight = APP_ID_IMAGE;
static uint32_t m_tick_acc_ms = 0;         // 当前 app 的 on_tick 累计时长（按 tick_ms 分频）
static TickType_t m_switch_last_tick = 0;  // 切换菜单最近一次输入时刻（无操作超时判定）
static app_switch_mode_t m_switch_mode = APP_SWITCH_MODE_NONE;  // 生效模式（init 时解析）
static app_id_t m_last_guest_app = APP_ID_MAX;  // 最近一次切入的非图片 app（简易模式确认键切换目标）

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
static int app_param_ch_route(uint8_t ch, const uint8_t *data, uint8_t len);
static void app_switch_restore(void);
static app_input_result_t app_manager_process_input(input_press_type_t key);
static void app_input_dispatch(input_press_type_t key);
static int app_state_save_of(const app_entry_t *app);
static int app_state_load_of(const app_entry_t *app);

/*********************************************************************
 * LOCAL HELPERS
 */

/**
 * @brief 确保当前 app 已进入（on_enter）
 */
static void app_ensure_running(void)
{
    if(!m_app_running)
    {
        const app_entry_t *app = m_app_registry[m_current_app];
        if(app && app->on_enter)
        {
            app->on_enter();
        }
        m_app_running = 1;
    }
}

/**
 * @brief 绘制切换菜单中某个 app 的封面（名称来自注册表，避免 id→名 硬编码映射）
 */
static void app_render_menu_show(app_id_t id)
{
    const app_entry_t *app = (id < APP_ID_MAX) ? m_app_registry[id] : NULL;
    app_render_switch_menu(app ? app->name : NULL);
}

/**
 * @brief 解析生效的按键切换模式
 *
 * FULL 需要屏幕具备封面菜单能力（整屏封面 + MonoFast 快刷），否则自动降级为
 * SIMPLE，避免 sys_cfg.h 的配置与屏幕宏不一致时切换功能整体失效。
 */
static app_switch_mode_t app_switch_effective_mode(void)
{
    if(SYS_APP_SWITCH_MODE == SYS_APP_SWITCH_NONE)
    {
        return APP_SWITCH_MODE_NONE;
    }
    if(SYS_APP_SWITCH_MODE == SYS_APP_SWITCH_FULL)
    {
        return app_render_has_cover_menu() ? APP_SWITCH_MODE_FULL : APP_SWITCH_MODE_SIMPLE;
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
 * @brief 在“已注册 app”集合内循环取相邻项
 *
 * @param from 起点
 * @param step +1 下一项，-1 上一项
 * @return 相邻的已注册 app；若集合中只有自身则原样返回
 */
static app_id_t app_cycle_registered(app_id_t from, int step)
{
    app_id_t id = from;
    for(int i = 0; i < APP_ID_MAX; i++)
    {
        id = (app_id_t)((id + APP_ID_MAX + step) % APP_ID_MAX);
        if(m_app_registry[id] != NULL)
        {
            return id;
        }
    }
    return from;
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
    }
}

/**
 * @brief BLE app 参数通道路由（SET/GET）
 *
 * 按 ch 查归属 app，与“当前 app”无关：手机可在显示图片时预设动图参数。
 * SET 只调 on_param_set（app 仅改自己的结构体，不刷屏、不落盘），
 * 落盘由本函数按归属 app 完成（见下方 app_state_save_of）；
 * GET 调 on_param_get 组帧后经 service_ble_send_resp 回发。
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
        uint8_t n = (owner->on_param_get != NULL) ? owner->on_param_get(out, sizeof(out)) : 0;
        service_ble_send_resp(ch, out, n);
    }
    return 1;
}

static void app_handle_event(const app_event_t *e)
{
    /* BLE app 参数通道：先于菜单态闸门处理，保证目标 app 即便不是当前 app 也能收到 */
    if(e->type == APP_EVT_SYS && (uint16_t)e->cmd == SYS_EVT_BLE_APP_CMD && e->len >= 1)
    {
        if(app_param_ch_route(e->payload[0], &e->payload[1], (uint8_t)(e->len - 1)))
        {
            return;
        }
    }

    /* 切换菜单态：仅输入事件参与交互，周期心跳与总线事件挂起，
       避免时钟/动图/推送内容刷新覆盖菜单封面。
       心跳（100ms）比菜单超时（10s）频繁得多，故超时判定放在心跳里做。 */
    if(m_switch_state == APP_SWITCH_SELECT && e->type != APP_EVT_INPUT)
    {
        if(e->type == APP_EVT_TIMER &&
           (xTaskGetTickCount() - m_switch_last_tick) >= pdMS_TO_TICKS(APP_SWITCH_TIMEOUT_MS))
        {
            app_switch_restore();
        }
        return;
    }

    /* 切换事件：由调度器消费 */
    if(e->type == APP_EVT_SWITCH)
    {
        app_id_t target = (app_id_t)(uintptr_t)e->data;
        app_do_switch(target);
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

    /* 输入事件：切换菜单态先由 app_manager 接管 */
    if(e->type == APP_EVT_INPUT)
    {
        if(app_manager_process_input(e->input) == APP_INPUT_CONSUMED)
        {
            return;
        }
    }

    /* 全局总线事件：统一走 APP_EVT_SYS，避免兼容路径绕过 app_entry.events 过滤 */
    if(e->type == APP_EVT_SYS)
    {
        /* 调度器级：BLE 请求切换 app（payload: [0]=命令通道，[1]=目标 app id） */
        if((uint16_t)e->cmd == SYS_EVT_BLE_APP_CMD)
        {
            if(e->len >= 2 && e->payload[0] == BLE_FILM_TRANS_CH_CTRL_APP_SWITCH)
            {
                app_do_switch((app_id_t)e->payload[1]);
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

    /* 进入当前 app 后转发给它的 on_event */
    app_ensure_running();
    const app_entry_t *app = m_app_registry[m_current_app];
    if(app && app->on_event)
    {
        app->on_event(e);
    }
}

static void app_do_switch(app_id_t id)
{
    /* 未注册的 app 不可切换：BLE 传来的非法 id 或按模式裁剪掉的 app 会走到这里，
       若无此守卫会切到空注册项 → 无 on_enter → 屏幕停在上一帧，看起来像卡死 */
    if(!app_is_registered(id))
    {
        sys_logw(APP_MANAGER_TAG, "switch ignored: app id=%d not registered", (int)id);
        return;
    }
    if(id == m_current_app && m_app_running)
    {
        return;   // 已是当前运行 app
    }

    /* 记录最近一次切入的非图片 app：简易模式下确认键在该 app 与图片之间互切 */
    if(id != APP_ID_IMAGE)
    {
        m_last_guest_app = id;
    }

    const app_entry_t *old = m_app_registry[m_current_app];
    if(old && old->on_exit)
    {
        old->on_exit();
    }
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

    /* 数据源切换：按 app 声明的目录同步等待文件列表刷新，
       on_enter 即可安全读取列表（避免异步刷新导致的空列表误判）。 */
    const app_entry_t *app = m_app_registry[id];
    if(app && app->data_dir != NULL)
    {
        uint32_t count = service_file_set_dir_sync(app->data_dir);
        sys_logi(APP_MANAGER_TAG, "switch data dir=%s count=%u", app->data_dir, (unsigned)count);
    }

    app_state_load();   // ② 切入先载入状态，再 on_enter
    app_ensure_running();
    service_param_app_current_set((uint8_t)id);   // ③ 记录当前 app，供下次启动恢复
}

/**
 * @brief 退出切换菜单：回 RUN 并重绘当前 app，恢复完整画面
 * @note 先清 running 标志，否则 app_do_switch 会因“已是当前 app”而早退，
 *       导致菜单残影无法被 app 画面覆盖。
 */
static void app_switch_restore(void)
{
    m_switch_state = APP_SWITCH_RUN;
    m_app_running = 0;
    app_do_switch(m_current_app);
}

static app_input_result_t app_manager_process_input(input_press_type_t key)
{
    if(key == INPUT_PRESS_NONE || m_switch_mode == APP_SWITCH_MODE_NONE)
    {
        return APP_INPUT_PASS;   // 未开启按键切换：全部按键放行给当前 app（BLE 远程切换不受影响）
    }

    /* 封面菜单态（FULL）：由 app_manager 接管，不透明，绝不透传 */
    if(m_switch_state == APP_SWITCH_SELECT)
    {
        switch(key)
        {
        case INPUT_PRESS_UP:   // 下一项（只在已注册 app 间循环）
            m_switch_highlight = app_cycle_registered(m_switch_highlight, 1);
            m_switch_last_tick = xTaskGetTickCount();
            app_render_menu_show(m_switch_highlight);
            break;
        case INPUT_PRESS_DOWN: // 上一项
            m_switch_highlight = app_cycle_registered(m_switch_highlight, -1);
            m_switch_last_tick = xTaskGetTickCount();
            app_render_menu_show(m_switch_highlight);
            break;
        case INPUT_PRESS_SHORT: // 确认切换
            app_do_switch(m_switch_highlight);
            m_switch_state = APP_SWITCH_RUN;
            break;
        default:
            break;
        }
        return APP_INPUT_CONSUMED;
    }

    /* FULL：长按确认键进入封面菜单 */
    if(m_switch_mode == APP_SWITCH_MODE_FULL && key == INPUT_PRESS_LONG)
    {
        m_switch_state = APP_SWITCH_SELECT;
        m_switch_highlight = m_current_app;
        m_switch_last_tick = xTaskGetTickCount();
        app_render_menu_show(m_switch_highlight);
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
    for(;;)
    {
        app_event_t evt;

        /* 切换菜单态带超时：无输入一段时间后自动回 RUN（取消切换） */
        TickType_t timeout = portMAX_DELAY;
        if(m_switch_state == APP_SWITCH_SELECT)
        {
            timeout = pdMS_TO_TICKS(APP_SWITCH_TIMEOUT_MS);
        }

        if(xQueueReceive(m_app_queue_hdl, &evt, timeout) == pdPASS)
        {
            app_handle_event(&evt);
        }
        else
        {
            /* 心跳定时器未生效时的兜底：队列等待超时同样退出切换菜单 */
            app_switch_restore();
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
    m_switch_state = APP_SWITCH_RUN;
    m_last_guest_app = APP_ID_MAX;

    /* 解析生效的按键切换模式：FULL 在无封面菜单能力的屏上自动降级为 SIMPLE */
    m_switch_mode = app_switch_effective_mode();
    sys_logi(APP_MANAGER_TAG, "app switch mode cfg=%d effective=%d cover_menu=%d",
             (int)SYS_APP_SWITCH_MODE, (int)m_switch_mode, app_render_has_cover_menu());
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

void app_manager_notify_boot(void)
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
