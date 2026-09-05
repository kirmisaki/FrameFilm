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
 * FileName : /film_service/inc/service_audio.h
 * Author: Kiritro  Version: v0.1  Date: 2026/9/5
 * Description: 音频服务层（语音采集/播放状态机）
 * ChangeLog: Change Notes
 *
 *********************************************************************/

#ifndef __SERVICE_AUDIO_H__
#define __SERVICE_AUDIO_H__

/*********************************************************************
 * INCLUDES
 */
#include "sys_cfg.h"

#if SYS_FUNC_AUDIO_EN

#include <stdint.h>

/*********************************************************************
 * CPPMIX
 */
#ifdef __cplusplus
extern "C" {
#endif

/*********************************************************************
 * MACROS
 */

/*********************************************************************
* TYPEDEFS
*/
typedef enum {
    AUDIO_STATE_IDLE = 0,    // 空闲（无采集/播放）
    AUDIO_STATE_RECORDING,   // 录音中
    AUDIO_STATE_PLAYING,     // 播放中
} audio_state_t;

/*********************************************************************
 * CONSTANTS
 */

/*********************************************************************
 * GLOBAL FUNCTIONS
 */

/**
 * @brief 初始化音频服务层
 *
 * 注册录音/播放任务与消息队列，登记按键回调（短按=录音并回放自测，
 * 长按=停止当前采集/播放），并在需要时缓冲录音空间。
 * 仅当 sys_cfg.h 中 SYS_FUNC_AUDIO_EN 为 1 时有效。
 */
void service_audio_init(void);

/**
 * @brief 获取当前音频状态机状态
 * @return audio_state_t 当前状态
 */
audio_state_t service_audio_get_state(void);

/**
 * @brief 启动录音（异步，采集任务内部环形缓冲）
 * @return 0 成功
 */
int32_t service_audio_start_record(void);

/**
 * @brief 停止录音（异步；停止后可通过 service_audio_get_recorded 读取数据）
 * @return 0 成功
 */
int32_t service_audio_stop_record(void);

/**
 * @brief 获取最近一次录音数据（阶段三用于发送给后端）
 * @param p_buf 输出：录音数据指针（16bit 单声道 16kHz）
 * @param p_frames 输出：帧数
 * @return 0 成功
 */
int32_t service_audio_get_recorded(const int16_t **p_buf, uint32_t *p_frames);

/**
 * @brief 播放最近一次录音（异步）
 * @return 0 成功
 */
int32_t service_audio_play_recorded(void);

/**
 * @brief 播放外部 PCM 数据（阻塞到播完，供阶段三后端回包使用）
 * @param buf PCM 数据（16bit 单声道 16kHz）
 * @param frames 帧数
 * @return 实际播放帧数；失败返回 -1
 */
int32_t service_audio_play(const int16_t *buf, uint32_t frames);

/**
 * @brief 设置播放软音量
 * @param vol 音量（0-100）
 */
void service_audio_set_volume(uint8_t vol);

#ifdef __cplusplus
}
#endif

#endif /* SYS_FUNC_AUDIO_EN */

#endif /* __SERVICE_AUDIO_H__ */
