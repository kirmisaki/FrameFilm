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

#include "sys_log.h"
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

/*********************************************************************
* TYPEDEFS
*/


/*********************************************************************
 * CONSTANTS
 */

/*********************************************************************
 * LOCAL VARIABLES
 */
static TaskHandle_t m_film_task_hdl = NULL;
static QueueHandle_t m_film_msg_hdl = NULL;

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
 * @brief 等待文件加载完成（最多 3s）
 */
static void film_wait_load_done(void)
{
    uint32_t wait_count = 0;
    while((service_file_get_load_complete() != FILE_LOAD_STATE_DONE) && wait_count < 300)
    {
        vTaskDelay(10 / portTICK_PERIOD_MS);
        wait_count++;
    }
}

/**
 * @brief 确保指定文件已加载到 PSRAM 缓冲
 * @return 0 成功，-1 失败
 */
static int film_ensure_loaded(uint32_t file_id)
{
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

    if(service_file_get_load_complete() != FILE_LOAD_STATE_DONE)
    {
        sys_loge(FILM_TAG, "load file %d timeout", file_id);
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
    default:
        return 0;
    }
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
                break;
            case MSG_FILM_RENDER:
                film_render_frame_event(msg.file_id, msg.frame_idx);
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

    // 检查文件ID是否有效
    uint32_t file_count = service_file_get_count();
    sys_logi(FILM_TAG, "Total count: %d", file_count);
    if(file_count == 0)
    {
        sys_logw(FILM_TAG, "No files available");
        return;
    }

    if(file_id >= file_count)
    {
        sys_logw(FILM_TAG, "Invalid file ID: %d, using 0", file_id);
        file_id = 0;
    }

    // 加载文件
    if(service_file_load(file_id) != 0)
    {
        sys_loge(FILM_TAG, "Failed to load file: %d", file_id);
        return;
    }

    char filename[256];
    if(service_file_get_filename_safe(file_id, filename, sizeof(filename)) == 0)
    {
        sys_logi(FILM_TAG, "File name: %s", filename);
    }

    // 等待文件加载完成（最多等待3秒）
    film_wait_load_done();

    uint8_t* buffer = NULL;
    if((buffer = service_file_get_buffer()) == NULL)
    {
        sys_loge(FILM_TAG, "Failed to get file buffer");
        return;
    }

    // 面板能力守卫：非 3.7" 驱动的 hal_epd_display_film() 只解析 v1 4bpp，
    // 若目录混入 v2 单帧（mono/8bpp）会被当作 4bpp 误解析而花屏，这里提前拦截
    uint8_t format = buffer[FILM_HDR_OFFSET_FORMAT];
    uint32_t caps = hal_epd_get_capabilities();
    if((format == 0x01 && !(caps & EPD_CAP_MONOFAST))
    || ((format == 0x02 || format == 0x03) && !(caps & EPD_CAP_8BPP)))
    {
        sys_logw(FILM_TAG, "display: format 0x%02X unsupported on this panel", format);
        return;
    }

    // 调用EPD显示接口
    hal_epd_display_init();
    hal_epd_display_film(buffer);
    hal_epd_pwroff();

    sys_logi(FILM_TAG, "Refresh event completed");
}

static void film_render_frame_event(uint32_t file_id, uint32_t frame_idx)
{
    if(film_ensure_loaded(file_id) != 0)
    {
        return;
    }

    uint8_t *buffer = service_file_get_buffer();
    if(buffer == NULL)
    {
        sys_loge(FILM_TAG, "render_frame: buffer NULL");
        return;
    }

    uint8_t format = buffer[FILM_HDR_OFFSET_FORMAT];
    uint16_t frame_count = (uint16_t)(buffer[FILM_HDR_OFFSET_FRAMECOUNT]
                                     | (buffer[FILM_HDR_OFFSET_FRAMECOUNT + 1] << 8));
    uint32_t count = (frame_count == 0) ? 1u : (uint32_t)frame_count;

    if(frame_idx >= count)
    {
        sys_loge(FILM_TAG, "render_frame: frame %d out of %d", frame_idx, count);
        return;
    }

    uint32_t frame_size = film_frame_size_by_format(format);
    if(frame_size == 0)
    {
        sys_loge(FILM_TAG, "render_frame: unknown format 0x%02X", format);
        return;
    }

    const unsigned char *frame_ptr = buffer + FILM_HDR_SIZE + frame_idx * frame_size;

    sys_logi(FILM_TAG, "Render frame %d/%d, format 0x%02X", frame_idx, count, format);

    hal_epd_display_init();
    switch(format)
    {
    case 0x01:  // v2 MonoFast
        hal_epd_display_mono(frame_ptr);
        break;
    case 0x02:  // v2 ColorQual（3 相）
        hal_epd_display_8bpp_mode(frame_ptr, 1);
        break;
    case 0x03:  // v2 ColorFast（2 相）
        hal_epd_display_8bpp_mode(frame_ptr, 0);
        break;
    default:    // v1 4bpp 单帧
        hal_epd_display_film(buffer);
        break;
    }
    hal_epd_pwroff();
}

void service_film_display(uint32_t file_id)
{
    film_msg_t msg;
    msg.ID = MSG_FILM_DISPLAY;
    msg.file_id = file_id;
    msg.frame_idx = 0;
    film_msg_send(&msg, 0);
}

void service_film_render_frame(uint32_t file_id, uint32_t frame_idx)
{
    film_msg_t msg;
    msg.ID = MSG_FILM_RENDER;
    msg.file_id = file_id;
    msg.frame_idx = frame_idx;
    film_msg_send(&msg, 0);
}

uint32_t service_film_get_frame_count(uint32_t file_id)
{
    if(film_ensure_loaded(file_id) != 0)
    {
        return 1;
    }

    uint8_t *buffer = service_file_get_buffer();
    if(buffer == NULL)
    {
        return 1;
    }

    uint16_t frame_count = (uint16_t)(buffer[FILM_HDR_OFFSET_FRAMECOUNT]
                                     | (buffer[FILM_HDR_OFFSET_FRAMECOUNT + 1] << 8));
    return (frame_count == 0) ? 1u : (uint32_t)frame_count;
}
