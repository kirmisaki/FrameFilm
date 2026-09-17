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
 * FileName : /film_app/src/app_animation.c
 * Author: Kiritro  Version: v0.1  Date: 2026/9/9
 * Description: 动图 app：持续循环播放 film v2 多帧（独立目录 /sdcard/animation）
 *              支持单个文件循环 / 按顺序播放两种模式；蓝牙下传动图后刷新列表并重置到第一个。
 * ChangeLog: Change Notes
 *
 *********************************************************************/

/*********************************************************************
 * INCLUDES
 */
#include "sys_log.h"
#include "sys_event.h"
#include "hal_input.h"
#include "app_animation.h"
#include "app_manager.h"
#include "service_file.h"
#include "service_film.h"
#include "service_ble.h"
#include "service_param.h"

/*********************************************************************
 * MACROS
 */
#define APP_ANIM_TAG    "app_anim"

// on_tick 心跳周期（毫秒）：app_manager 以 100ms 为基准分频，故取 100ms
// （更小的值会被向上取整到 100ms，见 app_manager.c 的 APP_TICK_MS）
#define APP_ANIM_TICK_MS        (100)

// 每帧间隔范围（毫秒）：下限受 APP_ANIM_TICK_MS 约束
#define APP_ANIM_FRAME_MS_MIN   (100)
#define APP_ANIM_FRAME_MS_MAX   (2000)

// 循环间隔范围（秒）
#define APP_ANIM_LOOP_SEC_MAX   (600)

/* 播放模式 */
#define APP_ANIM_PLAY_SINGLE    (0)     // 单 film 循环：只播当前文件，播完等待后重播
#define APP_ANIM_PLAY_SEQ       (1)     // film 列表循环：依次播每个文件，到末尾回绕

/* 参数通道 TAG（payload = TLV 列表，多字节大端） */
#define APP_ANIM_TAG_PLAY_MODE  (0x01)  // 1B：0=单 film 循环 1=列表循环
#define APP_ANIM_TAG_FRAME_MS   (0x02)  // 2B：100~2000 毫秒
#define APP_ANIM_TAG_LOOP_SEC   (0x03)  // 2B：0~600 秒
#define APP_ANIM_TAG_FILE_ID    (0x04)  // 4B：当前文件下标

/*********************************************************************
* TYPEDEFS
*/
/**
 * @brief 动图 app 状态（持久化到 NVS）
 *
 * frame_idx 不持久化：帧是高频状态，逐帧写 NVS 会迅速耗尽擦写寿命；
 * 只记"哪个文件"，重进 app 时从首帧开始。
 */
typedef struct {
    uint8_t  play_mode;     // APP_ANIM_PLAY_SINGLE / APP_ANIM_PLAY_SEQ
    uint16_t frame_ms;      // 每帧间隔（毫秒，100 ~ 2000），即"播放速度"
    uint16_t loop_seconds;  // 循环间隔（秒，0 ~ 600）：一个 film 播完到播下一个的等待时间，0=不等待
    uint32_t file_id;       // 当前播放的文件下标
} app_anim_state_t;

/* 状态结构体不得超过 NVS blob 的 app 数据上限 */
_Static_assert(sizeof(app_anim_state_t) <= SERVICE_PARAM_APP_DATA_MAX,
               "app_anim_state_t exceeds SERVICE_PARAM_APP_DATA_MAX");

/*********************************************************************
 * LOCAL VARIABLES
 */
static app_anim_state_t m_anim;
static uint32_t m_frame_count = 0;    // 当前文件总帧数（RAM，不持久化）
static uint32_t m_frame_idx = 0;      // 当前帧索引（RAM，不持久化）
static uint32_t m_frame_acc_ms = 0;   // 帧分频累计（毫秒）
static uint32_t m_loop_wait_ms = 0;   // 剩余循环间隔（毫秒），>0 表示处于两轮之间的停顿

static const app_anim_state_t m_anim_default = {
    .play_mode = APP_ANIM_PLAY_SINGLE,
    .frame_ms = 200,
    .loop_seconds = 0,
    .file_id = 0,
};

// 关注的全局事件：新动图落盘后刷新列表并重置到第一个播放
static const uint16_t m_anim_events[] = {
    SYS_EVT_FILE_SAVED,
    0,
};

/*********************************************************************
 * LOCAL FUNCTIONS
 */
static void app_animation_on_enter(void);
static void app_animation_on_exit(void);
static void app_animation_on_event(const app_event_t *e);
static void app_animation_on_tick(void);
static void app_animation_param_set(const uint8_t *tlv, uint8_t len);
static uint8_t app_animation_param_get(uint8_t *out, uint8_t max);
static void anim_start(uint32_t file_id, int persist);
static void anim_step(int32_t delta, int persist);
static void anim_toggle_mode(void);
static void anim_next_round(void);
static void app_animation_on_downloaded(void);

/*********************************************************************
 * GLOBAL VARIABLES
 */
const app_entry_t g_app_animation_entry = {
    .id = APP_ID_ANIMATION,
    .name = "animation",
    .data_dir = ANIM_DIR,      // 动图独立目录，切换时由 app_manager 同步设好
    .keys = APP_KEY_SHORT | APP_KEY_UP | APP_KEY_DOWN,  // 短按切播放模式，上/下换动图
    .tick_ms = APP_ANIM_TICK_MS,  // 按 frame_ms 分频推进帧
    .events = m_anim_events,
    .on_enter = app_animation_on_enter,
    .on_exit = app_animation_on_exit,
    .on_event = app_animation_on_event,
    .on_tick = app_animation_on_tick,

    /* 状态持久化：休眠唤醒后仍能记住播放模式 / 速度 / 循环间隔 / 当前文件 */
    .state = &m_anim,
    .state_size = (uint16_t)sizeof(m_anim),
    .state_ver = 1,
    .state_default = &m_anim_default,

    /* BLE 参数通道：0x49 设置 / 0x4A 查询 */
    .param_ch = BLE_FILM_TRANS_CH_APP_ANIM_PARAM,
    .on_param_set = app_animation_param_set,
    .on_param_get = app_animation_param_get,
};

/*********************************************************************
 * LOCAL FUNCTIONS
 */

/**
 * @brief 开始播放指定动图文件（取首帧）
 *
 * @param persist 1=文件变更时落盘；0=只改 RAM（自动轮播走此路径）
 *
 * 顺序模式 + loop_seconds=0 时，自动轮播每 frame_ms（最小 100ms）就会切一次文件；
 * 若每次都写 NVS 会迅速耗尽擦写寿命，故自动轮播不落盘——最新文件在离开 app 时由
 * app_manager 统一保存（app_do_switch 会先 on_exit 再 app_state_save）。
 */
static void anim_start(uint32_t file_id, int persist)
{
    if(service_file_get_count() == 0)
    {
        sys_logw(APP_ANIM_TAG, "no animation files in %s", service_file_get_dir());
        return;
    }

    if(file_id >= service_file_get_count())
    {
        file_id = 0;
    }

    if(file_id != m_anim.file_id)
    {
        m_anim.file_id = file_id;
        if(persist)
        {
            app_state_save();   // 手动切换：变更即写
        }
    }

    m_frame_count = service_film_get_frame_count(file_id);
    m_frame_idx = 0;
    m_frame_acc_ms = 0;
    m_loop_wait_ms = 0;

    sys_logi(APP_ANIM_TAG, "start anim file=%u frames=%u mode=%s", (unsigned)file_id,
             (unsigned)m_frame_count, (m_anim.play_mode == APP_ANIM_PLAY_SEQ) ? "seq" : "single");
    service_film_render_frame(file_id, 0);
}

/**
 * @brief 在动图列表中前后切换文件（UP/DOWN；顺序模式下播完自动切下一个）
 */
static void anim_step(int32_t delta, int persist)
{
    uint32_t count = service_file_get_count();
    if(count == 0)
    {
        return;
    }

    uint32_t cur = m_anim.file_id;
    if(cur >= count)
    {
        cur = 0;
    }

    uint32_t next = (uint32_t)((cur + delta + count) % count);
    anim_start(next, persist);   // 内部处理"是否落盘"并从首帧渲染
}

/**
 * @brief 切换播放模式：单个循环 <-> 按顺序播放
 */
static void anim_toggle_mode(void)
{
    m_anim.play_mode = (m_anim.play_mode == APP_ANIM_PLAY_SEQ) ? APP_ANIM_PLAY_SINGLE : APP_ANIM_PLAY_SEQ;
    m_frame_acc_ms = 0;
    m_loop_wait_ms = 0;
    app_state_save();   // 播放模式属于用户设置，按键改动即落盘
    sys_logi(APP_ANIM_TAG, "play mode switch to %s",
             (m_anim.play_mode == APP_ANIM_PLAY_SEQ) ? "seq" : "single");
}

/**
 * @brief 开启新一轮播放：单个循环重播本文件，列表循环切下一个文件
 *
 * 由 on_tick 在一轮播完、且循环间隔（loop_seconds）消减完毕后调用。
 */
static void anim_next_round(void)
{
    m_frame_acc_ms = 0;
    m_loop_wait_ms = 0;

    if(m_anim.play_mode == APP_ANIM_PLAY_SEQ)
    {
        anim_step(1, 0);   // 列表循环：切下一个文件，从头播放；自动轮播不落盘
        return;
    }

    m_frame_idx = 0;    // 单文件循环：重播当前文件
    service_film_render_frame(m_anim.file_id, 0);
}

/**
 * @brief 新动图落盘后：复位到第一个文件并从头播放
 *
 * 此时 service_file 层已完成列表刷新（SYS_EVT_FILE_SAVED 在其后投递），
 * 直接取第一个文件即可；若当前目录无文件则 anim_start 内部兜底。
 */
static void app_animation_on_downloaded(void)
{
    sys_logi(APP_ANIM_TAG, "new animation downloaded, reset to first");
    anim_start(0, 1);
}

static void app_animation_on_enter(void)
{
    sys_logi(APP_ANIM_TAG, "enter animation app, dir=%s mode=%s", service_file_get_dir(),
             (m_anim.play_mode == APP_ANIM_PLAY_SEQ) ? "seq" : "single");

    // 数据目录已由 app_manager 同步切换到 ANIM_DIR 并等待列表就绪
    uint32_t count = service_file_get_count();
    if(count == 0)
    {
        sys_logw(APP_ANIM_TAG, "animation dir empty");
        return;
    }

    // 按当前播放的 index 继续（越界则重置到第一个）；状态已由 app_manager 载入，无需落盘
    uint32_t cur = m_anim.file_id;
    if(cur >= count)
    {
        cur = 0;
    }
    anim_start(cur, 0);
}

static void app_animation_on_exit(void)
{
    sys_logi(APP_ANIM_TAG, "exit animation app");
}

/**
 * @brief 周期心跳：按 frame_ms 分频推进帧，一轮播完按 loop_seconds 停顿
 *
 * 心跳基准固定为 APP_ANIM_TICK_MS(100ms)；frame_ms 为 100 的整数倍时精确，
 * 非整数倍时用"减去 frame_ms"而非清零来保留余数，避免累积漂移。
 */
static void app_animation_on_tick(void)
{
    if(service_file_get_count() == 0 || m_frame_count == 0)
    {
        return;
    }

    /* 两轮之间的停顿：只消减等待时间，不推进帧 */
    if(m_loop_wait_ms > 0)
    {
        if(m_loop_wait_ms > APP_ANIM_TICK_MS)
        {
            m_loop_wait_ms -= APP_ANIM_TICK_MS;
        }
        else
        {
            m_loop_wait_ms = 0;
            anim_next_round();
        }
        return;
    }

    /* 帧分频：累计满 frame_ms 才推进一帧 */
    m_frame_acc_ms += APP_ANIM_TICK_MS;
    if(m_frame_acc_ms < m_anim.frame_ms)
    {
        return;
    }
    m_frame_acc_ms -= m_anim.frame_ms;

    /* 还有后续帧：直接推进 */
    if(m_frame_idx + 1 < m_frame_count)
    {
        m_frame_idx++;
        service_film_render_frame(m_anim.file_id, m_frame_idx);
        return;
    }

    /* 一轮播完。单帧静态图在"单文件循环"下没有下一帧，无需重复刷屏 */
    if(m_frame_count <= 1 && m_anim.play_mode == APP_ANIM_PLAY_SINGLE)
    {
        return;
    }

    if(m_anim.loop_seconds > 0)
    {
        m_loop_wait_ms = (uint32_t)m_anim.loop_seconds * 1000u;
        sys_logi(APP_ANIM_TAG, "round end, wait %us", (unsigned)m_anim.loop_seconds);
        return;
    }

    anim_next_round();
}

/**
 * @brief BLE 参数设置（0x49，TLV 列表）
 *
 * 只改自己的结构体：落盘由框架在参数通道 SET 后按归属 app 完成
 * （参数回调可能在本 app 非当前 app 时执行，不能直接调 app_state_save()）。
 */
static void app_animation_param_set(const uint8_t *tlv, uint8_t len)
{
    uint8_t off = 0;
    app_tlv_t item;
    int restart = 0;

    while(app_tlv_next(tlv, len, &off, &item))
    {
        switch(item.tag)
        {
        case APP_ANIM_TAG_PLAY_MODE:
            if(item.len != 1)
            {
                sys_logw(APP_ANIM_TAG, "bad len %u for play_mode", item.len);
                break;
            }
            m_anim.play_mode = (item.val[0] != 0) ? APP_ANIM_PLAY_SEQ : APP_ANIM_PLAY_SINGLE;
            m_frame_acc_ms = 0;
            m_loop_wait_ms = 0;
            break;

        case APP_ANIM_TAG_FRAME_MS:
            if(item.len != 2)
            {
                sys_logw(APP_ANIM_TAG, "bad len %u for frame_ms", item.len);
                break;
            }
            {
                uint16_t v = app_tlv_be16(item.val);
                if(v < APP_ANIM_FRAME_MS_MIN) { v = APP_ANIM_FRAME_MS_MIN; }
                if(v > APP_ANIM_FRAME_MS_MAX) { v = APP_ANIM_FRAME_MS_MAX; }
                m_anim.frame_ms = v;
            }
            m_frame_acc_ms = 0;
            break;

        case APP_ANIM_TAG_LOOP_SEC:
            if(item.len != 2)
            {
                sys_logw(APP_ANIM_TAG, "bad len %u for loop_seconds", item.len);
                break;
            }
            {
                uint16_t v = app_tlv_be16(item.val);
                if(v > APP_ANIM_LOOP_SEC_MAX) { v = APP_ANIM_LOOP_SEC_MAX; }
                m_anim.loop_seconds = v;
            }
            break;

        case APP_ANIM_TAG_FILE_ID:
            if(item.len != 4)
            {
                sys_logw(APP_ANIM_TAG, "bad len %u for file_id", item.len);
                break;
            }
            m_anim.file_id = app_tlv_be32(item.val);
            restart = 1;
            break;

        default:
            sys_logw(APP_ANIM_TAG, "param set: skip unknown tag 0x%02X", item.tag);
            break;
        }
    }

    sys_logi(APP_ANIM_TAG, "param set: mode=%s frame=%ums loop=%us id=%u",
             (m_anim.play_mode == APP_ANIM_PLAY_SEQ) ? "seq" : "single",
             m_anim.frame_ms, m_anim.loop_seconds, (unsigned)m_anim.file_id);

    /* 立即生效需刷屏：仅在本 app 为当前 app 时执行（未 enter 时屏幕可能属于别的 app）；
       落盘由框架在参数通道 SET 后按归属 app 完成 */
    if(restart && app_manager_get_current() == APP_ID_ANIMATION)
    {
        anim_start(m_anim.file_id, 0);
    }
}

/**
 * @brief BLE 参数查询（0x4A）：回 TLV 列表
 *
 * @return 写入字节数；缓冲区不足返回 0
 */
static uint8_t app_animation_param_get(uint8_t *out, uint8_t max)
{
    /* 3 + 4 + 4 + 6 = 17 字节 */
    if(max < 17)
    {
        return 0;
    }

    uint8_t n = 0;
    n += app_tlv_put_u8(&out[n], APP_ANIM_TAG_PLAY_MODE, m_anim.play_mode);
    n += app_tlv_put_u16(&out[n], APP_ANIM_TAG_FRAME_MS, m_anim.frame_ms);
    n += app_tlv_put_u16(&out[n], APP_ANIM_TAG_LOOP_SEC, m_anim.loop_seconds);
    n += app_tlv_put_u32(&out[n], APP_ANIM_TAG_FILE_ID, m_anim.file_id);
    return n;
}

static void app_animation_on_event(const app_event_t *e)
{
    if(e == NULL)
    {
        return;
    }

    switch(e->type)
    {
    case APP_EVT_INPUT:
        switch(e->input)
        {
        case INPUT_PRESS_SHORT:   // 切换播放模式（单个循环 / 按顺序）
            anim_toggle_mode();
            break;
        case INPUT_PRESS_UP:      // 上一个动图
            anim_step(-1, 1);
            break;
        case INPUT_PRESS_DOWN:    // 下一个动图
            anim_step(1, 1);
            break;
        default:
            break;
        }
        break;

    case APP_EVT_SYS:
        // 新动图落盘：列表已刷新，重置到第一个播放
        if((sys_event_id_t)e->cmd == SYS_EVT_FILE_SAVED)
        {
            app_animation_on_downloaded();
        }
        break;

    default:
        break;
    }
}
