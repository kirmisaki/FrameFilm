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
 * FileName : /film_service/src/service_film.c
 * Author: Kiritro  Version: v0.1  Date: 2026/4/22
 * Description: Film service function
 * ChangeLog: Change Notes
 *
 *********************************************************************/


/*********************************************************************
 * INCLUDES
 */
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/timers.h"

#include "esp_heap_caps.h"

#include "sys_log.h"
#include "sys_event.h"
#include "hal_api.h"
#include "service_file.h"
#include "service_film.h"

/*********************************************************************
 * MACROS
 */
#define FILM_MSG_QUEUE_LENGTH       30
#define FILM_MSG_QUEUE_ITEM_SIZE    sizeof( film_msg_t )

#define SYS_OS_PRI_FILM_TASK        (5)
#define SYS_OS_SIZE_FILM_TASK       (4096)
#define SYS_OS_NAME_FILM_TASK       "film_task"

#define FILM_HDR_SIZE               (32)      // .film 文件头字节数
// .film 文件头关键字段偏移（与 docs/film/film.md 一致）
#define FILM_HDR_OFFSET_FORMAT      (0x09)    // 格式判别码
#define FILM_HDR_OFFSET_FRAMECOUNT  (0x0A)    // 帧数（小端）

/* 整份读进 PSRAM 的尺寸上限（与 service_file 的预检同源）：
   超过它的文件必然整份分配失败，改走"按帧从 SD 读"的流式路径 */
#define FILM_PSRAM_LOAD_MAX         (SERVICE_FILE_PSRAM_MAX_BYTES)

/*********************************************************************
* TYPEDEFS
*/
/**
 * @brief 从 .film 文件头解出的播放信息
 */
typedef struct {
    uint8_t  format;        // Format 判别码
    uint32_t frame_count;   // 帧数（已归一为 ≥1）
    uint32_t frame_size;    // 单帧主体字节数（由 Format 决定）
} film_info_t;

/*********************************************************************
* CONSTANTS
*/

/*********************************************************************
* LOCAL VARIABLES
*/
static TaskHandle_t m_film_task_hdl = NULL;
static QueueHandle_t m_film_msg_hdl = NULL;

/* 流式播放用的"单帧"缓冲（PSRAM）：文件装不进 PSRAM 时，每帧只读这一帧进来。
   只在 film_task 里使用，故无需加锁；按需扩容后复用。 */
static uint8_t *m_stream_buf = NULL;
static uint32_t m_stream_cap = 0;

/* 已投递但尚未处理完的按帧渲染请求数（见 service_film_render_frame）。
   动图 app 据此做流控：面板刷新远慢于推送节奏，不控就会堆请求。 */
static volatile uint32_t m_render_pending = 0;

/* 已投递但尚未落屏的整图显示请求数（见 service_film_display）。
   图片 app 据此在上一张还在刷新时丢弃翻页按键：否则请求会堆在队列里，
   刷完之后继续一张张翻；退出 app 时也用它等最后一张落屏再切页。 */
static volatile uint32_t m_display_pending = 0;

/*********************************************************************
 * GLOBAL VARIABLES
 */

/*********************************************************************
 * LOCAL FUNCTIONS
 */
static void film_task_handle(void *pvParameters);
static void film_msg_send(void *p_msg, bool in_isr);

static void film_display_event(uint32_t file_id);
static void film_render_frame_event(uint32_t file_id, uint32_t frame_idx);

/*********************************************************************
 * LOCAL HELPERS
 */

/**
 * @brief 等待文件加载完成
 *
 * 结束条件是 DONE 或 FAILED：失败时立刻返回，不能空等满超时——调用方可能
 * 每帧渲染都走这里，等满超时会表现为"按键无响应 / 界面卡住"。
 */
static void film_wait_load_done(void)
{
    uint32_t wait_count = 0;
    uint8_t state;

    do
    {
        state = service_file_get_load_complete();
        if(state == FILE_LOAD_STATE_DONE || state == FILE_LOAD_STATE_FAILED)
        {
            break;
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
    } while(++wait_count < 300);
}

/**
 * @brief 确保指定文件已加载到 PSRAM 缓冲
 * @return 0 成功，-1 失败（含"文件过大，不该整份加载"）
 *
 * 只在 film_task 里调用：加载会换掉 PSRAM 缓冲，其它任务在渲染途中触发加载
 * 会让正在用的帧指针失效。
 */
static int film_ensure_loaded(uint32_t file_id)
{
    // 超过"整份加载"上限：不发起加载（必然失败，还要白等超时），交给流式路径
    if(service_file_get_size(file_id) > FILM_PSRAM_LOAD_MAX)
    {
        return -1;
    }

    // 已判定失败的文件不再重复发起：否则每帧渲染都要空等一次加载超时
    if(service_file_is_load_failed(file_id))
    {
        return -1;
    }

    if(service_file_get_current_id() != file_id ||
       service_file_get_load_complete() != FILE_LOAD_STATE_DONE)
    {
        if(service_file_load(file_id) != 0)
        {
            sys_loge(FILM_TAG, "load file %d failed", file_id);
            return -1;
        }
        film_wait_load_done();
    }

    uint8_t state = service_file_get_load_complete();
    if(state != FILE_LOAD_STATE_DONE)
    {
        sys_loge(FILM_TAG, "load file %d %s", file_id,
                 (state == FILE_LOAD_STATE_FAILED) ? "failed" : "timeout");
        return -1;
    }

    return service_file_get_buffer() ? 0 : -1;
}

/**
 * @brief 读取单帧主体大小
 * @return 帧大小，或 0（未知格式）
 */
static uint32_t film_frame_size_by_format(uint8_t format)
{
    switch(format)
    {
    case 0x00:  // v1 4bpp
        return (EPD_WIDTH * EPD_HEIGHT) / 2;
    case 0x01:  // v2 MonoFast 1bpp
        return (EPD_WIDTH * EPD_HEIGHT) / 8;
    case 0x02:  // v2 ColorQual 8bpp
    case 0x03:  // v2 ColorFast 8bpp
        return EPD_WIDTH * EPD_HEIGHT;
#if defined(CONFIG_ARK_EPD_ABC_PREVIEW) || defined(CONFIG_ARK_EPD_ABC_DIAG)
    case 0x7F:
        return EPD_WIDTH * EPD_HEIGHT * 2u;
#endif
    default:
        return 0;
    }
}

/**
 * @brief 直读文件的一段字节（按 file_id 拼路径）
 *
 * 与 file_task 的读写由 VFS 串行化，故在 film_task 里直接读是安全的。
 */
static int film_read_file_range(uint32_t file_id, uint32_t offset, uint8_t *dst, uint32_t len)
{
    /* 目录与文件名拼进同一个缓冲：film/app 两个任务栈都只有 4KB，
       少一个 256B 的临时数组就少一分溢出风险 */
    char path[512];
    int n = snprintf(path, sizeof(path), "%s/", service_file_get_dir());
    if(n < 0 || n >= (int)sizeof(path))
    {
        sys_loge(FILM_TAG, "read range: dir path too long");
        return -1;
    }
    if(service_file_get_filename_safe(file_id, path + n, (uint32_t)sizeof(path) - (uint32_t)n) != 0)
    {
        sys_loge(FILM_TAG, "read range: bad file id %u", (unsigned)file_id);
        return -1;
    }

    FILE *fp = fopen(path, "rb");
    if(fp == NULL)
    {
        sys_loge(FILM_TAG, "read range: open failed %s", path);
        return -1;
    }

    if(fseek(fp, (long)offset, SEEK_SET) != 0 || fread(dst, 1, len, fp) != len)
    {
        sys_loge(FILM_TAG, "read range: read failed off=%u len=%u",
                 (unsigned)offset, (unsigned)len);
        fclose(fp);
        return -1;
    }

    fclose(fp);
    return 0;
}

/**
 * @brief 解析 32 字节文件头
 * @return 0 成功，-1 格式未知
 */
static int film_parse_header(const uint8_t *hdr, film_info_t *info)
{
    if(hdr == NULL || info == NULL)
    {
        return -1;
    }

    memset(info, 0, sizeof(*info));
    info->format = hdr[FILM_HDR_OFFSET_FORMAT];
    info->frame_size = film_frame_size_by_format(info->format);
    if(info->frame_size == 0)
    {
        sys_loge(FILM_TAG, "header: unknown format 0x%02X", info->format);
        return -1;
    }

    uint32_t count = (uint32_t)(hdr[FILM_HDR_OFFSET_FRAMECOUNT]
                              | (hdr[FILM_HDR_OFFSET_FRAMECOUNT + 1] << 8));
#if defined(CONFIG_ARK_EPD_ABC_PREVIEW) || defined(CONFIG_ARK_EPD_ABC_DIAG)
    if(info->format == 0x7F)
    {
        uint32_t body_size = (uint32_t)hdr[0] | ((uint32_t)hdr[1] << 8) |
                             ((uint32_t)hdr[2] << 16) | ((uint32_t)hdr[3] << 24);
        uint16_t width = hdr[4] | ((uint16_t)hdr[5] << 8);
        uint16_t height = hdr[6] | ((uint16_t)hdr[7] << 8);
        if(count != 1 || width != EPD_WIDTH || height != EPD_HEIGHT ||
           body_size != info->frame_size || memcmp(hdr + 12, "ABC9", 4) != 0)
        {
            sys_loge(FILM_TAG, "Invalid private ABC9 header");
            return -1;
        }
    }
#endif
    info->frame_count = (count == 0) ? 1u : count;
    return 0;
}

/**
 * @brief 读并解析文件头（不触发整份加载）
 *
 * 刻意不在这里调 film_ensure_loaded()：本函数也会被 app 任务调用（取帧数），
 * 触发加载会在 film_task 渲染途中把缓冲换掉。
 */
static int film_read_header(uint32_t file_id, film_info_t *info)
{
    if(info == NULL)
    {
        return -1;
    }

    // 内存里已有整份数据：直接解析（快路径）
    if(service_file_get_load_complete() == FILE_LOAD_STATE_DONE &&
       service_file_get_current_id() == file_id &&
       service_file_get_buffer() != NULL &&
       film_parse_header(service_file_get_buffer(), info) == 0)
    {
#if defined(CONFIG_ARK_EPD_ABC_PREVIEW) || defined(CONFIG_ARK_EPD_ABC_DIAG)
        if(info->format == 0x7F && service_file_get_size(file_id) != FILM_HDR_SIZE + info->frame_size)
            return -1;
#endif
        return 0;
    }

    // 否则只从 SD 读 32 字节头
    uint8_t hdr[FILM_HDR_SIZE];
    if(film_read_file_range(file_id, 0, hdr, FILM_HDR_SIZE) != 0)
    {
        return -1;
    }
    if(film_parse_header(hdr, info) != 0)
    {
        return -1;
    }

    /* 文件被截断 / 帧数虚高时按帧读会读到垃圾，先按实际大小把住。
       用除法而不是乘法：frame_count × frame_size 可能溢出 uint32
       （如 48 帧 ColorQual ≈ 16MB 尚可，但损坏头部的 65535 帧会溢出） */
    uint32_t size = service_file_get_size(file_id);
#if defined(CONFIG_ARK_EPD_ABC_PREVIEW) || defined(CONFIG_ARK_EPD_ABC_DIAG)
    if(info->format == 0x7F && size != FILM_HDR_SIZE + info->frame_size) return -1;
#endif
    if(size < FILM_HDR_SIZE ||
       info->frame_count > (size - FILM_HDR_SIZE) / info->frame_size)
    {
        sys_loge(FILM_TAG, "file %u truncated: size=%u frames=%u frame_size=%u",
                 (unsigned)file_id, (unsigned)size,
                 (unsigned)info->frame_count, (unsigned)info->frame_size);
        return -1;
    }

    return 0;
}

/**
 * @brief 取某一帧数据的指针
 *
 * 装得下就整份加载（RAM 播放更快）；装不下则只把这一帧读进复用缓冲。
 * 返回值只在 film_task 内有效（下一次调用可能覆盖）。
 */
static const uint8_t *film_frame_ptr(uint32_t file_id, uint32_t frame_idx, const film_info_t *info)
{
    if(film_ensure_loaded(file_id) == 0)
    {
        uint8_t *buf = service_file_get_buffer();
        if(buf != NULL)
        {
            return buf + FILM_HDR_SIZE + frame_idx * info->frame_size;
        }
    }

    // 流式兜底：复用一块"单帧大小"的 PSRAM 缓冲
    if(m_stream_cap < info->frame_size)
    {
        if(m_stream_buf != NULL)
        {
            heap_caps_free(m_stream_buf);
            m_stream_buf = NULL;
            m_stream_cap = 0;
        }
        m_stream_buf = (uint8_t*)heap_caps_malloc(info->frame_size, MALLOC_CAP_SPIRAM);
        if(m_stream_buf == NULL)
        {
            sys_loge(FILM_TAG, "stream buffer alloc failed: %u", (unsigned)info->frame_size);
            return NULL;
        }
        m_stream_cap = info->frame_size;
    }

    if(film_read_file_range(file_id, FILM_HDR_SIZE + frame_idx * info->frame_size,
                            m_stream_buf, info->frame_size) != 0)
    {
        return NULL;
    }
    return m_stream_buf;
}

/**
 * @brief 把一帧送屏（按 Format 选刷新方式）
 *
 * @param frame v1 传"整份文件"起始指针（hal 内部自己解析），v2 传帧数据指针
 */
static void film_show_frame(const uint8_t *frame, uint8_t format)
{
    /* MonoFast 走差分局刷：hal_epd_display_init()（硬复位）与 hal_epd_pwroff()（深睡）
       都会丢掉控制器里的上一帧，使下一次刷新退化为整屏刷新，故该路径不经这两步，
       电源（PON/REF/POF）由 hal_epd_display_mono 内部完成 */
    if(format == 0x01)
    {
        hal_epd_display_mono(frame);
        return;
    }

#if defined(CONFIG_ARK_EPD_ABC_PREVIEW) || defined(CONFIG_ARK_EPD_ABC_DIAG)
    if(format == 0x7F)
    {
        /* HAL validates all high bits before initialization or any REF. */
        if(hal_epd_display_abc9(frame, EPD_WIDTH * EPD_HEIGHT * 2u) != ESP_OK)
            sys_loge(FILM_TAG, "ABC9 frame rejected before display");
        return;
    }
#endif
    /* 整帧事务（init + 刷图 + 断电）必须在同一把面板锁里：
       否则切页时 ui_task 的复位/清场会插进这几步之间，把正在刷的一帧打断，
       双方都卡在 BUSY 等待里（见 hal_epd_lock 的说明）。锁可重入，
       下面各 hal_epd_* 自己也会取同一把锁。 */
    hal_epd_lock();
    hal_epd_display_init();
    switch(format)
    {
    case 0x02:  // v2 ColorQual（3 相）
        hal_epd_display_8bpp_mode(frame, 1);
        break;
    case 0x03:  // v2 ColorFast（2 相）
        hal_epd_display_8bpp_mode(frame, 0);
        break;
    default:    // v1 4bpp：整份文件
        hal_epd_display_film(frame);
        break;
    }
    hal_epd_pwroff();
    hal_epd_unlock();
}

/**
 * @brief 显示某个文件的第 frame_idx 帧（单帧图就是第 0 帧）
 */
static void film_show_file_frame(uint32_t file_id, uint32_t frame_idx)
{
    uint32_t file_count = service_file_get_count();
    if(file_count == 0)
    {
        sys_logw(FILM_TAG, "No files available");
        return;
    }
    if(file_id >= file_count)
    {
        sys_logw(FILM_TAG, "Invalid file ID: %u, using 0", (unsigned)file_id);
        file_id = 0;
    }

    film_info_t info;
    if(film_read_header(file_id, &info) != 0)
    {
        sys_loge(FILM_TAG, "show frame: bad file %u", (unsigned)file_id);
        return;
    }
    if(frame_idx >= info.frame_count)
    {
        sys_loge(FILM_TAG, "show frame: %u out of %u", (unsigned)frame_idx, (unsigned)info.frame_count);
        return;
    }

    // 面板能力守卫：非 3.7" 驱动的 hal_epd_display_film() 只解析 v1 4bpp，
    // 若目录混入 v2 单帧（mono/8bpp）会被当作 4bpp 误解析而花屏，这里提前拦截
    uint32_t caps = hal_epd_get_capabilities();
    if((info.format == 0x01 && !(caps & EPD_CAP_MONOFAST))
    || ((info.format == 0x02 || info.format == 0x03) && !(caps & EPD_CAP_8BPP)))
    {
        sys_logw(FILM_TAG, "format 0x%02X unsupported on this panel", info.format);
        return;
    }

    /* v1（Format 0x00）的 hal 入口要的是"整份文件"，走原有的整份加载路径 */
    if(info.format == 0x00)
    {
        if(film_ensure_loaded(file_id) != 0)
        {
            sys_loge(FILM_TAG, "v1 film load failed: %u", (unsigned)file_id);
            return;
        }
        film_show_frame(service_file_get_buffer(), info.format);
        sys_logi(FILM_TAG, "show v1 film %u, %u bytes",
                 (unsigned)file_id, (unsigned)service_file_get_buffer_size());
        return;
    }

    const uint8_t *frame = film_frame_ptr(file_id, frame_idx, &info);
    if(frame == NULL)
    {
        sys_loge(FILM_TAG, "frame %u/%u unavailable", (unsigned)frame_idx, (unsigned)info.frame_count);
        return;
    }

    sys_logd(FILM_TAG, "show frame %u/%u, format 0x%02X", (unsigned)frame_idx,
             (unsigned)info.frame_count, info.format);
    film_show_frame(frame, info.format);
}

/*********************************************************************
 * GLOBAL FUNCTIONS
 */

void service_film_init(void)
{
    if(m_film_task_hdl == NULL)
    {
        if ( pdPASS != xTaskCreate( film_task_handle, SYS_OS_NAME_FILM_TASK, SYS_OS_SIZE_FILM_TASK, NULL, SYS_OS_PRI_FILM_TASK, NULL ))
        {
            sys_loge(FILM_TAG, "film task create error!");
        }
    }
}

static void film_task_handle(void *pvParameters)
{
    m_film_msg_hdl = xQueueCreate( FILM_MSG_QUEUE_LENGTH, FILM_MSG_QUEUE_ITEM_SIZE );
    if(m_film_msg_hdl == NULL)
    {
        sys_loge(FILM_TAG, "film msg queue create error!");
        vTaskDelete(NULL);
        return;
    }

    for(;;)
    {
        film_msg_t msg;
        if(xQueueReceive( m_film_msg_hdl, (void *const)&msg, portMAX_DELAY ) == pdPASS)
        {
            switch(msg.ID)
            {
            case MSG_FILM_DISPLAY:
                film_display_event(msg.file_id);
                if(m_display_pending > 0)
                {
                    m_display_pending--;
                }
                break;
            case MSG_FILM_RENDER:
                /* 按帧推屏 = 连续逐帧刷新的会话：打开黑白快刷会话，
                   帧间不再各付一趟 PON/POF（每帧省约 200ms）。
                   收尾由 MSG_FILM_MONO_END 关会话并断电。 */
                hal_epd_mono_session_begin();
                film_render_frame_event(msg.file_id, msg.frame_idx);
                if(m_render_pending > 0)
                {
                    m_render_pending--;
                }
                /* 帧已落屏：通知动图 app 立刻推下一帧，省掉等下一个 100ms 心跳的空档。
                   订阅方只做非阻塞入队（见 app_manager_on_sys_event），不会拖慢本任务。 */
                sys_event_publish(SYS_EVT_FILM_FRAME_DONE, NULL, 0);
                break;
            case MSG_FILM_MONO_END:
                /* 结束黑白快刷会话：把播放期间保持上电的面板断电 */
                hal_epd_mono_session_end();
                if(m_render_pending > 0)
                {
                    m_render_pending--;
                }
                break;
            default:
                break;
            }
        }
    }
}

static void film_msg_send(void *p_msg, bool in_isr)
{
    if(m_film_msg_hdl != NULL)
    {
        if(in_isr == 0)
        {
            if(xQueueSend(m_film_msg_hdl, p_msg, portMAX_DELAY) != pdPASS)
            {
                sys_loge(FILM_TAG, "film msg send error!");
            }
        }
        else
        {
            BaseType_t xHigherPriorityTaskWoken;
            xHigherPriorityTaskWoken = pdFALSE;
            xQueueSendFromISR( m_film_msg_hdl, p_msg, &xHigherPriorityTaskWoken );
            portYIELD_FROM_ISR( xHigherPriorityTaskWoken );
        }
    }
}

static void film_display_event(uint32_t file_id)
{
    sys_logi(FILM_TAG, "Displaying file: %d", file_id);
    film_show_file_frame(file_id, 0);
}

static void film_render_frame_event(uint32_t file_id, uint32_t frame_idx)
{
    film_show_file_frame(file_id, frame_idx);
}

void service_film_display(uint32_t file_id)
{
    if(m_film_msg_hdl == NULL)
    {
        return;
    }

    film_msg_t msg;
    msg.ID = MSG_FILM_DISPLAY;
    msg.file_id = file_id;
    msg.frame_idx = 0;
    m_display_pending++;
    film_msg_send(&msg, 0);
}

void service_film_render_frame(uint32_t file_id, uint32_t frame_idx)
{
    if(m_film_msg_hdl == NULL)
    {
        return;
    }

    film_msg_t msg;
    msg.ID = MSG_FILM_RENDER;
    msg.file_id = file_id;
    msg.frame_idx = frame_idx;

    /* 非阻塞投递：单帧刷新耗时（数百 ms ~ 数秒）远大于动图的推送节奏，
       若像 service_film_display() 那样 portMAX_DELAY 排队，队列会积压几十帧
       ——退出页面后它还在刷屏，排满后更会把调用任务（app 任务，含按键处理）
       一起卡死。队列满就丢这一帧，调用方靠 service_film_is_busy() 下个 tick 再试。 */
    if(xQueueSend(m_film_msg_hdl, &msg, 0) == pdPASS)
    {
        m_render_pending++;
    }
    else
    {
        sys_logw(FILM_TAG, "render queue full, drop frame %u", (unsigned)frame_idx);
    }
}

int service_film_is_busy(void)
{
    return (m_render_pending > 0 || m_display_pending > 0) ? 1 : 0;
}

void service_film_cancel_pending(void)
{
    if(m_film_msg_hdl == NULL)
    {
        return;
    }

    /* 抽干队列里所有尚未处理的请求：切走页面之后它们不该再落屏。
       丢弃的请求要同时从计数里扣掉，否则下面的等待会空等到超时。 */
    film_msg_t msg;
    while(xQueueReceive(m_film_msg_hdl, &msg, 0) == pdPASS)
    {
        if(msg.ID == MSG_FILM_RENDER && m_render_pending > 0)
        {
            m_render_pending--;
        }
        else if(msg.ID == MSG_FILM_DISPLAY && m_display_pending > 0)
        {
            m_display_pending--;
        }
    }

    /* 让 film_task 收掉"保持上电"的黑白快刷会话（补一次 POF）。
       断电必须由 film_task 执行：面板 SPI 是单写者，从别的任务直接发命令
       会和正在上屏的帧交错。这里把它也算作一个未完成操作，与在途帧一起等。 */
    film_msg_t off = {0};
    off.ID = MSG_FILM_MONO_END;
    m_render_pending++;
    if(xQueueSend(m_film_msg_hdl, &off, 0) != pdPASS)
    {
        m_render_pending--;
        sys_logw(FILM_TAG, "mono end msg send error!");
    }

    /* 等在途帧与断电都完成（上限 15s）：否则它们会在新页面之后落屏／断电，
       表现为"按返回没反应 / 画面停在动图"。整图首帧（尤其 v1 4bpp）
       单次刷新可达数秒，超时按 3s 会在画面还在刷时放行切页，与 UI 层抢 SPI。 */
    uint32_t wait_count = 0;
    while((m_render_pending > 0 || m_display_pending > 0) && wait_count < 750)
    {
        vTaskDelay(10 / portTICK_PERIOD_MS);
        wait_count++;
    }
    if(m_render_pending > 0 || m_display_pending > 0)
    {
        /* 面板还在刷（大图 8bpp 可达数十秒）：HAL 的会话锁会保证后续页面
           排在它后面，不会并发驱动；这里只提示这次的等待提前结束。 */
        sys_logw(FILM_TAG, "cancel pending timeout %ums: render=%u display=%u",
                 (unsigned)(wait_count * 10), (unsigned)m_render_pending,
                 (unsigned)m_display_pending);
    }
    else
    {
        sys_logi(FILM_TAG, "cancel pending done in %ums", (unsigned)(wait_count * 10));
    }
    m_render_pending = 0;
    m_display_pending = 0;
}

uint32_t service_film_get_frame_count(uint32_t file_id)
{
    /* 只读文件头（不触发整份加载）：本函数会被 app 任务调用，
       在这里加载会把 film_task 正在用的缓冲换掉 */
    film_info_t info;
    if(film_read_header(file_id, &info) != 0)
    {
        /* 失败返回 0：调用方（动图 app）据此停止推进/重试，
           以前这里回 1 会让它以为"单帧动图"而无限循环切文件 */
        return 0;
    }
    return info.frame_count;
}
