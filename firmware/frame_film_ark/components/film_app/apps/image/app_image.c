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
 * FileName : /film_app/apps/image/app_image.c
 * Author: Kiritro  Version: v0.1  Date: 2026/9/9
 * Description: 图片 app：本地 TF / BLE / WiFi 三来源，全屏 film 渲染
 * ChangeLog: Change Notes
 *
 *********************************************************************/


/*********************************************************************
 * INCLUDES
 */
#include "sys_log.h"
#include "sys_event.h"
#include "hal_input.h"
#include "app_image.h"
#include "app_manager.h"
#include "service_film.h"
#include "service_file.h"
#include "service_param.h"
#include "service_ble.h"

/*********************************************************************
 * MACROS
 */
#define APP_IMAGE_TAG           "app_image"

/* 播放模式 / 间隔范围 / 参数通道 TAG 都定义在 app_image.h：设置页按同一套
   TAG 读写参数（见 app_manager_param_get/set），语义只在头文件里放一份 */

/* on_tick 周期（毫秒）：定时间隔以秒计时即可，1s 一拍 */
#define APP_IMAGE_TICK_MS           (1000)

/*********************************************************************
 * TYPEDEFS
 */
/**
 * @brief 图片 app 状态（持久化到 NVS）
 */
typedef struct {
    uint8_t  play_mode;      // 0=手动 1=自动
    uint16_t auto_interval;  // 自动切换间隔（分钟，1 ~ 120），仅休眠关闭时生效
    uint32_t file_id;        // 当前播放的文件下标
} app_image_state_t;

/* 状态结构体不得超过 NVS blob 的 app 数据上限 */
_Static_assert(sizeof(app_image_state_t) <= SERVICE_PARAM_APP_DATA_MAX,
               "app_image_state_t exceeds SERVICE_PARAM_APP_DATA_MAX");

/*********************************************************************
 * LOCAL VARIABLES
 */
static app_image_state_t m_image;
/* First draw is owned by the enter/boot/save path, not load_complete:
   the file task may still be loading after the first display was queued. */
static bool m_first_draw_pending = false;
static uint32_t m_open_file_id = FILE_ID_NONE;
static bool m_skip_boot_advance = false;
static uint32_t m_elapsed_s = 0;   // 定时切换累计秒数（RAM，不持久化）

static const app_image_state_t m_image_default = {
    .play_mode = APP_IMAGE_PLAY_MANUAL,
    .auto_interval = 10,
    .file_id = 0,
};

/* 关注的全局事件：文件落盘（自动显示新图）、列表刷新（首帧补显） */
static const uint16_t m_image_events[] = {
    SYS_EVT_FILE_SAVED,
    SYS_EVT_FILE_LIST,
    0,
};

/*********************************************************************
 * LOCAL FUNCTIONS
 */
static void app_image_on_enter(void);
static void app_image_on_event(const app_event_t *e);
static void app_image_on_tick(void);
static void app_image_param_set(const uint8_t *tlv, uint8_t len);
static uint8_t app_image_param_get(uint8_t *out, uint8_t max);
static void image_show(uint32_t file_id);
static void image_show_next(void);
static const char *app_image_enter_block_reason(void);
static bool app_image_prepare_open(const char *filename);

/*********************************************************************
 * GLOBAL VARIABLES
 */
const app_entry_t g_app_image_entry = {
    .id = APP_ID_IMAGE,
    .name = "image",
    .data_dir = FILM_DIR,     // 图片数据目录，切换时由 app_manager 同步设好
    .keys = APP_KEY_UP | APP_KEY_DOWN,  // 上/下：上一张/下一张（简易模式切换需避让）
    .tick_ms = APP_IMAGE_TICK_MS,  // 定时切换以秒计时
    .events = m_image_events,
    .on_enter = app_image_on_enter,
    .on_exit = NULL,
    .on_event = app_image_on_event,
    .on_tick = app_image_on_tick,
    .enter_block_reason = app_image_enter_block_reason,
    .prepare_open = app_image_prepare_open,

    /* 睡后留画面：屏上这张照片本来就是用户要挂着的，不该被休眠卡盖掉 */
    .sleep_face = APP_SLEEP_FACE_KEEP,

    /* 状态持久化：休眠唤醒后仍能记住播放模式 / 间隔 / 当前下标 */
    .state = &m_image,
    .state_size = (uint16_t)sizeof(m_image),
    .state_ver = 1,
    .state_default = &m_image_default,

    /* BLE 参数通道：0x45 设置 / 0x46 查询 */
    .param_ch = BLE_FILM_TRANS_CH_APP_IMAGE_PARAM,
    .on_param_set = app_image_param_set,
    .on_param_get = app_image_param_get,
};

/*********************************************************************
 * LOCAL FUNCTIONS
 */

/**
 * @brief 显示指定下标的图片
 *
 * 下标越界回落 0；下标变更时落盘（低频操作）。无文件时 service_film 内部会提示。
 *
 * @param file_id 文件下标
 */
static void image_show(uint32_t file_id)
{
    uint32_t count = service_file_get_count();
    if(count == 0)
    {
        sys_logw(APP_IMAGE_TAG, "no file to show");
        return;
    }
    if(file_id >= count)
    {
        file_id = 0;
    }

    if(file_id != m_image.file_id)
    {
        m_image.file_id = file_id;
        app_state_save();   // 进度类：变更即写（按键/自动切换均为低频）
    }

    m_elapsed_s = 0;   // 手动或自动换图都重新计时
    m_first_draw_pending = false;
    service_film_display(file_id);
}

/**
 * @brief 显示下一张（循环）
 */
static void image_show_next(void)
{
    uint32_t count = service_file_get_count();
    if(count == 0)
    {
        sys_logw(APP_IMAGE_TAG, "no file to show");
        return;
    }
    image_show((m_image.file_id + 1) % count);
}

/**
 * @brief 进入图片 app：显示上次看到的图片
 *
 * 数据目录由 app_manager 在切换时同步设置为 FILM_DIR，此处直接使用列表。
 */
/**
 * @brief 进入前预检：图片目录里没有 .film 就拦下来
 *
 * image_show() 在无文件时只打日志、不绘制，切过去屏幕仍停在上一帧 —— 用户会以为
 * 已经进到图片 app 了。所以在这里（app_manager 已切好 FILM_DIR 之后）先判掉。
 * 拦下后主菜单会显示原因，切换动作整体不生效。
 */
static const char *app_image_enter_block_reason(void)
{
    if(service_file_get_count() == 0)
    {
        return "NO FILM IN /sdcard/film";
    }
    return NULL;
}

/**
 * @brief "开机自动切图"是否生效
 *
 * on_enter（决定首帧要不要现在画）与 APP_EVT_BOOT 两处判据必须完全一致，
 * 否则会出现"谁都不画"或"连刷两屏"，故集中在这里。
 */
static bool image_boot_advance_effective(void)
{
    return m_image.play_mode == APP_IMAGE_PLAY_AUTO && g_service_param.sleep.sleep_mode == 1;
}

static bool app_image_prepare_open(const char *filename)
{
    uint32_t file_id;
    if(service_file_find_id(filename, &file_id) != 0) return false;
    m_open_file_id = file_id;
    return true;
}

static void app_image_on_enter(void)
{
    m_first_draw_pending = true;
    m_skip_boot_advance = m_open_file_id != FILE_ID_NONE;
    if(m_skip_boot_advance)
    {
        uint32_t file_id = m_open_file_id;
        m_open_file_id = FILE_ID_NONE;
        image_show(file_id);
        return;
    }
    sys_logi(APP_IMAGE_TAG, "enter image app, count=%u id=%u",
             (unsigned)service_file_get_count(), (unsigned)m_image.file_id);

    /* 本次启动还会补投 APP_EVT_BOOT，而它生效时会推进到下一张并重画：
       首帧就交给那条路径画，否则同一张（或相邻两张）会在 EPD 上连刷两屏，
       每屏约 1s，看着像"第一张刷新了两次"（见 app_manager_boot_advance_pending） */
    if(app_manager_boot_advance_pending() && image_boot_advance_effective())
    {
        /* APP_EVT_BOOT owns this first draw, including late list events. */
        m_first_draw_pending = false;
        sys_logi(APP_IMAGE_TAG, "boot auto switch pending, defer first draw");
        return;
    }

    /* m_image 已由 app_state_load() 载入；image_show 内部处理越界回落 */
    image_show(m_image.file_id);
}

/**
 * @brief 图片 app 事件处理
 */
static void app_image_on_event(const app_event_t *e)
{
    if(e->type == APP_EVT_INPUT)
    {
        uint32_t count = service_file_get_count();
        if(count == 0)
        {
            sys_logw(APP_IMAGE_TAG, "no file to show");
            return;
        }

        switch(e->input)
        {
        case INPUT_PRESS_UP:   // 下一张（循环）
            image_show((m_image.file_id + 1) % count);
            break;
        case INPUT_PRESS_DOWN: // 上一张（循环）
            image_show((m_image.file_id + count - 1) % count);
            break;
        default:
            break;
        }
        return;
    }

    /* 开机自动：仅启动后投递一次（用户手动切回图片 app 不会触发） */
    if(e->type == APP_EVT_BOOT)
    {
        if(m_skip_boot_advance)
        {
            m_skip_boot_advance = false;
            return;
        }
        if(image_boot_advance_effective())
        {
            sys_logi(APP_IMAGE_TAG, "boot auto switch");
            image_show_next();
        }
        return;
    }

    if(e->type != APP_EVT_SYS)
    {
        return;
    }

    switch((sys_event_id_t)e->cmd)
    {
    case SYS_EVT_FILE_SAVED:
        /* payload[0] = auto_load：0 表示静默保存（批量上传），不自动刷新显示 */
        if(e->len == 0 || e->payload[0] != 0)
        {
            /* 以下载/推送后的当前文件为准，同步回 app 状态 */
            sys_logi(APP_IMAGE_TAG, "file saved, display id=%u", (unsigned)service_file_get_current_id());
            image_show(service_file_get_current_id());
        }
        break;

    case SYS_EVT_FILE_LIST:
        /* 保存刷新与 FILE_SAVED 成对；由后者决定显示新图或静默保存。
           load_complete 在保存后失效，不能把它当作需要补显旧图的依据。 */
        if(e->len > sizeof(uint32_t) &&
           e->payload[sizeof(uint32_t)] == SYS_FILE_LIST_SAVE_PENDING)
        {
            break;
        }
        /* 列表刷新完成：若尚未显示过（首帧竞态/列表晚于超时到达），补显一次 */
        if(m_first_draw_pending && service_file_get_count() > 0)
        {
            sys_logi(APP_IMAGE_TAG, "list ready, display id=%u", (unsigned)m_image.file_id);
            image_show(m_image.file_id);
        }
        break;

    default:
        break;
    }
}

/**
 * @brief 周期心跳：休眠关闭时的定时切换
 *
 * 休眠开启时设备靠 deep sleep + 唤醒周期工作，"定时"由唤醒周期承担，
 * 换图在 APP_EVT_BOOT 路径完成，此处早退避免重复计时。
 */
static void app_image_on_tick(void)
{
    if(m_image.play_mode != APP_IMAGE_PLAY_AUTO)
    {
        return;
    }
    if(g_service_param.sleep.sleep_mode != 0)
    {
        return;
    }
    if(service_file_get_count() == 0)
    {
        return;
    }

    m_elapsed_s++;
    if(m_elapsed_s >= (uint32_t)m_image.auto_interval * 60u)
    {
        sys_logi(APP_IMAGE_TAG, "auto switch after %us", (unsigned)m_elapsed_s);
        image_show_next();
    }
}

/**
 * @brief BLE 参数设置（0x45，TLV 列表）
 *
 * 只改自己的结构体：落盘由框架在参数通道 SET 后按归属 app 完成
 * （参数回调可能在本 app 非当前 app 时执行，不能直接调 app_state_save()）。
 */
static void app_image_param_set(const uint8_t *tlv, uint8_t len)
{
    uint8_t off = 0;
    app_tlv_t item;
    int show_now = 0;

    while(app_tlv_next(tlv, len, &off, &item))
    {
        switch(item.tag)
        {
        case APP_IMAGE_TAG_PLAY_MODE:
            if(item.len != 1)
            {
                sys_logw(APP_IMAGE_TAG, "bad len %u for play_mode", item.len);
                break;
            }
            m_image.play_mode = (item.val[0] != 0) ? APP_IMAGE_PLAY_AUTO : APP_IMAGE_PLAY_MANUAL;
            m_elapsed_s = 0;
            break;

        case APP_IMAGE_TAG_INTERVAL:
            if(item.len != 2)
            {
                sys_logw(APP_IMAGE_TAG, "bad len %u for interval", item.len);
                break;
            }
            {
                uint16_t v = app_tlv_be16(item.val);
                if(v < APP_IMAGE_INTERVAL_MIN) { v = APP_IMAGE_INTERVAL_MIN; }
                if(v > APP_IMAGE_INTERVAL_MAX) { v = APP_IMAGE_INTERVAL_MAX; }
                m_image.auto_interval = v;
            }
            m_elapsed_s = 0;
            break;

        case APP_IMAGE_TAG_FILE_ID:
            if(item.len != 4)
            {
                sys_logw(APP_IMAGE_TAG, "bad len %u for file_id", item.len);
                break;
            }
            m_image.file_id = app_tlv_be32(item.val);
            show_now = 1;
            break;

        default:
            sys_logw(APP_IMAGE_TAG, "param set: skip unknown tag 0x%02X", item.tag);
            break;
        }
    }

    sys_logi(APP_IMAGE_TAG, "param set: play=%u interval=%umin id=%u",
             m_image.play_mode, m_image.auto_interval, (unsigned)m_image.file_id);

    /* 立即生效需刷屏：仅在本 app 为当前 app 时执行（未 enter 时屏幕可能属于别的 app） */
    if(show_now && app_manager_get_current() == APP_ID_IMAGE)
    {
        image_show(m_image.file_id);
    }
}

/**
 * @brief BLE 参数查询（0x46）：回 TLV 列表
 *
 * @return 写入字节数；缓冲区不足返回 0
 */
static uint8_t app_image_param_get(uint8_t *out, uint8_t max)
{
    /* 3 + 4 + 6 = 13 字节 */
    if(max < 13)
    {
        return 0;
    }

    uint8_t n = 0;
    n += app_tlv_put_u8(&out[n], APP_IMAGE_TAG_PLAY_MODE, m_image.play_mode);
    n += app_tlv_put_u16(&out[n], APP_IMAGE_TAG_INTERVAL, m_image.auto_interval);
    n += app_tlv_put_u32(&out[n], APP_IMAGE_TAG_FILE_ID, m_image.file_id);
    return n;
}
