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
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "freertos/semphr.h"

#include "esp_heap_caps.h"
#include "esp_attr.h"

#include "sys_log.h"
#include "sys_event.h"
#include "hal_api.h"
#include "service_file.h"
#include "esp_timer.h"

/*********************************************************************
 * MACROS
 */
#define FILE_MSG_QUEUE_LENGTH       30
#define FILE_MSG_QUEUE_ITEM_SIZE    sizeof( file_msg_t )

#define SYS_OS_PRI_FILE_TASK        (6)
/* 启动时清理上传残留最多递归 3 层，每层有路径/stat 局部变量；4KB 实机溢出。 */
#define SYS_OS_SIZE_FILE_TASK       (8192)
#define SYS_OS_NAME_FILE_TASK       "file_task"

#define FILE_TIMER_BASE_INTERVAL_MS (1000)
#define FILE_SD_CHECK_INTERVAL_MS   (5000)
#define FILE_SD_CHECK_TICK_COUNT    (FILE_SD_CHECK_INTERVAL_MS / FILE_TIMER_BASE_INTERVAL_MS)

#define FILM_HEADER_SIZE            (32)

// .film 文件头字段均为小端
#define FILM_HDR_OFFSET_SIZE        (0x00)
#define FILM_HDR_OFFSET_WIDTH       (0x04)
#define FILM_HDR_OFFSET_HEIGHT      (0x06)
#define FILM_HDR_OFFSET_COLORCOUNT  (0x08)
#define FILM_HDR_OFFSET_FORMAT      (0x09)
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
    uint32_t failed_file_id; // 最近一次"判定加载失败"的文件ID（FILE_ID_NONE 表示无）
    uint32_t buffer_size;    // 缓冲区大小
    uint8_t sd_mounted;      // SD卡挂载状态
    FILE* save_file_handle;  // 文件保存句柄
    uint32_t save_file_size; // 要保存的文件大小
    uint32_t save_written;   // 已写入的字节数
    char save_filename[256]; // 当前保存的文件名
    char save_dir[64];       // 保存时的工作目录快照（避免保存途中切目录导致路径错乱）
    char save_path[512];     // 保存时的完整路径（显式路径模式下由相对路径拼接而来）
    char save_part_path[544];
    uint8_t save_header[FILM_HEADER_SIZE]; // First bytes observed before fwrite.
    size_t save_header_len;
    uint8_t save_owner;
    uint8_t save_failed;
    uint8_t save_skip_relocate; // 1：显式路径保存，完成后跳过 relocate/列表刷新/事件上浮
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
/* file_task is the sole writer. DMA_ATTR keeps this out of RTC FAST heap. */
static DMA_ATTR uint8_t m_save_dma_buffer[4096];
static SemaphoreHandle_t m_file_list_mutex = NULL;  // 保护 file_list/file_count 跨任务访问
static SemaphoreHandle_t m_save_call_mutex = NULL;
static SemaphoreHandle_t m_save_done = NULL;
static int m_save_result = -1;
static char m_file_active_dir[64];                  // 当前工作目录（/sdcard/film 或 /sdcard/animation）
static volatile uint8_t m_list_ready = 0;           // 当前目录列表是否已刷新完成（供 set_dir_sync 等待）

/*********************************************************************
 * GLOBAL VARIABLES
 */

/*********************************************************************
 * LOCAL FUNCTIONS
 */
/* Keep full-sector writes aligned in both the file and normal DMA DRAM.
 * Partial sectors still go through FatFS, but never pass the PSRAM source. */
static size_t file_save_write_dma(FILE *fp, const uint8_t *data, size_t len, uint32_t offset)
{
    size_t written = 0;
    while(written < len)
    {
        size_t remaining = len - written;
        size_t partial = (offset + written) % 512;
        size_t count;
        if(partial != 0)
        {
            count = 512 - partial;
            if(count > remaining) count = remaining;
        }
        else if(remaining >= 512)
        {
            count = remaining > sizeof(m_save_dma_buffer) ? sizeof(m_save_dma_buffer) : remaining;
            count -= count % 512;
        }
        else count = remaining;

        memcpy(m_save_dma_buffer, data + written, count);
        size_t saved = fwrite(m_save_dma_buffer, 1, count, fp);
        written += saved;
        if(saved != count) break;
    }
    return written;
}

static void file_log_header(const char *stage, const uint8_t *hdr, size_t len)
{
    char hex[FILM_HEADER_SIZE * 2 + 1];
    static const char digits[] = "0123456789ABCDEF";
    for(size_t i = 0; i < len; ++i)
    {
        hex[i * 2] = digits[hdr[i] >> 4];
        hex[i * 2 + 1] = digits[hdr[i] & 15];
    }
    hex[len * 2] = '\0';
    sys_logd(FILE_TAG, "Film header trace stage=%s owner=%u bytes=%u hex=%s",
             stage, (unsigned)m_file_state.save_owner, (unsigned)len, hex);
}

static void file_task_handle(void *pvParameters);
static void file_msg_send(void *p_msg, bool in_isr);
static void file_timer_callback(TimerHandle_t xTimer);

static void file_list_refresh_event(void);
static void file_load_event(uint32_t file_id);
static void file_load_next_event(void);
static void file_sd_check_event(void);
static void file_free_buffer(void);
static int file_validate_film(uint16_t *frame_count);
static int file_target_by_frame_count(char *dst_path, size_t dst_size);
static void file_invalidate_buffer(void);
static void file_publish_list_event(uint8_t save_pending);
static int  file_mkdir_parents(const char *filepath);
static void file_save_discard(void);
static int file_save_commit(const char *target);
static int file_recover_target(const char *target);
static int file_has_suffix(const char *name, const char *suffix);
static void file_clean_dir(const char *dir_path);
static int file_save_call(file_msg_t *msg);

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
    m_file_state.failed_file_id = FILE_ID_NONE;
    m_file_state.save_file_handle = NULL;
    m_file_state.save_file_size = 0;
    m_file_state.save_written = 0;

    m_list_ready = 0;   // 首次列表由 SD 挂载后的刷新事件置位

    // 默认工作目录为图片目录
    strncpy(m_file_active_dir, FILM_DIR, sizeof(m_file_active_dir) - 1);
    m_file_active_dir[sizeof(m_file_active_dir) - 1] = '\0';

    if(m_file_list_mutex == NULL)
    {
        m_file_list_mutex = xSemaphoreCreateMutex();
    }
    if(m_save_call_mutex == NULL) m_save_call_mutex = xSemaphoreCreateMutex();
    if(m_save_done == NULL) m_save_done = xSemaphoreCreateBinary();

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
    m_file_state.failed_file_id = FILE_ID_NONE;   // 换目录：失败记录一并作废
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
                file_publish_list_event(0);
                break;
            case MSG_FILE_LOAD:
                file_load_event(msg.file_id);
                break;
            case MSG_FILE_LOAD_NEXT:
                file_load_next_event();
                break;
            case MSG_SD_MOUNTED:
                // 先刷新列表再上浮事件，避免订阅者读到旧的 count
                file_clean_dir(FILM_DIR);
                file_clean_dir(ANIM_DIR);
                file_clean_dir("/sdcard/app");
                file_list_refresh_event();
                file_publish_list_event(0);
                sys_event_publish(SYS_EVT_SD_MOUNT, NULL, 0);
                break;
            case MSG_SD_UNMOUNTED:
                file_save_discard();
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
                m_save_result = -1;
                if(m_file_state.save_owner != 0 || !m_file_state.sd_mounted ||
                   msg.pdata == NULL || msg.file_size == 0)
                {
                    goto save_start_done;
                }
                m_file_state.save_skip_relocate = (msg.ID == MSG_FILE_SAVE_START_TO);
                if(m_file_state.save_skip_relocate)
                {
                    if(snprintf(m_file_state.save_path, sizeof(m_file_state.save_path),
                                "/sdcard/%s", (char*)msg.pdata) >= sizeof(m_file_state.save_path) ||
                       file_mkdir_parents(m_file_state.save_path) != 0) goto save_start_done;
                    m_file_state.save_filename[0] = '\0';
                    m_file_state.save_dir[0] = '\0';
                }
                else
                {
                    snprintf(m_file_state.save_filename, sizeof(m_file_state.save_filename), "%s", (char*)msg.pdata);
                    snprintf(m_file_state.save_dir, sizeof(m_file_state.save_dir), "%s", m_file_active_dir);
                    if(snprintf(m_file_state.save_path, sizeof(m_file_state.save_path), "%s/%s",
                                m_file_state.save_dir, m_file_state.save_filename) >= sizeof(m_file_state.save_path)) goto save_start_done;
                }
                if(snprintf(m_file_state.save_part_path, sizeof(m_file_state.save_part_path), "%s.ffupload.part",
                            m_file_state.save_path) >= sizeof(m_file_state.save_part_path)) goto save_start_done;
                if(file_recover_target(m_file_state.save_path) != 0) goto save_start_done;
                remove(m_file_state.save_part_path);
                m_file_state.save_file_handle = fopen(m_file_state.save_part_path, "wb");
                if(m_file_state.save_file_handle)
                {
                    if(setvbuf(m_file_state.save_file_handle, NULL, _IONBF, 0) != 0)
                    {
                        fclose(m_file_state.save_file_handle);
                        m_file_state.save_file_handle = NULL;
                        goto save_start_done;
                    }
                    m_file_state.save_file_size = msg.file_size;
                    m_file_state.save_written = 0;
                    m_file_state.save_header_len = 0;
                    m_file_state.save_failed = 0;
                    m_file_state.save_owner = msg.owner;
                    m_save_result = 0;
                }
save_start_done:
                if(msg.pdata)
                {
                    free(msg.pdata);
                }
                xSemaphoreGive(m_save_done);
                break;
            case MSG_FILE_SAVE_DATA:
            {
                int result = -1;
                int64_t write_start = esp_timer_get_time();
                if(m_file_state.save_owner == msg.owner && m_file_state.save_file_handle &&
                   !m_file_state.save_failed && msg.pdata &&
                   msg.data_len <= m_file_state.save_file_size - m_file_state.save_written)
                {
                    if(m_file_state.save_header_len < FILM_HEADER_SIZE &&
                       (!m_file_state.save_skip_relocate || file_has_suffix(m_file_state.save_path, ".film")))
                    {
                        size_t take = FILM_HEADER_SIZE - m_file_state.save_header_len;
                        if(take > msg.data_len) take = msg.data_len;
                        memcpy(m_file_state.save_header + m_file_state.save_header_len, msg.pdata, take);
                        m_file_state.save_header_len += take;
                        file_log_header("prewrite", m_file_state.save_header, m_file_state.save_header_len);
                    }
                    size_t written = file_save_write_dma(m_file_state.save_file_handle, msg.pdata,
                                                         msg.data_len, m_file_state.save_written);
                    m_file_state.save_written += written;
                    if(written == msg.data_len) result = 0;
                    else m_file_state.save_failed = 1;
                }
                else if(m_file_state.save_owner == msg.owner)
                {
                    m_file_state.save_failed = 1;
                }
                if(msg.pdata && !msg.done)
                {
                    free(msg.pdata);
                }
                if(msg.done) msg.done(msg.context, result, esp_timer_get_time() - write_start);
                else { m_save_result = result; xSemaphoreGive(m_save_done); }
                break;
            }
            case MSG_FILE_SAVE_STOP:
                m_save_result = -1;
                if(m_file_state.save_owner == msg.owner && m_file_state.save_file_handle)
                {
                    int close_result = fclose(m_file_state.save_file_handle);
                    int close_errno = errno;
                    m_file_state.save_file_handle = NULL;
                    char target[sizeof(m_file_state.save_path)];
                    snprintf(target, sizeof(target), "%s", m_file_state.save_path);
                    int is_film = !m_file_state.save_skip_relocate ||
                                  file_has_suffix(m_file_state.save_path, ".film");
                    if(close_result != 0)
                    {
                        sys_loge(FILE_TAG, "Save close failed: %s (errno=%d)",
                                 m_file_state.save_part_path, close_errno);
                    }
                    else if(m_file_state.save_failed)
                    {
                        sys_loge(FILE_TAG, "Save write failed: %s", m_file_state.save_part_path);
                    }
                    else if(m_file_state.save_written != m_file_state.save_file_size)
                    {
                        sys_loge(FILE_TAG, "Save size mismatch: %s (%lu/%lu)",
                                 m_file_state.save_part_path,
                                 (unsigned long)m_file_state.save_written,
                                 (unsigned long)m_file_state.save_file_size);
                    }
                    else if(m_file_state.save_skip_relocate ?
                            (is_film && file_validate_film(NULL) != 0) :
                            file_target_by_frame_count(target, sizeof(target)) != 0)
                    {
                        sys_loge(FILE_TAG, "Save validation/target failed: %s", m_file_state.save_part_path);
                    }
                    else if(file_save_commit(target) != 0)
                    {
                        sys_loge(FILE_TAG, "Save commit failed: %s -> %s",
                                 m_file_state.save_part_path, target);
                    }
                    else
                    {
                        m_save_result = 0;
                        sys_logi(FILE_TAG, "File committed: %s", target);
                        if(!m_file_state.save_skip_relocate)
                        {
                            file_list_refresh_event();
                            file_publish_list_event(SYS_FILE_LIST_SAVE_PENDING);
                            uint8_t auto_load = 0;
                            const char *target_name = strrchr(target, '/');
                            if(msg.file_id && target_name &&
                               strlen(m_file_active_dir) == (size_t)(target_name - target) &&
                               strncmp(m_file_active_dir, target, target_name - target) == 0)
                            {
                                uint32_t found = FILE_ID_NONE;
                                file_list_lock();
                                for(uint32_t i = 0; m_file_state.file_list != NULL &&
                                    i < m_file_state.file_count; i++)
                                {
                                    if(strcmp(m_file_state.file_list[i].filename, m_file_state.save_filename) == 0)
                                    {
                                        found = i;
                                        break;
                                    }
                                }
                                file_list_unlock();
                                if(found != FILE_ID_NONE)
                                {
                                    m_file_state.current_file_id = found;
                                    auto_load = 1;
                                }
                            }
                            file_invalidate_buffer();
                            sys_event_publish(SYS_EVT_FILE_SAVED, &auto_load, sizeof(auto_load));
                        }
                    }
                }
                else
                {
                    sys_loge(FILE_TAG, "Save stop state mismatch: request_owner=%u save_owner=%u handle=%p",
                             (unsigned)msg.owner, (unsigned)m_file_state.save_owner,
                             (void *)m_file_state.save_file_handle);
                }
                if(m_file_state.save_owner == msg.owner) file_save_discard();
                xSemaphoreGive(m_save_done);
                break;
            case MSG_FILE_SAVE_ABORT:
                m_save_result = -1;
                if(m_file_state.save_owner == msg.owner)
                {
                    file_save_discard();
                    m_save_result = 0;
                }
                else if(m_file_state.save_owner == 0)
                {
                    /* STOP 失败可能已丢弃暂存文件；重复取消应视为已完成。 */
                    m_save_result = 0;
                }
                xSemaphoreGive(m_save_done);
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

    file_clean_dir(m_file_active_dir);

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
                            /* 两趟扫描之间目录可能变（上传刚落盘 / 偶发 stat 失败），
                               越过第一趟分配的容量就停手，否则越界写堆 */
                            if(index >= m_file_state.file_count)
                            {
                                sys_logw(FILE_TAG, "file count changed during scan (%u >= %u), stop",
                                         (unsigned)index, (unsigned)m_file_state.file_count);
                                break;
                            }
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

        /* 以第二趟实际填充的条数为准：不足时多出来的槽位是 malloc 未清零的垃圾，
           留在列表里会被后续 strcmp/strncpy 当字符串读到越界 */
        m_file_state.file_count = index;

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
static void file_publish_list_event(uint8_t save_pending)
{
    uint32_t count = service_file_get_count();
    uint8_t payload[sizeof(count) + 1];
    memcpy(payload, &count, sizeof(count));
    payload[sizeof(count)] = save_pending;
    sys_event_publish(SYS_EVT_FILE_LIST, payload, save_pending ? sizeof(payload) : sizeof(count));
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

#if defined(CONFIG_ARK_EPD_ABC_PREVIEW) || defined(CONFIG_ARK_EPD_ABC_DIAG)
/* Validate the complete experimental body before publishing the uploaded file. */
static int file_validate_abc9_body(void)
{
    FILE *fp = fopen(m_file_state.save_part_path, "rb");
    if(fp == NULL) return -1;
    int result = -1;
    uint8_t chunk[256];
    if(fseek(fp, FILM_HEADER_SIZE, SEEK_SET) == 0)
    {
        uint32_t remaining = EPD_WIDTH * EPD_HEIGHT * 2u;
        while(remaining)
        {
            size_t count = remaining < sizeof(chunk) ? remaining : sizeof(chunk);
            if(fread(chunk, 1, count, fp) != count) break;
            for(size_t i = 1; i < count; i += 2)
            {
                if(chunk[i] & 0xFE)
                {
                    sys_loge(FILE_TAG, "ABC9 upload rejected: high bits set");
                    fclose(fp);
                    return -1;
                }
            }
            remaining -= count;
        }
        if(remaining == 0) result = 0;
    }
    fclose(fp);
    return result;
}
#endif

/* Validate the uploaded container before committing or choosing its directory. */
static int file_validate_film(uint16_t *frame_count_out)
{
    FILE *fp = fopen(m_file_state.save_part_path, "rb");
    if(fp == NULL)
    {
        int err = errno;
        sys_loge(FILE_TAG, "Film header open failed: %s (errno=%d)",
                 m_file_state.save_part_path, err);
        return -1;
    }

    uint8_t hdr[FILM_HEADER_SIZE];
    errno = 0;
    size_t rd = fread(hdr, 1, sizeof(hdr), fp);
    int read_error = ferror(fp);
    int read_errno = errno;
    file_log_header("readback", hdr, rd);
    size_t compared = rd < m_file_state.save_header_len ? rd : m_file_state.save_header_len;
    for(size_t i = 0; i < compared; ++i)
    {
        if(hdr[i] != m_file_state.save_header[i])
        {
            sys_loge(FILE_TAG, "Film header first difference owner=%u offset=%u prewrite=%02X readback=%02X",
                     (unsigned)m_file_state.save_owner, (unsigned)i,
                     m_file_state.save_header[i], hdr[i]);
            break;
        }
    }
    fclose(fp);

    if(rd != FILM_HEADER_SIZE || m_file_state.save_written < FILM_HEADER_SIZE)
    {
        sys_loge(FILE_TAG, "Film header read failed: %s (read=%u/%d, written=%lu, ferror=%d, errno=%d)",
                 m_file_state.save_part_path, (unsigned)rd, FILM_HEADER_SIZE,
                 (unsigned long)m_file_state.save_written, read_error, read_errno);
        return -1;
    }

    uint32_t body_size = m_file_state.save_written - FILM_HEADER_SIZE;
    uint32_t header_size = (uint32_t)hdr[FILM_HDR_OFFSET_SIZE]
                         | ((uint32_t)hdr[FILM_HDR_OFFSET_SIZE + 1] << 8)
                         | ((uint32_t)hdr[FILM_HDR_OFFSET_SIZE + 2] << 16)
                         | ((uint32_t)hdr[FILM_HDR_OFFSET_SIZE + 3] << 24);
    uint16_t width = (uint16_t)hdr[FILM_HDR_OFFSET_WIDTH]
                   | ((uint16_t)hdr[FILM_HDR_OFFSET_WIDTH + 1] << 8);
    uint16_t height = (uint16_t)hdr[FILM_HDR_OFFSET_HEIGHT]
                    | ((uint16_t)hdr[FILM_HDR_OFFSET_HEIGHT + 1] << 8);
    uint16_t frame_count = (uint16_t)hdr[FILM_HDR_OFFSET_FRAMECOUNT]
                         | ((uint16_t)hdr[FILM_HDR_OFFSET_FRAMECOUNT + 1] << 8);
    if(frame_count == 0) frame_count = 1;

    uint32_t frame_size;
    switch(hdr[FILM_HDR_OFFSET_FORMAT])
    {
    case 0x00:
        if(hdr[FILM_HDR_OFFSET_COLORCOUNT] < 2 || hdr[FILM_HDR_OFFSET_COLORCOUNT] > 6)
        {
            sys_loge(FILE_TAG, "Invalid film color count: %u (format=0)",
                     (unsigned)hdr[FILM_HDR_OFFSET_COLORCOUNT]);
            return -1;
        }
        frame_size = (EPD_WIDTH * EPD_HEIGHT) / 2;
        break;
    case 0x01:
        frame_size = (EPD_WIDTH * EPD_HEIGHT) / 8;
        break;
    case 0x02:
    case 0x03:
        frame_size = EPD_WIDTH * EPD_HEIGHT;
        break;
#if defined(CONFIG_ARK_EPD_ABC_PREVIEW) || defined(CONFIG_ARK_EPD_ABC_DIAG)
    case 0x7F:  // Experimental ABC9 container, not public film2.
        if(hdr[FILM_HDR_OFFSET_FRAMECOUNT] != 1 || hdr[FILM_HDR_OFFSET_FRAMECOUNT + 1] != 0 ||
           memcmp(hdr + 12, "ABC9", 4) != 0) return -1;
        frame_size = EPD_WIDTH * EPD_HEIGHT * 2u;
        break;
#endif
    default:
        sys_loge(FILE_TAG, "Invalid film format: %u (file=%s)",
                 (unsigned)hdr[FILM_HDR_OFFSET_FORMAT], m_file_state.save_part_path);
        return -1;
    }
    if(width != EPD_WIDTH || height != EPD_HEIGHT || header_size != body_size ||
       body_size % frame_size != 0 || body_size / frame_size != frame_count)
    {
        sys_loge(FILE_TAG, "Invalid film header: %s (size=%lu/%lu, dimensions=%ux%u/%ux%u, color=%u, format=%u, frames=%u, frame_size=%lu)",
                 m_file_state.save_part_path, (unsigned long)header_size,
                 (unsigned long)body_size, (unsigned)width, (unsigned)height,
                 (unsigned)EPD_WIDTH, (unsigned)EPD_HEIGHT,
                 (unsigned)hdr[FILM_HDR_OFFSET_COLORCOUNT],
                 (unsigned)hdr[FILM_HDR_OFFSET_FORMAT], (unsigned)frame_count,
                 (unsigned long)frame_size);
        return -1;
    }

#if defined(CONFIG_ARK_EPD_ABC_PREVIEW) || defined(CONFIG_ARK_EPD_ABC_DIAG)
    if(hdr[FILM_HDR_OFFSET_FORMAT] == 0x7F && file_validate_abc9_body() != 0) return -1;
#endif
    if(frame_count_out) *frame_count_out = frame_count;
    return 0;
}

static int file_target_by_frame_count(char *dst_path, size_t dst_size)
{
    uint16_t frame_count;
    if(file_validate_film(&frame_count) != 0) return -1;

    const char *dst_dir = (frame_count > 1) ? ANIM_DIR : FILM_DIR;

    if(strcmp(m_file_state.save_dir, dst_dir) == 0) return 0;
    DIR *dir = opendir(dst_dir);
    if(dir == NULL && mkdir(dst_dir, 0777) != 0)
    {
        int err = errno;
        sys_loge(FILE_TAG, "Save target directory failed: %s (errno=%d)", dst_dir, err);
        return -1;
    }
    if(dir) closedir(dir);
    if(snprintf(dst_path, dst_size, "%s/%s", dst_dir, m_file_state.save_filename) >= dst_size)
    {
        sys_loge(FILE_TAG, "Save target path too long: %s/%s", dst_dir, m_file_state.save_filename);
        return -1;
    }
    return 0;
}

static void file_save_discard(void)
{
    if(m_file_state.save_file_handle)
    {
        fclose(m_file_state.save_file_handle);
        m_file_state.save_file_handle = NULL;
    }
    if(m_file_state.save_owner && m_file_state.save_part_path[0]) remove(m_file_state.save_part_path);
    m_file_state.save_owner = 0;
    m_file_state.save_failed = 0;
    m_file_state.save_file_size = 0;
    m_file_state.save_written = 0;
    m_file_state.save_part_path[0] = '\0';
}

/* 仅处理本保存机制的固定 .bak；提交中断时优先恢复旧正式文件。 */
static int file_recover_target(const char *target)
{
    char backup[544];
    if(snprintf(backup, sizeof(backup), "%s.ffupload.bak", target) >= sizeof(backup)) return -1;
    struct stat st;
    if(stat(backup, &st) != 0) return 0;
    if(stat(target, &st) == 0)
    {
        if(remove(backup) == 0) return 0;
        int err = errno;
        sys_loge(FILE_TAG, "Remove stale backup failed: %s (errno=%d)", backup, err);
        return -1;
    }
    if(rename(backup, target) == 0) return 0;
    int err = errno;
    sys_loge(FILE_TAG, "Restore rename failed: %s -> %s (errno=%d)", backup, target, err);
    return -1;
}

static int file_save_commit(const char *target)
{
    char backup[544];
    if(snprintf(backup, sizeof(backup), "%s.ffupload.bak", target) >= sizeof(backup)) return -1;
    if(file_recover_target(target) != 0) return -1;
    struct stat st;
    int had_old = stat(target, &st) == 0;
    if(had_old && rename(target, backup) != 0)
    {
        int err = errno;
        sys_loge(FILE_TAG, "Backup rename failed: %s -> %s (errno=%d)", target, backup, err);
        return -1;
    }
    if(rename(m_file_state.save_part_path, target) != 0)
    {
        int err = errno;
        sys_loge(FILE_TAG, "Commit rename failed: %s -> %s (errno=%d)",
                 m_file_state.save_part_path, target, err);
        if(had_old && rename(backup, target) != 0)
        {
            int restore_err = errno;
            sys_loge(FILE_TAG, "Restore after commit failure failed: %s -> %s (errno=%d)",
                     backup, target, restore_err);
        }
        return -1;
    }
    if(had_old) remove(backup);
    return 0;
}

static int file_has_suffix(const char *name, const char *suffix)
{
    size_t n = strlen(name), s = strlen(suffix);
    return n >= s && strcmp(name + n - s, suffix) == 0;
}

static void file_clean_tree(const char *dir_path, unsigned depth)
{
    DIR *dir = opendir(dir_path);
    if(!dir) return;
    struct dirent *entry;
    while((entry = readdir(dir)) != NULL)
    {
        char path[544];
        if(snprintf(path, sizeof(path), "%s/%s", dir_path, entry->d_name) >= sizeof(path)) continue;
        struct stat st;
        if(stat(path, &st) != 0) continue;
        if(depth > 0 && S_ISDIR(st.st_mode) &&
           strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0)
        {
            file_clean_tree(path, depth - 1);
            continue;
        }
        if(!S_ISREG(st.st_mode)) continue;
        if(!file_has_suffix(entry->d_name, ".ffupload.part") &&
           !file_has_suffix(entry->d_name, ".ffupload.bak")) continue;
        if(m_file_state.save_owner && strcmp(path, m_file_state.save_part_path) == 0) continue;
        if(file_has_suffix(path, ".ffupload.bak"))
        {
            path[strlen(path) - strlen(".ffupload.bak")] = '\0';
            file_recover_target(path);
        }
        else remove(path);
    }
    closedir(dir);
}

static void file_clean_dir(const char *dir_path)
{
    file_clean_tree(dir_path, 2);
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

/**
 * @brief 把"已加载进 PSRAM"的状态置为无效（卡上的内容被改动过）
 *
 * 保存/覆盖/删除都会让 PSRAM 里那份缓冲不再对应当前列表：
 *  - 覆盖同名文件：缓冲里是**同一个 file_id 的旧内容**，而显示路径的快速判断
 *    只看 `current_id` + `load_complete`，会误判成"已加载"而复用旧图
 *    （表现：重传同名文件后，画面还是上一版，直到重新进 app/重启）；
 *  - 删除文件：后续 file_id 整体前移，缓存的 current_id 已经指向别的文件。
 *
 * 置 NONE 后，下一次显示会重新从卡里读。current_file_id 不动：它还要给
 * BLE 查询"当前是第几张"用，且上面的快速判断已经不再依赖它。
 */
static void file_invalidate_buffer(void)
{
    m_file_state.load_complete = FILE_LOAD_STATE_NONE;
    m_file_state.failed_file_id = FILE_ID_NONE;
}

static void file_load_event(uint32_t file_id)
{
    if(!m_file_state.sd_mounted)
    {
        sys_logw(FILE_TAG, "SD card not mounted");
        return;
    }

    // 进入加载状态；同时清掉上一次的失败记录（本次加载会重新判定）
    m_file_state.load_complete = FILE_LOAD_STATE_LOADING;
    m_file_state.failed_file_id = FILE_ID_NONE;

    /* 在校验+取出文件名时**必须持锁**：service_file_set_dir() 可能在别的任务
       （BLE/app 任务）里把 file_list 整个 free 掉并置 NULL，而它只持有 file_list_lock
       —— 不加锁读 file_list 会和"先置 NULL、再清零 file_count"这两步之间竞态，
       拿到 (file_count 非 0, file_list 为 NULL) 的撕裂状态，随后
       file_list[file_id] 就是一次野指针解引用（实测崩在 strlen，EXCVADDR=file_id*260）。
       临界区里只做一次 strncpy，不再读列表。 */
    char filename[256];
    file_list_lock();
    if(m_file_state.file_list == NULL || file_id >= m_file_state.file_count)
    {
        file_list_unlock();
        sys_logw(FILE_TAG, "Invalid file ID: %d", file_id);
        m_file_state.load_complete = FILE_LOAD_STATE_FAILED;
        m_file_state.failed_file_id = file_id;
        return;
    }
    strncpy(filename, m_file_state.file_list[file_id].filename, sizeof(filename) - 1);
    filename[sizeof(filename) - 1] = '\0';
    file_list_unlock();

    // 释放现有缓冲区
    file_free_buffer();

    // 构建文件路径
    char filepath[512];
    snprintf(filepath, sizeof(filepath), "%s/%s", m_file_active_dir, filename);

    // 打开文件
    FILE* file = fopen(filepath, "rb");
    if(file == NULL)
    {
        sys_loge(FILE_TAG, "Open file failed: %s", filepath);
        m_file_state.load_complete = FILE_LOAD_STATE_FAILED;
        m_file_state.failed_file_id = file_id;
        return;
    }

    // 获取文件大小
    fseek(file, 0, SEEK_END);
    uint32_t file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    // 尺寸预检：整份 malloc 必然失败的大文件直接判失败，别去撞堆（撞了也只能报个分配错误）
    if(file_size > SERVICE_FILE_PSRAM_MAX_BYTES)
    {
        sys_loge(FILE_TAG, "File too large to load as a whole: %s size=%u limit=%u",
                 filename, (unsigned)file_size,
                 (unsigned)SERVICE_FILE_PSRAM_MAX_BYTES);
        fclose(file);
        m_file_state.load_complete = FILE_LOAD_STATE_FAILED;
        m_file_state.failed_file_id = file_id;
        return;
    }

    // 分配PSRAM缓冲区
    m_file_state.psram_buffer = (uint8_t*)heap_caps_malloc(file_size, MALLOC_CAP_SPIRAM);
    if(m_file_state.psram_buffer == NULL)
    {
        // 带上堆余量：PSRAM 申请失败时区分"文件异常大"与"PSRAM 真没空间"
        sys_loge(FILE_TAG, "Allocate PSRAM buffer failed: size=%u psram_free=%u psram_largest=%u internal_free=%u",
                 (unsigned)file_size,
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
        fclose(file);
        m_file_state.load_complete = FILE_LOAD_STATE_FAILED;
        m_file_state.failed_file_id = file_id;
        return;
    }

    // 读取文件数据
    size_t read_size = fread(m_file_state.psram_buffer, 1, file_size, file);
    if(read_size != file_size)
    {
        sys_loge(FILE_TAG, "Read file failed");
        file_free_buffer();
        fclose(file);
        m_file_state.load_complete = FILE_LOAD_STATE_FAILED;
        m_file_state.failed_file_id = file_id;
        return;
    }

    fclose(file);

    m_file_state.buffer_size = file_size;
    m_file_state.current_file_id = file_id;

    sys_logi(FILE_TAG, "Loaded file: %s, size: %d bytes", filename, file_size);

    // 加载完成
    m_file_state.load_complete = FILE_LOAD_STATE_DONE;
}

int service_file_is_load_failed(uint32_t file_id)
{
    return (m_file_state.load_complete == FILE_LOAD_STATE_FAILED &&
            m_file_state.failed_file_id == file_id) ? 1 : 0;
}

static void file_load_next_event(void)
{
    uint32_t count;
    uint32_t cur;

    /* 持锁快照：别处的 service_file_set_dir() 可能正在 free/重建列表 */
    file_list_lock();
    count = m_file_state.file_count;
    cur   = m_file_state.current_file_id;
    file_list_unlock();

    if(count == 0)
    {
        sys_logw(FILE_TAG, "No files to load");
        return;
    }

    // 计算下一个文件ID（循环）
    uint32_t next_file_id = (cur + 1) % count;
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
    
    // 清空加载状态（含失败记录：显式请求加载即视为"重新试一次"）
    m_file_state.load_complete = FILE_LOAD_STATE_NONE;
    m_file_state.failed_file_id = FILE_ID_NONE;

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
    m_file_state.failed_file_id = FILE_ID_NONE;

    file_msg_t msg;
    msg.ID = MSG_FILE_LOAD_NEXT;
    file_msg_send(&msg, 0);
}

static int file_save_call(file_msg_t *msg)
{
    if(msg->owner != FILE_SAVE_BLE && msg->owner != FILE_SAVE_WIFI)
    {
        if(msg->pdata) vPortFree(msg->pdata);
        return -1;
    }
    if(!m_file_msg_hdl || !m_save_call_mutex || !m_save_done)
    {
        if(msg->pdata) vPortFree(msg->pdata);
        return -1;
    }
    xSemaphoreTake(m_save_call_mutex, portMAX_DELAY);
    if(xQueueSend(m_file_msg_hdl, msg, portMAX_DELAY) != pdPASS)
    {
        if(msg->pdata) vPortFree(msg->pdata);
        xSemaphoreGive(m_save_call_mutex);
        return -1;
    }
    xSemaphoreTake(m_save_done, portMAX_DELAY);
    int result = m_save_result;
    xSemaphoreGive(m_save_call_mutex);
    return result;
}

int service_file_save_data(file_save_owner_t owner, uint8_t *pdata, uint32_t data_len)
{
    if(!pdata || !data_len)
    {
        if(pdata) vPortFree(pdata);
        return -1;
    }
    file_msg_t msg = {.ID = MSG_FILE_SAVE_DATA, .owner = owner, .pdata = pdata, .data_len = data_len};
    return file_save_call(&msg); // 入队成功后 file_task 接管 pdata，包括失败路径
}

int service_file_save_data_async(file_save_owner_t owner, uint8_t *pdata,
                                 uint32_t data_len, void (*done)(void *, int, int64_t), void *context)
{
    if((owner != FILE_SAVE_BLE && owner != FILE_SAVE_WIFI) || !pdata || !data_len ||
       !done || !m_file_msg_hdl) return -1;
    file_msg_t msg = {.ID = MSG_FILE_SAVE_DATA, .owner = owner, .pdata = pdata,
                     .data_len = data_len, .done = done, .context = context};
    return xQueueSend(m_file_msg_hdl, &msg, pdMS_TO_TICKS(100)) == pdPASS ? 0 : -1;
}

int service_file_save_start(file_save_owner_t owner, const char *pfilename, uint32_t file_size)
{
    if(!pfilename || !pfilename[0] || strcmp(pfilename, ".") == 0 ||
       strcmp(pfilename, "..") == 0 || strchr(pfilename, '/') || strchr(pfilename, '\\') ||
       strlen(pfilename) >= sizeof(m_file_state.save_filename) || file_size < FILM_HEADER_SIZE) return -1;

    file_msg_t msg = {0};
    msg.ID = MSG_FILE_SAVE_START;
    msg.owner = owner;
    msg.file_size = file_size;
    msg.pdata = (uint8_t*)pvPortMalloc(strlen(pfilename) + 1);
    if(!msg.pdata) return -1;
    memcpy(msg.pdata, pfilename, strlen(pfilename) + 1);
    int result = file_save_call(&msg);
    return result;
}

bool service_file_save_path_valid(const char *rel_path)
{
    return rel_path != NULL && rel_path[0] != '\0' && rel_path[0] != '/' &&
           strchr(rel_path, '\\') == NULL && strstr(rel_path, "..") == NULL &&
           strlen(rel_path) + sizeof("/sdcard/.ffupload.part") < sizeof(m_file_state.save_part_path);
}

int service_file_save_start_to(file_save_owner_t owner, const char *rel_path, uint32_t file_size)
{
    if(!service_file_save_path_valid(rel_path))
    {
        sys_logw(FILE_TAG, "Invalid explicit path: %s", rel_path ? rel_path : "(null)");
        return -1;
    }

    if(file_size == 0)
    {
        sys_logw(FILE_TAG, "Invalid file size: %d, must >= %d", file_size, FILM_HEADER_SIZE);
        return -1;
    }

    file_msg_t msg = {0};
    msg.ID = MSG_FILE_SAVE_START_TO;
    msg.owner = owner;
    msg.file_size = file_size;
    msg.pdata = (uint8_t*)pvPortMalloc(strlen(rel_path) + 1);
    if(msg.pdata == NULL)
    {
        return -1;
    }
    memcpy(msg.pdata, rel_path, strlen(rel_path) + 1);
    int result = file_save_call(&msg);
    return result;
}

int service_file_save_stop(file_save_owner_t owner, uint8_t auto_load)
{
    file_msg_t msg = {.ID = MSG_FILE_SAVE_STOP, .owner = owner, .file_id = auto_load ? 1 : 0};
    return file_save_call(&msg);
}

int service_file_save_abort(file_save_owner_t owner)
{
    file_msg_t msg = {.ID = MSG_FILE_SAVE_ABORT, .owner = owner};
    return file_save_call(&msg);
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

bool service_file_has_film(const char *dir, const char *filename)
{
    if(dir == NULL || filename == NULL) return false;
    const char *ext = strrchr(filename, '.');
    if(ext == NULL || strcmp(ext, FILM_FILE_EXT) != 0) return false;
    char path[512];
    int n = snprintf(path, sizeof(path), "%s/%s", dir, filename);
    struct stat st;
    return n > 0 && (size_t)n < sizeof(path) && stat(path, &st) == 0 &&
           S_ISREG(st.st_mode) && st.st_size >= FILM_HEADER_SIZE;
}

int service_file_find_id(const char *filename, uint32_t *out_id)
{
    if(filename == NULL || out_id == NULL) return -1;
    int ret = -1;
    file_list_lock();
    if(m_list_ready && m_file_state.file_list != NULL)
    {
        for(uint32_t i = 0; i < m_file_state.file_count; i++)
        {
            if(strcmp(filename, m_file_state.file_list[i].filename) == 0)
            {
                *out_id = i;
                ret = 0;
                break;
            }
        }
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
        /* 删除后 file_id 整体前移：缓存的 current_id 已指向别的文件，
           不作废缓冲会拿旧内容顶替新下标（见 file_invalidate_buffer） */
        file_invalidate_buffer();
        service_file_refresh_list();
        return 0;
    }
    else
    {
        sys_logw(FILE_TAG, "Failed to delete file: %s", filepath);
        return -1;
    }
}

/* No set_dir/refresh/clean or playback state mutation; missing directory is empty. */
int service_file_catalog(uint8_t directory, uint32_t index, uint32_t *count,
                         char *name, size_t name_size)
{
    const char *path = directory == 1 ? FILM_DIR : directory == 2 ? ANIM_DIR : NULL;
    if(!path || !count || (name && name_size == 0)) return 3;
    *count = 0;
    if(name) name[0] = '\0';
    if(hal_sd_get_status() != SD_MOUNT) return 1;
    DIR *dir = opendir(path);
    if(!dir) return errno == ENOENT ? 0 : 2;
    struct dirent *entry;
    int result = 0;
    errno = 0;
    while((entry = readdir(dir)) != NULL)
    {
        char *ext = strrchr(entry->d_name, '.');
        if(!ext || strcmp(ext, FILM_FILE_EXT)) continue;
        char full[512];
        struct stat st;
        if(snprintf(full, sizeof(full), "%s/%s", path, entry->d_name) >= sizeof(full) ||
           stat(full, &st) != 0) { result = 2; break; }
        if(!S_ISREG(st.st_mode) || st.st_size < FILM_HEADER_SIZE) continue;
        if(name && *count == index)
        {
            if(strlen(entry->d_name) >= name_size) { result = 5; break; }
            strcpy(name, entry->d_name);
        }
        ++*count;
        errno = 0;
    }
    if(errno != 0) result = 2;
    closedir(dir);
    return result;
}
