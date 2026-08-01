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
#include "service_param.h"
#include "service_film.h"
#include "service_wifi.h"

/*********************************************************************
 * MACROS
 */
#define FILM_MSG_QUEUE_LENGTH       30
#define FILM_MSG_QUEUE_ITEM_SIZE    sizeof( film_msg_t )

#define SYS_OS_PRI_FILM_TASK        (5)
#define SYS_OS_SIZE_FILM_TASK       (4096)
#define SYS_OS_NAME_FILM_TASK       "film_task"

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
static void film_next_event(void);
static void film_prev_event(void);
static void film_init_event(void);
static void film_clear_event(void);
static void film_download_event(void);

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

    // 等待文件完成加载
    vTaskDelay(2000 / portTICK_PERIOD_MS);

    // 发送初始化消息
    film_msg_t msg;
    msg.ID = MSG_FILM_INIT;
    film_msg_send(&msg, 0);

    // 注册按键回调
    // hal_button_register_cb(HAL_BUTTON_PRESS_SHORT, service_film_next);
    hal_button_register_cb(HAL_BUTTON_PRESS_UP, service_film_prev);
    hal_button_register_cb(HAL_BUTTON_PRESS_DOWN, service_film_next);
    hal_button_register_cb(HAL_BUTTON_PRESS_LONG, service_film_clear);
    hal_button_register_cb(HAL_BUTTON_PRESS_LONG, film_download_event);

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
            case MSG_FILM_NEXT:
                film_next_event();
                break;
            case MSG_FILM_PREV:
                film_prev_event();
                break;
            case MSG_FILM_INIT:
                film_init_event();
                break;
            case MSG_FILM_CLEAR:
                film_clear_event();
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

static void film_init_event(void)
{
    sys_logi(FILM_TAG, "Film init event complete %d play mode %d", g_service_param.film.load_complete, g_service_param.film.play_mode);

    // 检查服务参数
    if(g_service_param.film.load_complete)
    {
        // 加载完成
        if(g_service_param.film.play_mode == FILM_PLAY_MODE_AUTO)
        {
            // 本地轮播模式下，每次芯片启动后自动播放下一张照片
            sys_logi(FILM_TAG, " local play mode, displaying next image");
            film_next_event();
        }
        else if(g_service_param.film.play_mode == FILM_PLAY_MODE_WIFI)
        {
            // WiFi轮播模式下，等待WiFi连接建立（最长10s）
            sys_logi(FILM_TAG, "WiFi play mode, waiting for WiFi connection...");
            int retry = 0;
            while(retry < 100 && !service_wifi_get_connect_status())
            {
                vTaskDelay(pdMS_TO_TICKS(100));
                retry++;
            }
            if(service_wifi_get_connect_status())
            {
                sys_logi(FILM_TAG, "WiFi connected, starting download");
                service_wifi_download_start();
            }
            else
            {
                sys_logw(FILM_TAG, "WiFi connection timeout, fallback to local mode");
                film_next_event();
            }
        }
    }
    else
    {
        // 未加载完成，刷新照片
        sys_logi(FILM_TAG, "Load not complete, refreshing image");
        film_display_event(g_service_param.film.current_file_id);
    }
}

static void film_clear_event(void)
{
    sys_logi(FILM_TAG, "Film clear event");
    
    g_service_param.film.load_complete = 0;
    g_service_param.film.current_file_id = 0;
    g_service_param.film.play_mode = 0;
    service_param_save();

    hal_epd_display_init();
    hal_epd_display_white();
    hal_epd_pwroff();
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
    else
    {
        char filename[256];
        if(service_file_get_filename_safe(file_id, filename, sizeof(filename)) == 0)
        {
            sys_logi(FILM_TAG, "File name: %s", filename);
        }
    }

    // 等待文件加载完成（最多等待3秒）
    uint32_t wait_count = 0;
    while((service_file_get_load_complete() != FILE_LOAD_STATE_DONE) && wait_count < 300)
    {
        vTaskDelay(10 / portTICK_PERIOD_MS);
        wait_count++;
    }
    
    uint8_t* buffer = NULL;
    if((buffer = service_file_get_buffer()) == NULL)
    {
        sys_loge(FILM_TAG, "Failed to get file buffer");
        return;
    }

    // 更新状态
    g_service_param.film.current_file_id = file_id;
    g_service_param.film.load_complete = 0;
    service_param_save();

    // 调用EPD显示接口
    hal_epd_display_init();
    hal_epd_display_film(buffer);
    hal_epd_pwroff();

    // 更新状态
    g_service_param.film.load_complete = 1;
    service_param_save();

    sys_logi(FILM_TAG, "Refresh event completed");
}

static void film_next_event(void)
{
    // 获取文件总数
    uint32_t file_count = service_file_get_count();
    if(file_count == 0)
    {
        sys_logw(FILM_TAG, "No files available");
        return;
    }

    // 计算下一张图片ID
    uint32_t current_id = g_service_param.film.current_file_id;
    uint32_t next_id = (current_id + 1) % file_count;

    // 显示下一张图片
    if(g_service_param.film.load_complete)
    {
        // 加载完成，直接显示下一张图片
        film_display_event(next_id);
    }
    else
    {
        // 未加载完成，更新无效
        sys_logi(FILM_TAG, "Load not complete, updating invalid");
    }
}

static void film_prev_event(void)
{
    // 获取文件总数
    uint32_t file_count = service_file_get_count();
    if(file_count == 0)
    {
        sys_logw(FILM_TAG, "No files available");
        return;
    }

    // 计算上一张图片ID
    uint32_t current_id = g_service_param.film.current_file_id;
    uint32_t prev_id = (current_id == 0) ? (file_count - 1) : (current_id - 1);

    // 显示上一张图片
    if(g_service_param.film.load_complete)
    {
        // 加载完成，直接显示上一张图片
        film_display_event(prev_id);
    }
    else
    {
        // 未加载完成，更新无效
        sys_logi(FILM_TAG, "Load not complete, updating invalid");
    }
}

static void film_download_event(void)
{
    if(g_service_param.film.load_complete)
    {
        service_wifi_download_start();
    }
}


void service_film_display(uint32_t file_id)
{
    film_msg_t msg;
    msg.ID = MSG_FILM_DISPLAY;
    msg.file_id = file_id;
    film_msg_send(&msg, 0);
}

void service_film_next(void)
{
    if(g_service_param.film.load_complete)
    {
        // 加载完成，显示下一张图片
        film_msg_t msg;
        msg.ID = MSG_FILM_NEXT;
        film_msg_send(&msg, 0);
    }
    else
    {
        sys_logi(FILM_TAG, "Load not complete, next event invalid");
    }
}

void service_film_prev(void)
{
    if(g_service_param.film.load_complete)
    {
        // 加载完成，显示上一张图片
        film_msg_t msg;
        msg.ID = MSG_FILM_PREV;
        film_msg_send(&msg, 0);
    }
    else
    {
        sys_logi(FILM_TAG, "Load not complete, prev event invalid");
    }
}

void service_film_clear(void)
{
    if(g_service_param.film.load_complete)
    {
        // 加载完成，清除显示
        film_msg_t msg;
        msg.ID = MSG_FILM_CLEAR;
        film_msg_send(&msg, 0);
    }
    else
    {
        sys_logi(FILM_TAG, "Load not complete, clear event invalid");
    }
}

void service_film_set_play_mode(uint8_t mode)
{
    g_service_param.film.play_mode = mode;
    service_param_save();
    
    sys_logi(FILM_TAG, "Set play mode: %d", mode);
}

uint32_t service_film_get_current_id(void)
{
    return g_service_param.film.current_file_id;
}

uint8_t service_film_get_load_complete(void)
{
    return g_service_param.film.load_complete;
}

