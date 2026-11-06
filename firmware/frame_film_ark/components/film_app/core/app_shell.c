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
 * FileName : /film_app/core/app_shell.c
 * Author: Kiritro  Version: v0.1  Date: 2026/9/19
 * Description: UI 页公共外壳：顶部状态栏（品牌 + 电量/WiFi/蓝牙）+ 底部提示行
 * ChangeLog: Change Notes
 *
 *********************************************************************/

/*********************************************************************
 * INCLUDES
 */
#include <stdio.h>
#include <string.h>
#include <time.h>
#ifdef ARK_UI_SIMULATOR
#include <stdlib.h>
#else
#include "esp_random.h"
#endif

#include "sys_log.h"
#include "hal_bat.h"
#include "service_param.h"
#include "service_wifi.h"      /* WiFi 连接状态（状态栏第二档） */
#include "service_ble_gatts.h" /* 蓝牙连接状态 */

#include "ui_core.h"
#include "app_shell.h"
#include "app_language.h"
#include "ui_fonts.h"
#include "app_typography.h"
#include "ui_boot_logo.h"
#include "app_manager.h"
#if (SYS_NFC_ENABLE == 1)
#include "app_wallet.h"    /* 状态栏 NFC 开关 / 模拟状态 */
#endif

/*********************************************************************
 * MACROS
 */
#define APP_SHELL_TAG       "app_shell"

/*********************************************************************
 * LOCAL FUNCTIONS
 */
/**
 * @brief 刷新居中的时间标签（HH:MM）
 *
 * 只在**字符串真的变了**时才写控件 —— 1-bit 电子纸任何一次写控件都是一整帧
 * （约 940ms），所以哪怕 tick 每 10s 来一次，实际也只会在分钟跳变时刷一次。
 *
 * 时间直接问 libc（与时钟页同一套：`service_time` 已在启动时把 TZ 灌进环境变量，
 * `localtime_r` 走的就是本地时区）。ui_task 读它是安全的：不碰任何项目内的跨任务数据。
 */
static void shell_time_update(app_shell_t *s)
{
    time_t now = time(NULL);
    struct tm tmv;
    char buf[8];
    unsigned hour;
    unsigned minute;

    if(s == NULL || s->time_label == NULL)
    {
        return;
    }
    if(localtime_r(&now, &tmv) == NULL)
    {
        return;
    }

    /* 还没校过时（连上端同步时间之前是 1970）→ 显示占位，别顶着假时间 */
    if((tmv.tm_year + 1900) < 2020)
    {
        if(strcmp("--:--", s->last_time) == 0)
        {
            return;
        }
        memcpy(s->last_time, "--:--", sizeof(s->last_time));
        lv_label_set_text(s->time_label, s->last_time);
        return;
    }

    /* 收敛到窄类型再格式化：GCC 按 int 全域推演 %02u 会判为可能截断
       （-Wformat-truncation 在本工程是错误级） */
    hour   = (unsigned)tmv.tm_hour & 0xFFu;
    minute = (unsigned)tmv.tm_min  & 0xFFu;
    snprintf(buf, sizeof(buf), "%02u:%02u", hour, minute);

    if(strcmp(buf, s->last_time) == 0)
    {
        return;   // 同一分钟内，不上屏
    }
    memcpy(s->last_time, buf, sizeof(s->last_time));
    lv_label_set_text(s->time_label, buf);
}

static void shell_tick_cb(lv_timer_t *timer)
{
    app_shell_t *s = (app_shell_t *)lv_timer_get_user_data(timer);

    if(s == NULL)
    {
        return;
    }

    shell_time_update(s);

    if(s->tick_cb != NULL)
    {
        s->tick_cb();
    }
    else
    {
        app_shell_request_status();
    }
}

static lv_obj_t *shell_label(lv_obj_t *parent, lv_color_t color, const char *txt)
{
    lv_obj_t *l = lv_label_create(parent);

    lv_obj_set_style_text_font(l, &lv_font_unscii_8, LV_PART_MAIN);
    lv_obj_set_style_text_color(l, color, LV_PART_MAIN);
    lv_label_set_text(l, txt);
    /* 中文页面使用子集字体；保留原有英文页的小字与几何布局。 */
    for(const unsigned char *p = (const unsigned char *)txt; *p; p++)
    {
        if(*p >= 0x80)
        {
            lv_obj_set_style_text_font(l, &ui_font_14, LV_PART_MAIN);
            break;
        }
    }
    return l;
}

/**
 * @brief 弹性空档（把同一 flex 行里的元素推到两端）
 */
static void shell_spacer(lv_obj_t *parent)
{
    lv_obj_t *s = lv_obj_create(parent);

    lv_obj_remove_style_all(s);
    lv_obj_set_scrollable(s, false);
    lv_obj_set_size(s, 1, 1);
    lv_obj_set_flex_grow(s, 1);
}

/**
 * @brief 状态指示块（9x9 方块）
 *
 * 1-bit 面板没有灰度，"亮/暗"只能靠"实心/描边"表达。三档状态：
 *   关闭   → 整项（文字 + 方块）都不显示
 *   已开启 → 文字 + 空框（描边）
 *   已连接 → 文字 + 实心框
 */
static lv_obj_t *shell_pip(lv_obj_t *parent)
{
    lv_obj_t *p = lv_obj_create(parent);

    lv_obj_remove_style_all(p);
    lv_obj_set_scrollable(p, false);
    lv_obj_set_size(p, 9, 9);
    lv_obj_set_style_border_width(p, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(p, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_color(p, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(p, LV_OPA_TRANSP, LV_PART_MAIN);
    return p;
}

/**
 * @brief 落一个指示项的三档状态（文字 + 方块一起管）
 */
static void shell_ind_set(lv_obj_t *label, lv_obj_t *pip, uint8_t on, uint8_t conn)
{
    if(label != NULL)
    {
        /* 用 HIDDEN 而不是透明：隐藏的 flex 子项不占位，关闭时右侧直接收成电量百分比 */
        lv_obj_set_hidden(label, on ? false : true);
    }
    if(pip == NULL)
    {
        return;
    }

    if(!on)
    {
        lv_obj_set_hidden(pip, true);
        return;
    }

    lv_obj_set_hidden(pip, false);
    lv_obj_set_style_bg_opa(pip, conn ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_MAIN);
}

/*********************************************************************
 * GLOBAL FUNCTIONS
 */

void app_shell_build(lv_obj_t *root, const char *hint, const char *page, app_shell_t *out)
{
    lv_obj_t *status;
    lv_obj_t *foot;

    if(root == NULL)
    {
        return;
    }
    if(out != NULL)
    {
        memset(out, 0, sizeof(*out));
    }

    /* ============ 顶部状态栏：品牌 + WiFi / 蓝牙 / 电量 ============ */
    status = lv_obj_create(root);
    lv_obj_remove_style_all(status);
    lv_obj_set_scrollable(status, false);
    lv_obj_set_size(status, LV_PCT(100), SHELL_STATUS_H);
    lv_obj_align(status, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_pad_hor(status, SHELL_PAD_X, LV_PART_MAIN);
    lv_obj_set_style_border_side(status, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
    lv_obj_set_style_border_width(status, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(status, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_flex_flow(status, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(status, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(status, 6, LV_PART_MAIN);
    {
        /* 品牌标记：1-bit 无灰度，用实心方块代替设计稿的三角 */
        lv_obj_t *mark = lv_obj_create(status);

        lv_obj_remove_style_all(mark);
        lv_obj_set_scrollable(mark, false);
        lv_obj_set_size(mark, 7, 7);
        lv_obj_set_style_bg_color(mark, lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(mark, LV_OPA_COVER, LV_PART_MAIN);
    }
    {
        lv_obj_t *brand = shell_label(status, lv_color_black(), "RHODES ISLAND");

        lv_obj_set_style_text_letter_space(brand, 1, LV_PART_MAIN);
    }
    shell_spacer(status);
    if(out != NULL)
    {
        /* 居中时间：绝对定位（忽略 flex 布局，否则会被当普通子项排到左右两端去）。
           真实值由 shell_time_update() 填，未校时（年份 < 2020）显示 "--:--"，
           免得开机就顶着 1970 的假时间。 */
        lv_obj_t *t = shell_label(status, lv_color_black(), "");

        lv_obj_set_ignore_layout(t, true);
        lv_obj_align(t, LV_ALIGN_CENTER, 0, 0);
        out->time_label = t;
        shell_time_update(out);
    }
    {
        lv_obj_t *bat;
        lv_obj_t *wifi_label;
        lv_obj_t *wifi_pip;
        lv_obj_t *bt_label;
        lv_obj_t *bt_pip;
        lv_obj_t *nfc_label;
        lv_obj_t *nfc_pip;

        wifi_label = shell_label(status, lv_color_black(), "WIFI");
        wifi_pip = shell_pip(status);
        bt_label = shell_label(status, lv_color_black(), "BT");
        bt_pip = shell_pip(status);
        /* NFC 开关状态（右上角，与 WiFi/蓝牙同一套图标）：
           关=整项隐藏；开=空框；正在模拟卡片=实心框 */
        nfc_label = shell_label(status, lv_color_black(), "NFC");
        nfc_pip = shell_pip(status);
        bat = shell_label(status, lv_color_black(), "--%");

        if(out != NULL)
        {
            out->bat_label = bat;
            out->wifi_label = wifi_label;
            out->wifi_pip = wifi_pip;
            out->bt_label = bt_label;
            out->bt_pip = bt_pip;
            out->nfc_label = nfc_label;
            out->nfc_pip = nfc_pip;
        }
    }

    /* ============ 底部提示行：操作提示 + 页面名 ============ */
    foot = lv_obj_create(root);
    lv_obj_remove_style_all(foot);
    lv_obj_set_scrollable(foot, false);
    lv_obj_set_size(foot, LV_PCT(100), SHELL_FOOT_H);
    lv_obj_align(foot, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_pad_hor(foot, SHELL_PAD_X, LV_PART_MAIN);
    lv_obj_set_style_border_side(foot, LV_BORDER_SIDE_TOP, LV_PART_MAIN);
    lv_obj_set_style_border_width(foot, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(foot, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_flex_flow(foot, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(foot, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *hint_label = shell_label(foot, lv_color_black(), (hint != NULL) ? hint : "");
    shell_spacer(foot);
    lv_obj_t *page_label = shell_label(foot, lv_color_black(), (page != NULL) ? page : "");
    if(out != NULL)
    {
        out->hint_label = hint_label;
        out->page_label = page_label;
    }
}

void app_shell_set_text(app_shell_t *s, const char *hint, const char *page)
{
    if(s == NULL) return;
    if(s->hint_label != NULL)
    {
        lv_obj_set_style_text_font(s->hint_label, app_font_small_text(hint), LV_PART_MAIN);
        lv_label_set_text(s->hint_label, hint);
    }
    if(s->page_label != NULL)
    {
        lv_obj_set_style_text_font(s->page_label, app_font_small_text(page), LV_PART_MAIN);
        lv_label_set_text(s->page_label, page);
    }
}

void app_shell_request_status(void)
{
    (void)app_manager_post_ui_msg(APP_UI_REQ_STATUS_SYNC, NULL, 0);
}

lv_obj_t *app_shell_brandmark(lv_obj_t *parent)
{
    static const char *const quotes_zh[] = {
        "博士，你还在和你的小动物一起玩吗？",
        "这里万籁俱寂……太安静了，别留下我。",
        "深陷长梦的混沌之时，你会想起----",
        "关闭PRTS，然后闭上眼睛。等你醒来，我会履行我们的约定。",
        "你没有忘记我",
    };
    /* English game loading-screen text, source recorded in boot-logo.md.
       The first Chinese quote has no verified English counterpart yet. */
    static const char *const quotes_en[] = {
        "All is quiet here... too quiet. Don't leave me here.",
        "In the void of the long dream, you will remember—",
        "Close PRTS and close your eyes. I will fulfill our promise when you wake up.",
        "You did not forget me.",
    };
    const uint8_t english = app_language_get() == APP_LANGUAGE_EN;
    const unsigned count = english ? 4u : 5u;
    const char *const *quotes = english ? quotes_en : quotes_zh;
    /* One draw per creation: 4/40 or 5/50 buckets, both 10% overall.
       Timer updates never reroll it. Preview overrides do not enter firmware. */
#ifdef ARK_UI_SIMULATOR
    static unsigned preview_seeded;
    if(!preview_seeded) { srand((unsigned)time(NULL)); preview_seeded = 1; }
    unsigned pick = (unsigned)rand() % (10u * count);
#else
    unsigned pick = esp_random() % (10u * count);
#endif
    const char *quote = pick < count ? quotes[pick] : NULL;
#ifdef ARK_UI_SIMULATOR
    const char *forced = getenv("ARK_BOOT_EGG");
    if(forced && forced[0] >= '0' && forced[0] <= '5' && forced[1] == '\0')
    {
        /* Preview IDs match Chinese entries; English ID 1 is unavailable. */
        unsigned id = (unsigned)(forced[0] - '0');
        quote = id == 0 || (english && id == 1) ? NULL : quotes[id - (english ? 2u : 1u)];
    }
#endif
    lv_obj_t *mark = lv_obj_create(parent);
    lv_obj_remove_style_all(mark);
    lv_obj_set_scrollable(mark, false);
    lv_obj_set_size(mark, 440, APP_SHELL_BRAND_H);
    lv_obj_t *logo = lv_image_create(mark);
    lv_image_set_src(logo, &ui_boot_logo);
    lv_obj_align(logo, LV_ALIGN_TOP_MID, 0, 0);
    if(quote != NULL)
    {
        lv_obj_t *word = shell_label(mark, lv_color_black(), quote);
        lv_obj_set_style_text_font(word, app_font_small_text(quote), 0);
        if(!english) lv_obj_set_style_text_letter_space(word, -1, LV_PART_MAIN);
        lv_obj_set_width(word, 440);
        lv_obj_set_style_text_align(word, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(word, LV_LABEL_LONG_WRAP);
        lv_obj_align(word, LV_ALIGN_TOP_MID, 0, 268);
    }
    return mark;
}

void app_shell_start_tick(app_shell_t *s, app_shell_tick_cb_t cb)
{
    if(s == NULL || s->tick != NULL)
    {
        return;
    }

    s->tick_cb = cb;
    s->tick = lv_timer_create(shell_tick_cb, SHELL_TICK_MS, s);
    if(s->tick == NULL)
    {
        sys_loge(APP_SHELL_TAG, "create tick timer failed, status bar will not auto refresh");
    }
}

void app_shell_stop_tick(app_shell_t *s)
{
    if(s == NULL)
    {
        return;
    }

    /* 定时器不在 root 的对象树里，页面销毁不会顺带删它 —— 必须自己删，
       否则它会在下一次触发时访问已经销毁的 time_label（野指针）。 */
    if(s->tick != NULL)
    {
        lv_timer_delete(s->tick);
        s->tick = NULL;
    }
}

void app_shell_refresh_time(app_shell_t *s)
{
    shell_time_update(s);
}

void app_shell_release(app_shell_t *s)
{
    if(s == NULL) return;
    app_shell_stop_tick(s);
    memset(s, 0, sizeof(*s));
}

void app_shell_apply(app_shell_t *s, const app_status_t *st)
{
    if(s == NULL || st == NULL)
    {
        return;
    }

    /* 变更检测：一模一样就不碰控件。电子纸一次写控件 = 一整帧（约 940ms 且会闪），
       而周期 tick 每 10s 就会调到这里 —— 没有这层判断，停在任一页面就会每 10s 白闪一次。 */
    if(s->status_valid && (memcmp(&s->last_status, st, sizeof(*st)) == 0))
    {
        return;
    }
    memcpy(&s->last_status, st, sizeof(s->last_status));
    s->status_valid = 1;

    if(s->bat_label != NULL)
    {
        lv_label_set_text_fmt(s->bat_label, "%u%%", (unsigned)st->bat_pct);
    }
    shell_ind_set(s->wifi_label, s->wifi_pip, st->wifi_on, st->wifi_conn);
    shell_ind_set(s->bt_label, s->bt_pip, st->bt_on, st->bt_conn);
    /* NFC：开=空框，正在模拟=实心框（与 WiFi 的"开/连"两档同一语汇） */
    shell_ind_set(s->nfc_label, s->nfc_pip, st->nfc_on, st->nfc_emu);

    /* 顺带对一次时间：同步时间（BLE 0x4D）后状态栏要立刻反映出来，
       而校时事件不一定伴随状态采集 */
    shell_time_update(s);
}

int app_shell_handle_msg(app_shell_t *s, uint32_t cmd, const void *data, uint8_t len)
{
    if(cmd != APP_UI_MSG_STATUS || data == NULL || len < sizeof(app_status_t))
    {
        return 0;
    }

    app_shell_apply(s, (const app_status_t *)data);
    return 1;
}

void app_shell_status_snapshot(app_status_t *st)
{
    int level;
    if(st == NULL) return;

    level = hal_bat_get_percent();
    if(level < 0)
    {
        level = 0;
    }
    if(level > 100)
    {
        level = 100;
    }
    st->bat_pct = (uint8_t)level;
    st->wifi_on = g_service_param.network.wifi_enable ? 1 : 0;
    st->bt_on   = g_service_param.ble.ble_enable ? 1 : 0;
    /* 只有"开着"才去问"连上没有"：栈没起时那两个查询只会返回未连接，
       但"关着"与"连着"在状态栏是两档不同的表现，别让它们混起来 */
    st->wifi_conn = (st->wifi_on && service_wifi_get_connect_status()) ? 1 : 0;
    st->bt_conn   = (st->bt_on && service_ble_gatts_get_connect()) ? 1 : 0;
#if (SYS_NFC_ENABLE == 1)
    st->nfc_on  = app_wallet_nfc_enabled();
    st->nfc_emu = app_wallet_emu_active();
#else
    st->nfc_on  = 0;
    st->nfc_emu = 0;
#endif
}

void app_shell_on_event(const app_event_t *e)
{
    app_status_t st;
    if(e->type != APP_EVT_UI_MSG || e->cmd != APP_UI_REQ_STATUS_SYNC) return;
    app_shell_status_snapshot(&st);

    (void)ui_core_post(APP_UI_MSG_STATUS, &st, (uint8_t)sizeof(st));
}
