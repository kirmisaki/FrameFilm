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
 * FileName : /film_app/pages/app_menu.c
 * Author: Kiritro  Version: v0.1  Date: 2026/9/17
 * Description: 主菜单（UI 层）：横向轮播选择 app，单击进入
 * ChangeLog: Change Notes
 *
 *********************************************************************/

/*********************************************************************
 * INCLUDES
 */
#include <stdio.h>
#include <string.h>

#include "sys_log.h"

#include "ui_assets.h"
#include "ui_conf.h"    /* UI_LOGICAL_W/H：正文宽度按屏宽算，不写死 */
#include "ui_ops.h"
#include "ui_fonts.h"
#include "app_shell.h"
#include "app_language.h"
#include "app_menu.h"
/*********************************************************************
 * MACROS
 */
#define APP_MENU_TAG        "app_menu"

/* 版式（逻辑竖屏 480x720，与 tools/ui-mockup 的 .device 同尺寸，故设计稿数值可 1:1 照搬）
 *
 * 外壳（状态栏 / 提示行 / 正文留白）见 app_shell.h；本页只排"正文"里的行。
 * 正文行宽 = 屏宽 - 两侧安全边距；轮播那一行例外，取整屏宽好让卡片延伸到边缘。 */
#define CONTENT_W           (UI_LOGICAL_W - 2 * SHELL_BODY_PAD_X)
#define SHELL_W             (UI_LOGICAL_W)

#define ROW_HEADER_H        (22)     // 中文表头；保留原轮播和其余布局
#define CAROUSEL_H          (290)    // 轮播（比当前卡片高，给上下留呼吸）
#define ROW_DOTS_H          (5)      // 指示点
#define DOTS_CUR_W          (26)     // 当前项指示点宽（实心）
#define DOTS_SIDE_W         (18)     // 其余指示点宽（描边）
#define DOTS_PAD_TOP        (8)
#define DOTS_PAD_BOT        (20)
#define ROW_RULE_H          (1)
#define ROW_ACTIVE_H        (64)     // ACTIVE 行（含上下 14 内边距；名字用 Montserrat 24，行高约 29px）
#define ROW_DESC_H          (23)
#define ROW_SPACER_H        (16)
#define ROW_PANEL_H         (68)     // APP/ENTRY 面板

/* 进入失败提示：画在面板下方那块留白里（正文总高 626，上面这些行只占 509）。
   默认隐藏，隐藏时 flex 不占位，故不影响其它行的排布。 */
#define ROW_NOTICE_H        (34)
#define NOTICE_MARGIN_TOP   (18)
#define NOTICE_TAG_W        (62)
#define NOTICE_TAG_H        (22)
#define NOTICE_TEXT_MAX     (48)

/* 轮播卡片：侧卡 132x200、当前卡 178x238、间距 10。
 * 5 张卡片总宽 746 > 屏宽，两侧被容器边界裁掉——这正是设计稿"继续延伸"的观感。 */
#define PLATE_CUR_W         (178)
#define PLATE_CUR_H         (238)
#define PLATE_SIDE_W        (132)
#define PLATE_SIDE_H        (200)
#define PLATE_GAP           (10)
#define PLATE_SPAN          (2)      // 当前项左右各画 2 张（共 5 张，正好覆盖全部条目）

/* 当前卡片在轮播容器里的位置：5 张共 746 宽 > 屏宽，"居中"排布后超出部分向两侧溢出，
   于是当前卡（178 宽）正好水平居中；垂直也居中于容器。切角要从这里算。 */
#define PLATE_CUR_LEFT      ((SHELL_W - PLATE_CUR_W) / 2)
#define PLATE_CUR_BOTTOM    ((CAROUSEL_H + PLATE_CUR_H) / 2)
#define PLATE_CHAMFER       (9)      // 左下角 45° 切角边长（设计稿 .chamfer-bl）

/*********************************************************************
 * CONSTANTS
 */
/* 轮播内容表。**顺序必须与 app_manager 的 m_menu_entries 一致**（选择索引即此表下标）。
 * 保留原轮播、图标和代码标签；功能文案使用中文子集字体。 */
static const menu_item_t MENU_ITEMS[APP_MENU_ENTRY_NUM] = {
    { "图片",   "IMG", "浏览存储中的照片，上下切换图片", "图片浏览", "休眠后保留当前图片", UI_ICON_IMAGE },
    { "通行证", "PAS", "查看个人档案与身份资料",         "个人档案", "确认键重新加载资料", UI_ICON_PASS },
#if (SYS_NFC_ENABLE == 1)
    { "卡包",   "WLT", "NFC 卡包：模拟 / 读卡 / 封面",   "卡包",     "双击确认键进入读卡", UI_ICON_WALLET },
#endif
    { "模板",   "TPL", "显示实时推送的天气与日历内容",   "实时内容", "接收手机推送",       UI_ICON_TEMPLATE },
    { "时钟",   "CLK", "查看设备时间、日期与星期",       "时间显示", "保持设备时间同步",   UI_ICON_CLOCK },
    { "动图",   "ANI", "播放多帧画面，支持调整速度",     "连续播放", "设置中调整播放参数", UI_ICON_ANIMATION },
    { "设置",   "SET", "设备信息、连接与系统参数",       "系统管理", "确认键修改当前选项", UI_ICON_SETTINGS },
};
static const menu_item_t MENU_ITEMS_EN[APP_MENU_ENTRY_NUM] = {
    {"IMAGE", "IMG", "SD PHOTO WALL / UP-DOWN PAGING", "DIRECT", "MONO SESSION REBUILD", UI_ICON_IMAGE},
    {"PASS", "PAS", "PERSONAL PROFILE / ID RECORD", "UI", "ENTER TO RELOAD PROFILE", UI_ICON_PASS},
#if (SYS_NFC_ENABLE == 1)
    {"WALLET", "WLT", "NFC CARD WALLET / EMULATE, READ, COVER", "WALLET", "DOUBLE ENTER TO READ", UI_ICON_WALLET},
#endif
    {"TEMPLATE", "TPL", "LIVE PUSH CONTENT / WEATHER, CALENDAR", "DIRECT", "MONO SESSION REBUILD", UI_ICON_TEMPLATE},
    {"CLOCK", "CLK", "DEVICE TIME / DATE AND WEEKDAY", "UI", "UI PAGE (LOW COST)", UI_ICON_CLOCK},
    {"ANIMATION", "ANI", "FRAME ANIMATION / ADJUSTABLE RATE", "DIRECT", "MONO SESSION REBUILD", UI_ICON_ANIMATION},
    {"SETTINGS", "SET", "DEVICE INFO AND SYSTEM PARAMETERS", "UI", "UI PAGE (LOW COST)", UI_ICON_SETTINGS},
};

static const menu_item_t *menu_item(uint8_t idx)
{
    return app_language_get() == APP_LANGUAGE_EN ? &MENU_ITEMS_EN[idx] : &MENU_ITEMS[idx];
}

/*********************************************************************
 * LOCAL VARIABLES
 */
static lv_obj_t *m_carousel = NULL;   // 轮播容器（选中项变化时整块重建）
static lv_obj_t *m_dots[APP_MENU_ENTRY_NUM] = {0};  // 指示点（选中项变化时改宽/填充）
static lv_obj_t *m_idx_label = NULL;  // "03 / 05"
static lv_obj_t *m_name_label = NULL; // ACTIVE 后面的 app 名
static lv_obj_t *m_desc_label = NULL;
static lv_obj_t *m_panel_line1 = NULL;
static lv_obj_t *m_panel_line2 = NULL;
static lv_obj_t *m_notice_row = NULL;   // 进入失败提示整行（默认隐藏）
static lv_obj_t *m_notice_label = NULL;
static app_shell_t m_shell;           // 顶部状态栏（电量/WiFi/蓝牙）的动态控件
static uint8_t   m_sel = 0;

/* 选中卡片左下角的切角。位图是一次算好后常驻的，不随选择变化。 */
static uint8_t m_chamfer_l8[PLATE_CHAMFER * PLATE_CHAMFER];
static lv_image_dsc_t m_chamfer_dsc;

/*********************************************************************
 * LOCAL FUNCTIONS
 */
static void menu_ui_create(lv_obj_t *root);
static void menu_ui_destroy(void);
static void menu_ui_on_msg(uint32_t cmd, const void *data, uint8_t len);
static void menu_notice_clear(void);
static void menu_notice_set(const void *data, uint8_t len);

/* 统一的小工具：本工程是 1-bit 面板，颜色只有黑/白，故一切颜色显式指定，
   不依赖主题的中间灰（I1 下中间灰会被亮度阈值化，结果不可预期）。 */
static lv_obj_t *menu_label(lv_obj_t *parent, const lv_font_t *font,
                            lv_color_t color, const char *txt)
{
    lv_obj_t *l = lv_label_create(parent);

    lv_obj_set_style_text_font(l, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(l, color, LV_PART_MAIN);
    lv_label_set_text(l, txt);
    return l;
}

static lv_obj_t *menu_rule(lv_obj_t *parent, lv_color_t color)
{
    lv_obj_t *r = lv_obj_create(parent);

    lv_obj_remove_style_all(r);
    lv_obj_set_scrollable(r, false);
    lv_obj_set_size(r, LV_PCT(100), ROW_RULE_H);
    lv_obj_set_style_bg_color(r, color, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, LV_PART_MAIN);
    return r;
}

/**
 * @brief 建一块轮播卡片
 *
 * @param idx 条目下标
 * @param cur 是否当前选中项（实心黑底 + 反白图标 + 反白文字 + 加粗描边）
 */
static lv_obj_t *menu_make_plate(lv_obj_t *parent, uint8_t idx, int cur)
{
    const menu_item_t *it = menu_item(idx);
    lv_color_t fg = cur ? lv_color_white() : lv_color_black();
    lv_obj_t *plate = lv_obj_create(parent);
    lv_obj_t *grp;
    lv_obj_t *img;
    const void *src;

    lv_obj_remove_style_all(plate);
    lv_obj_set_scrollable(plate, false);
    lv_obj_set_size(plate, cur ? PLATE_CUR_W : PLATE_SIDE_W,
                           cur ? PLATE_CUR_H : PLATE_SIDE_H);
    lv_obj_set_style_border_width(plate, cur ? 3 : 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(plate, lv_color_black(), LV_PART_MAIN);
    if(cur)
    {
        lv_obj_set_style_bg_color(plate, lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(plate, LV_OPA_COVER, LV_PART_MAIN);
        /* 选中项左下角要削一刀（终末地的"切削"语汇）。
           LVGL 没有 clip-path、边框宽度也是全边统一的，做不出"只削一个角"，
           故切角是另画的一张位图 —— 见 menu_plate_chamfer() */
    }

    /* 左上角编号：与卡片名字同字号（16px），2 字符 = 32px 宽，占左上角不挡图标 */
    {
        lv_obj_t *n = menu_label(plate, &lv_font_unscii_16, fg, "");

        lv_label_set_text_fmt(n, "%02u", (unsigned)(idx + 1));
        lv_obj_align(n, LV_ALIGN_TOP_LEFT, 8, 6);
    }

    /* 图标 + 短码：竖排居中（选中项用反色版，保证黑底上可见） */
    grp = lv_obj_create(plate);
    lv_obj_remove_style_all(grp);
    lv_obj_set_scrollable(grp, false);
    lv_obj_set_size(grp, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(grp, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(grp, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(grp, 12, LV_PART_MAIN);

    src = cur ? ui_assets_icon_inverted(it->icon) : ui_assets_icon(it->icon);
    img = lv_image_create(grp);
    if(src != NULL)
    {
        lv_image_set_src(img, src);
    }
    /* 英文短码用 8px UNSCII，中文名称用 Source Han Sans Bold 16px 点阵；编号仍用 16px UNSCII。
       字体随语言切换，卡片尺寸与定位保持不变。 */
    menu_label(grp, app_language_get() == APP_LANGUAGE_EN ? &lv_font_unscii_8 : &ui_font_14,
               fg, app_language_get() == APP_LANGUAGE_EN ? it->code : it->name);
    lv_obj_center(grp);

    return plate;
}

/**
 * @brief 给选中卡片补上左下角 45° 的切角
 *
 * LVGL 的边框宽度是全边统一的、也没有 clip-path，做不出"只削一个角"；所以这里
 * 造一张白三角位图盖在那个角上：三角外填黑，与卡片底色融为一体，只有三角是"切掉"的。
 *
 * 它挂在**轮播容器**下而不是卡片下 —— 卡片的子对象会被裁到内容区（3px 描边之内），
 * 盖不住外角。加在所有卡片之后，保证画在最上层。
 */
static void menu_plate_chamfer(void)
{
    lv_obj_t *img;
    int x, y;

    if(m_carousel == NULL)
    {
        return;
    }

    /* L8 用亮度直接表达黑白（0x00 = 黑、0xFF = 白），与 ui_assets 的图标同一约定 */
    if(m_chamfer_dsc.data == NULL)
    {
        for(y = 0; y < PLATE_CHAMFER; y++)
        {
            for(x = 0; x < PLATE_CHAMFER; x++)
            {
                /* 45° 对角线以下（含）为白：被"切掉"的就是这块 */
                m_chamfer_l8[y * PLATE_CHAMFER + x] = (y >= x) ? 0xFF : 0x00;
            }
        }
        m_chamfer_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
        m_chamfer_dsc.header.cf = LV_COLOR_FORMAT_L8;
        m_chamfer_dsc.header.w = PLATE_CHAMFER;
        m_chamfer_dsc.header.h = PLATE_CHAMFER;
        m_chamfer_dsc.header.stride = PLATE_CHAMFER;
        m_chamfer_dsc.data_size = PLATE_CHAMFER * PLATE_CHAMFER;
        m_chamfer_dsc.data = m_chamfer_l8;
    }

    img = lv_image_create(m_carousel);
    lv_obj_set_ignore_layout(img, true);   // 不参与 flex 排布，自己定位
    lv_image_set_src(img, &m_chamfer_dsc);
    lv_obj_set_pos(img, PLATE_CUR_LEFT, PLATE_CUR_BOTTOM - PLATE_CHAMFER);
}

/**
 * @brief 重建轮播：当前项 ±PLATE_SPAN，循环取模
 *
 * 条目数 6 > ±2 覆盖的 5 张，故不会出现重复卡片，也不需要"两端空位"——
 * 超出的部分由屏幕边界自然裁掉，形成设计稿的"继续延伸"观感。
 */
static void menu_rebuild_carousel(void)
{
    uint8_t i;

    if(m_carousel == NULL)
    {
        return;
    }

    lv_obj_clean(m_carousel);
    for(i = 0; i < (uint8_t)(PLATE_SPAN * 2 + 1); i++)
    {
        int off = (int)i - PLATE_SPAN;                                    /* -2..+2 */
        uint8_t idx = (uint8_t)((m_sel + APP_MENU_ENTRY_NUM + off) % APP_MENU_ENTRY_NUM);

        menu_make_plate(m_carousel, idx, (off == 0) ? 1 : 0);
    }

    menu_plate_chamfer();   // 必须在最后：切角要盖在卡片之上
}

/**
 * @brief 清除"进入失败"提示
 */
static void menu_notice_clear(void)
{
    if(m_notice_row != NULL)
    {
        lv_obj_set_hidden(m_notice_row, true);
    }
}

/**
 * @brief 显示"进入失败"提示（切页/换选中项即作废，见 menu_apply_sel）
 *
 * 载荷是 app 给出的原因（如 "NO FILM IN /sdcard/animation"），页面补上前缀
 * "<app 名> - "；长度按 NOTICE_TEXT_MAX 截断，超出部分宁可丢字也不越界。
 */
static void menu_notice_set(const void *data, uint8_t len)
{
    char buf[NOTICE_TEXT_MAX];
    uint32_t n;

    if(m_notice_row == NULL || m_notice_label == NULL)
    {
        return;
    }
    if(data == NULL || len == 0)
    {
        menu_notice_clear();
        return;
    }

    if(app_language_get() == APP_LANGUAGE_EN)
        n = snprintf(buf, sizeof(buf), "%s - %.*s",
                     menu_item(m_sel)->name, (int)len, (const char *)data);
    else if(len >= 7 && memcmp(data, "NO FILM", 7) == 0)
        n = snprintf(buf, sizeof(buf), app_text("%s / 暂无内容", "%s / No content"), menu_item(m_sel)->name);
    else if(len == 20 && memcmp(data, "ANIM FILE UNREADABLE", 20) == 0)
        n = snprintf(buf, sizeof(buf), "%s", app_text("动图文件无法读取", "Cannot read animation"));
    else
        n = snprintf(buf, sizeof(buf), "%s - %.*s",
                     menu_item(m_sel)->name, (int)len, (const char *)data);
    if(n >= sizeof(buf))
    {
        sys_logw(APP_MENU_TAG, "notice truncated (%u chars)", (unsigned)n);
    }
    lv_label_set_text(m_notice_label, buf);
    lv_obj_set_hidden(m_notice_row, false);
}

/**
 * @brief 把当前选中项同步到所有控件
 */
static void menu_apply_sel(void)
{
    const menu_item_t *it = menu_item(m_sel);
    uint8_t i;

    /* 换了选中项，上一条"进入失败"提示就指向别人了 —— 作废 */
    menu_notice_clear();

    menu_rebuild_carousel();

    /* 指示点：当前项 26x5 实心，其余 18x5 描边。宽度变化会影响 flex 重排，
       所以"高亮"是改宽度 + 填充两件事，都要跟着 m_sel 走。 */
    for(i = 0; i < APP_MENU_ENTRY_NUM; i++)
    {
        if(m_dots[i] == NULL)
        {
            continue;
        }
        lv_obj_set_width(m_dots[i], (i == m_sel) ? DOTS_CUR_W : DOTS_SIDE_W);
        lv_obj_set_style_bg_opa(m_dots[i], (i == m_sel) ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_MAIN);
    }

    if(m_idx_label != NULL)
    {
        lv_label_set_text_fmt(m_idx_label, "%02u / %02u",
                              (unsigned)(m_sel + 1), (unsigned)APP_MENU_ENTRY_NUM);
    }
    if(m_name_label != NULL)
    {
        lv_label_set_text(m_name_label, it->name);
    }
    if(m_desc_label != NULL)
    {
        lv_label_set_text(m_desc_label, it->desc);
    }
    if(m_panel_line1 != NULL)
    {
        lv_label_set_text_fmt(m_panel_line1, app_text("%02u  /  %s", "APP %02u   LAYER %s"),
                              (unsigned)(m_sel + 1), it->layer);
    }
    if(m_panel_line2 != NULL)
    {
        if(app_language_get() == APP_LANGUAGE_EN)
        {
            lv_label_set_text_fmt(m_panel_line2, "ENTRY %s", it->entry);
        }
        else
        {
            lv_label_set_text(m_panel_line2, it->entry);
        }
    }

    /* 状态栏顺手刷一次：换选中项本来就要整屏重绘，这次请求不额外付刷新代价。
       （1-bit 面板每次更新都是全帧 + 闪一下，所以交互点同步一次就够；
         页面空闲时的电量/时间由 app_shell 的周期 tick 兜住，见 app_shell_start_tick）
         页面 create 也会走到这里，故不必在别处再请求一次。 */
    app_shell_request_status();
}

/*********************************************************************
 * 页面契约
 */

static void menu_ui_create(lv_obj_t *root)
{
    lv_obj_t *body;
    lv_obj_t *stack;
    lv_obj_t *header;
    lv_obj_t *dots;
    lv_obj_t *tab;
    lv_obj_t *panel;
    uint8_t i;

    sys_logi(APP_MENU_TAG, "create menu page (%u entries)", (unsigned)APP_MENU_ENTRY_NUM);

    /* ============ 外壳：顶部状态栏 + 底部提示行 ============ */
    app_shell_build(root, app_text("上下选择  确认进入  长按休眠", "UP/DOWN SELECT   ENTER OPEN   HOLD SLEEP"),
                    app_text("菜单", "MENU"), &m_shell);
    if(app_language_get() != APP_LANGUAGE_EN)
    {
        lv_obj_set_style_text_font(m_shell.hint_label, &ui_font_14, LV_PART_MAIN);
        lv_obj_set_style_text_font(m_shell.page_label, &ui_font_14, LV_PART_MAIN);
    }

    /* ============ 正文：夹在状态栏与提示行之间 ============ */
    body = lv_obj_create(root);
    lv_obj_remove_style_all(body);
    lv_obj_set_scrollable(body, false);
    lv_obj_set_size(body, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_top(body, SHELL_BODY_TOP, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(body, SHELL_BODY_BOTTOM, LV_PART_MAIN);
    /* 左右不留内边距：正文各行自带 CONTENT_W 宽度并居中，
       这样轮播才能横向延伸到屏幕边缘（由容器边界裁掉超出的卡片） */

    /* 正文按列排：每行高度与设计稿一致 */
    stack = lv_obj_create(body);
    lv_obj_remove_style_all(stack);
    lv_obj_set_scrollable(stack, false);
    lv_obj_set_size(stack, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(stack, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(stack, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(stack, 0, LV_PART_MAIN);

    /* ---- 表头：SELECT APPLICATION ──── 01 / 05 ---- */
    header = lv_obj_create(stack);
    lv_obj_remove_style_all(header);
    lv_obj_set_scrollable(header, false);
    lv_obj_set_size(header, CONTENT_W, ROW_HEADER_H);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(header, 8, LV_PART_MAIN);
    menu_label(header, app_language_get() ? &lv_font_unscii_8 : &ui_font_18,
               lv_color_black(), app_text("选择应用", "SELECT APPLICATION"));
    {
        /* 中间的弹性横线：宽度全交给 flex 分配（显式给宽度会与分配到的空间叠加而溢出） */
        lv_obj_t *sp = menu_rule(header, lv_color_black());

        lv_obj_set_width(sp, 0);
        lv_obj_set_flex_grow(sp, 1);
    }
    m_idx_label = menu_label(header, &lv_font_unscii_8, lv_color_black(), "01 / 05");

    /* ---- 轮播：当前项 ±2 共 5 张。容器取整屏宽，超出的卡片由容器边界裁掉 ---- */
    m_carousel = lv_obj_create(stack);
    lv_obj_remove_style_all(m_carousel);
    lv_obj_set_scrollable(m_carousel, false);
    lv_obj_set_size(m_carousel, SHELL_W, CAROUSEL_H);
    lv_obj_set_flex_flow(m_carousel, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(m_carousel, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(m_carousel, PLATE_GAP, LV_PART_MAIN);

    /* ---- 指示点（当前 26x5 实心，其余 18x5 描边） ---- */
    dots = lv_obj_create(stack);
    lv_obj_remove_style_all(dots);
    lv_obj_set_scrollable(dots, false);
    lv_obj_set_size(dots, CONTENT_W, ROW_DOTS_H);
    lv_obj_set_style_margin_top(dots, DOTS_PAD_TOP, LV_PART_MAIN);
    lv_obj_set_style_margin_bottom(dots, DOTS_PAD_BOT, LV_PART_MAIN);
    lv_obj_set_flex_flow(dots, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(dots, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(dots, 6, LV_PART_MAIN);
    for(i = 0; i < APP_MENU_ENTRY_NUM; i++)
    {
        lv_obj_t *d = lv_obj_create(dots);

        lv_obj_remove_style_all(d);
        lv_obj_set_scrollable(d, false);
        lv_obj_set_size(d, (i == m_sel) ? DOTS_CUR_W : DOTS_SIDE_W, ROW_DOTS_H);
        lv_obj_set_style_border_width(d, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(d, lv_color_black(), LV_PART_MAIN);
        /* 底色一律先写好，只靠 opa 开关填充：这样 menu_apply_sel() 里
           来回切高亮时不用再设颜色 */
        lv_obj_set_style_bg_color(d, lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(d, (i == m_sel) ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_MAIN);
        m_dots[i] = d;
    }

    /* ---- 分隔线 ---- */
    {
        lv_obj_t *r = menu_rule(stack, lv_color_black());

        lv_obj_set_width(r, CONTENT_W);
    }

    /* ---- ACTIVE + app 名 ---- */
    {
        lv_obj_t *row = lv_obj_create(stack);

        lv_obj_remove_style_all(row);
        lv_obj_set_scrollable(row, false);
        lv_obj_set_size(row, CONTENT_W, ROW_ACTIVE_H);
        lv_obj_set_style_pad_ver(row, 14, LV_PART_MAIN);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(row, 12, LV_PART_MAIN);

        tab = lv_obj_create(row);
        lv_obj_remove_style_all(tab);
        lv_obj_set_scrollable(tab, false);
        lv_obj_set_size(tab, 62, 22);
        lv_obj_set_style_bg_color(tab, lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(tab, LV_OPA_COVER, LV_PART_MAIN);
        {
            lv_obj_t *t = menu_label(tab, app_language_get() ? &lv_font_unscii_8 : &ui_font_18,
                                     lv_color_white(), app_text("当前", "ACTIVE"));

            lv_obj_center(t);
        }
        m_name_label = menu_label(row, app_language_get() == APP_LANGUAGE_EN ? &lv_font_montserrat_24 : &ui_font_24,
                                  lv_color_black(), app_text("图片", "IMAGE"));
    }

    /* ---- 一句话说明 ---- */
    m_desc_label = menu_label(stack, app_language_get() == APP_LANGUAGE_EN ? &lv_font_unscii_8 : &ui_font_18,
                              lv_color_black(), "");
    lv_obj_set_size(m_desc_label, CONTENT_W, ROW_DESC_H);

    /* ---- 间隙 ---- */
    {
        lv_obj_t *sp = lv_obj_create(stack);

        lv_obj_remove_style_all(sp);
        lv_obj_set_scrollable(sp, false);
        lv_obj_set_size(sp, CONTENT_W, ROW_SPACER_H);
    }

    /* ---- 层级/进入代价面板 ---- */
    panel = lv_obj_create(stack);
    lv_obj_remove_style_all(panel);
    lv_obj_set_scrollable(panel, false);
    lv_obj_set_size(panel, CONTENT_W, ROW_PANEL_H);
    lv_obj_set_style_border_width(panel, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(panel, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_pad_all(panel, 10, LV_PART_MAIN);

    m_panel_line1 = menu_label(panel, app_language_get() == APP_LANGUAGE_EN ? &lv_font_unscii_8 : &ui_font_18,
                               lv_color_black(), "");
    lv_obj_set_pos(m_panel_line1, 0, 0);
    m_panel_line2 = menu_label(panel, app_language_get() == APP_LANGUAGE_EN ? &lv_font_unscii_8 : &ui_font_18,
                               lv_color_black(), "");
    lv_obj_set_pos(m_panel_line2, 0, 24);

    /* ---- 进入失败提示：面板下方的留白处，默认隐藏 ---- */
    m_notice_row = lv_obj_create(stack);
    lv_obj_remove_style_all(m_notice_row);
    lv_obj_set_scrollable(m_notice_row, false);
    lv_obj_set_size(m_notice_row, CONTENT_W, ROW_NOTICE_H);
    lv_obj_set_style_margin_top(m_notice_row, NOTICE_MARGIN_TOP, LV_PART_MAIN);
    lv_obj_set_flex_flow(m_notice_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(m_notice_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(m_notice_row, 12, LV_PART_MAIN);
    {
        /* 黑底反白标签，与 ACTIVE 那个同一套语汇：状态由实心块承载 */
        lv_obj_t *tag = lv_obj_create(m_notice_row);

        lv_obj_remove_style_all(tag);
        lv_obj_set_scrollable(tag, false);
        lv_obj_set_size(tag, NOTICE_TAG_W, NOTICE_TAG_H);
        lv_obj_set_style_bg_color(tag, lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(tag, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_center(menu_label(tag, app_language_get() == APP_LANGUAGE_EN ? &lv_font_unscii_8 : &ui_font_18,
                                 lv_color_white(), app_text("提示", "FAILED")));

        m_notice_label = menu_label(m_notice_row, app_language_get() == APP_LANGUAGE_EN ? &lv_font_unscii_8 : &ui_font_18,
                                    lv_color_black(), "");
    }

    menu_apply_sel();

    /* 空闲时也要刷新状态栏（电量 / 居中时间）：菜单是常驻页，用户可能停在这里很久。
       tick 只每 10s 取一次值，内容没变不碰控件，所以实际约每分钟才上屏一次（分钟跳变）。 */
    app_shell_start_tick(&m_shell, NULL);
}

static void menu_ui_destroy(void)
{
    sys_logi(APP_MENU_TAG, "destroy menu page");

    m_carousel = NULL;
    memset(m_dots, 0, sizeof(m_dots));
    m_idx_label = NULL;
    m_name_label = NULL;
    m_desc_label = NULL;
    m_panel_line1 = NULL;
    m_panel_line2 = NULL;
    m_notice_row = NULL;
    m_notice_label = NULL;
    /* 释放外壳：顺带删周期定时器（它不在对象树里，不删会变野指针） */
    app_shell_release(&m_shell);
}

static void menu_ui_on_msg(uint32_t cmd, const void *data, uint8_t len)
{
    if(cmd == APP_UI_MSG_MENU_NOTICE)
    {
        /* 进入失败的原因（app 给文案，页面补 app 名）。空负载 = 清除 */
        menu_notice_set(data, len);
        return;
    }

    if(cmd == APP_UI_MSG_MENU_SEL && data != NULL && len >= 1)
    {
        uint8_t sel = ((const uint8_t *)data)[0];

        if(sel < APP_MENU_ENTRY_NUM)
        {
            /* 与 app_manager 的选择索引对齐：值相同也要落一次（初次进入）。
               提示的作废在 menu_apply_sel() 里统一处理 */
            m_sel = sel;
            menu_apply_sel();
        }
        return;
    }

    (void)app_shell_handle_msg(&m_shell, cmd, data, len);
}

/*********************************************************************
 * GLOBAL VARIABLES
 */
static const app_ui_ops_t g_menu_ui_ops = {
    .create  = menu_ui_create,
    .destroy = menu_ui_destroy,
    .on_msg  = menu_ui_on_msg,
    .on_key  = NULL,     // 按键由 app_manager（app 任务）统一裁决，页面只负责显示
};

const app_entry_t g_app_menu_entry = {
    .id = APP_ID_MENU,
    .name = "menu",
    .data_dir = NULL,
    .keys = APP_KEY_NONE,
    .tick_ms = 0,
    .events = NULL,
    .on_event = app_shell_on_event,   // 只服务状态栏（上行采集 → 回投）
    .layer = APP_LAYER_UI,
    .ui_ops = &g_menu_ui_ops,
};
