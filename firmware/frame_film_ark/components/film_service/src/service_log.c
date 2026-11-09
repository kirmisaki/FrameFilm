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
 * FileName : /film_service/src/service_log.c
 * Author: Kiritro  Version: v0.1  Date: 2026/11/9
 * Description: 蓝牙实时日志流（仅打开开关时生效）
 * ChangeLog: Change Notes
 *
 *********************************************************************/

/*********************************************************************
 * INCLUDES
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "esp_log.h"
#include "esp_heap_caps.h"

#include "service_ble.h"
#include "service_ble_gatts.h"
#include "service_log.h"

/*********************************************************************
 * MACROS
 */
/* 环形缓冲：只在第一次打开时分配，之后复用（避免关闭瞬间与在途日志钩子竞争） */
#define LOG_STREAM_RING_BYTES       (4096)
/* 单条 notify 的日志负载上限（+4 字节帧头/校验 = 196 <= 200 特征值上限） */
#define LOG_STREAM_PAYLOAD_MAX      (192)
#define LOG_STREAM_TASK_STACK       (3072)
#define LOG_STREAM_TASK_PRIO        (3)
#define LOG_STREAM_TX_INTERVAL_MS   (6)
#define LOG_STREAM_IDLE_MS          (20)

/*********************************************************************
* TYPEDEFS
*/


/*********************************************************************
 * CONSTANTS
 */


/*********************************************************************
 * LOCAL VARIABLES
 */
static SemaphoreHandle_t m_log_lock = NULL;
static uint8_t *m_ring = NULL;
static volatile uint16_t m_head = 0;   /* 写指针 */
static volatile uint16_t m_tail = 0;   /* 读指针 */
static volatile bool m_on = false;
static TaskHandle_t m_log_task = NULL;
/* 会话代号：快速关-开时让上一轮发送任务自行退出，不会与新任务并行推流 */
static volatile uint32_t m_log_gen = 0;
static vprintf_like_t m_prev_vprintf = NULL;

/*********************************************************************
 * LOCAL FUNCTIONS
 */
static int log_stream_vprintf(const char *fmt, va_list args);

/* 丢弃最早的数据直到下一次换行为止（保持行完整），无锁调用方负责持锁 */
static void log_ring_drop_one_line(void)
{
    while(m_tail != m_head && m_ring[m_tail] != '\n')
    {
        m_tail = (uint16_t)((m_tail + 1) % LOG_STREAM_RING_BYTES);
    }
    if(m_tail != m_head)
    {
        m_tail = (uint16_t)((m_tail + 1) % LOG_STREAM_RING_BYTES);
    }
}

/* 日志钩子内调用：写入环形缓冲。永不阻塞（拿不到锁/缓冲满就丢最旧行） */
static void log_ring_push(const char *data, size_t len)
{
    if(!m_on || m_ring == NULL || m_log_lock == NULL) return;
    if(xSemaphoreTake(m_log_lock, 0) != pdTRUE) return;

    for(size_t i = 0; i < len; i++)
    {
        uint16_t next = (uint16_t)((m_head + 1) % LOG_STREAM_RING_BYTES);
        if(next == m_tail)
        {
            log_ring_drop_one_line();
            next = (uint16_t)((m_head + 1) % LOG_STREAM_RING_BYTES);
        }
        m_ring[m_head] = (uint8_t)data[i];
        m_head = next;
    }
    xSemaphoreGive(m_log_lock);
}

/* esp_log 输出钩子：先抓一份到环形缓冲，再原样转给原来的 vprintf（UART 照常输出） */
static int log_stream_vprintf(const char *fmt, va_list args)
{
    char buf[256];
    va_list copy;

    va_copy(copy, args);
    int n = vsnprintf(buf, sizeof(buf), fmt, copy);
    va_end(copy);

    if(n > 0)
    {
        size_t len = (n < (int)sizeof(buf)) ? (size_t)n : (sizeof(buf) - 1);
        log_ring_push(buf, len);
    }
    return m_prev_vprintf ? m_prev_vprintf(fmt, args) : 0;
}

/* 后台发送任务：把环形缓冲按行切分，打包成 0x55 0x5F LEN TEXT SUM 经 CH1 notify 推给连接端 */
static void log_stream_task(void *arg)
{
    uint8_t frame[LOG_STREAM_PAYLOAD_MAX + 4];
    /* 代号随任务创建时传入，避免任务还没跑起来就被后续开关改写 */
    uint32_t gen = (uint32_t)(uintptr_t)arg;

    while(m_on && gen == m_log_gen)
    {
        size_t n = 0;
        bool got = false;

        if(xSemaphoreTake(m_log_lock, pdMS_TO_TICKS(LOG_STREAM_IDLE_MS)) == pdTRUE)
        {
            while(n < LOG_STREAM_PAYLOAD_MAX && m_tail != m_head)
            {
                uint8_t c = m_ring[m_tail];
                m_tail = (uint16_t)((m_tail + 1) % LOG_STREAM_RING_BYTES);
                frame[3 + n++] = c;
                got = true;
                if(c == '\n') break;
            }
            xSemaphoreGive(m_log_lock);
        }

        if(!got || n == 0)
        {
            vTaskDelay(pdMS_TO_TICKS(LOG_STREAM_IDLE_MS));
            continue;
        }

        if(service_ble_gatts_get_connect())
        {
            uint8_t sum = 0;
            frame[0] = BLE_CMD_HEAD;
            frame[1] = BLE_FILM_TRANS_CH_LOG_STREAM_DATA;
            frame[2] = (uint8_t)n;
            for(size_t i = 0; i < (size_t)(3 + n); i++) sum = (uint8_t)(sum + frame[i]);
            frame[3 + n] = sum;
            service_ble_send_notify_data(BLE_NOTIFY_SEND_CH1, frame, (uint16_t)(4 + n));
            vTaskDelay(pdMS_TO_TICKS(LOG_STREAM_TX_INTERVAL_MS));
        }
        else
        {
            /* 无人订阅/已断开：继续排空缓冲但放慢，避免空转 */
            vTaskDelay(pdMS_TO_TICKS(LOG_STREAM_IDLE_MS));
        }
    }

    /* 只有"当前会话"的任务才清句柄：快速关-开时旧任务无权动新任务的句柄 */
    if(gen == m_log_gen)
    {
        m_log_task = NULL;
    }
    vTaskDelete(NULL);
}

/*********************************************************************
 * GLOBAL FUNCTIONS
 */
void service_log_stream_set(uint8_t on)
{
    if(on)
    {
        if(m_on) return;

        if(m_ring == NULL)
        {
            m_ring = (uint8_t *)heap_caps_malloc(LOG_STREAM_RING_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if(m_ring == NULL)
            {
                m_ring = (uint8_t *)malloc(LOG_STREAM_RING_BYTES);
            }
            if(m_ring == NULL) return;
        }
        if(m_log_lock == NULL)
        {
            m_log_lock = xSemaphoreCreateMutex();
            if(m_log_lock == NULL) return;
        }

        m_head = 0;
        m_tail = 0;
        m_log_gen++;
        m_on = true;

        /* 不改动全局日志级别：只采集固件当前已启用的日志（默认 INFO）。
           需要观察的帧级日志（如 HAL_NFC 的 m1 frame）应以 INFO 级别输出 */

        m_prev_vprintf = esp_log_set_vprintf(log_stream_vprintf);

        /* 必须新建任务而不是复用旧句柄：旧任务可能仍在退出途中 */
        if(xTaskCreate(log_stream_task, "log_stream", LOG_STREAM_TASK_STACK,
                       (void *)(uintptr_t)m_log_gen, LOG_STREAM_TASK_PRIO, &m_log_task) != pdPASS)
        {
            /* 任务起不来就当场回滚，避免"状态=开却没有任何输出" */
            m_log_task = NULL;
            m_on = false;
            m_log_gen++;
            if(m_prev_vprintf != NULL)
            {
                (void)esp_log_set_vprintf(m_prev_vprintf);
                m_prev_vprintf = NULL;
            }
        }
    }
    else
    {
        if(!m_on) return;

        /* 先停写入：钩子看到 m_on=false 即不再碰缓冲；随后还原输出函数。
           不在这里等待/删除发送任务（GATT 回调不允许阻塞），只递增代号让旧任务
           在下一次循环自行退出。缓冲也不释放，避免与"刚好还在钩子里"的写线程
           竞争；后续再开直接复用。 */
        m_on = false;
        m_log_gen++;
        m_log_task = NULL;

        if(m_prev_vprintf != NULL)
        {
            (void)esp_log_set_vprintf(m_prev_vprintf);
            m_prev_vprintf = NULL;
        }
    }
}

uint8_t service_log_stream_get(void)
{
    return m_on ? 1 : 0;
}
