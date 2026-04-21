/***********************************************************
 *
 * MIT License
 *
 * Copyright (c) 2025 kiritro
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * 
 * FileName : /film_service/src/service_file.c
 * Author: Kiritro  Version: v0.1  Date: 2026/4/21
 * Description: 文件服务初始化
 * ChangeLog: Change Notes
 *
***********************************************************/

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

#include "esp_heap_caps.h"

#include "sys_log.h"
#include "hal_sd.h"
#include "service_file.h"

/*********************************************************************
 * MACROS
 */
#define FILE_MSG_QUEUE_LENGTH       30
#define FILE_MSG_QUEUE_ITEM_SIZE    sizeof( file_msg_t )

#define SYS_OS_PRI_FILE_TASK        (10)
#define SYS_OS_SIZE_FILE_TASK       (4096)
#define SYS_OS_NAME_FILE_TASK       "file_task"

#define FILE_TIMER_BASE_INTERVAL_MS (1000)
#define FILE_SD_CHECK_INTERVAL_MS   (5000)
#define FILE_SD_CHECK_TICK_COUNT    (FILE_SD_CHECK_INTERVAL_MS / FILE_TIMER_BASE_INTERVAL_MS)

/*********************************************************************
* TYPEDEFS
*/
typedef struct {
    file_item_t* file_list;  // 文件列表
    uint32_t file_count;     // 文件数量
    uint32_t current_file_id; // 当前加载的文件ID
    uint8_t* psram_buffer;   // PSRAM缓冲区
    uint32_t buffer_size;    // 缓冲区大小
    uint8_t sd_mounted;      // SD卡挂载状态
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
                if(m_file_state.file_list)
                {
                    free(m_file_state.file_list);
                    m_file_state.file_list = NULL;
                }
                m_file_state.file_count = 0;
                m_file_state.current_file_id = 0;
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
    if(!m_file_state.sd_mounted)
    {
        sys_logw(FILE_TAG, "SD card not mounted");
        return;
    }

    // 释放旧的文件列表
    if(m_file_state.file_list)
    {
        free(m_file_state.file_list);
        m_file_state.file_list = NULL;
    }
    m_file_state.file_count = 0;

    // 扫描目录
    DIR* dir = opendir(FILM_DIR);
    if(dir == NULL)
    {
        sys_logw(FILE_TAG, "Open film directory failed");
        return;
    }

    // 先统计文件数量
    struct dirent* entry;
    while((entry = readdir(dir)) != NULL)
    {
        if(entry->d_type == DT_REG)
        {
            char* ext = strrchr(entry->d_name, '.');
            if(ext && strcmp(ext, FILM_FILE_EXT) == 0)
            {
                m_file_state.file_count++;
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
                    strncpy(m_file_state.file_list[index].filename, entry->d_name, sizeof(m_file_state.file_list[index].filename) - 1);
                    m_file_state.file_list[index].filename[sizeof(m_file_state.file_list[index].filename) - 1] = '\0';

                    // 获取文件大小
                    char filepath[512];
                    snprintf(filepath, sizeof(filepath), "%s/%s", FILM_DIR, entry->d_name);
                    struct stat st;
                    if(stat(filepath, &st) == 0)
                    {
                        m_file_state.file_list[index].file_size = st.st_size;
                    }
                    else
                    {
                        m_file_state.file_list[index].file_size = 0;
                    }

                    index++;
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

    if(file_id >= m_file_state.file_count)
    {
        sys_logw(FILE_TAG, "Invalid file ID: %d", file_id);
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
        return;
    }

    // 读取文件数据
    size_t read_size = fread(m_file_state.psram_buffer, 1, file_size, file);
    if(read_size != file_size)
    {
        sys_loge(FILE_TAG, "Read file failed");
        file_free_buffer();
        fclose(file);
        return;
    }

    fclose(file);

    m_file_state.buffer_size = file_size;
    m_file_state.current_file_id = file_id;

    sys_logi(FILE_TAG, "Loaded file: %s, size: %d bytes", m_file_state.file_list[file_id].filename, file_size);
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

void service_file_load(uint32_t file_id)
{
    file_msg_t msg;
    msg.ID = MSG_FILE_LOAD;
    msg.file_id = file_id;
    file_msg_send(&msg, 0);
}

void service_file_load_next(void)
{
    file_msg_t msg;
    msg.ID = MSG_FILE_LOAD_NEXT;
    file_msg_send(&msg, 0);
}

uint32_t service_file_get_count(void)
{
    return m_file_state.file_count;
}

const char* service_file_get_name(uint32_t file_id)
{
    if(file_id >= m_file_state.file_count)
    {
        return NULL;
    }
    return m_file_state.file_list[file_id].filename;
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
