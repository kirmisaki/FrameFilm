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
 * FileName : /film_app/apps/template/app_template.c
 * Author: Kiritro  Version: v0.1  Date: 2026/9/9
 * Description: 模板 app：通用实时推送显示
 *              蓝牙/WiFi 推送的 film 落盘后装载到缓存并整屏渲染（天气/日历等内容共用）
 * ChangeLog: Change Notes
 *
 *********************************************************************/

/*********************************************************************
 * INCLUDES
 */
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_heap_caps.h"

#include "sys_log.h"
#include "sys_event.h"
#include "hal_epd.h"
#include "app_template.h"
#include "app_render.h"
#include "service_ble.h"
#include "service_file.h"
#include "service_film.h"
#include "service_param.h"
#include "service_wifi.h"

/*********************************************************************
 * MACROS
 */
#define APP_TEMPLATE_TAG    "app_template"

/* 装载等待上限：文件服务任务优先级更高，正常数十~数百毫秒内完成。
 * 本等待在 app 任务上下文执行（期间无法处理 BLE 参数/按键），故上限与
 * service_file 的 FILE_DIR_READY_TIMEOUT_MS 对齐取 2s，超时即放弃本次渲染。 */
#define TEMPLATE_LOAD_TIMEOUT_MS    (2000)
#define TEMPLATE_LOAD_POLL_MS       (20)

/* 网络拉取参数 */
#define APP_TEMPLATE_TICK_MS        (1000)  // on_tick 周期：秒级计时足够
#define APP_TEMPLATE_PULL_MIN       (1)     // 定时拉取间隔下限（分钟）
#define APP_TEMPLATE_PULL_MAX       (120)   // 定时拉取间隔上限（分钟）
#define APP_TEMPLATE_WIFI_WAIT_S    (30)    // 拉取前等待 WiFi 就绪的上限（秒），超时放弃

/* 参数通道 TAG（payload = TLV 列表，多字节大端） */
#define APP_TEMPLATE_TAG_PULL_ENABLE    (0x01)  // 1B：0=关闭 1=开启
#define APP_TEMPLATE_TAG_PULL_INTERVAL  (0x02)  // 2B：1~120 分钟

/* .film 文件头（与 docs/film/film.md 一致） */
#define TEMPLATE_FILM_HDR_SIZE          (32)
#define TEMPLATE_FILM_OFFSET_FORMAT     (0x09)
#define TEMPLATE_FILM_OFFSET_COLORTABLE (0x10)

/* v1 ColorTable 的实际颜色值 → ColorFast（Format 0x03）8bpp 波形索引
 * 与 ui_display.c 的 native_to_fast 同源（六色按 ForFilm cfClosestIndex 选出的
 * 波形索引）：黑 0x00→0、白 0xFF→21、黄 0xFC→63、红 0xE0→47、蓝 0x03→32、绿 0x1C→52。
 * 未列出的值按黑色处理（0）。 */
static const uint8_t m_template_color_to_fast[256] = {
    [0x00] = 0,
    [0xFF] = 21,
    [0xFC] = 63,
    [0xE0] = 47,
    [0x03] = 32,
    [0x1C] = 52,
};

/* 8bpp ColorFast 帧缓冲字节数与 v1 4bpp 主体字节数（720x480） */
#define TEMPLATE_FAST_BYTES             ((uint32_t)EPD_WIDTH * EPD_HEIGHT)
#define TEMPLATE_V1_BODY_BYTES          ((uint32_t)EPD_WIDTH * EPD_HEIGHT / 2u)

/*********************************************************************
* TYPEDEFS
*/
/**
 * @brief 模板 app 持久化状态（NVS blob）
 *
 * 只放参数类字段。展示态（是否显示过、上次文件名）是 RAM 量，不进 NVS。
 */
typedef struct {
    uint8_t  pull_enable;    // 网络拉取开关 0=关闭 1=开启
    uint16_t pull_interval;  // 定时拉取间隔（分钟，1 ~ 120），仅休眠关闭时生效
} app_template_state_t;

/* 状态结构体不得超过 NVS blob 的 app 数据上限 */
_Static_assert(sizeof(app_template_state_t) <= SERVICE_PARAM_APP_DATA_MAX,
               "app_template_state_t exceeds SERVICE_PARAM_APP_DATA_MAX");

/*********************************************************************
 * LOCAL VARIABLES
 */
static app_template_state_t m_template;

static const app_template_state_t m_template_default = {
    .pull_enable = 0,
    .pull_interval = 10,
};

/* 展示态（RAM，不持久化）：重进 app 时按文件名找回上次显示的内容 */
static uint8_t m_has_film = 0;
static char m_last_name[256] = {0};

/* 拉取计时（RAM，不持久化） */
static uint32_t m_pull_elapsed_s = 0;   // 定时拉取累计秒数
static uint8_t  m_pull_pending = 0;     // 已请求拉取、正在等 WiFi 就绪
static uint32_t m_pull_wait_s = 0;      // 等待 WiFi 就绪的秒数

// 关注的全局事件：内存直显（模板在场时推送/拉取不落卡）、落盘（旧路径兼容）
static const uint16_t m_template_events[] = {
    SYS_EVT_FILM_PUSH,
    SYS_EVT_FILE_SAVED,
    0,
};

/*********************************************************************
 * LOCAL FUNCTIONS
 */
static void app_template_on_enter(void);
static void app_template_on_exit(void);
static void app_template_on_event(const app_event_t *e);
static void app_template_on_tick(void);
static void app_template_param_set(const uint8_t *tlv, uint8_t len);
static uint8_t app_template_param_get(uint8_t *out, uint8_t max);
static int template_load_wait(uint32_t file_id);
static int template_render_file(uint32_t file_id);
static int template_render_memory(void);
static void template_show_film(const uint8_t *film, uint32_t size);
static int template_show_colorfast(const uint8_t *film, uint32_t size);
static void template_render_pushed(void);
static int template_find_by_name(const char *name, uint32_t *out_id);
static void template_trigger_pull(void);

/*********************************************************************
 * GLOBAL VARIABLES
 */
const app_entry_t g_app_template_entry = {
    .id = APP_ID_TEMPLATE,
    .name = "template",
    .data_dir = FILM_DIR,   // 单帧推送内容落盘于 film 目录，便于按列表下标装载
    .keys = APP_KEY_NONE,   // 不占用按键，可被上/下（回图片）与确认键（互切）接管
    .tick_ms = APP_TEMPLATE_TICK_MS,
    .events = m_template_events,
    .on_enter = app_template_on_enter,
    .on_exit = app_template_on_exit,
    .on_event = app_template_on_event,
    .on_tick = app_template_on_tick,

    /* 状态持久化：拉取开关 / 间隔 */
    .state = &m_template,
    .state_size = (uint16_t)sizeof(m_template),
    .state_ver = 1,
    .state_default = &m_template_default,

    /* BLE 参数通道：0x47 设置 / 0x48 查询 */
    .param_ch = BLE_FILM_TRANS_CH_APP_TEMPLATE_PARAM,
    .on_param_set = app_template_param_set,
    .on_param_get = app_template_param_get,
};

/*********************************************************************
 * LOCAL FUNCTIONS
 */

/**
 * @brief 触发文件装载并等待完成
 *
 * 保存链路（蓝牙/WiFi）只负责落盘，不装载 PSRAM 缓存；本 app 主动
 * 调用 service_file_load() 并轮询等待，避免渲染到旧缓存。
 */
static int template_load_wait(uint32_t file_id)
{
    if(service_file_load(file_id) != 0)
    {
        sys_logw(APP_TEMPLATE_TAG, "load request failed, id=%u", (unsigned)file_id);
        return -1;
    }

    uint32_t waited = 0;
    while(service_file_get_load_complete() != FILE_LOAD_STATE_DONE)
    {
        if(waited >= TEMPLATE_LOAD_TIMEOUT_MS)
        {
            sys_logw(APP_TEMPLATE_TAG, "load timeout, id=%u", (unsigned)file_id);
            return -1;
        }
        vTaskDelay(pdMS_TO_TICKS(TEMPLATE_LOAD_POLL_MS));
        waited += TEMPLATE_LOAD_POLL_MS;
    }
    return 0;
}

/**
 * @brief 把 v1 film 转成 8bpp 索引，用 48 色 ColorFast（2 相）刷新上屏
 *
 * 模板是实时推送的信息面板（天气/日历等），服务端渲染出来的是 v1 六色 4bpp；
 * 直接走 v1 整屏刷新慢且会闪。这里按 ColorTable 把六色映射成 ColorFast 的
 * 波形索引（与 UI 层彩页同一套映射），再用 2 相刷新上屏。
 *
 * 只处理 Format 0x00：其余格式（0x01 MonoFast / 0x02 ColorQual / 0x03 ColorFast）
 * 返回 -1，由调用方按文件自带的格式走 app_render_display_full —— 其中 ColorQual
 * 的索引是另一套测量色板，固件内没有 RGB 表，不能直接换成 ColorFast 波形。
 *
 * @param film .film 文件起始（含 32 字节头）
 * @param size 文件总字节数（含头）
 * @return 0 已显示；-1 不适用/数据长度不足/内存不足（调用方回落）
 */
static int template_show_colorfast(const uint8_t *film, uint32_t size)
{
    if(film == NULL || size <= TEMPLATE_FILM_HDR_SIZE)
    {
        return -1;
    }
    if(film[TEMPLATE_FILM_OFFSET_FORMAT] != 0x00)
    {
        return -1;
    }

    uint32_t body_size = size - TEMPLATE_FILM_HDR_SIZE;
    if(body_size < TEMPLATE_V1_BODY_BYTES)
    {
        sys_logw(APP_TEMPLATE_TAG, "v1 film too short: %u", (unsigned)body_size);
        return -1;
    }

    /* 8bpp 整屏 345KB：与图片 app 的 8bpp 播放同量级，优先放 PSRAM */
    uint8_t *fast = (uint8_t *)heap_caps_malloc(TEMPLATE_FAST_BYTES, MALLOC_CAP_SPIRAM);
    if(fast == NULL)
    {
        sys_logw(APP_TEMPLATE_TAG, "colorfast buffer alloc failed");
        return -1;
    }

    const uint8_t *body = film + TEMPLATE_FILM_HDR_SIZE;
    const uint8_t *color_table = film + TEMPLATE_FILM_OFFSET_COLORTABLE;
    const uint32_t src_row_bytes = (uint32_t)EPD_WIDTH / 2u;   // v1 每字节 2 像素

    for(uint32_t y = 0; y < EPD_HEIGHT; y++)
    {
        const uint8_t *src = body + y * src_row_bytes;
        uint8_t *dst = fast + y * (uint32_t)EPD_WIDTH;

        for(uint32_t x = 0; x < (uint32_t)EPD_WIDTH; x += 2u)
        {
            uint8_t packed = src[x >> 1];

            dst[x] = m_template_color_to_fast[color_table[packed >> 4]];
            dst[x + 1u] = m_template_color_to_fast[color_table[packed & 0x0Fu]];
        }
    }

    app_render_display_8bpp(fast, 0);   // 0 = ColorFast（2 相）
    heap_caps_free(fast);
    return 0;
}

/**
 * @brief 渲染一帧 film（v1 先转 48 色 ColorFast；其它格式按文件自带方式）
 *
 * 先校验主体长度：驱动是按本机固定的行/列读的，服务器若按别的尺寸渲染
 * （如 basic 600x400），短包直接交给整屏路径会越界读，这里先拦下来。
 */
static void template_show_film(const uint8_t *film, uint32_t size)
{
    if(film == NULL || size <= TEMPLATE_FILM_HDR_SIZE)
    {
        sys_logw(APP_TEMPLATE_TAG, "film too short: %u", (unsigned)size);
        return;
    }

    uint8_t format = film[TEMPLATE_FILM_OFFSET_FORMAT];
    uint32_t need;
    switch(format)
    {
    case 0x01:  // MonoFast：1bpp
        need = (uint32_t)EPD_WIDTH * EPD_HEIGHT / 8u;
        break;
    case 0x02:  // ColorQual：8bpp
    case 0x03:  // ColorFast：8bpp
        need = (uint32_t)EPD_WIDTH * EPD_HEIGHT;
        break;
    default:    // 0x00 v1 4bpp
        need = TEMPLATE_V1_BODY_BYTES;
        break;
    }

    if(size - TEMPLATE_FILM_HDR_SIZE < need)
    {
        sys_logw(APP_TEMPLATE_TAG, "film size mismatch: fmt=0x%02X body=%u need=%u",
                 format, (unsigned)(size - TEMPLATE_FILM_HDR_SIZE), (unsigned)need);
        return;
    }

    if(format == 0x00 && template_show_colorfast(film, size) == 0)
    {
        return;
    }

    app_render_display_full(film);
}

/**
 * @brief 渲染内存里最近一次推送的 film（模板在场时推送/拉取不落卡）
 *
 * 复制一份再渲染：渲染一帧要数秒，期间新的推送会把内存里的旧缓冲换掉，
 * 直接借出会有被释放的风险（见 service_film_push_copy）。
 *
 * @return 0 已显示；-1 内存里没有可显示的内容
 */
static int template_render_memory(void)
{
    uint32_t len = 0;
    uint8_t *film = service_film_push_copy(&len);

    if(film == NULL)
    {
        return -1;
    }

    template_show_film(film, len);
    heap_caps_free(film);
    sys_logi(APP_TEMPLATE_TAG, "render RAM film, %u bytes", (unsigned)len);
    return 0;
}

/**
 * @brief 装载指定文件并整屏渲染，成功后记录文件名用于重进恢复
 */
static int template_render_file(uint32_t file_id)
{
    if(template_load_wait(file_id) != 0)
    {
        return -1;
    }

    uint8_t *buffer = service_file_get_buffer();
    if(buffer == NULL)
    {
        sys_logw(APP_TEMPLATE_TAG, "film buffer is null, id=%u", (unsigned)file_id);
        return -1;
    }

    template_show_film(buffer, service_file_get_buffer_size());

    if(service_file_get_filename_safe(file_id, m_last_name, sizeof(m_last_name)) == 0)
    {
        m_has_film = 1;
    }
    sys_logi(APP_TEMPLATE_TAG, "render pushed content, id=%u", (unsigned)file_id);
    return 0;
}

/**
 * @brief 渲染刚落盘的推送内容（保存流程已把 current_file_id 定位到新文件）
 */
static void template_render_pushed(void)
{
    uint32_t count = service_file_get_count();
    if(count == 0)
    {
        sys_logw(APP_TEMPLATE_TAG, "no film to display");
        return;
    }

    uint32_t id = service_file_get_current_id();
    if(id >= count)
    {
        id = 0;
    }

    template_render_file(id);
}

/**
 * @brief 在当前文件列表中按文件名查找下标
 */
static int template_find_by_name(const char *name, uint32_t *out_id)
{
    if(name == NULL || out_id == NULL || name[0] == '\0')
    {
        return -1;
    }

    char cur[256];
    uint32_t count = service_file_get_count();
    for(uint32_t i = 0; i < count; i++)
    {
        if(service_file_get_filename_safe(i, cur, sizeof(cur)) == 0 && strcmp(cur, name) == 0)
        {
            *out_id = i;
            return 0;
        }
    }
    return -1;
}

static void app_template_on_enter(void)
{
    sys_logi(APP_TEMPLATE_TAG, "enter template app");

    /* 在场期间：BLE 直推 / WiFi 拉取的 film 只进内存直显，不写 TF 卡 */
    service_film_push_mode_set(1);

    /* 优先显示内存里最近一次推送（断电即丢，不占卡） */
    if(template_render_memory() == 0)
    {
        return;
    }

    // 旧路径：按文件名找回曾经落过卡的推送内容（列表下标可能因上传而变动）
    uint32_t id = 0;
    if(m_has_film &&
       template_find_by_name(m_last_name, &id) == 0 &&
       template_render_file(id) == 0)
    {
        return;
    }

    // 尚未收到任何推送：维持白屏，等待蓝牙/WiFi 推送
    app_render_clear();
    sys_logw(APP_TEMPLATE_TAG, "no pushed content, waiting for ble/wifi film push");
}

static void app_template_on_exit(void)
{
    /* 离开后恢复"落卡"行为：此时推送的内容属于相册/其它 app */
    service_film_push_mode_set(0);
    sys_logi(APP_TEMPLATE_TAG, "exit template app");
}

/**
 * @brief 发起一次网络拉取
 *
 * WiFi 未就绪不阻塞等待：标记 pending，由 on_tick 轮询连接状态，
 * 就绪即拉取，超过 APP_TEMPLATE_WIFI_WAIT_S 秒则放弃本次（交给心跳链路）。
 */
static void template_trigger_pull(void)
{
    if(service_wifi_get_connect_status() == 1)
    {
        sys_logi(APP_TEMPLATE_TAG, "pull start");
        service_wifi_download_start();
        m_pull_pending = 0;
        m_pull_wait_s = 0;
        return;
    }

    sys_logi(APP_TEMPLATE_TAG, "wifi not ready, pull pending");
    m_pull_pending = 1;
    m_pull_wait_s = 0;
}

/**
 * @brief 周期心跳：定时拉取（休眠关闭）+ 等待 WiFi 就绪的挂起拉取
 *
 * 休眠开启时设备靠 deep sleep + 唤醒周期工作，拉取在 APP_EVT_BOOT 路径完成，
 * 此处不重复计时。
 */
static void app_template_on_tick(void)
{
    /* 优先处理挂起的拉取：等 WiFi 就绪或超时放弃 */
    if(m_pull_pending)
    {
        if(service_wifi_get_connect_status() == 1)
        {
            sys_logi(APP_TEMPLATE_TAG, "wifi ready, pull start");
            service_wifi_download_start();
            m_pull_pending = 0;
            m_pull_wait_s = 0;
            return;
        }

        if(++m_pull_wait_s > APP_TEMPLATE_WIFI_WAIT_S)
        {
            sys_logw(APP_TEMPLATE_TAG, "wifi wait timeout, give up pull");
            m_pull_pending = 0;
            m_pull_wait_s = 0;
        }
        return;
    }

    /* 定时拉取仅在休眠关闭时生效 */
    if(m_template.pull_enable != 1 || g_service_param.sleep.sleep_mode != 0)
    {
        m_pull_elapsed_s = 0;
        return;
    }

    if(++m_pull_elapsed_s < (uint32_t)m_template.pull_interval * 60u)
    {
        return;
    }
    m_pull_elapsed_s = 0;
    template_trigger_pull();
}

/**
 * @brief BLE 参数设置（0x47，TLV 列表）
 *
 * 只改自己的结构体：落盘由框架在参数通道 SET 后按归属 app 完成
 * （参数回调可能在本 app 非当前 app 时执行，不能直接调 app_state_save()）。
 */
static void app_template_param_set(const uint8_t *tlv, uint8_t len)
{
    uint8_t off = 0;
    app_tlv_t item;

    while(app_tlv_next(tlv, len, &off, &item))
    {
        switch(item.tag)
        {
        case APP_TEMPLATE_TAG_PULL_ENABLE:
            if(item.len != 1)
            {
                sys_logw(APP_TEMPLATE_TAG, "bad len %u for pull_enable", item.len);
                break;
            }
            m_template.pull_enable = (item.val[0] != 0) ? 1 : 0;
            break;

        case APP_TEMPLATE_TAG_PULL_INTERVAL:
            if(item.len != 2)
            {
                sys_logw(APP_TEMPLATE_TAG, "bad len %u for pull_interval", item.len);
                break;
            }
            {
                uint16_t v = app_tlv_be16(item.val);
                if(v < APP_TEMPLATE_PULL_MIN) { v = APP_TEMPLATE_PULL_MIN; }
                if(v > APP_TEMPLATE_PULL_MAX) { v = APP_TEMPLATE_PULL_MAX; }
                m_template.pull_interval = v;
            }
            m_pull_elapsed_s = 0;
            break;

        default:
            sys_logw(APP_TEMPLATE_TAG, "param set: skip unknown tag 0x%02X", item.tag);
            break;
        }
    }

    sys_logi(APP_TEMPLATE_TAG, "param set: pull=%u interval=%umin",
             m_template.pull_enable, m_template.pull_interval);
}

/**
 * @brief BLE 参数查询（0x48）：回 TLV 列表
 *
 * @return 写入字节数；缓冲区不足返回 0
 */
static uint8_t app_template_param_get(uint8_t *out, uint8_t max)
{
    /* 3 + 4 = 7 字节 */
    if(max < 7)
    {
        return 0;
    }

    uint8_t n = 0;
    n += app_tlv_put_u8(&out[n], APP_TEMPLATE_TAG_PULL_ENABLE, m_template.pull_enable);
    n += app_tlv_put_u16(&out[n], APP_TEMPLATE_TAG_PULL_INTERVAL, m_template.pull_interval);
    return n;
}

static void app_template_on_event(const app_event_t *e)
{
    if(e == NULL)
    {
        return;
    }

    /* 开机自动拉取：仅启动后投递一次（用户手动切到模板 app 不会触发） */
    if(e->type == APP_EVT_BOOT)
    {
        if(m_template.pull_enable == 1 &&
           g_service_param.sleep.sleep_mode == 1)   // 休眠开启 → 开机自动拉取
        {
            sys_logi(APP_TEMPLATE_TAG, "boot pull");
            m_pull_elapsed_s = 0;
            template_trigger_pull();
        }
        return;
    }

    if(e->type != APP_EVT_SYS)
    {
        return;
    }

    switch((sys_event_id_t)e->cmd)
    {
    case SYS_EVT_FILM_PUSH:
        /* 内存直显：推送/拉取的内容已整包放在 RAM，不落卡 */
        if(template_render_memory() != 0)
        {
            sys_logw(APP_TEMPLATE_TAG, "RAM film render failed");
        }
        break;

    case SYS_EVT_FILE_SAVED:
        /* payload[0] = auto_load：0 表示静默批量上传，不自动显示 */
        if(e->len > 0 && e->payload[0] == 0)
        {
            sys_logi(APP_TEMPLATE_TAG, "silent save, skip display");
            break;
        }
        sys_logi(APP_TEMPLATE_TAG, "content pushed, display id=%u", (unsigned)service_file_get_current_id());
        template_render_pushed();
        break;

    default:
        break;
    }
}
