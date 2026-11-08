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
 * FileName : /film_app/pages/app_sleep.c
 * Author: Kiritro  Version: v0.1  Date: 2026/9/19
 * Description: 休眠卡（UI 页）：徽章 + STANDBY + 唤醒说明，画完即进 deep sleep
 * ChangeLog: Change Notes
 *
 *********************************************************************/

/*********************************************************************
 * INCLUDES
 */
#include <stdio.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "sys_log.h"
#include "hal_pwr.h"
#include "service_param.h"
#include "service_monitor.h"

#include "ui_core.h"
#include "ui_assets.h"
#include "ui_fonts.h"
#include "app_typography.h"
#include "ui_ops.h"
#include "app_shell.h"
#include "app_language.h"
#include "app_manager.h"
#include "app_sleep.h"

/*********************************************************************
 * MACROS
 */
#define APP_SLEEP_TAG       "app_sleep"

/* ---- 版式 ----
 * 整屏 480x720，**不带外壳**（没有状态栏与提示行），故坐标就是屏幕坐标。
 * 与设计稿 tools/ui-mockup §08 的表格逐行对应；构图块高 448、垂直居中于 y=136。
 *
 * logo + 彩蛋使用 APP_SHELL_BRAND_H；底部状态与唤醒说明保持原位置。 */
#define SP_BADGE_Y          (104)    // logo 与彩蛋顶部（440x304，水平居中）
#define SP_RULE1_Y          (452)    // 徽章下 44px
#define SP_WORD_Y           (473)    // 细线下 20px
#define SP_RULE2_Y          (551)    // 状态字下 20px
#define SP_WAKE_Y           (574)    // 细线下 22px
#define SP_FOOT_FROM_BOTTOM (50)     // 页脚距屏幕底边（不参与居中构图）
#define SP_RULE_W           (400)    // 细线宽（左右各留 40）

/* 字距。LVGL 对每个字形都补一次字距（含最后一个），于是 label 的宽度比可见文字
 * 多出一个字距，居中时会整体偏左半个字距 —— 用 align 的正向偏移补回来。 */
#define SP_WORD_LS          (8)
#define SP_MICRO_LS         (2)

/* 页脚文案缓冲。长度写成编译期常量而不是传 sizeof 进来：
 * -Wformat-truncation 只在 snprintf 的界是已知常量时才能判定"不会截断"，
 * 传一个 size_t 参数进来它就分析不了（本工程把该警告当错误）。 */
#define SP_FOOT_BUF_LEN     (32)

/* 入睡前的地板时间：切到休眠卡后先等这么久，再允许断电。
 *
 * 为什么不能"刷完就走"：ui_core_page_enter() 是**异步**的，没有回调告诉你
 * "第一帧已上屏"，只能按时间兜。而且换页那一帧不一定只有一次全刷
 * （新页面建好后 LVGL 可能不止一轮 invalidate → 多次全帧），单帧 ~940ms，
 * 一次换页实测能吃掉 2s 出头 —— 早先取 2s 就会在第二刷没走完时断电，
 * 表现就是"卡还没刷出来设备就睡了"（EPD 在外设供电轨上，断电即停在半途）。
 * 4s 对最坏两次全刷留了约 2 倍余量。 */
#define SP_DRAW_WAIT_MS     (4000)

/* 除地板时间外，还要等唤醒条件解除（ext0 电平触发，长按期间它一直是成立的）。
 * 这条只会让入睡**更晚**、不会更早，所以与地板时间是叠加关系而非二选一。 */
#define SP_RELEASE_MAX_MS   (15000)  // app 阻塞等待上限；超时后由 monitor 继续等松手
#define SP_POLL_MS          (50)

/*********************************************************************
 * LOCAL VARIABLES
 */
/* 定时唤醒信息（app 任务侧在展示前填好） */
static uint8_t  m_auto_on = 0;
static uint16_t m_minutes = 0;

/*********************************************************************
 * LOCAL FUNCTIONS
 */
static void sleep_ui_create(lv_obj_t *root);
static void sleep_ui_destroy(void);
static void sleep_rule(lv_obj_t *root, int32_t y);
static void sleep_foot_text(char *buf);
static void sleep_wait_ready(uint8_t wait_frame);

/*********************************************************************
 * GLOBAL VARIABLES
 */
/**
 * @brief UI 页面契约
 *
 * 本页无交互：睡眠期间没有按键语义（按确认键是硬件唤醒，不走软件）。
 */
static const app_ui_ops_t g_sleep_ui_ops = {
    .create  = sleep_ui_create,
    .destroy = sleep_ui_destroy,
    .on_msg  = NULL,
    .on_key  = NULL,
};

/*********************************************************************
 * LOCAL FUNCTIONS
 */

/**
 * @brief 通栏细线（居中、定宽）
 */
static void sleep_rule(lv_obj_t *root, int32_t y)
{
    lv_obj_t *o = lv_obj_create(root);

    lv_obj_remove_style_all(o);
    lv_obj_set_scrollable(o, false);
    lv_obj_set_size(o, SP_RULE_W, 1);
    lv_obj_align(o, LV_ALIGN_TOP_MID, 0, y);
    lv_obj_set_style_bg_color(o, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, LV_PART_MAIN);
}

/**
 * @brief 页脚文案：定时唤醒时刻或关闭状态。
 *
 * 显示的"时刻"由当前时间 + 唤醒间隔算出，与 monitor 任务侧设置的定时唤醒同一含义。
 */
static void sleep_foot_text(char *buf)
{
    time_t wake;
    struct tm tmv;
    unsigned hour, minute;

    if(!m_auto_on)
    {
        snprintf(buf, SP_FOOT_BUF_LEN, "%s", app_text("定时唤醒已关闭", "AUTO WAKE OFF"));
        return;
    }

    wake = time(NULL) + (time_t)m_minutes * 60;
    if(localtime_r(&wake, &tmv) == NULL)
    {
        snprintf(buf, SP_FOOT_BUF_LEN, "%s", app_text("定时唤醒 --:--", "AUTO WAKE --:--"));
        return;
    }

    /* 收敛到窄类型：GCC 按 int 全域推演 %02u 会判为可能截断（本项目 -Wformat-truncation 是错误级） */
    hour   = (unsigned)tmv.tm_hour & 0xFFu;
    minute = (unsigned)tmv.tm_min  & 0xFFu;
    snprintf(buf, SP_FOOT_BUF_LEN, app_text("定时唤醒 %02u:%02u", "AUTO WAKE %02u:%02u"), hour, minute);
}

static void sleep_ui_create(lv_obj_t *root)
{
    lv_obj_t *o;
    char buf[SP_FOOT_BUF_LEN];

    sys_logi(APP_SLEEP_TAG, "create sleep card (auto_wake=%u, %u min)",
             (unsigned)m_auto_on, (unsigned)m_minutes);

    /* 与开机页共用 logo 和每次展示独立抽取的 10% 概率彩蛋。 */
    o = app_shell_brandmark(root);
    lv_obj_align(o, LV_ALIGN_TOP_MID, 0, SP_BADGE_Y);

    o = lv_label_create(root);
    lv_obj_set_style_text_font(o, app_font_small(), LV_PART_MAIN);
    lv_obj_set_style_text_color(o, lv_color_black(), LV_PART_MAIN);
    lv_label_set_text(o, "PRTS SYNTHESIZE INFORMATION ANALYSIS OS");
    lv_obj_align(o, LV_ALIGN_TOP_MID, 0, SP_BADGE_Y + APP_SHELL_BRAND_H + 12);

    /* ---- 细线 + 状态字 + 细线：与开机页同一套骨架，大字换成"我现在怎样" ---- */
    sleep_rule(root, SP_RULE1_Y);

    o = lv_label_create(root);
    lv_obj_set_style_text_font(o, app_font_hero(), LV_PART_MAIN);
    lv_obj_set_style_text_color(o, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_text_letter_space(o, SP_WORD_LS, LV_PART_MAIN);
    lv_label_set_text(o, app_text("已休眠", "STANDBY"));
    lv_obj_align(o, LV_ALIGN_TOP_MID, SP_WORD_LS / 2, SP_WORD_Y);

    sleep_rule(root, SP_RULE2_Y);

    /* ---- 唤醒说明 ---- */
    o = lv_label_create(root);
    lv_obj_set_style_text_font(o, app_font_body(), LV_PART_MAIN);
    lv_obj_set_style_text_color(o, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_text_letter_space(o, SP_MICRO_LS, LV_PART_MAIN);
    lv_label_set_text(o, app_text("按确认键唤醒", "PRESS ENTER TO WAKE"));
    lv_obj_align(o, LV_ALIGN_TOP_MID, SP_MICRO_LS / 2, SP_WAKE_Y);

    /* ---- 页脚：锚在屏幕底边，不参与居中构图 ---- */
    sleep_foot_text(buf);
    o = lv_label_create(root);
    lv_obj_set_style_text_font(o, app_font_small(), LV_PART_MAIN);
    lv_obj_set_style_text_color(o, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_text_letter_space(o, SP_MICRO_LS, LV_PART_MAIN);
    lv_label_set_text(o, buf);
    lv_obj_align(o, LV_ALIGN_BOTTOM_MID, SP_MICRO_LS / 2, -SP_FOOT_FROM_BOTTOM);
}

static void sleep_ui_destroy(void)
{
    sys_logi(APP_SLEEP_TAG, "destroy sleep card");
}

/**
 * @brief 等"卡片已画完 + 唤醒条件已解除"（见 SP_DRAW_WAIT_MS 的说明）
 *
 * @param wait_frame 是否要等地板时间。**不画卡时传 0**：调用方已经同步停掉了 app
 *                   （见 app_manager_sleep_from_app），不可能有半帧在途，只等唤醒
 *                   条件解除即可 —— 长按松手就立刻断电，不必白等 4s。
 *
 * app_task 上下文。休眠前这段等待不影响别处：事件队列满了也只是让输入任务
 * 停在发送上，而我们马上就断电了。
 */
static void sleep_wait_ready(uint8_t wait_frame)
{
    TickType_t t0 = xTaskGetTickCount();
    TickType_t wait = wait_frame ? pdMS_TO_TICKS(SP_DRAW_WAIT_MS) : 0;
    TickType_t limit = pdMS_TO_TICKS(SP_RELEASE_MAX_MS);

    for(;;)
    {
        TickType_t elapsed = xTaskGetTickCount() - t0;

        if(!hal_pwr_wake_condition_met() && (elapsed >= wait))
        {
            return;
        }
        if(elapsed >= limit)
        {
            sys_logw(APP_SLEEP_TAG, "wake pin still active after %u ms, monitor will wait for release",
                     (unsigned)SP_RELEASE_MAX_MS);
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(SP_POLL_MS));
    }
}

/*********************************************************************
 * GLOBAL FUNCTIONS
 */

const app_ui_ops_t *app_sleep_ops(void)
{
    return &g_sleep_ui_ops;
}

void app_sleep_set_info(uint8_t auto_on, uint16_t minutes)
{
    m_auto_on = auto_on ? 1 : 0;
    m_minutes = minutes;
}

void app_sleep_present(uint8_t show_card, uint8_t from_armed_timer)
{
    if(show_card)
    {
        if(!ui_core_is_ready())
        {
            sys_logw(APP_SLEEP_TAG, "ui layer not ready, sleep without card");
            show_card = 0;
        }
        else
        {
            uint32_t wake_min;

            /* 1. 参数在 app 任务侧读好（页面在 ui_task，读不到这些状态）。
                  页脚要显示"会不会自动醒"：
                  - 自动/低电路径（from_armed_timer）直接问实际配下去的唤醒源 ——
                    低电入睡不设定时唤醒，照参数推断会显示一个不会发生的时刻；
                  - 手动路径（app_sleep_run）此刻 monitor 还没配唤醒源，只能按参数推。 */
            if(from_armed_timer)
                wake_min = hal_pwr_get_timer_wakeup();
            else
                wake_min = (g_service_param.sleep.sleep_auto && g_service_param.sleep.sleep_time > 0)
                           ? g_service_param.sleep.sleep_time : 0;
            app_sleep_set_info(wake_min > 0 ? 1 : 0, (uint16_t)wake_min);

            /* 2. 出示休眠卡：ui_task 建页并做全帧刷新（异步，故第 3 步按时间兜） */
            app_manager_sleep_show(app_sleep_ops());
        }
    }

    /* 3. 等卡片画完（只画了卡才等）+ 唤醒条件解除 */
    sleep_wait_ready(show_card);
}

void app_sleep_run(uint8_t show_card)
{
    app_sleep_present(show_card, 0);

    /* 4. 交给 monitor 任务走上既有的低功耗路径（停心跳 → 逐个 deinit → deep sleep） */
    sys_logi(APP_SLEEP_TAG, "entering low power (card=%u)", (unsigned)show_card);
    service_monitor_request_sleep();
}
