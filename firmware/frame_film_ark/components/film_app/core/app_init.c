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
 * FileName : /film_app/core/app_init.c
 * Author: Kiritro  Version: v0.1  Date: 2026/9/9
 * Description: App 层统一入口
 * ChangeLog: Change Notes
 *
 *********************************************************************/


/*********************************************************************
 * INCLUDES
 */
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"

#include "esp_heap_caps.h"

#include "sys_log.h"
#include "sys_cfg.h"
#include "hal_epd.h"
#include "hal_sd.h"
#include "hal_pwr.h"
#include "hal_nfc.h"
#include "service_ble.h"
#include "service_monitor.h"
#include "ui_conf.h"    /* UI_CALIB_FRAME：标定帧模式下开机页暂停自动前进 */
#include "app_init.h"
#include "app_manager.h"
#include "app_render.h"
#include "app_interface.h"
#include "app_image.h"
#include "app_template.h"
#include "app_clock.h"
#include "app_animation.h"
#include "app_menu.h"
#include "app_settings.h"
#include "app_pass.h"
#if (SYS_NFC_ENABLE == 1)
#include "app_wallet.h"
#endif
#include "app_boot.h"
#include "app_boot_cfg.h"

/*********************************************************************
 * MACROS
 */
#define APP_INIT_TAG    "app_init"

/*********************************************************************
 * LOCAL VARIABLES
 */
static TimerHandle_t m_boot_timer = NULL;   // 开机页"到点进主菜单"的单次定时器

/*********************************************************************
 * LOCAL FUNCTIONS
 */
static void app_init_input_short(void);
static void app_init_input_long(void);
static void app_init_input_up(void);
static void app_init_input_down(void);
static void app_init_input_double(void);
#if CONFIG_ARK_SCREENSHOT
static void app_init_input_back_double(void);
#endif
#if (SYS_NFC_ENABLE == 1)
static void app_init_input_back(void);
#endif
static void app_init_register_inputs(void);
static uint8_t app_init_ble_app_id_get(void);
static void app_init_boot_start(app_id_t next);
#if (UI_CALIB_FRAME == 0)
static void app_init_boot_fallback(TimerHandle_t xTimer);
#endif

/*********************************************************************
 * GLOBAL FUNCTIONS
 */

void film_app_init(void)
{
    sys_logi(APP_INIT_TAG, "app layer init start");

    /* 启动内存快照：PSRAM 的分片/余量直接决定能否加载大 film
       （8bpp v2 film 需 345KB 连续，4bpp v1 需 172KB） */
    sys_logi(APP_INIT_TAG, "heap: psram total=%u free=%u largest=%u | internal total=%u free=%u largest=%u",
             (unsigned)heap_caps_get_total_size(MALLOC_CAP_SPIRAM),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM),
             (unsigned)heap_caps_get_total_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));

    /* 1. 创建 app 任务 + 事件队列 + 周期 tick 心跳定时器 */
    app_manager_init();

    /* 2. UI 框架层（LVGL）：必须在任何 app 切换之前就绪，
          否则首个 UI 层 app 的 page_enter 会因队列未建而被丢弃。
          面板不支持 mono 时内部直接返回错误，UI 层保持不可用。 */
    if(ui_core_init() != 0)
    {
        sys_logw(APP_INIT_TAG, "ui core init failed, ui layer unavailable");
    }
    service_monitor_set_sleep_prepare_cb(app_manager_prepare_sleep);

    /* 3. 注册 app：图片/模板为通用底座（模板承接蓝牙/WiFi 的任意实时推送内容）；
          仅全功能模式额外注册其余 app，简易/关闭模式省下这部分 flash 与运行开销。
          注意按“配置模式”而非“生效模式”判定：FULL 在跑不动主菜单的屏上虽降级为
          简易交互，仍保留 4 个内容 app 的注册，使连接端远程切换（BLE 0x4B）行为不变。 */
    app_manager_register(&g_app_image_entry);
    app_manager_register(&g_app_template_entry);
#if (SYS_APP_SWITCH_MODE == SYS_APP_SWITCH_FULL)
    app_manager_register(&g_app_animation_entry);   // 直绘层，不依赖 UI 框架
#if (SYS_UI_ENABLE == 1)
    /* 以下三个是 UI 层页面：UI 层没编进来（SYS_UI_ENABLE=0）时无从渲染，故不注册。
       主菜单是调度器的“根”状态（上电后落在它上面），设置页只能经主菜单进入 */
    app_manager_register(&g_app_clock_entry);
    app_manager_register(&g_app_menu_entry);
    app_manager_register(&g_app_settings_entry);
    app_manager_register(&g_app_pass_entry);    // 通行证占位页（菜单第 2 项，功能待开发）
#if (SYS_NFC_ENABLE == 1)
    app_manager_register(&g_app_wallet_entry);  // 卡包（仅 n16r8）
#endif
#endif
#endif

    /* 4. 切换交互形态日志（cfg 与生效值可能不同：FULL 在跑不动主菜单的屏上降级为 SIMPLE） */
    sys_logi(APP_INIT_TAG, "app switch cfg=%d effective=%d app_menu=%d panel=%#x",
             (int)SYS_APP_SWITCH_MODE, (int)app_manager_get_switch_mode(),
             app_render_has_app_menu(), (unsigned)EPD_PANEL_ID);

    /* 5. 注册 BLE→app 查询回调（切换/保存完成等上行事件已走 sys_event 总线） */
    service_ble_set_app_id_get_cb(app_init_ble_app_id_get);

    /* 6. 注册输入回调并统一转发到 app_manager */
    app_init_register_inputs();

    /* 6.5 载入开机行为配置（app7 槽位）：必须在下面做上电决策之前。
          放这里而不是更早：它要读 NVS（service 层已就绪）并查 app 注册表（上面刚注册完）。 */
    app_boot_cfg_init();

    /* 6.6 载入设置页持久化参数（自动休眠超时）：必须在首个自动休眠判断之前生效 */
    app_settings_init();

    /* 7. 上电流程：由 BOOT PAGE / START APP 两个参数决定（见 app_boot_cfg）
          · 显示开机画面：还需 FULL + UI 层就绪 + 菜单已注册 —— 否则连页面都建不出来，
            此时即便配了"显示"也只能跳过
          · 自检结束后落到 START APP 指定的 app（默认主菜单，与加参数前一致）
          · 跳过开机画面：直接落到 START APP 指定的 app
          START APP 里被当前模式裁剪掉的 app（简易模式没有菜单、没编 UI 就没有设置页）
          由 app_boot_cfg_resolve_target() 统一回落图片（照片墙） */
    {
#if (SYS_NFC_ENABLE == 1) && (HAL_NFC_WAKE_ENABLE == 1)
        /* NFC 贴卡唤醒：跳过开机画面，直接进卡包；模拟在 enter_block_reason 里恢复。 */
        if(hal_pwr_get_wakeup_cause() == HAL_PWR_WAKE_NFC &&
           app_manager_is_registered(APP_ID_WALLET))
        {
            sys_logi(APP_INIT_TAG, "NFC wake -> wallet");
            app_manager_switch(APP_ID_WALLET);
            return;
        }
#endif
        app_id_t target = app_boot_cfg_resolve_target();

        if(app_boot_cfg_get()->boot_page == APP_BOOT_PAGE_SHOW
           && app_manager_get_switch_mode() == APP_SWITCH_MODE_FULL
           && ui_core_is_ready()
           && app_manager_is_registered(APP_ID_MENU))
        {
            app_init_boot_start(target);
            sys_logi(APP_INIT_TAG, "app layer init done (boot page -> app %d)", (int)target);
            return;
        }

        /* 8. 首个 app 的 APP_EVT_BOOT（开机自动切图 / 拉取）由 app_do_switch()
              在启动后补投：这里只标记，开机画面路径走的是同一处投递
              （见 app_manager_boot_mark），两条路径时序保持一致 */
        app_manager_boot_mark();
        app_manager_switch(target);
        sys_logi(APP_INIT_TAG, "app layer init done (direct -> app %d)", (int)target);
    }
}

/*********************************************************************
 * LOCAL FUNCTIONS
 */

/**
 * @brief 展示开机画面，并挂一个兜底定时器
 *
 * 开机画面是本次上电的首帧 mono，顺带完成整屏清场（实测约 3s），故这条路径
 * 不额外多付一次闪屏。遥测行取真实值，全部只读、无副作用。
 *
 * 进度条与切页时机都**不在这里驱动**：页面内的 lv_timer 一格一格推进，
 * 走满并停留后上报 APP_UI_REQ_BOOT_DONE，调度器据此切到 next 指定的 app。早先按"外部投
 * 步骤消息"驱动时，首帧 flush 会阻塞 ui_task 约 3.3s，期间投递的步骤全堆在
 * 队列里被一次性消费，进度条看起来完全不动；用一个固定延时去猜切页时机，
 * 也会因为这段阻塞而落在进度条走到一半的位置。
 */
static void app_init_boot_start(app_id_t next)
{
    static app_boot_tele_t s_tele[4];
    static char s_val[4][24];
    uint16_t panel_w = (uint16_t)EPD_WIDTH;
    uint16_t panel_h = (uint16_t)EPD_HEIGHT;

    /* 取值先收敛到窄类型再格式化：GCC 按 int 全域推演会判为可能截断
       （-Wformat-truncation 在本工程是错误级） */
    snprintf(s_val[0], sizeof(s_val[0]), "%ux%u E6", (unsigned)panel_w, (unsigned)panel_h);
    snprintf(s_val[1], sizeof(s_val[1]), "%u KB FREE",
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
    snprintf(s_val[2], sizeof(s_val[2]), "%s",
             (hal_sd_get_status() == SD_MOUNT) ? "SD MOUNTED" : "NO SD");
    snprintf(s_val[3], sizeof(s_val[3]), "%s", SYS_FIRMWARE_VERSION);

    s_tele[0].name = "PANEL";    s_tele[0].value = s_val[0];
    s_tele[1].name = "PSRAM";    s_tele[1].value = s_val[1];
    s_tele[2].name = "STORAGE";  s_tele[2].value = s_val[2];
    s_tele[3].name = "FIRMWARE"; s_tele[3].value = s_val[3];
    app_boot_set_telemetry(s_tele, 4);

    /* 交给调度器托管（按键在此期间被丢弃），页面在 ui_task 异步建立；
       next 决定自检结束后落到哪个 app */
    app_manager_boot_show(app_boot_ops(), next);

#if (UI_CALIB_FRAME == 1)
    /* 标定帧模式：停在开机页不自动前进（短按确认键继续，见 ui_conf.h） */
    sys_logi(APP_INIT_TAG, "calib frame: hold boot page, press ENTER to continue");
    (void)m_boot_timer;
#else
    /* 单次定时器只是兜底：正常路径由开机页走完进度后上报（见 app_boot.h） */
    m_boot_timer = xTimerCreate("boot_fallback", pdMS_TO_TICKS(APP_BOOT_FALLBACK_MS), pdFALSE, NULL,
                                app_init_boot_fallback);
    if(m_boot_timer != NULL)
    {
        xTimerStart(m_boot_timer, 0);
    }
    else
    {
        /* 定时器创建失败（内存不足）：直接进主菜单，别把用户卡在开机页 */
        sys_loge(APP_INIT_TAG, "boot timer create failed, jump to menu");
        app_manager_boot_end();
    }
#endif
}

#if (UI_CALIB_FRAME == 0)
/**
 * @brief 兜底：页面没能上报进度完成时，到点也切到主菜单
 *
 * 定时器任务上下文：只投递一条与页面同款的上报消息，不在此直接切页。
 * `app_manager_boot_end()` 会走 app_do_switch（SD 目录刷新、NVS 落盘等），
 * 那些开销不该出现在 Timer 服务任务里；投递后由 app_task 消费并切页。
 */
static void app_init_boot_fallback(TimerHandle_t xTimer)
{
    (void)xTimer;
    sys_logw(APP_INIT_TAG, "boot page did not report done, fallback to menu");
    (void)app_manager_post_ui_msg(APP_UI_REQ_BOOT_DONE, NULL, 0);
}
#endif

static void app_init_input_short(void)
{
    app_manager_on_input(INPUT_PRESS_SHORT);
}

static void app_init_input_long(void)
{
    app_manager_on_input(INPUT_PRESS_LONG);
}

static void app_init_input_up(void)
{
    app_manager_on_input(INPUT_PRESS_UP);
}

static void app_init_input_down(void)
{
    app_manager_on_input(INPUT_PRESS_DOWN);
}

static void app_init_input_double(void)
{
    app_manager_on_input(INPUT_PRESS_DOUBLE);
}

#if CONFIG_ARK_SCREENSHOT
static void app_init_input_back_double(void)
{
    app_manager_on_input(INPUT_PRESS_BACK_DOUBLE);
}
#endif

#if (SYS_NFC_ENABLE == 1)
static void app_init_input_back(void)
{
    app_manager_on_input(INPUT_PRESS_BACK);
}
#endif

static void app_init_register_inputs(void)
{
    /* hal_input 支持同一类型多回调注册；这里回调无参，按类型固定转发 key */
    hal_input_register_cb(INPUT_PRESS_SHORT, app_init_input_short);
    hal_input_register_cb(INPUT_PRESS_LONG,  app_init_input_long);
    hal_input_register_cb(INPUT_PRESS_UP,    app_init_input_up);
    hal_input_register_cb(INPUT_PRESS_DOWN,  app_init_input_down);
    hal_input_register_cb(INPUT_PRESS_DOUBLE, app_init_input_double);   // 确认键双击：退出 app
#if CONFIG_ARK_SCREENSHOT
    hal_input_register_cb(INPUT_PRESS_BACK_DOUBLE, app_init_input_back_double); // 返回键双击：截图
#endif
#if (SYS_NFC_ENABLE == 1)
    hal_input_register_cb(INPUT_PRESS_BACK, app_init_input_back);       // n16r8 GPIO18 返回
#endif
}

/**
 * @brief 当前 app 查询回调（APP_CURRENT_GET 回包用）
 */
static uint8_t app_init_ble_app_id_get(void)
{
    return (uint8_t)app_manager_get_current();
}
