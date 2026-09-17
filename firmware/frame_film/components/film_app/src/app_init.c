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
 * FileName : /film_app/src/app_init.c
 * Author: Kiritro  Version: v0.1  Date: 2026/9/9
 * Description: App 层统一入口
 * ChangeLog: Change Notes
 *
 *********************************************************************/


/*********************************************************************
 * INCLUDES
 */
#include "sys_log.h"
#include "hal_epd.h"
#include "service_ble.h"
#include "service_param.h"
#include "app_init.h"
#include "app_manager.h"
#include "app_render.h"
#include "app_interface.h"
#include "app_image.h"
#include "app_template.h"
#include "app_clock.h"
#include "app_animation.h"

/*********************************************************************
 * MACROS
 */
#define APP_INIT_TAG    "app_init"

/*********************************************************************
 * LOCAL FUNCTIONS
 */
static void app_init_input_short(void);
static void app_init_input_long(void);
static void app_init_input_up(void);
static void app_init_input_down(void);
static void app_init_register_inputs(void);
static uint8_t app_init_ble_app_id_get(void);

/*********************************************************************
 * GLOBAL FUNCTIONS
 */

void film_app_init(void)
{
    sys_logi(APP_INIT_TAG, "app layer init start");

    /* 1. 创建 app 任务 + 事件队列 + 周期 tick 心跳定时器 */
    app_manager_init();

    /* 2. 注册 app：图片/模板为通用底座（模板承接蓝牙/WiFi 的任意实时推送内容）；
          仅全功能模式额外注册时钟/动图，简易/关闭模式省下这部分 flash 与运行开销。
          注意按“配置模式”而非“生效模式”判定：FULL 在非 3.7 屏上虽降级为简易交互，
          仍保留 4 个 app 的注册，使连接端远程切换（BLE 0x43）行为不变。 */
    app_manager_register(&g_app_image_entry);
    app_manager_register(&g_app_template_entry);
#if (SYS_APP_SWITCH_MODE == SYS_APP_SWITCH_FULL)
    app_manager_register(&g_app_clock_entry);
    app_manager_register(&g_app_animation_entry);
#endif

    /* 3. 切换交互形态日志（cfg 与生效值可能不同：FULL 在无封面菜单的屏上降级为 SIMPLE） */
    sys_logi(APP_INIT_TAG, "app switch cfg=%d effective=%d cover_menu=%d panel=%#x",
             (int)SYS_APP_SWITCH_MODE, (int)app_manager_get_switch_mode(),
             app_render_has_cover_menu(), (unsigned)EPD_PANEL_ID);

    /* 4. 注册 BLE→app 查询回调（切换/保存完成等上行事件已走 sys_event 总线） */
    service_ble_set_app_id_get_cb(app_init_ble_app_id_get);

    /* 5. 注册输入回调并统一转发到 app_manager */
    app_init_register_inputs();

    /* 6. 恢复上次运行的 app：被当前模式裁剪掉的 app（简易模式下无 clock/动图）、
          首次开机（无记录）或记录非法 → 回落图片（照片墙） */
    int last = service_param_app_current_get();
    if(last < 0 || last >= APP_ID_MAX || !app_manager_is_registered((app_id_t)last))
    {
        last = APP_ID_IMAGE;
    }
    app_manager_switch((app_id_t)last);

    /* 7. 投递一次 BOOT 事件：首个 app 据此执行“开机自动”行为（自动切图 / 自动拉取） */
    app_manager_notify_boot();

    sys_logi(APP_INIT_TAG, "app layer init done");
}

/*********************************************************************
 * LOCAL FUNCTIONS
 */

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

static void app_init_register_inputs(void)
{
    /* hal_input 支持同一类型多回调注册；这里回调无参，按类型固定转发 key */
    hal_input_register_cb(INPUT_PRESS_SHORT, app_init_input_short);
    hal_input_register_cb(INPUT_PRESS_LONG,  app_init_input_long);
    hal_input_register_cb(INPUT_PRESS_UP,    app_init_input_up);
    hal_input_register_cb(INPUT_PRESS_DOWN,  app_init_input_down);
}

/**
 * @brief 当前 app 查询回调（APP_CURRENT_GET 回包用）
 */
static uint8_t app_init_ble_app_id_get(void)
{
    return (uint8_t)app_manager_get_current();
}
