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
 * FileName : /film_app/pages/app_settings.c
 * Author: Kiritro  Version: v0.1  Date: 2026/9/17
 * Description: 系统设置（UI 层）：设备信息 + 系统参数，UP/DOWN 选择、ENTER 切换、双击退出
 * ChangeLog: Change Notes
 *
 *********************************************************************/

/*********************************************************************
 * INCLUDES
 */
#include <stdio.h>
#include <string.h>

#include "sys_log.h"
#include "sys_cfg.h"
#include "hal_bat.h"
#include "hal_epd.h"
#include "hal_sd.h"
#include "service_file.h"
#include "service_param.h"
#include "service_wifi.h"
#include "service_ble.h"        /* service_ble_apply_enable：蓝牙开关运行期起停 */
#include "service_ble_gatts.h"  /* 蓝牙连接状态（状态栏第二档） */

#include "ui_conf.h"    /* UI_CMD_DATA_MAX（快照尺寸断言） */
#include "ui_ops.h"
#include "ui_fonts.h"
#include "app_typography.h"
#include "app_shell.h"
#include "app_image.h"      /* 图片 app 参数 TAG / 取值范围（app 参数行用） */
#include "app_animation.h"  /* 动图 app 参数 TAG / 取值范围 */
#include "app_boot_cfg.h"   /* 开机行为参数（BOOT PAGE / START APP 两行） */
#include "app_language.h"
#if (SYS_NFC_ENABLE == 1)
#include "app_wallet.h"     /* NFC 总开关（NFC 行） */
#endif
#include "app_settings.h"
#include "app_manager.h"

/*********************************************************************
 * MACROS
 */
#define APP_SET_TAG         "app_set"

#define ROW_H               (28)     // 行高（19 行，分节标题缩为 17px）
#define Y_LIST              (66)     // 列表起点（表头 + 分节标题之下）

/* BATTERY 行的电量条几何：外框 62 宽（含 1px 描边），填充按内宽 60 按比例给 */
#define SET_BAR_TRACK_W     (62)
#define SET_BAR_TRACK_H     (9)
#define SET_BAR_FILL_W      (SET_BAR_TRACK_W - 2)

/* 未选中：进页面时不预选任何行，第一次 UP/DOWN 才落条（也用于 ENTER 的越界判断） */
#define SET_SEL_NONE        (0xFF)

/* 循环取值的候选项（也是校验表，上报值必须落在其中） */
#define WAKE_OPT_NUM        (8)
static const char *const WAKE_OPTS[WAKE_OPT_NUM] = {
    "10分钟", "30分钟", "60分钟", "2小时", "6小时", "12小时", "24小时", "48小时"
};
static const uint16_t WAKE_OPTS_MIN[WAKE_OPT_NUM] = {
    10, 30, 60, 120, 360, 720, 1440, 2880       // 与 WAKE_OPTS 一一对应的分钟值
};
#define HB_OPT_NUM          (6)
static const char *const HB_OPTS[HB_OPT_NUM] = {
    "5秒", "10秒", "30秒", "60秒", "120秒", "180秒"
};
static const uint16_t HB_OPTS_SEC[HB_OPT_NUM] = {
    5, 10, 30, 60, 120, 180                      // 与 HB_OPTS 一一对应的秒值
};

/* ---- LED 模式（下标 == 落盘值，与 service_param.h 的 SERVICE_LED_MODE_* 同序）---- */
#define LED_OPT_NUM         (SERVICE_LED_MODE_NUM)
static const char *const LED_OPTS[LED_OPT_NUM] = {
    "持续呼吸", "关闭", "自动"               // 常亮 / 关闭 / 闲置自动熄灭
};
static const char *const LED_EN[LED_OPT_NUM] = {
    "ALWAYS", "OFF", "AUTO"
};

/* ---- 开机行为（BOOT PAGE / START APP）----
 * 候选下标与 app_boot_cfg.h 的 APP_BOOT_PAGE_* / app_start_t **必须同序**，
 * 存的就是下标本身（协议/落盘值即候选下标）。 */
#define BOOTPAGE_OPT_NUM    (2)
static const char *const BOOTPAGE_OPTS[BOOTPAGE_OPT_NUM] = {
    "显示", "跳过"                              // 下标 0/1 == APP_BOOT_PAGE_SHOW/SKIP
};
#define STARTAPP_OPT_NUM    (APP_START_NUM)
static const char *const STARTAPP_OPTS[STARTAPP_OPT_NUM] = {
    "菜单", "上次", "图片", "模板", "时钟", "动图", "设置", "通行证", "卡包"
};                                              // 顺序 == app_start_t

/* ---- app 参数行的候选值 ----
 * 值本身由归属 app 的 TLV 通道定义（TAG / 范围见 app_image.h、app_animation.h），
 * 这里只规定"界面上给几个档位"以及下标 ↔ 物理值的对照。 */
#define IMG_PLAY_OPT_NUM    (2)
static const char *const IMG_PLAY_OPTS[IMG_PLAY_OPT_NUM] = {
    "手动", "自动"                            // 下标 0/1 == APP_IMAGE_PLAY_MANUAL/AUTO
};
#define IMG_INT_OPT_NUM     (5)
static const char *const IMG_INT_OPTS[IMG_INT_OPT_NUM] = {
    "1分钟", "5分钟", "10分钟", "30分钟", "60分钟"
};
static const uint16_t IMG_INT_OPTS_MIN[IMG_INT_OPT_NUM] = {
    1, 5, 10, 30, 60                            // 分钟（参数范围 1~120）
};
#define ANIM_LOOP_OPT_NUM   (2)
static const char *const ANIM_LOOP_OPTS[ANIM_LOOP_OPT_NUM] = {
    "单个", "列表"                            // 下标 0/1 == APP_ANIM_PLAY_SINGLE/SEQ
};
#define ANIM_SPEED_OPT_NUM  (5)
static const char *const ANIM_SPEED_OPTS[ANIM_SPEED_OPT_NUM] = {
    "100毫秒", "200毫秒", "500毫秒", "1秒", "2秒"
};
static const uint16_t ANIM_SPEED_OPTS_MS[ANIM_SPEED_OPT_NUM] = {
    100, 200, 500, 1000, 2000                   // 每帧毫秒（参数范围 100~2000）
};

/* 分节标题（行表里的 sec 即下标） */
static const char *const SEC_TITLES[] = {
    "设备", "系统参数", "应用参数"
};

/* 快照要经 ui_core_post 一次性下发，长度受命令负载上限约束 */
_Static_assert(sizeof(settings_snapshot_t) <= UI_CMD_DATA_MAX,
               "settings_snapshot_t exceeds ui cmd payload");

/* 列表总高：3 个分节标题（3 行标题 + 3 处 2px 间距） + 19 行（NFC 机型）。
   正文可用高度 = 720 - 状态栏 30 - 提示行 28 - 上下留白 36 = 626；
   列表从表头下方的 y=24 起，可用 602 —— 19 行共 589，余 13px 不会顶到提示行。
   行高由 29 收到 28、分节间距由 4 收到 2，都是为 NFC 行腾出的空间；再压行内文字会挤。 */
#define SET_LIST_H          (3 * (17 + 2) + SETTINGS_ROW_NUM * ROW_H)

/*********************************************************************
* TYPEDEFS
*/
typedef struct {
    uint8_t sec;        // 0=DEVICE 1=PARAMETERS（分节首行）
    const char *key;
    uint8_t kind;
    uint8_t field;      // 在 settings_snapshot_t 中的取值字段编号
} settings_row_t;

/* 字段编号（避免依赖结构体偏移，页面对快照只读） */
enum {
    F_BAT = 0, F_WIFI, F_BT, F_LED, F_PANEL, F_STORAGE, F_FW,
    F_SLEEP, F_AUTOWAKE, F_WIFI_ON, F_BT_ON, F_NFC, F_WAKE_SEL, F_HB_SEL,
    F_BOOT_PAGE, F_START_APP, F_LANGUAGE,
    F_IMG_PLAY, F_IMG_INT, F_ANIM_LOOP, F_ANIM_SPEED,
};

/* 行表。**顺序即显示顺序，下标即上报的行号。** */
static const settings_row_t ROWS[SETTINGS_ROW_NUM] = {
    { 0, "电量",         SET_KIND_BAR,    F_BAT        },
    { 0, "WiFi",         SET_KIND_TOGGLE, F_WIFI_ON    },
    { 0, "蓝牙",         SET_KIND_TOGGLE, F_BT_ON      },
#if (SYS_NFC_ENABLE == 1)
    { 0, "NFC",          SET_KIND_TOGGLE, F_NFC        },
#endif
    { 0, "指示灯",       SET_KIND_CYCLE,  F_LED        },
    { 0, "屏幕",         SET_KIND_TEXT,   F_PANEL      },
    { 0, "存储",         SET_KIND_TEXT,   F_STORAGE    },
    { 0, "固件",         SET_KIND_TEXT,   F_FW         },
    { 1, "开机画面",     SET_KIND_CYCLE,  F_BOOT_PAGE  },
    { 1, "启动应用",     SET_KIND_CYCLE,  F_START_APP  },
    { 1, "语言 / Language", SET_KIND_CYCLE, F_LANGUAGE },
    { 1, "自动休眠",     SET_KIND_TOGGLE, F_SLEEP      },
    { 1, "定时唤醒",     SET_KIND_TOGGLE, F_AUTOWAKE   },
    { 1, "唤醒间隔",     SET_KIND_CYCLE,  F_WAKE_SEL   },
    { 1, "心跳间隔",     SET_KIND_CYCLE,  F_HB_SEL     },
    { 2, "图片播放",     SET_KIND_CYCLE,  F_IMG_PLAY   },
    { 2, "图片间隔",     SET_KIND_CYCLE,  F_IMG_INT    },
    { 2, "动图播放",     SET_KIND_CYCLE,  F_ANIM_LOOP  },
    { 2, "动图速度",     SET_KIND_CYCLE,  F_ANIM_SPEED },
};
static const char *const ROW_KEYS_EN[SETTINGS_ROW_NUM] = {
    "BATTERY", "WIFI", "BLUETOOTH",
#if (SYS_NFC_ENABLE == 1)
    "NFC",
#endif
    "LED", "PANEL", "STORAGE", "FIRMWARE",
    "BOOT PAGE", "START APP", "LANGUAGE", "SLEEP MODE", "AUTO WAKE",
    "WAKE INTERVAL", "HEARTBEAT", "IMG PLAY", "IMG INTERVAL", "ANIM LOOP", "ANIM SPEED"
};
static const char *const WAKE_EN[] = {"10 MIN", "30 MIN", "60 MIN", "2 H", "6 H", "12 H", "24 H", "48 H"};
static const char *const HB_EN[] = {"5 S", "10 S", "30 S", "60 S", "120 S", "180 S"};
static const char *const START_EN[] = {"MENU", "LAST", "IMAGE", "TEMPLATE", "CLOCK", "ANIMATION", "SETTINGS", "PASS", "WALLET"};
static const char *const IMG_INT_EN[] = {"1 MIN", "5 MIN", "10 MIN", "30 MIN", "60 MIN"};
static const char *const SPEED_EN[] = {"100 MS", "200 MS", "500 MS", "1 S", "2 S"};
static const char *const SEC_EN[] = {"DEVICE", "PARAMETERS", "APP PARAMS"};

/*********************************************************************
 * LOCAL VARIABLES
 */
static lv_obj_t *m_rows[SETTINGS_ROW_NUM];
static lv_obj_t *m_row_key[SETTINGS_ROW_NUM];    // 键名标签（选中行要反白，否则黑底黑字看不见）
static lv_obj_t *m_row_fill[SETTINGS_ROW_NUM];   // 电量条填充块（仅 BATTERY 行非空）
static lv_obj_t *m_row_val[SETTINGS_ROW_NUM];    // 右侧取值标签
static lv_obj_t *m_row_tog[SETTINGS_ROW_NUM];    // 开关块
static lv_obj_t *m_row_ptr[SETTINGS_ROW_NUM];    // 选择指针（只读行恒隐藏）
static lv_obj_t *m_row_st[SETTINGS_ROW_NUM];     // 开关行左侧的状态文案（仅 WiFi/蓝牙行非空）
static lv_obj_t *m_row_idx_label = NULL;
static lv_obj_t *m_head_label = NULL;
static lv_obj_t *m_sec_label[3];

static app_shell_t m_shell;      // 顶部状态栏（数据复用设置快照，不额外往返）
static settings_snapshot_t m_snap;
/* 进页面时**不预选**：没有高亮条，第一次 UP/DOWN 才落到第一条可操作行 */
static uint8_t m_sel = SET_SEL_NONE;

/*********************************************************************
 * LOCAL FUNCTIONS
 */
/**
 * @brief 周期 tick 钩子（ui_task 上下文）
 *
 * 本页的"状态栏数据"也来自设置快照，所以不能走 app_shell 的默认钩子
 * （那只会刷电量/WiFi/蓝牙三项，列表里的 BATTERY 行会不跟着变）。
 */
static void settings_tick_request(void)
{
    (void)app_manager_post_ui_msg(APP_UI_REQ_SETTINGS_SYNC, NULL, 0);
}

static void set_ui_create(lv_obj_t *root);
static void set_ui_destroy(void);
static void set_ui_on_key(input_press_type_t key);
static void set_ui_on_msg(uint32_t cmd, const void *data, uint8_t len);
static void settings_tick_request(void);
static void settings_on_event(const app_event_t *e);

static lv_obj_t *set_label(lv_obj_t *parent, const lv_font_t *font,
                           lv_color_t color, const char *txt)
{
    lv_obj_t *l = lv_label_create(parent);

    lv_obj_set_style_text_font(l, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(l, color, LV_PART_MAIN);
    lv_label_set_text(l, txt);
    return l;
}

static int row_is_ro(uint8_t idx)
{
    return (ROWS[idx].kind == SET_KIND_BAR || ROWS[idx].kind == SET_KIND_TEXT);
}

/**
 * @brief 取某行当前显示文本（只读行给设备信息，可操作行给候选值）
 */
static const char *row_text(uint8_t idx)
{
    static char storage_text[24];

    switch(ROWS[idx].field)
    {
    case F_BAT:        return m_snap.bat;
    case F_WIFI_ON:    return !m_snap.wifi_on ? app_text("已关闭", "Off") :
                              (m_snap.wifi_conn ? app_text("已连接", "Connected") : app_text("未连接", "Idle"));
    case F_BT_ON:      return !m_snap.bt_on ? app_text("已关闭", "Off") :
                              (m_snap.bt_conn ? app_text("已连接", "Connected") : app_text("等待连接", "Waiting"));
#if (SYS_NFC_ENABLE == 1)
    case F_NFC:        return !m_snap.nfc_on ? app_text("已关闭", "Off") :
                              (m_snap.nfc_emu ? app_text("模拟中", "Emulating") : app_text("无卡", "No card"));
#endif
    case F_PANEL:      return m_snap.panel;
    case F_STORAGE:
    {
        unsigned count;
        if(sscanf(m_snap.storage, "%u FILM", &count) == 1)
        {
            (void)snprintf(storage_text, sizeof(storage_text), app_text("%u 张", "%u films"), count);
            return storage_text;
        }
        return app_text("未插卡", "No SD");
    }
    case F_FW:         return m_snap.fw;
    case F_WAKE_SEL:   return app_text(WAKE_OPTS[m_snap.wake_sel % WAKE_OPT_NUM], WAKE_EN[m_snap.wake_sel % WAKE_OPT_NUM]);
    case F_HB_SEL:     return app_text(HB_OPTS[m_snap.hb_sel % HB_OPT_NUM], HB_EN[m_snap.hb_sel % HB_OPT_NUM]);
    case F_BOOT_PAGE:  return app_text(BOOTPAGE_OPTS[m_snap.boot_page_sel % BOOTPAGE_OPT_NUM],
                                  m_snap.boot_page_sel ? "SKIP" : "SHOW");
    case F_START_APP:  return app_text(STARTAPP_OPTS[m_snap.start_app_sel % STARTAPP_OPT_NUM], START_EN[m_snap.start_app_sel % STARTAPP_OPT_NUM]);
    case F_LANGUAGE:   return m_snap.language_sel ? "English" : "简体中文";
    case F_LED:        return app_text(LED_OPTS[m_snap.led_mode_sel % LED_OPT_NUM], LED_EN[m_snap.led_mode_sel % LED_OPT_NUM]);
    case F_IMG_PLAY:   return app_text(IMG_PLAY_OPTS[m_snap.img_play_sel % IMG_PLAY_OPT_NUM], m_snap.img_play_sel ? "AUTO" : "MANUAL");
    case F_IMG_INT:    return app_text(IMG_INT_OPTS[m_snap.img_interval_sel % IMG_INT_OPT_NUM], IMG_INT_EN[m_snap.img_interval_sel % IMG_INT_OPT_NUM]);
    case F_ANIM_LOOP:  return app_text(ANIM_LOOP_OPTS[m_snap.anim_loop_sel % ANIM_LOOP_OPT_NUM], m_snap.anim_loop_sel ? "LIST" : "SINGLE");
    case F_ANIM_SPEED: return app_text(ANIM_SPEED_OPTS[m_snap.anim_speed_sel % ANIM_SPEED_OPT_NUM], SPEED_EN[m_snap.anim_speed_sel % ANIM_SPEED_OPT_NUM]);
    default:           return "";
    }
}

static uint8_t row_on(uint8_t idx)
{
    switch(ROWS[idx].field)
    {
    case F_WIFI_ON:  return m_snap.wifi_on;
    case F_BT_ON:    return m_snap.bt_on;
#if (SYS_NFC_ENABLE == 1)
    case F_NFC:      return m_snap.nfc_on;
#endif
    case F_SLEEP:    return m_snap.sleep_mode;
    case F_AUTOWAKE: return m_snap.sleep_auto;
    default:         return 0;
    }
}

/**
 * @brief 在候选值表里反查下标（设备侧存的是物理量，页面用的是候选下标）
 */
static uint8_t idx_of_u16(const uint16_t *tab, uint8_t num, uint16_t v)
{
    uint8_t i;

    for(i = 0; i < num; i++)
    {
        if(tab[i] == v)
        {
            return i;
        }
    }
    return 0;   // 不在候选表内（如手机侧下发过任意值）：显示为第一项
}

/**
 * @brief 从 TLV 列表里取一个 1B / 2B 字段（没有或长度不符时给回落值）
 */
static uint8_t tlv_get_u8(const uint8_t *tlv, uint8_t len, uint8_t tag, uint8_t fallback)
{
    uint8_t off = 0;
    app_tlv_t item;

    while(app_tlv_next(tlv, len, &off, &item))
    {
        if((item.tag == tag) && (item.len == 1))
        {
            return item.val[0];
        }
    }
    return fallback;
}

static uint16_t tlv_get_u16(const uint8_t *tlv, uint8_t len, uint8_t tag, uint16_t fallback)
{
    uint8_t off = 0;
    app_tlv_t item;

    while(app_tlv_next(tlv, len, &off, &item))
    {
        if((item.tag == tag) && (item.len == 2))
        {
            return app_tlv_be16(item.val);
        }
    }
    return fallback;
}

/**
 * @brief 读回归属 app 的参数，换算成候选下标（app_task 上下文）
 *
 * 走 app_manager_param_get —— 与 BLE 参数查询（0x46 / 0x4A）同一套 TLV，
 * 不直接碰 app 内部状态；目标 app 从未进入过时，该接口内部会先载入/套默认值。
 */
static void settings_app_params_pull(void)
{
    uint8_t tlv[32];
    uint8_t n;

    n = app_manager_param_get((uint8_t)APP_ID_IMAGE, tlv, (uint8_t)sizeof(tlv));
    m_snap.img_play_sel = (tlv_get_u8(tlv, n, APP_IMAGE_TAG_PLAY_MODE, APP_IMAGE_PLAY_MANUAL) != 0) ? 1 : 0;
    m_snap.img_interval_sel = idx_of_u16(IMG_INT_OPTS_MIN, IMG_INT_OPT_NUM,
                                         tlv_get_u16(tlv, n, APP_IMAGE_TAG_INTERVAL, IMG_INT_OPTS_MIN[0]));

    n = app_manager_param_get((uint8_t)APP_ID_ANIMATION, tlv, (uint8_t)sizeof(tlv));
    m_snap.anim_loop_sel = (tlv_get_u8(tlv, n, APP_ANIM_TAG_PLAY_MODE, APP_ANIM_PLAY_SINGLE) != 0) ? 1 : 0;
    m_snap.anim_speed_sel = idx_of_u16(ANIM_SPEED_OPTS_MS, ANIM_SPEED_OPT_NUM,
                                       tlv_get_u16(tlv, n, APP_ANIM_TAG_FRAME_MS, ANIM_SPEED_OPTS_MS[1]));
}

/**
 * @brief 采集一次设备状态快照（app_task 上下文）
 *
 * 只读各服务/驱动，无副作用。**页面侧不得直接读这些接口**：它们没有跨任务
 * 保证，ui_task 与各服务任务并发访问会取到撕裂值。
 */
static void settings_snapshot_pull(void)
{
    int level = hal_bat_get_percent();
    uint16_t panel_w = (uint16_t)EPD_WIDTH;
    uint16_t panel_h = (uint16_t)EPD_HEIGHT;
    uint8_t  panel_id = (uint8_t)EPD_PANEL_ID;
    uint8_t  pct;

    if(level < 0)
    {
        level = 0;
    }
    if(level > 100)
    {
        level = 100;
    }
    pct = (uint8_t)level;

    /* 格式化前先把取值收敛到窄类型：GCC 按 int 全域推演 %d 时会判为可能截断
       （-Wformat-truncation 在本工程是错误级），窄类型的值域编译器是确定的 */
    m_snap.bat_pct = pct;
    snprintf(m_snap.bat, sizeof(m_snap.bat), "%u%%", (unsigned)pct);

    m_snap.wifi_on = g_service_param.network.wifi_enable ? 1 : 0;
    m_snap.bt_on   = g_service_param.ble.ble_enable ? 1 : 0;
#if (SYS_NFC_ENABLE == 1)
    m_snap.nfc_on  = app_wallet_nfc_enabled();
    m_snap.nfc_emu = app_wallet_emu_active();
#else
    m_snap.nfc_on  = 0;
    m_snap.nfc_emu = 0;
#endif
    m_snap.wifi_conn = (m_snap.wifi_on && service_wifi_get_connect_status()) ? 1 : 0;
    m_snap.bt_conn   = (m_snap.bt_on && service_ble_gatts_get_connect()) ? 1 : 0;
    snprintf(m_snap.wifi, sizeof(m_snap.wifi), "%s",
             m_snap.wifi_on ? (m_snap.wifi_conn ? "CONNECTED" : "IDLE") : "OFF");
    snprintf(m_snap.bt, sizeof(m_snap.bt), "%s",
             m_snap.bt_on ? (m_snap.bt_conn ? "CONNECTED" : "ADVERTISING") : "OFF");

    snprintf(m_snap.panel, sizeof(m_snap.panel), "E6 %ux%u ID%02X",
             (unsigned)panel_w, (unsigned)panel_h, (unsigned)panel_id);
    if(hal_sd_get_status() == SD_MOUNT)
    {
        snprintf(m_snap.storage, sizeof(m_snap.storage), "%u FILM", (unsigned)service_file_get_count());
    }
    else
    {
        snprintf(m_snap.storage, sizeof(m_snap.storage), "NO SD");
    }
    snprintf(m_snap.fw, sizeof(m_snap.fw), "%s", SYS_FIRMWARE_VERSION);

    m_snap.sleep_mode = g_service_param.sleep.sleep_mode ? 1 : 0;
    m_snap.sleep_auto = g_service_param.sleep.sleep_auto ? 1 : 0;
    m_snap.led_mode_sel = g_service_param.sys.led_mode % LED_OPT_NUM;
    m_snap.wake_sel   = idx_of_u16(WAKE_OPTS_MIN, WAKE_OPT_NUM, g_service_param.sleep.sleep_time);
    m_snap.hb_sel     = idx_of_u16(HB_OPTS_SEC, HB_OPT_NUM, g_service_param.network.film_heartbeat_interval);

    /* 开机行为（app 层参数，存在 app7 槽位）：下标即存盘值，直接取 */
    m_snap.boot_page_sel = app_boot_cfg_get()->boot_page % BOOTPAGE_OPT_NUM;
    m_snap.start_app_sel = app_boot_cfg_get()->start_app % STARTAPP_OPT_NUM;
    m_snap.language_sel = app_language_get();

    /* app 参数不在 service_param 里，走归属 app 的参数通道读回 */
    settings_app_params_pull();
}

/**
 * @brief 写一条 app 参数（TLV）到归属 app
 *
 * 只负责组帧 + 转交；落盘与"是否立刻刷屏"都由 app_manager / 归属 app 决定
 * （见 app_manager_param_set）。
 */
static void app_param_put_u8(uint8_t app_id, uint8_t tag, uint8_t v)
{
    uint8_t tlv[4];
    uint8_t n = app_tlv_put_u8(tlv, tag, v);

    app_manager_param_set(app_id, tlv, n);
}

static void app_param_put_u16(uint8_t app_id, uint8_t tag, uint16_t v)
{
    uint8_t tlv[4];
    uint8_t n = app_tlv_put_u16(tlv, tag, v);

    app_manager_param_set(app_id, tlv, n);
}

/**
 * @brief 落地一条页面改动（app_task 上下文）
 *
 * 值先校验再写：页面只上报"行号 + 候选下标"，行号/值都可能因版本不匹配而越界。
 * WiFi 开关与 BLE 通道设置走同一条路径（置参数 + 落盘 + 起停协议栈）；
 * app 参数则转交给归属 app 自己的参数通道。
 */
static void settings_apply(uint8_t row, uint8_t value)
{
    if(row >= SETTINGS_ROW_NUM)
    {
        return;
    }

    switch(ROWS[row].field)
    {
    case F_WIFI_ON:
        if(value > 1) { return; }
        g_service_param.network.wifi_enable = value;
        service_param_save();
        if(value)
        {
            service_wifi_init();
        }
        else
        {
            service_wifi_disconnect();
            service_wifi_deinit();
        }
        break;

    case F_BT_ON:
        if(value > 1) { return; }
        /* 与 WiFi 同一套语义：置参数 + 立刻起停协议栈（不再等重启） */
        service_ble_apply_enable(value);
        service_param_save();
        break;

#if (SYS_NFC_ENABLE == 1)
    /* NFC 总开关：落盘 + 立即起停模拟（开关/卡库状态由卡包模块内部校验） */
    case F_NFC:
        if(value > 1) { return; }
        app_wallet_set_nfc_enabled(value);
        break;
#endif

    /* LED 模式：只落参数，运行期由 monitor 的 LED 事件按它决定亮/不亮（最迟 500ms 生效） */
    case F_LED:
        if(value >= LED_OPT_NUM) { return; }
        g_service_param.sys.led_mode = value;
        service_param_save();
        break;

    case F_SLEEP:
        if(value > 1) { return; }
        g_service_param.sleep.sleep_mode = value;
        service_param_save();
        break;

    case F_AUTOWAKE:
        if(value > 1) { return; }
        g_service_param.sleep.sleep_auto = value;
        service_param_save();
        break;

    case F_WAKE_SEL:
        if(value >= WAKE_OPT_NUM) { return; }
        g_service_param.sleep.sleep_time = WAKE_OPTS_MIN[value];
        service_param_save();
        break;

    case F_HB_SEL:
        if(value >= HB_OPT_NUM) { return; }
        g_service_param.network.film_heartbeat_interval = (uint8_t)HB_OPTS_SEC[value];
        service_param_save();
        break;

    /* ---- 开机行为：两个字段一起写（本行只带一个值，另一个保持当前） ---- */
    case F_BOOT_PAGE:
        if(value >= BOOTPAGE_OPT_NUM) { return; }
        app_boot_cfg_set(value, app_boot_cfg_get()->start_app);
        break;

    case F_START_APP:
        if(value >= STARTAPP_OPT_NUM) { return; }
        app_boot_cfg_set(app_boot_cfg_get()->boot_page, value);
        break;
    case F_LANGUAGE:
        if(value > APP_LANGUAGE_EN) { return; }
        app_language_set(value);
        break;

    /* ---- app 参数：转交归属 app 的参数通道（TAG/范围由 app 自己的头文件定） ---- */
    case F_IMG_PLAY:
        if(value >= IMG_PLAY_OPT_NUM) { return; }
        app_param_put_u8((uint8_t)APP_ID_IMAGE, APP_IMAGE_TAG_PLAY_MODE, value);
        break;

    case F_IMG_INT:
        if(value >= IMG_INT_OPT_NUM) { return; }
        app_param_put_u16((uint8_t)APP_ID_IMAGE, APP_IMAGE_TAG_INTERVAL, IMG_INT_OPTS_MIN[value]);
        break;

    case F_ANIM_LOOP:
        if(value >= ANIM_LOOP_OPT_NUM) { return; }
        app_param_put_u8((uint8_t)APP_ID_ANIMATION, APP_ANIM_TAG_PLAY_MODE, value);
        break;

    case F_ANIM_SPEED:
        if(value >= ANIM_SPEED_OPT_NUM) { return; }
        app_param_put_u16((uint8_t)APP_ID_ANIMATION, APP_ANIM_TAG_FRAME_MS, ANIM_SPEED_OPTS_MS[value]);
        break;

    default:
        return;   // 只读行不接受写入
    }

    sys_logi(APP_SET_TAG, "apply row=%u(%s) value=%u",
             (unsigned)row, ROWS[row].key, (unsigned)value);
}

/**
 * @brief 把当前快照下发给页面（下行动作，只投递不渲染）
 *
 * 带去重：页面侧每 10s 会要一次（见 app_shell_start_tick），但设置页的快照里
 * 真正会变的只有电量/连接状态这几个字节 —— 值没变就不下发，否则 set_apply()
 * 会无条件重写一堆控件，每 10s 白刷一整帧电子纸。
 *
 * @param force 1 = 忽略去重（页面刚建好、或刚 APPLY 完，必须给一帧真实值）
 */
static void settings_snapshot_push(uint8_t force)
{
    static settings_snapshot_t s_last;
    static uint8_t s_last_valid = 0;

    settings_snapshot_pull();

    if(!force && s_last_valid && (memcmp(&s_last, &m_snap, sizeof(m_snap)) == 0))
    {
        return;
    }
    memcpy(&s_last, &m_snap, sizeof(s_last));
    s_last_valid = 1;

    (void)ui_core_post(APP_UI_MSG_SETTINGS_SNAPSHOT, &m_snap, (uint8_t)sizeof(m_snap));
}

/**
 * @brief 设置 app 的事件处理（app_task 上下文）
 *
 * 只处理页面发来的上行请求；其余事件（BLE/网络）本 app 不关心。
 */
static void settings_on_event(const app_event_t *e)
{
    if(e->type != APP_EVT_UI_MSG)
    {
        return;
    }

    if(e->cmd == APP_UI_REQ_SETTINGS_SYNC)
    {
        /* 页面刚建好 / 周期 tick：给首帧真实值。去重由 settings_snapshot_push 负责，
           所以周期请求不会造成无谓刷屏。 */
        settings_snapshot_push(0);
        return;
    }

    if(e->cmd == APP_UI_REQ_SETTINGS_APPLY && e->len >= 2)
    {
        settings_apply(e->payload[0], e->payload[1]);
        /* 回刷以设备侧真实值为准：如 WiFi 打开后状态文案由 OFF 变 IDLE。
           这里 force=1：刚改完必须回投一帧，否则"改回原值"这类操作会因为
           去重被吞掉，页面停在乐观更新的结果上。 */
        settings_snapshot_push(1);
    }
}

/**
 * @brief 按快照与当前选择刷新整页
 *
 * 选中行 = 实心黑底 + 反白文字（1-bit 下最强的对比），并在行首给三角指针；
 * 只读行不给指针，明确传达"这行不可操作"。
 */
static void set_apply(void)
{
    uint8_t i;

    for(i = 0; i < SETTINGS_ROW_NUM; i++)
    {
        int sel = (i == m_sel);
        lv_color_t fg = sel ? lv_color_white() : lv_color_black();
        const settings_row_t *r = &ROWS[i];

        if(m_rows[i] == NULL)
        {
            continue;
        }

        lv_obj_set_style_bg_opa(m_rows[i], sel ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_bg_color(m_rows[i], lv_color_black(), LV_PART_MAIN);

        /* 键名也要跟着反白：整行是黑底，键名不变色就成了黑底黑字 */
        if(m_row_key[i] != NULL)
        {
            lv_obj_set_style_text_color(m_row_key[i], fg, LV_PART_MAIN);
            lv_label_set_text(m_row_key[i], app_text(r->key, ROW_KEYS_EN[i]));
            lv_obj_set_style_text_font(m_row_key[i], app_font_small(), LV_PART_MAIN);
        }
        if(m_row_ptr[i] != NULL)
        {
            /* 只读行不给指针 */
            lv_obj_set_style_bg_opa(m_row_ptr[i], (sel && !row_is_ro(i)) ? LV_OPA_COVER : LV_OPA_TRANSP,
                                    LV_PART_MAIN);
        }
        if(m_row_val[i] != NULL)
        {
            lv_obj_set_style_text_color(m_row_val[i], fg, LV_PART_MAIN);
            lv_obj_set_style_text_font(m_row_val[i],
                                       app_font_small_text(row_text(i)), LV_PART_MAIN);
            /* 开关行的 ON/OFF 由下面的开关块分支接管，其余（含电量条的 "82%"）都取快照文本 */
            if(r->kind != SET_KIND_TOGGLE)
            {
                lv_label_set_text(m_row_val[i], row_text(i));
            }
        }
        if(m_row_tog[i] != NULL)
        {
            /* 开关块：ON 实心（黑底白字 / 选中行白底黑字），OFF 空心描边 */
            uint8_t on = row_on(i);

            lv_obj_set_style_border_color(m_row_tog[i], fg, LV_PART_MAIN);
            lv_obj_set_style_bg_color(m_row_tog[i], fg, LV_PART_MAIN);
            lv_obj_set_style_bg_opa(m_row_tog[i], on ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_MAIN);
            if(m_row_val[i] != NULL)
            {
                lv_label_set_text(m_row_val[i], on ? app_text("开", "ON") : app_text("关", "OFF"));
                /* ON 时文字要相对开关自身底色反色 */
                lv_obj_set_style_text_color(m_row_val[i],
                                            on ? (sel ? lv_color_black() : lv_color_white()) : fg,
                                            LV_PART_MAIN);
            }
        }
        if(m_row_st[i] != NULL)
        {
            /* 开关行左侧的状态文案（WiFi / 蓝牙）：取设备侧真实状态，随快照刷新 */
            lv_obj_set_style_text_color(m_row_st[i], fg, LV_PART_MAIN);
            lv_obj_set_style_text_font(m_row_st[i], app_font_small(), LV_PART_MAIN);
            lv_label_set_text(m_row_st[i], row_text(i));
        }
        if(m_row_fill[i] != NULL)
        {
            /* 填充块按内宽等比：满电正好铺满外框内侧 */
            lv_obj_set_style_bg_color(m_row_fill[i], fg, LV_PART_MAIN);
            lv_obj_set_width(m_row_fill[i], (int32_t)(SET_BAR_FILL_W * m_snap.bat_pct / 100));
        }
    }

    if(m_row_idx_label != NULL)
    {
        lv_obj_set_style_text_font(m_row_idx_label, app_font_small(), LV_PART_MAIN);
        if(m_sel < SETTINGS_ROW_NUM)
        {
            lv_label_set_text_fmt(m_row_idx_label, app_text("第 %02u / %02u 项", "ROW %02u / %02u"),
                                  (unsigned)(m_sel + 1), (unsigned)SETTINGS_ROW_NUM);
        }
        else
        {
            /* 还没选中任何行（刚进页面） */
            lv_label_set_text_fmt(m_row_idx_label, app_text("未选 / %02u 项", "ROW -- / %02u"), (unsigned)SETTINGS_ROW_NUM);
        }
    }
    if(m_head_label != NULL)
    {
        lv_label_set_text(m_head_label, app_text("系统设置", "SYSTEM CONFIGURATION"));
        lv_obj_set_style_text_font(m_head_label, app_font_small(), LV_PART_MAIN);
    }
    for(i = 0; i < 3; i++)
    {
        if(m_sec_label[i] != NULL)
        {
            lv_label_set_text(m_sec_label[i], app_text(SEC_TITLES[i], SEC_EN[i]));
            lv_obj_set_style_text_font(m_sec_label[i], app_font_small(), LV_PART_MAIN);
        }
    }
    app_shell_set_text(&m_shell,
                       app_text("上下选择  确认修改  双击返回  长按休眠", "UP/DOWN  ENTER TOGGLE  DBL EXIT  HOLD SLEEP"),
                       app_text("设置", "SETTINGS"));
}

/**
 * @brief 跳到下一个可操作行（跳过只读行）
 *
 * 未选中时（刚进页面）从列表两端起算：UP 落第一条、DOWN 落最后一条。
 */
static void set_move(int delta)
{
    uint8_t i;
    uint8_t step;

    if(m_sel < SETTINGS_ROW_NUM)
    {
        i = m_sel;
    }
    else
    {
        i = (delta > 0) ? (uint8_t)(SETTINGS_ROW_NUM - 1) : 0;
    }

    for(step = 0; step < SETTINGS_ROW_NUM; step++)
    {
        i = (uint8_t)((i + delta + SETTINGS_ROW_NUM) % SETTINGS_ROW_NUM);
        if(!row_is_ro(i))
        {
            m_sel = i;
            set_apply();
            return;
        }
    }
}

/**
 * @brief ENTER：切换开关 / 循环取值，并把结果上报给 app 任务落盘
 *
 * 页面只做"乐观更新"以即时反馈；真正的 g_service_param 写入由 app 任务串行执行
 * （service_param 模块无内部锁）。
 */
static void set_toggle(void)
{
    uint8_t req[2];
    const settings_row_t *r;

    /* 未选中（刚进页面）不接受 ENTER：m_sel 越界时连 ROWS[m_sel] 都不能碰 */
    if(m_sel >= SETTINGS_ROW_NUM || row_is_ro(m_sel))
    {
        return;
    }

    r = &ROWS[m_sel];
    req[0] = m_sel;

    switch(r->field)
    {
    case F_WIFI_ON:     m_snap.wifi_on    = (uint8_t)!m_snap.wifi_on;    req[1] = m_snap.wifi_on;    break;
    case F_BT_ON:       m_snap.bt_on      = (uint8_t)!m_snap.bt_on;      req[1] = m_snap.bt_on;      break;
#if (SYS_NFC_ENABLE == 1)
    case F_NFC:         m_snap.nfc_on     = (uint8_t)!m_snap.nfc_on;     req[1] = m_snap.nfc_on;     break;
#endif
    case F_SLEEP:       m_snap.sleep_mode = (uint8_t)!m_snap.sleep_mode; req[1] = m_snap.sleep_mode; break;
    case F_AUTOWAKE:    m_snap.sleep_auto = (uint8_t)!m_snap.sleep_auto; req[1] = m_snap.sleep_auto; break;
    case F_LED:         m_snap.led_mode_sel = (uint8_t)((m_snap.led_mode_sel + 1) % LED_OPT_NUM);
                        req[1] = m_snap.led_mode_sel;  break;
    case F_WAKE_SEL:    m_snap.wake_sel   = (uint8_t)((m_snap.wake_sel + 1) % WAKE_OPT_NUM);
                        req[1] = m_snap.wake_sel;   break;
    case F_HB_SEL:      m_snap.hb_sel     = (uint8_t)((m_snap.hb_sel + 1) % HB_OPT_NUM);
                        req[1] = m_snap.hb_sel;     break;
    case F_BOOT_PAGE:   m_snap.boot_page_sel = (uint8_t)((m_snap.boot_page_sel + 1) % BOOTPAGE_OPT_NUM);
                        req[1] = m_snap.boot_page_sel; break;
    case F_START_APP:   m_snap.start_app_sel = (uint8_t)((m_snap.start_app_sel + 1) % STARTAPP_OPT_NUM);
                        req[1] = m_snap.start_app_sel; break;
    case F_LANGUAGE:    m_snap.language_sel = (uint8_t)!m_snap.language_sel;
                        req[1] = m_snap.language_sel; break;
    case F_IMG_PLAY:    m_snap.img_play_sel     = (uint8_t)((m_snap.img_play_sel + 1) % IMG_PLAY_OPT_NUM);
                        req[1] = m_snap.img_play_sel;      break;
    case F_IMG_INT:     m_snap.img_interval_sel = (uint8_t)((m_snap.img_interval_sel + 1) % IMG_INT_OPT_NUM);
                        req[1] = m_snap.img_interval_sel;  break;
    case F_ANIM_LOOP:   m_snap.anim_loop_sel    = (uint8_t)((m_snap.anim_loop_sel + 1) % ANIM_LOOP_OPT_NUM);
                        req[1] = m_snap.anim_loop_sel;     break;
    case F_ANIM_SPEED:  m_snap.anim_speed_sel   = (uint8_t)((m_snap.anim_speed_sel + 1) % ANIM_SPEED_OPT_NUM);
                        req[1] = m_snap.anim_speed_sel;    break;
    default:            return;
    }

    set_apply();
    app_manager_post_ui_msg(APP_UI_REQ_SETTINGS_APPLY, req, sizeof(req));
}

static void set_ui_create(lv_obj_t *root)
{
    lv_obj_t *body;
    lv_obj_t *head;
    lv_obj_t *list;
    uint8_t i;
    uint8_t cur_sec = 0xFF;
    int32_t y = 0;

    sys_logi(APP_SET_TAG, "create settings page");

    /* 外壳：顶部状态栏 + 底部提示行（与主菜单同一套版式） */
    app_shell_build(root, app_text("上下选择  确认修改  双击返回  长按休眠",
                                   "UP/DOWN  ENTER TOGGLE  DBL EXIT  HOLD SLEEP"),
                    app_text("设置", "SETTINGS"), &m_shell);

    /* 正文：夹在状态栏与提示行之间，左右留安全边距 */
    body = lv_obj_create(root);
    lv_obj_remove_style_all(body);
    lv_obj_set_scrollable(body, false);
    lv_obj_set_size(body, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_top(body, SHELL_BODY_TOP, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(body, SHELL_BODY_BOTTOM, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(body, SHELL_BODY_PAD_X, LV_PART_MAIN);

    /* 表头 */
    head = lv_obj_create(body);
    lv_obj_remove_style_all(head);
    lv_obj_set_scrollable(head, false);
    lv_obj_set_size(head, LV_PCT(100), 16);
    lv_obj_set_pos(head, 0, 0);
    m_head_label = set_label(head, app_font_small(), lv_color_black(), app_text("系统设置", "SYSTEM CONFIGURATION"));
    m_row_idx_label = set_label(head, app_font_small(), lv_color_black(), "");
    lv_obj_align(m_row_idx_label, LV_ALIGN_TOP_RIGHT, 0, 0);

    list = lv_obj_create(body);
    lv_obj_remove_style_all(list);
    lv_obj_set_scrollable(list, false);
    lv_obj_set_size(list, LV_PCT(100), SET_LIST_H);
    lv_obj_set_pos(list, 0, 24);

    for(i = 0; i < SETTINGS_ROW_NUM; i++)
    {
        const settings_row_t *r = &ROWS[i];
        lv_obj_t *row;

        /* 分节标题：实心小标签 + 横线 */
        if(r->sec != cur_sec)
        {
            lv_obj_t *sh = lv_obj_create(list);
            lv_obj_t *tab;
            lv_obj_t *line;

            cur_sec = r->sec;
            lv_obj_remove_style_all(sh);
            lv_obj_set_scrollable(sh, false);
            lv_obj_set_size(sh, LV_PCT(100), 17);
            lv_obj_set_pos(sh, 0, y);
            y += 19;

            tab = lv_obj_create(sh);
            lv_obj_remove_style_all(tab);
            lv_obj_set_scrollable(tab, false);
            lv_obj_set_size(tab, 96, 17);
            lv_obj_set_style_bg_color(tab, lv_color_black(), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(tab, LV_OPA_COVER, LV_PART_MAIN);
            {
                lv_obj_t *t = set_label(tab, app_font_small(), lv_color_white(),
                                        app_text(SEC_TITLES[cur_sec], SEC_EN[cur_sec]));
                lv_obj_center(t);
                m_sec_label[cur_sec] = t;
            }
            line = lv_obj_create(sh);
            lv_obj_remove_style_all(line);
            lv_obj_set_scrollable(line, false);
            lv_obj_set_size(line, 322, 1);
            lv_obj_set_style_bg_color(line, lv_color_black(), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(line, LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_align(line, LV_ALIGN_LEFT_MID, 104, 0);
        }

        /* 行 */
        row = lv_obj_create(list);
        lv_obj_remove_style_all(row);
        lv_obj_set_scrollable(row, false);
        lv_obj_set_size(row, LV_PCT(100), ROW_H);
        lv_obj_set_pos(row, 0, y);
        lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
        lv_obj_set_style_border_width(row, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(row, lv_color_black(), LV_PART_MAIN);
        y += ROW_H;

        m_rows[i] = row;

        /* 选择指针（只读行恒隐藏） */
        {
            lv_obj_t *p = lv_obj_create(row);
            lv_obj_remove_style_all(p);
            lv_obj_set_scrollable(p, false);
            lv_obj_set_size(p, 9, 12);
            lv_obj_set_style_bg_color(p, lv_color_white(), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(p, LV_OPA_TRANSP, LV_PART_MAIN);
            lv_obj_align(p, LV_ALIGN_LEFT_MID, 0, 0);
            m_row_ptr[i] = p;
        }

        {
            /* 原版 29px 行高与行位置不变；中文字使用等尺寸字库。 */
            lv_obj_t *k = set_label(row, app_font_small(), lv_color_black(), app_text(r->key, ROW_KEYS_EN[i]));

            lv_obj_align(k, LV_ALIGN_LEFT_MID, 14, 0);
            m_row_key[i] = k;   // 选中行整行反白时，键名要跟着变白
        }

        if(r->kind == SET_KIND_BAR)
        {
            lv_obj_t *track = lv_obj_create(row);
            lv_obj_t *fill;

            lv_obj_remove_style_all(track);
            lv_obj_set_scrollable(track, false);
            lv_obj_set_size(track, SET_BAR_TRACK_W, SET_BAR_TRACK_H);
            lv_obj_set_style_border_width(track, 1, LV_PART_MAIN);
            lv_obj_set_style_border_color(track, lv_color_black(), LV_PART_MAIN);
            lv_obj_align(track, LV_ALIGN_RIGHT_MID, -54, 0);

            fill = lv_obj_create(track);
            lv_obj_remove_style_all(fill);
            lv_obj_set_scrollable(fill, false);
            /* remove_style_all 把 bg_opa 也清成透明了，这里必须显式打开，
               否则电量条是个只有外框的空槽（宽度由 set_apply 按电量给） */
            lv_obj_set_style_bg_opa(fill, LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_set_size(fill, 0, SET_BAR_TRACK_H - 2);
            lv_obj_align(fill, LV_ALIGN_LEFT_MID, 0, 0);
            m_row_fill[i] = fill;

            m_row_val[i] = set_label(row, app_font_small(), lv_color_black(), "");
            lv_obj_align(m_row_val[i], LV_ALIGN_RIGHT_MID, 0, 0);
        }
        else if(r->kind == SET_KIND_TOGGLE)
        {
            lv_obj_t *tog = lv_obj_create(row);
            lv_obj_t *t;

            lv_obj_remove_style_all(tog);
            lv_obj_set_scrollable(tog, false);
            lv_obj_set_size(tog, 40, 18);
            lv_obj_set_style_border_width(tog, 1, LV_PART_MAIN);
            lv_obj_set_style_border_color(tog, lv_color_black(), LV_PART_MAIN);
            lv_obj_align(tog, LV_ALIGN_RIGHT_MID, 0, 0);
            m_row_tog[i] = tog;

            t = set_label(tog, app_font_small(), lv_color_black(), "开");
            lv_obj_center(t);
            m_row_val[i] = t;

            /* 开关行左侧的状态文案（WiFi / 蓝牙 / NFC：状态 + 开关同一行） */
            if(ROWS[i].field == F_WIFI_ON || ROWS[i].field == F_BT_ON
#if (SYS_NFC_ENABLE == 1)
               || ROWS[i].field == F_NFC
#endif
              )
            {
                lv_obj_t *st = set_label(row, app_font_small(), lv_color_black(), "");
                lv_obj_align(st, LV_ALIGN_RIGHT_MID, -48, 0);
                m_row_st[i] = st;   // 文字与颜色由 set_apply 按快照刷新
            }
        }
        else
        {
            m_row_val[i] = set_label(row, app_font_small(), lv_color_black(), "");
            lv_obj_set_width(m_row_val[i], 220);
            lv_label_set_long_mode(m_row_val[i], LV_LABEL_LONG_DOT);
            lv_obj_set_style_text_align(m_row_val[i], LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
            lv_obj_align(m_row_val[i], LV_ALIGN_RIGHT_MID, 0, 0);
        }
    }

    set_apply();

    /* 首帧只是静态骨架（可见项为页面初始值）；真实设备状态由 app 任务侧采集后回投
       —— ui_task 不直接读 g_service_param / 电池 / WiFi，那些没有跨任务保证 */
    (void)app_manager_post_ui_msg(APP_UI_REQ_SETTINGS_SYNC, NULL, 0);

    /* 空闲时也要刷新（电量 / 状态栏时间）：本页数据全部来自快照，所以 tick 的钩子是
       "再要一份快照"而不是默认的 app_shell_request_status()。app 任务侧对快照做了
       去重，值没变不会回投，也就不会白刷屏。 */
    app_shell_start_tick(&m_shell, settings_tick_request);
}

static void set_ui_destroy(void)
{
    memset(m_rows, 0, sizeof(m_rows));
    memset(m_row_key, 0, sizeof(m_row_key));
    memset(m_row_fill, 0, sizeof(m_row_fill));
    memset(m_row_val, 0, sizeof(m_row_val));
    memset(m_row_tog, 0, sizeof(m_row_tog));
    memset(m_row_ptr, 0, sizeof(m_row_ptr));
    memset(m_row_st, 0, sizeof(m_row_st));
    m_row_idx_label = NULL;
    m_head_label = NULL;
    memset(m_sec_label, 0, sizeof(m_sec_label));
    /* 释放外壳：顺带删周期定时器（它不在对象树里，不删会变野指针） */
    app_shell_release(&m_shell);
    sys_logi(APP_SET_TAG, "destroy settings page");
}

static void set_ui_on_key(input_press_type_t key)
{
    switch(key)
    {
    case INPUT_PRESS_UP:    set_move(+1);   break;
    case INPUT_PRESS_DOWN:  set_move(-1);   break;
    case INPUT_PRESS_SHORT: set_toggle();   break;
    default:                                break;   // 长按/双击由 app_manager 处理（长按=休眠、双击=退回主菜单）
    }
}

static void set_ui_on_msg(uint32_t cmd, const void *data, uint8_t len)
{
    if(cmd == APP_UI_MSG_SETTINGS_SNAPSHOT && data != NULL && len == sizeof(settings_snapshot_t))
    {
        app_status_t st;

        memcpy(&m_snap, data, sizeof(m_snap));
        set_apply();

        /* 状态栏要的几项快照里都有，直接复用，省一次上行/下行往返 */
        st.bat_pct = m_snap.bat_pct;
        st.wifi_on = m_snap.wifi_on;
        st.wifi_conn = m_snap.wifi_conn;
        st.bt_on   = m_snap.bt_on;
        st.bt_conn = m_snap.bt_conn;
        st.nfc_on  = m_snap.nfc_on;
        st.nfc_emu = m_snap.nfc_emu;
        app_shell_apply(&m_shell, &st);
        return;
    }

    (void)app_shell_handle_msg(&m_shell, cmd, data, len);
}

/*********************************************************************
 * GLOBAL VARIABLES
 */
static const app_ui_ops_t g_settings_ui_ops = {
    .create  = set_ui_create,
    .destroy = set_ui_destroy,
    .on_msg  = set_ui_on_msg,
    .on_key  = set_ui_on_key,
};

const app_entry_t g_app_settings_entry = {
    .id = APP_ID_SETTINGS,
    .name = "settings",
    .data_dir = NULL,
    .keys = APP_KEY_UP | APP_KEY_DOWN | APP_KEY_SHORT,   // 长按不占用：留给"退出到主菜单"
    .tick_ms = 0,
    .events = NULL,
    .on_event = settings_on_event,
    .layer = APP_LAYER_UI,
    .ui_ops = &g_settings_ui_ops,
};
