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
 * FileName : /film_service/src/service_file.c
 * Author: Kiritro  Version: v0.1  Date: 2026/4/21
 * Description: 文件服务初始化
 * ChangeLog: Change Notes
 *
 *********************************************************************/

/*********************************************************************
 * INCLUDES
 */
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "freertos/semphr.h"

#include "esp_heap_caps.h"

#include "sys_log.h"
#include "sys_event.h"
#include "hal_api.h"
#include "service_file.h"

/*********************************************************************
 * MACROS
 */
#define FILE_MSG_QUEUE_LENGTH       30
#define FILE_MSG_QUEUE_ITEM_SIZE    sizeof( file_msg_t )

#define SYS_OS_PRI_FILE_TASK        (6)
#define SYS_OS_SIZE_FILE_TASK       (4096)
#define SYS_OS_NAME_FILE_TASK       "file_task"

#define FILE_TIMER_BASE_INTERVAL_MS (1000)
#define FILE_SD_CHECK_INTERVAL_MS   (5000)
#define FILE_SD_CHECK_TICK_COUNT    (FILE_SD_CHECK_INTERVAL_MS / FILE_TIMER_BASE_INTERVAL_MS)

#define FILM_HEADER_SIZE            (32)

// .film 文件头偏移：帧数（2 字节小端）
#define FILM_HDR_OFFSET_FRAMECOUNT  (0x0A)

// set_dir_sync 等待列表刷新的超时上限（SD 首次挂载/大目录扫描可能较慢）
#define FILE_DIR_READY_TIMEOUT_MS   (2000)

/*********************************************************************
* TYPEDEFS
*/
typedef struct {
    file_item_t* file_list;  // 文件列表
    uint32_t file_count;     // 文件数量
    uint32_t current_file_id; // 当前加载的文件ID
    uint8_t* psram_buffer;   // PSRAM缓冲区
    uint8_t load_complete;   // 文件加载完成状态
    uint32_t buffer_size;    // 缓冲区大小
    uint8_t sd_mounted;      // SD卡挂载状态
    FILE* save_file_handle;  // 文件保存句柄
    uint32_t save_file_size; // 要保存的文件大小
    uint32_t save_written;   // 已写入的字节数
    char save_filename[256]; // 当前保存的文件名
    char save_dir[64];       // 保存时的工作目录快照（避免保存途中切目录导致路径错乱）
    char save_path[512];     // 保存时的完整路径（显式路径模式下由相对路径拼接而来）
    uint8_t save_skip_relocate; // 1：显式路径保存，完成后跳过 relocate/列表刷新/事件上浮
    uint8_t save_auto_load;  // 保存完成后是否自动加载显示（0：静默，1：自动加载）
} file_service_state_t;

/*********************************************************************
 * CONSTANTS
 */

/*********************************************************************
 * LOCAL VARIABLES
 */
static TaskHandle_t m_file_task_hdl = NULL;
static QueueHandle_t m_file_msg_hdl = NULL;
static TimerHandle_t m_file_timer = NULL;
static file_service_state_t m_file_state;
static SemaphoreHandle_t m_file_list_mutex = NULL;  // 保护 file_list/file_count 跨任务访问
static char m_file_active_dir[64];                  // 当前工作目录（/sdcard/film 或 /sdcard/animation）
static volatile uint8_t m_list_ready = 0;           // 当前目录列表是否已刷新完成（供 set_dir_sync 等待）

/*********************************************************************
 * GLOBAL VARIABLES
 */

/*********************************************************************
 * LOCAL FUNCTIONS
 */
static void file_task_handle(void *pvParameters);
static void file_msg_send(void *p_msg, bool in_isr);
static void file_timer_callback(TimerHandle_t xTimer);

static void file_list_refresh_event(void);
static void file_load_event(uint32_t file_id);
static void file_load_next_event(void);
static void file_sd_check_event(void);
static void file_free_buffer(void);
static void file_relocate_by_frame_count(void);
static void file_publish_list_event(void);
static int  file_mkdir_parents(const char *filepath);

/*********************************************************************
 * LOCAL HELPERS
 */
static void file_list_lock(void)
{
    if(m_file_list_mutex)
    {
        xSemaphoreTake(m_file_list_mutex, portMAX_DELAY);
    }
}

static void file_list_unlock(void)
{
    if(m_file_list_mutex)
    {
        xSemaphoreGive(m_file_list_mutex);
    }
}

/*********************************************************************
 * GLOBAL FUNCTIONS
 */

void service_file_init(void)
{
    memset(&m_file_state, 0, sizeof(file_service_state_t));
    m_file_state.file_list = NULL;
    m_file_state.file_count = 0;
    m_file_state.current_file_id = 0;
    m_file_state.psram_buffer = NULL;
    m_file_state.buffer_size = 0;
    m_file_state.sd_mounted = 0;
    m_file_state.load_complete = FILE_LOAD_STATE_NONE;
    m_file_state.save_file_handle = NULL;
    m_file_state.save_file_size = 0;
    m_file_state.save_written = 0;
    m_file_state.save_auto_load = 1;

    m_list_ready = 0;   // 首次列表由 SD 挂载后的刷新事件置位

    // 默认工作目录为图片目录
    strncpy(m_file_active_dir, FILM_DIR, sizeof(m_file_active_dir) - 1);
    m_file_active_dir[sizeof(m_file_active_dir) - 1] = '\0';

    if(m_file_list_mutex == NULL)
    {
        m_file_list_mutex = xSemaphoreCreateMutex();
    }

    if(m_file_task_hdl == NULL)
    {
        if ( pdPASS != xTaskCreate( file_task_handle, SYS_OS_NAME_FILE_TASK, SYS_OS_SIZE_FILE_TASK, NULL, SYS_OS_PRI_FILE_TASK, NULL ))
        {
            sys_loge(FILE_TAG, "file task create error!");
        }
    }

    if(m_file_timer == NULL)
    {
        m_file_timer = xTimerCreate( "file_timer", pdMS_TO_TICKS(FILE_TIMER_BASE_INTERVAL_MS), pdTRUE, NULL, file_timer_callback );
        if(m_file_timer == NULL)
        {
            sys_loge(FILE_TAG, "file timer create error!");
        }
    }
    xTimerStart(m_file_timer, 0);

    // 检查SD卡状态
    file_sd_check_event();
}

void service_file_set_dir(const char *dir)
{
    if(dir == NULL)
    {
        return;
    }

    // 目录未变化则不处理
    if(strcmp(m_file_active_dir, dir) == 0)
    {
        return;
    }

    strncpy(m_file_active_dir, dir, sizeof(m_file_active_dir) - 1);
    m_file_active_dir[sizeof(m_file_active_dir) - 1] = '\0';

    // 切换目录后清空列表与当前ID，并触发刷新
    file_list_lock();
    if(m_file_state.file_list)
    {
        free(m_file_state.file_list);
        m_file_state.file_list = NULL;
    }
    m_file_state.file_count = 0;
    m_file_state.current_file_id = 0;
    m_file_state.load_complete = FILE_LOAD_STATE_NONE;
    /* 必须在锁内置零：若正在进行的旧刷新在锁外结束时把标志置 1，
       set_dir_sync 会误判新目录列表已就绪而立即返回空 count */
    m_list_ready = 0;
    file_list_unlock();

    sys_logi(FILE_TAG, "Switch file dir to: %s, refreshing", m_file_active_dir);
    service_file_refresh_list();
}

uint8_t service_file_is_list_ready(void)
{
    return m_list_ready;
}

uint32_t service_file_set_dir_sync(const char *dir)
{
    service_file_set_dir(dir);

    // 轮询等待列表刷新完成（set_dir 为异步投递）
    uint32_t waited = 0;
    while(m_list_ready == 0 && waited < FILE_DIR_READY_TIMEOUT_MS)
    {
        vTaskDelay(pdMS_TO_TICKS(10));
        waited += 10;
    }

    if(m_list_ready == 0)
    {
        sys_logw(FILE_TAG, "wait list ready timeout: %s", m_file_active_dir);
    }

    return service_file_get_count();
}

const char *service_file_get_dir(void)
{
    return m_file_active_dir;
}

static void file_task_handle(void *pvParameters)
{
    m_file_msg_hdl = xQueueCreate( FILE_MSG_QUEUE_LENGTH, FILE_MSG_QUEUE_ITEM_SIZE );
    if(m_file_msg_hdl == NULL)
    {
        sys_loge(FILE_TAG, "file msg queue create error!");
        vTaskDelete(NULL);
        return;
    }

    for(;;)
    {
        file_msg_t msg;
        if(xQueueReceive( m_file_msg_hdl, (void *const)&msg, portMAX_DELAY ) == pdPASS)
        {
            switch(msg.ID)
            {
            case MSG_FILE_LIST_REFRESH:
                file_list_refresh_event();
                file_publish_list_event();
                break;
            case MSG_FILE_LOAD:
                file_load_event(msg.file_id);
                break;
            case MSG_FILE_LOAD_NEXT:
                file_load_next_event();
                break;
            case MSG_SD_MOUNTED:
                // 先刷新列表再上浮事件，避免订阅者读到旧的 count
                file_list_refresh_event();
                file_publish_list_event();
                sys_event_publish(SYS_EVT_SD_MOUNT, NULL, 0);
                break;
            case MSG_SD_UNMOUNTED:
                file_free_buffer();
                file_list_lock();
                if(m_file_state.file_list)
                {
                    free(m_file_state.file_list);
                    m_file_state.file_list = NULL;
                }
                m_file_state.file_count = 0;
                m_file_state.current_file_id = 0;
                file_list_unlock();
                sys_event_publish(SYS_EVT_SD_UNMOUNT, NULL, 0);
                break;
            case MSG_FILE_SAVE_START:
            case MSG_FILE_SAVE_START_TO:
                if(msg.file_size >= FILM_HEADER_SIZE)
                {
                    if(msg.ID == MSG_FILE_SAVE_START_TO)
                    {
                        // 显式路径模式：pdata 为相对 /sdcard 的路径（如 app/image/cover.film）
                        snprintf(m_file_state.save_path, sizeof(m_file_state.save_path), "/sdcard/%s", (char*)msg.pdata);
                        memset(m_file_state.save_filename, 0, sizeof(m_file_state.save_filename));
                        m_file_state.save_dir[0] = '\0';
                        m_file_state.save_skip_relocate = 1;
                        if(file_mkdir_parents(m_file_state.save_path) != 0)
                        {
                            sys_loge(FILE_TAG, "Create parent dir failed: %s", m_file_state.save_path);
                        }
                    }
                    else
                    {
                        snprintf(m_file_state.save_filename, sizeof(m_file_state.save_filename), "%s", (char*)msg.pdata);
                        // 快照当前工作目录：保存途中若 app 切换目录，路径仍以开始保存时为准
                        strncpy(m_file_state.save_dir, m_file_active_dir, sizeof(m_file_state.save_dir) - 1);
                        m_file_state.save_dir[sizeof(m_file_state.save_dir) - 1] = '\0';
                        snprintf(m_file_state.save_path, sizeof(m_file_state.save_path), "%s/%s", m_file_state.save_dir, m_file_state.save_filename);
                        m_file_state.save_skip_relocate = 0;
                    }

                    m_file_state.save_file_handle = fopen(m_file_state.save_path, "wb");
                    if(m_file_state.save_file_handle == NULL)
                    {
                        sys_loge(FILE_TAG, "Open file for save failed: %s", m_file_state.save_path);
                    }
                    else
                    {
                        m_file_state.save_file_size = msg.file_size;
                        m_file_state.save_written = 0;
                        sys_logi(FILE_TAG, "Start save file: %s, size: %d", m_file_state.save_path, msg.file_size);
                    }
                }
                else
                {
                    sys_loge(FILE_TAG, "Invalid file size: %d, must >= %d", msg.file_size, FILM_HEADER_SIZE);
                }
                if(msg.pdata)
                {
                    free(msg.pdata);
                }
                break;
            case MSG_FILE_SAVE_DATA:
                if(m_file_state.save_file_handle && msg.pdata)
                {
                    size_t written = fwrite(msg.pdata, 1, msg.data_len, m_file_state.save_file_handle);
                    m_file_state.save_written += written;
                    // sys_logi(FILE_TAG, "Written %d bytes, total: %d/%d", written, m_file_state.save_written, m_file_state.save_file_size);
                }
                if(msg.pdata)
                {
                    free(msg.pdata);
                }
                break;
            case MSG_FILE_SAVE_STOP:
                if(m_file_state.save_file_handle)
                {
                    fclose(m_file_state.save_file_handle);
                    m_file_state.save_file_handle = NULL;
                    sys_logi(FILE_TAG, "Save file complete: %s, written: %d", m_file_state.save_path, m_file_state.save_written);
                    if(m_file_state.save_written != m_file_state.save_file_size)
                    {
                        sys_loge(FILE_TAG, "File size mismatch: written=%d, expected=%d", m_file_state.save_written, m_file_state.save_file_size);
                    }
                    else if(m_file_state.save_skip_relocate)
                    {
                        // 显式路径（app 封面等）：不参与图片/动图列表，不刷新列表、不上浮事件
                        sys_logi(FILE_TAG, "Explicit path save done, skip relocate/list/event");
                    }
                    else
                    {
                        sys_logi(FILE_TAG, "Refreshing file list and loading new photo...");
                        // 按帧数分流：多帧动图归入 /sdcard/animation，单帧图片归入 /sdcard/film
                        file_relocate_by_frame_count();
                        // 刷新当前工作目录的文件列表
                        file_list_refresh_event();
                        file_publish_list_event();

                        // 静默模式（批量上传）不自动加载显示，仅保存
                        if(m_file_state.save_auto_load)
                        {
                            // 定位新文件在列表中的下标，交由 app 层在事件中显示（服务层不直接驱动刷屏）
                            for(uint32_t i = 0; i < m_file_state.file_count; i++)
                            {
                                if(strcmp(m_file_state.file_list[i].filename, m_file_state.save_filename) == 0)
                                {
                                    sys_logi(FILE_TAG, "Found new file at index %d", i);
                                    m_file_state.current_file_id = i;
                                    break;
                                }
                            }
                        }

                        // 全部保存处理完成后上浮事件（刷新列表已就绪，动图重置到第一个 / 图片刷新显示）
                        // payload: u8 auto_load，0 表示静默保存，app 层据此决定是否自动显示
                        uint8_t auto_load = m_file_state.save_auto_load ? 1 : 0;
                        sys_event_publish(SYS_EVT_FILE_SAVED, &auto_load, sizeof(auto_load));
                    }
                }
                m_file_state.save_file_size = 0;
                m_file_state.save_written = 0;
                m_file_state.save_skip_relocate = 0;
                memset(m_file_state.save_filename, 0, sizeof(m_file_state.save_filename));
                memset(m_file_state.save_path, 0, sizeof(m_file_state.save_path));
                break;
            default:
                break;
            }
        }
    }
}

static void file_msg_send(void *p_msg, bool in_isr)
{
    if(m_file_msg_hdl != NULL)
    {
        if(in_isr == 0)
        {
            if(xQueueSend(m_file_msg_hdl, p_msg, portMAX_DELAY) != pdPASS)
            {
                sys_loge(FILE_TAG, "file msg send error!");
            }
        }
        else
        {
            BaseType_t xHigherPriorityTaskWoken;
            xHigherPriorityTaskWoken = pdFALSE;
            xQueueSendFromISR( m_file_msg_hdl, p_msg, &xHigherPriorityTaskWoken );
            portYIELD_FROM_ISR( xHigherPriorityTaskWoken );
        }
    }
}

static void file_timer_callback(TimerHandle_t xTimer)
{
    static uint32_t tick_counter = 0;
    tick_counter++;

    if((tick_counter % FILE_SD_CHECK_TICK_COUNT) == 0)
    {
        file_sd_check_event();
    }
}

static void file_sd_check_event(void)
{
    int sd_status = hal_sd_get_status();
    if(sd_status == SD_MOUNT)
    {
        if(m_file_state.sd_mounted == 0)
        {
            m_file_state.sd_mounted = 1;
            file_msg_t msg;
            msg.ID = MSG_SD_MOUNTED;
            file_msg_send(&msg, 0);
        }
    }
    else
    {
        if(m_file_state.sd_mounted == 1)
        {
            m_file_state.sd_mounted = 0;
            file_msg_t msg;
            msg.ID = MSG_SD_UNMOUNTED;
            file_msg_send(&msg, 0);
        }
    }
}

static void file_list_refresh_event(void)
{
    file_list_lock();

    if(!m_file_state.sd_mounted)
    {
        sys_logw(FILE_TAG, "SD card not mounted");
        m_list_ready = 1;   // 无 SD 也视为已结算，避免 set_dir_sync 空等超时
        file_list_unlock();
        return;
    }

    // 释放旧的文件列表
    if(m_file_state.file_list)
    {
        free(m_file_state.file_list);
        m_file_state.file_list = NULL;
    }
    m_file_state.file_count = 0;

    // 检查目录是否存在，不存在则创建
    DIR* dir = opendir(m_file_active_dir);
    if(dir == NULL)
    {
        sys_logi(FILE_TAG, "Film directory not found, creating %s...", m_file_active_dir);
        // 创建目录
        if(mkdir(m_file_active_dir, 0777) != 0)
        {
            sys_loge(FILE_TAG, "Create film directory failed");
            m_list_ready = 1;
            file_list_unlock();
            return;
        }
        sys_logi(FILE_TAG, "Film directory created successfully");
        // 重新打开目录
        dir = opendir(m_file_active_dir);
        if(dir == NULL)
        {
            sys_logw(FILE_TAG, "Open film directory failed");
            m_list_ready = 1;
            file_list_unlock();
            return;
        }
    }

    // 先统计文件数量
    struct dirent* entry;
    while((entry = readdir(dir)) != NULL)
    {
        if(entry->d_type == DT_REG)
        {
            sys_logi(FILE_TAG, "Found file: %s", entry->d_name);
            char* ext = strrchr(entry->d_name, '.');
            
            if(ext && strcmp(ext, FILM_FILE_EXT) == 0)
            {
                // 检查文件大小是否符合要求（宽松校验：扩展名 + 最小大小）
                char filepath[512];
                snprintf(filepath, sizeof(filepath), "%s/%s", m_file_active_dir, entry->d_name);
                struct stat st;
                if(stat(filepath, &st) == 0)
                {
                    if(st.st_size >= FILM_HEADER_SIZE)
                    {
                        m_file_state.file_count++;
                    }
                    else
                    {
                        sys_logw(FILE_TAG, "File size too small: %s, must >= %d, actual: %d", entry->d_name, FILM_HEADER_SIZE, st.st_size);
                    }
                }
                else
                {
                    sys_logw(FILE_TAG, "Get file size failed: %s", entry->d_name);
                }
            }
        }
    }
    closedir(dir);

    // 分配文件列表内存
    if(m_file_state.file_count > 0)
    {
        m_file_state.file_list = (file_item_t*)malloc(sizeof(file_item_t) * m_file_state.file_count);
        if(m_file_state.file_list == NULL)
        {
            sys_loge(FILE_TAG, "Allocate file list memory failed");
            m_file_state.file_count = 0;
            m_list_ready = 1;
            file_list_unlock();
            return;
        }

        // 重新扫描并填充文件列表
        dir = opendir(m_file_active_dir);
        if(dir == NULL)
        {
            sys_logw(FILE_TAG, "Open film directory failed");
            free(m_file_state.file_list);
            m_file_state.file_list = NULL;
            m_file_state.file_count = 0;
            m_list_ready = 1;
            file_list_unlock();
            return;
        }

        uint32_t index = 0;
        while((entry = readdir(dir)) != NULL)
        {
            if(entry->d_type == DT_REG)
            {
                char* ext = strrchr(entry->d_name, '.');
                if(ext && strcmp(ext, FILM_FILE_EXT) == 0)
                {
                    // 检查文件大小（宽松校验）
                    char filepath[512];
                    snprintf(filepath, sizeof(filepath), "%s/%s", m_file_active_dir, entry->d_name);
                    struct stat st;
                    if(stat(filepath, &st) == 0)
                    {
                        if(st.st_size >= FILM_HEADER_SIZE)
                        {
                            strncpy(m_file_state.file_list[index].filename, entry->d_name, sizeof(m_file_state.file_list[index].filename) - 1);
                            m_file_state.file_list[index].filename[sizeof(m_file_state.file_list[index].filename) - 1] = '\0';
                            m_file_state.file_list[index].file_size = st.st_size;
                            index++;
                        }
                        else
                        {
                            sys_logw(FILE_TAG, "Skip file size too small: %s, must >= %d, actual: %d", entry->d_name, FILM_HEADER_SIZE, st.st_size);
                        }
                    }
                    else
                    {
                        sys_logw(FILE_TAG, "Skip file size get failed: %s", entry->d_name);
                    }
                }
            }
        }
        closedir(dir);

        sys_logi(FILE_TAG, "Found %d film files", m_file_state.file_count);

        // 处理当前文件ID
        if(m_file_state.current_file_id >= m_file_state.file_count)
        {
            m_file_state.current_file_id = 0;
        }
    }
    else
    {
        sys_logi(FILE_TAG, "No film files found");
        m_file_state.current_file_id = 0;
    }

    m_list_ready = 1;   // 列表已刷新完成，唤醒 set_dir_sync
    file_list_unlock();
}

/**
 * @brief 广播文件列表刷新完成事件（须在 file_list_refresh_event 之后、锁外调用）
 */
static void file_publish_list_event(void)
{
    uint32_t count = service_file_get_count();
    sys_event_publish(SYS_EVT_FILE_LIST, &count, sizeof(count));
}

/**
 * @brief 逐级创建文件路径中的父目录
 *
 * 如 /sdcard/app/image/cover.film 会依次创建 /sdcard、/sdcard/app、
 * /sdcard/app/image。目录已存在时视为成功。
 *
 * @param filepath 完整文件路径
 * @return int 0:成功, -1:失败
 */
static int file_mkdir_parents(const char *filepath)
{
    if(filepath == NULL)
    {
        return -1;
    }

    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", filepath);

    // 去掉文件名部分，只保留父目录
    char *slash = strrchr(tmp, '/');
    if(slash == NULL || slash == tmp)
    {
        return 0;
    }
    *slash = '\0';

    // 逐级创建（跳过开头的 '/'）
    for(char *s = tmp + 1; *s != '\0'; s++)
    {
        if(*s == '/')
        {
            *s = '\0';
            struct stat st;
            if(mkdir(tmp, 0777) != 0 && stat(tmp, &st) != 0)
            {
                sys_loge(FILE_TAG, "mkdir failed: %s", tmp);
                *s = '/';
                return -1;
            }
            *s = '/';
        }
    }

    struct stat st;
    if(mkdir(tmp, 0777) != 0 && stat(tmp, &st) != 0)
    {
        sys_loge(FILE_TAG, "mkdir failed: %s", tmp);
        return -1;
    }

    return 0;
}

/**
 * @brief 按 .film 头部的帧数把刚保存的文件分流到图片/动图目录
 *
 * 读取 save_dir/save_filename 的帧数（偏移 0x0A，2 字节小端）：
 *  - FrameCount > 1  → /sdcard/animation
 *  - FrameCount <= 1 → /sdcard/film
 * 已在目标目录时不做任何操作。
 */
static void file_relocate_by_frame_count(void)
{
    char src_path[512];
    snprintf(src_path, sizeof(src_path), "%s/%s", m_file_state.save_dir, m_file_state.save_filename);

    FILE *fp = fopen(src_path, "rb");
    if(fp == NULL)
    {
        sys_logw(FILE_TAG, "relocate: open %s failed", src_path);
        return;
    }

    uint8_t hdr[FILM_HEADER_SIZE];
    size_t rd = fread(hdr, 1, sizeof(hdr), fp);
    fclose(fp);

    if(rd < FILM_HEADER_SIZE)
    {
        sys_logw(FILE_TAG, "relocate: read header failed: %s", src_path);
        return;
    }

    uint16_t frame_count = (uint16_t)hdr[FILM_HDR_OFFSET_FRAMECOUNT]
                         | ((uint16_t)hdr[FILM_HDR_OFFSET_FRAMECOUNT + 1] << 8);

    const char *dst_dir = (frame_count > 1) ? ANIM_DIR : FILM_DIR;

    // 已在目标目录，无需移动
    if(strcmp(m_file_state.save_dir, dst_dir) == 0)
    {
        return;
    }

    // 确保目标目录存在
    DIR *dir = opendir(dst_dir);
    if(dir == NULL)
    {
        if(mkdir(dst_dir, 0777) != 0)
        {
            sys_loge(FILE_TAG, "relocate: create %s failed", dst_dir);
            return;
        }
    }
    else
    {
        closedir(dir);
    }

    char dst_path[512];
    snprintf(dst_path, sizeof(dst_path), "%s/%s", dst_dir, m_file_state.save_filename);

    // 目标存在同名文件时先删除（FATFS 的 rename 不覆盖已存在文件）
    struct stat st;
    if(stat(dst_path, &st) == 0)
    {
        remove(dst_path);
    }

    if(rename(src_path, dst_path) != 0)
    {
        sys_loge(FILE_TAG, "relocate: %s -> %s failed", src_path, dst_path);
    }
    else
    {
        sys_logi(FILE_TAG, "relocate: %s -> %s (frames=%d)", src_path, dst_path, frame_count);
    }
}

static void file_free_buffer(void)
{
    if(m_file_state.psram_buffer)
    {
        heap_caps_free(m_file_state.psram_buffer);
        m_file_state.psram_buffer = NULL;
        m_file_state.buffer_size = 0;
    }
}

static void file_load_event(uint32_t file_id)
{
    if(!m_file_state.sd_mounted)
    {
        sys_logw(FILE_TAG, "SD card not mounted");
        return;
    }

    // 进入加载状态
    m_file_state.load_complete = FILE_LOAD_STATE_LOADING;

    if(file_id >= m_file_state.file_count)
    {
        sys_logw(FILE_TAG, "Invalid file ID: %d", file_id);
        m_file_state.load_complete = FILE_LOAD_STATE_NONE;
        return;
    }

    // 释放现有缓冲区
    file_free_buffer();

    // 构建文件路径
    char filepath[512];
    snprintf(filepath, sizeof(filepath), "%s/%s", m_file_active_dir, m_file_state.file_list[file_id].filename);

    // 打开文件
    FILE* file = fopen(filepath, "rb");
    if(file == NULL)
    {
        sys_loge(FILE_TAG, "Open file failed: %s", filepath);
        m_file_state.load_complete = FILE_LOAD_STATE_NONE;
        return;
    }

    // 获取文件大小
    fseek(file, 0, SEEK_END);
    uint32_t file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    // 分配PSRAM缓冲区
    m_file_state.psram_buffer = (uint8_t*)heap_caps_malloc(file_size, MALLOC_CAP_SPIRAM);
    if(m_file_state.psram_buffer == NULL)
    {
        sys_loge(FILE_TAG, "Allocate PSRAM buffer failed");
        fclose(file);
        m_file_state.load_complete = FILE_LOAD_STATE_NONE;
        return;
    }

    // 读取文件数据
    size_t read_size = fread(m_file_state.psram_buffer, 1, file_size, file);
    if(read_size != file_size)
    {
        sys_loge(FILE_TAG, "Read file failed");
        file_free_buffer();
        fclose(file);
        m_file_state.load_complete = FILE_LOAD_STATE_NONE;
        return;
    }

    fclose(file);

    m_file_state.buffer_size = file_size;
    m_file_state.current_file_id = file_id;

    sys_logi(FILE_TAG, "Loaded file: %s, size: %d bytes", m_file_state.file_list[file_id].filename, file_size);

    // 加载完成
    m_file_state.load_complete = FILE_LOAD_STATE_DONE;
}

static void file_load_next_event(void)
{
    if(m_file_state.file_count == 0)
    {
        sys_logw(FILE_TAG, "No files to load");
        return;
    }

    // 计算下一个文件ID（循环）
    uint32_t next_file_id = (m_file_state.current_file_id + 1) % m_file_state.file_count;
    file_load_event(next_file_id);
}

void service_file_refresh_list(void)
{
    file_msg_t msg;
    msg.ID = MSG_FILE_LIST_REFRESH;
    file_msg_send(&msg, 0);
}

int service_file_load(uint32_t file_id)
{
    uint32_t file_count;

    file_list_lock();
    file_count = m_file_state.file_count;
    file_list_unlock();

    if(file_id >= file_count)
    {
        sys_logw(FILE_TAG, "Invalid file ID: %d, total files: %d", file_id, file_count);
        return -1;
    }
    
    // 清空加载状态
    m_file_state.load_complete = FILE_LOAD_STATE_NONE;

    file_msg_t msg;
    msg.ID = MSG_FILE_LOAD;
    msg.file_id = file_id;
    file_msg_send(&msg, 0);
    return 0;
}

void service_file_load_next(void)
{
    // 清空加载状态
    m_file_state.load_complete = FILE_LOAD_STATE_NONE;

    file_msg_t msg;
    msg.ID = MSG_FILE_LOAD_NEXT;
    file_msg_send(&msg, 0);
}

int service_file_save_data(const char *pfilename, uint32_t file_size, uint8_t *pdata, uint32_t data_len)
{
    if(!m_file_state.sd_mounted)
    {
        sys_logw(FILE_TAG, "SD card not mounted");
        return -1;
    }

    file_msg_t msg = {0};
    msg.ID = MSG_FILE_SAVE_DATA;
    msg.file_size = file_size;
    msg.pdata = pdata;
    msg.data_len = data_len;
    file_msg_send(&msg, 0);
    return 0;
}

int service_file_save_start(const char *pfilename, uint32_t file_size)
{
    if(!m_file_state.sd_mounted)
    {
        sys_logw(FILE_TAG, "SD card not mounted");
        return -1;
    }

    if(file_size < FILM_HEADER_SIZE)
    {
        sys_logw(FILE_TAG, "Invalid file size: %d, must >= %d", file_size, FILM_HEADER_SIZE);
        return -1;
    }

    file_msg_t msg = {0};
    msg.ID = MSG_FILE_SAVE_START;
    msg.file_size = file_size;
    msg.pdata = (uint8_t*)pvPortMalloc(strlen(pfilename) + 1);
    if(msg.pdata)
    {
        memcpy(msg.pdata, pfilename, strlen(pfilename) + 1);
    }
    file_msg_send(&msg, 0);
    return 0;
}

int service_file_save_start_to(const char *rel_path, uint32_t file_size)
{
    if(!m_file_state.sd_mounted)
    {
        sys_logw(FILE_TAG, "SD card not mounted");
        return -1;
    }

    if(rel_path == NULL || rel_path[0] == '\0' || rel_path[0] == '/' || strstr(rel_path, "..") != NULL)
    {
        sys_logw(FILE_TAG, "Invalid explicit path: %s", rel_path ? rel_path : "(null)");
        return -1;
    }

    if(file_size < FILM_HEADER_SIZE)
    {
        sys_logw(FILE_TAG, "Invalid file size: %d, must >= %d", file_size, FILM_HEADER_SIZE);
        return -1;
    }

    file_msg_t msg = {0};
    msg.ID = MSG_FILE_SAVE_START_TO;
    msg.file_size = file_size;
    msg.pdata = (uint8_t*)pvPortMalloc(strlen(rel_path) + 1);
    if(msg.pdata == NULL)
    {
        return -1;
    }
    memcpy(msg.pdata, rel_path, strlen(rel_path) + 1);
    file_msg_send(&msg, 0);
    return 0;
}

void service_file_save_stop(uint8_t auto_load)
{
    m_file_state.save_auto_load = auto_load ? 1 : 0;
    file_msg_t msg = {0};
    msg.ID = MSG_FILE_SAVE_STOP;
    file_msg_send(&msg, 0);
}

uint32_t service_file_get_count(void)
{
    uint32_t count;

    file_list_lock();
    count = m_file_state.file_count;
    file_list_unlock();
    return count;
}

int service_file_get_filename_safe(uint32_t file_id, char *out, uint32_t out_size)
{
    if(out == NULL || out_size == 0)
    {
        return -1;
    }

    int ret = -1;
    file_list_lock();
    if(m_file_state.file_list != NULL && file_id < m_file_state.file_count)
    {
        strncpy(out, m_file_state.file_list[file_id].filename, out_size - 1);
        out[out_size - 1] = '\0';
        ret = 0;
    }
    file_list_unlock();
    return ret;
}

uint8_t* service_file_get_buffer(void)
{
    return m_file_state.psram_buffer;
}

uint32_t service_file_get_buffer_size(void)
{
    return m_file_state.buffer_size;
}

uint32_t service_file_get_current_id(void)
{
    return m_file_state.current_file_id;
}

uint8_t service_file_get_load_complete(void)
{
    return m_file_state.load_complete;
}

uint32_t service_file_get_size(uint32_t file_id)
{
    uint32_t size = 0;

    file_list_lock();
    if(m_file_state.file_list != NULL && file_id < m_file_state.file_count)
    {
        size = m_file_state.file_list[file_id].file_size;
    }
    file_list_unlock();
    return size;
}

int service_file_delete(uint32_t file_id)
{
    char filename[256];

    file_list_lock();
    if(m_file_state.file_list == NULL || file_id >= m_file_state.file_count)
    {
        file_list_unlock();
        sys_logw(FILE_TAG, "Invalid file id: %d", file_id);
        return -1;
    }
    strncpy(filename, m_file_state.file_list[file_id].filename, sizeof(filename) - 1);
    filename[sizeof(filename) - 1] = '\0';
    file_list_unlock();

    char filepath[512];
    snprintf(filepath, sizeof(filepath), "%s/%s", m_file_active_dir, filename);

    if(remove(filepath) == 0)
    {
        sys_logi(FILE_TAG, "Deleted file: %s", filepath);
        service_file_refresh_list();
        return 0;
    }
    else
    {
        sys_logw(FILE_TAG, "Failed to delete file: %s", filepath);
        return -1;
    }
}