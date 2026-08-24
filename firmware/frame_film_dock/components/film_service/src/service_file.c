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
#include "hal_api.h"
#include "service_file.h"
#include "service_film.h"

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

#define FILE_EPD_IMGAGE_WIDTH       (EPD_WIDTH)
#define FILE_EPD_IMGAGE_HEIGHT      (EPD_HEIGHT)
#define FILM_HEADER_SIZE            (32)
#define FILM_PIXEL_DATA_SIZE        ((FILE_EPD_IMGAGE_WIDTH * FILE_EPD_IMGAGE_HEIGHT) / 2)
#define FILE_EPD_IMGAGE_SIZE        (FILM_HEADER_SIZE + FILM_PIXEL_DATA_SIZE)

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
                break;
            case MSG_FILE_LOAD:
                file_load_event(msg.file_id);
                break;
            case MSG_FILE_LOAD_NEXT:
                file_load_next_event();
                break;
            case MSG_SD_MOUNTED:
                file_list_refresh_event();
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
                break;
            case MSG_FILE_SAVE_START:
                if(msg.file_size == FILE_EPD_IMGAGE_SIZE)
                {
                    snprintf(m_file_state.save_filename, sizeof(m_file_state.save_filename), "%s", msg.pdata);
                    char filepath[512];
                    snprintf(filepath, sizeof(filepath), "%s/%s", FILM_DIR, m_file_state.save_filename);

                    m_file_state.save_file_handle = fopen(filepath, "wb");
                    if(m_file_state.save_file_handle == NULL)
                    {
                        sys_loge(FILE_TAG, "Open file for save failed: %s", filepath);
                    }
                    else
                    {
                        m_file_state.save_file_size = msg.file_size;
                        m_file_state.save_written = 0;
                        sys_logi(FILE_TAG, "Start save file: %s, size: %d", filepath, msg.file_size);
                    }
                }
                else
                {
                    sys_loge(FILE_TAG, "Invalid file size: %d, expected: %d", msg.file_size, FILE_EPD_IMGAGE_SIZE);
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
                    sys_logi(FILE_TAG, "Save file complete: %s, written: %d", m_file_state.save_filename, m_file_state.save_written);
                    if(m_file_state.save_written != m_file_state.save_file_size)
                    {
                        sys_loge(FILE_TAG, "File size mismatch: written=%d, expected=%d", m_file_state.save_written, m_file_state.save_file_size);
                    }
                    else
                    {
                        sys_logi(FILE_TAG, "Refreshing file list and loading new photo...");
                        // 刷新文件列表
                        file_list_refresh_event();

                        // 静默模式（批量上传）不自动加载显示，仅保存
                        if(m_file_state.save_auto_load)
                        {
                            // 查找新文件并加载
                            for(uint32_t i = 0; i < m_file_state.file_count; i++)
                            {
                                if(strcmp(m_file_state.file_list[i].filename, m_file_state.save_filename) == 0)
                                {
                                    sys_logi(FILE_TAG, "Found new file at index %d, loading...", i);
                                    m_file_state.current_file_id = i;
                                    service_film_display(i);
                                    break;
                                }
                            }
                        }
                    }
                }
                m_file_state.save_file_size = 0;
                m_file_state.save_written = 0;
                memset(m_file_state.save_filename, 0, sizeof(m_file_state.save_filename));
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
    DIR* dir = opendir(FILM_DIR);
    if(dir == NULL)
    {
        sys_logi(FILE_TAG, "Film directory not found, creating...");
        // 创建目录
        if(mkdir(FILM_DIR, 0777) != 0)
        {
            sys_loge(FILE_TAG, "Create film directory failed");
            file_list_unlock();
            return;
        }
        sys_logi(FILE_TAG, "Film directory created successfully");
        // 重新打开目录
        dir = opendir(FILM_DIR);
        if(dir == NULL)
        {
            sys_logw(FILE_TAG, "Open film directory failed");
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
                // 检查文件大小是否符合要求
                char filepath[512];
                snprintf(filepath, sizeof(filepath), "%s/%s", FILM_DIR, entry->d_name);
                struct stat st;
                if(stat(filepath, &st) == 0)
                {
                    if(st.st_size == FILE_EPD_IMGAGE_SIZE)
                    {
                        m_file_state.file_count++;
                    }
                    else
                    {
                        sys_logw(FILE_TAG, "File size not match: %s, expected: %d, actual: %d", entry->d_name, FILE_EPD_IMGAGE_SIZE, st.st_size);
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
            file_list_unlock();
            return;
        }

        // 重新扫描并填充文件列表
        dir = opendir(FILM_DIR);
        if(dir == NULL)
        {
            sys_logw(FILE_TAG, "Open film directory failed");
            free(m_file_state.file_list);
            m_file_state.file_list = NULL;
            m_file_state.file_count = 0;
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
                    // 检查文件大小
                    char filepath[512];
                    snprintf(filepath, sizeof(filepath), "%s/%s", FILM_DIR, entry->d_name);
                    struct stat st;
                    if(stat(filepath, &st) == 0)
                    {
                        if(st.st_size == FILE_EPD_IMGAGE_SIZE)
                        {
                            strncpy(m_file_state.file_list[index].filename, entry->d_name, sizeof(m_file_state.file_list[index].filename) - 1);
                            m_file_state.file_list[index].filename[sizeof(m_file_state.file_list[index].filename) - 1] = '\0';
                            m_file_state.file_list[index].file_size = st.st_size;
                            index++;
                        }
                        else
                        {
                            sys_logw(FILE_TAG, "Skip file size not match: %s, expected: %d, actual: %d", entry->d_name, FILE_EPD_IMGAGE_SIZE, st.st_size);
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

    file_list_unlock();
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
    snprintf(filepath, sizeof(filepath), "%s/%s", FILM_DIR, m_file_state.file_list[file_id].filename);

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

    if(file_size != FILE_EPD_IMGAGE_SIZE)
    {
        sys_logw(FILE_TAG, "Invalid file size: %d, expected: %d", file_size, FILE_EPD_IMGAGE_SIZE);
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
    snprintf(filepath, sizeof(filepath), "%s/%s", FILM_DIR, filename);

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