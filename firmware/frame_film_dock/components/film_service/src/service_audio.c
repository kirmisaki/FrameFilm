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
 * FileName : /film_service/src/service_audio.c
 * Author: Kiritro  Version: v0.1  Date: 2026/9/5
 * Description: 音频服务层（采集/播放状态机 + 按键自测：录音后回放）
 * ChangeLog: Change Notes
 *
 *********************************************************************/

/*********************************************************************
 * INCLUDES
 */
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "esp_heap_caps.h"

#include "sys_cfg.h"
#include "sys_log.h"
#include "hal_api.h"
#include "service_audio.h"

#if SYS_FUNC_AUDIO_EN

/*********************************************************************
 * MACROS
 */
#define AUDIO_TAG                              "AUDIO"

// 任务与队列
#define AUDIO_MSG_QUEUE_LENGTH                 (10)
#define AUDIO_TASK_PRIO                        (7)
#define AUDIO_TASK_SIZE                        (4096)
#define AUDIO_TASK_NAME                        "audio_task"

// 录音长度：默认 3s（16kHz 16bit 单声道）
#define AUDIO_RECORD_SECONDS                   (3)
#define AUDIO_RECORD_MAX_FRAMES                (AUDIO_SAMPLE_RATE * AUDIO_RECORD_SECONDS)

// 单次读/写流的分块帧数（512 帧 = 32ms @16kHz）
#define AUDIO_STREAM_CHUNK_FRAMES              (512)

#define AUDIO_MIN(a, b)                        (((a) < (b)) ? (a) : (b))

/*********************************************************************
* TYPEDEFS
*/
// 消息事件
typedef enum {
    AUDIO_MSG_SELFTEST = 1,   // 按键自测：录音后回放
    AUDIO_MSG_START_REC,      // 开始采集
    AUDIO_MSG_STOP_REC,       // 停止采集
    AUDIO_MSG_PLAY_REC,       // 播放最近一次录音
    AUDIO_MSG_SET_VOL,        // 设置软音量
    AUDIO_MSG_ABORT,          // 停止当前采集/播放（长按）
} audio_msg_id_t;

typedef struct
{
    uint8_t  id;
    int32_t  param;
} audio_msg_t;

/*********************************************************************
 * CONSTANTS
 */

/*********************************************************************
 * LOCAL VARIABLES
 */
static TaskHandle_t s_audio_task = NULL;
static QueueHandle_t s_audio_queue = NULL;

static int16_t *s_rec_buf = NULL;          // 录音缓冲（内部）
static volatile uint32_t s_rec_frames = 0; // 已录帧数
static volatile uint8_t s_rec_ready = 0;   // 录音缓冲是否就绪

static volatile audio_state_t s_state = AUDIO_STATE_IDLE;
static uint8_t s_volume = AUDIO_DEFAULT_VOLUME;

/*********************************************************************
 * GLOBAL VARIABLES
 */

/*********************************************************************
 * LOCAL FUNCTIONS
 */
static void audio_task_handle(void *pvParameters);
static void audio_msg_send(uint8_t id, int32_t param);

static void audio_btn_short_cb(void);
static void audio_btn_long_cb(void);

static void audio_do_record(void);
static void audio_do_play(const int16_t *buf, uint32_t frames);

/*********************************************************************
 * GLOBAL FUNCTIONS
 */

void service_audio_init(void)
{
    // 录音缓冲优先放 PSRAM（大块），失败回退到内部 RAM
    size_t buf_bytes = (size_t)AUDIO_RECORD_MAX_FRAMES * sizeof(int16_t);
    s_rec_buf = heap_caps_malloc(buf_bytes, MALLOC_CAP_SPIRAM);
    if (s_rec_buf == NULL)
    {
        s_rec_buf = heap_caps_malloc(buf_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (s_rec_buf == NULL)
    {
        sys_loge(AUDIO_TAG, "record buffer alloc fail (%u bytes), recording disabled", (unsigned)buf_bytes);
        s_rec_ready = 0;
    }
    else
    {
        s_rec_ready = 1;
        sys_logi(AUDIO_TAG, "record buffer ready, %u frames (%u bytes)",
                 (unsigned)AUDIO_RECORD_MAX_FRAMES, (unsigned)buf_bytes);
    }

    // 默认音量
    s_volume = AUDIO_DEFAULT_VOLUME;
    hal_audio_set_volume(s_volume);

    // 按键：短按=录音并回放（自测），长按=停止当前采集/播放
    hal_input_register_cb(INPUT_PRESS_SHORT, audio_btn_short_cb);
    hal_input_register_cb(INPUT_PRESS_LONG, audio_btn_long_cb);

    if (s_audio_task == NULL)
    {
        if (pdPASS != xTaskCreate(audio_task_handle, AUDIO_TASK_NAME,
                                  AUDIO_TASK_SIZE, NULL, AUDIO_TASK_PRIO, NULL))
        {
            sys_loge(AUDIO_TAG, "task create error!");
        }
    }
}

audio_state_t service_audio_get_state(void)
{
    return s_state;
}

int32_t service_audio_start_record(void)
{
    audio_msg_send(AUDIO_MSG_START_REC, 0);
    return 0;
}

int32_t service_audio_stop_record(void)
{
    audio_msg_send(AUDIO_MSG_STOP_REC, 0);
    return 0;
}

int32_t service_audio_get_recorded(const int16_t **p_buf, uint32_t *p_frames)
{
    if ((p_buf == NULL) || (p_frames == NULL) || (s_rec_buf == NULL))
    {
        return -1;
    }
    *p_buf = s_rec_buf;
    *p_frames = s_rec_frames;
    return 0;
}

int32_t service_audio_play_recorded(void)
{
    audio_msg_send(AUDIO_MSG_PLAY_REC, 0);
    return 0;
}

int32_t service_audio_play(const int16_t *buf, uint32_t frames)
{
    if ((buf == NULL) || (frames == 0))
    {
        return -1;
    }
    audio_do_play(buf, frames);
    return (int32_t)frames;
}

void service_audio_set_volume(uint8_t vol)
{
    s_volume = vol;
    audio_msg_send(AUDIO_MSG_SET_VOL, vol);
}

/*********************************************************************
 * LOCAL FUNCTIONS
 */

static void audio_msg_send(uint8_t id, int32_t param)
{
    audio_msg_t msg;
    msg.id = id;
    msg.param = param;
    if (s_audio_queue != NULL)
    {
        xQueueSend(s_audio_queue, &msg, portMAX_DELAY);
    }
}

/* 按键回调：发送事件到任务队列（fromISR 版本在任务上下文也安全） */
static void audio_btn_short_cb(void)
{
    audio_msg_t msg;
    msg.id = AUDIO_MSG_SELFTEST;
    msg.param = 0;
    if (s_audio_queue != NULL)
    {
        xQueueSendFromISR(s_audio_queue, &msg, NULL);
    }
}

static void audio_btn_long_cb(void)
{
    audio_msg_t msg;
    msg.id = AUDIO_MSG_ABORT;
    msg.param = 0;
    if (s_audio_queue != NULL)
    {
        xQueueSendFromISR(s_audio_queue, &msg, NULL);
    }
}

static void audio_task_handle(void *pvParameters)
{
    s_audio_queue = xQueueCreate(AUDIO_MSG_QUEUE_LENGTH, sizeof(audio_msg_t));
    SYS_ERROR_CHECK(s_audio_queue == NULL);

    for (;;)
    {
        audio_msg_t msg;
        SYS_ERROR_CHECK(xQueueReceive(s_audio_queue, &msg, portMAX_DELAY) != pdPASS);

        switch (msg.id)
        {
        case AUDIO_MSG_SELFTEST:
            if ((s_state == AUDIO_STATE_IDLE) && s_rec_ready)
            {
                audio_do_record();
                if (s_rec_frames > 0)
                {
                    audio_do_play(s_rec_buf, s_rec_frames);
                }
            }
            break;

        case AUDIO_MSG_START_REC:
            if ((s_state == AUDIO_STATE_IDLE) && s_rec_ready)
            {
                audio_do_record();
            }
            break;

        case AUDIO_MSG_STOP_REC:
            // 录音中由 audio_do_record 内联消费该事件（提前终止），这里无需处理
            break;

        case AUDIO_MSG_PLAY_REC:
            if ((s_state == AUDIO_STATE_IDLE) && (s_rec_frames > 0))
            {
                audio_do_play(s_rec_buf, s_rec_frames);
            }
            break;

        case AUDIO_MSG_SET_VOL:
            hal_audio_set_volume((uint8_t)msg.param);
            sys_logi(AUDIO_TAG, "volume set to %d", (int)msg.param);
            break;

        case AUDIO_MSG_ABORT:
            // audio_do_record/audio_do_play 内联消费该事件，这里忽略
            break;

        default:
            break;
        }
    }
}

/* 采集：写入内部录音缓冲，直到录满或被 ABORT/STOP 终止 */
static void audio_do_record(void)
{
    s_state = AUDIO_STATE_RECORDING;
    s_rec_frames = 0;
    hal_audio_set_volume(s_volume);

    if (hal_audio_capture_start() != ESP_OK)
    {
        sys_loge(AUDIO_TAG, "capture start fail");
        s_state = AUDIO_STATE_IDLE;
        return;
    }

    while (s_rec_frames < AUDIO_RECORD_MAX_FRAMES)
    {
        // 消费队列中的终止/控制消息
        audio_msg_t m;
        if (xQueueReceive(s_audio_queue, &m, 0) == pdPASS)
        {
            if ((m.id == AUDIO_MSG_ABORT) || (m.id == AUDIO_MSG_STOP_REC))
            {
                sys_logi(AUDIO_TAG, "record aborted, %u frames", (unsigned)s_rec_frames);
                break;
            }
            else if (m.id == AUDIO_MSG_SET_VOL)
            {
                s_volume = (uint8_t)m.param;
                hal_audio_set_volume(s_volume);
                continue;
            }
            // 其它消息忽略，继续采集
        }

        int32_t got = hal_audio_read_stream(s_rec_buf + s_rec_frames, AUDIO_STREAM_CHUNK_FRAMES);
        if (got > 0)
        {
            s_rec_frames += got;
        }
        else
        {
            // 读不到数据（超时/异常），稍作等待避免忙等
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }

    hal_audio_capture_stop();
    sys_logi(AUDIO_TAG, "record done, %u frames (%.1f s)",
             (unsigned)s_rec_frames, (float)s_rec_frames / AUDIO_SAMPLE_RATE);
    s_state = AUDIO_STATE_IDLE;
}

/* 播放：写流直到播完或被 ABORT 终止 */
static void audio_do_play(const int16_t *buf, uint32_t frames)
{
    uint32_t written = 0;

    s_state = AUDIO_STATE_PLAYING;

    if (hal_audio_playback_start() != ESP_OK)
    {
        sys_loge(AUDIO_TAG, "playback start fail");
        s_state = AUDIO_STATE_IDLE;
        return;
    }

    while (written < frames)
    {
        audio_msg_t m;
        if (xQueueReceive(s_audio_queue, &m, 0) == pdPASS)
        {
            if (m.id == AUDIO_MSG_ABORT)
            {
                sys_logi(AUDIO_TAG, "playback aborted, %u frames", (unsigned)written);
                break;
            }
            else if (m.id == AUDIO_MSG_SET_VOL)
            {
                s_volume = (uint8_t)m.param;
                hal_audio_set_volume(s_volume);
                continue;
            }
        }

        uint32_t chunk = AUDIO_MIN(AUDIO_STREAM_CHUNK_FRAMES, frames - written);
        int32_t n = hal_audio_write_stream(buf + written, chunk);
        if (n <= 0)
        {
            sys_logw(AUDIO_TAG, "write stream fail");
            break;
        }
        written += n;
    }

    hal_audio_playback_stop();
    sys_logi(AUDIO_TAG, "playback done, %u frames", (unsigned)written);
    s_state = AUDIO_STATE_IDLE;
}

#endif /* SYS_FUNC_AUDIO_EN */
