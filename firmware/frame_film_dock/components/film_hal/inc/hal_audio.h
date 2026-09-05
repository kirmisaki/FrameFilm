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
 * FileName : /film_hal/inc/hal_audio.h
 * Author: Kiritro  Version: v0.1  Date: 2026/9/5
 * Description: ES8311+NS4150B 音频模块 HAL 驱动接口
 * ChangeLog: Change Notes
 *
 *********************************************************************/

#ifndef __HAL_AUDIO_H__
#define __HAL_AUDIO_H__

/*********************************************************************
 * INCLUDES
 */
#include "esp_system.h"
#include "driver/i2s_std.h"
#include "driver/i2c_master.h"
#include "sys_cfg.h"

#if SYS_FUNC_AUDIO_EN

/*********************************************************************
 * CPPMIX
 */
#ifdef __cplusplus
extern "C" {
#endif

/*********************************************************************
 * MACROS
 */
// ES8311 I2C 从机地址（7-bit）
#define AUDIO_I2C_ADDR                   (0x18)

// 采样率：输入必须 == 输出（I2S 全双工共享时钟）
#define AUDIO_SAMPLE_RATE                (16000)

// I2S 标准模式引脚（已确认接线）
#define AUDIO_I2S_HOST                   (I2S_NUM_0)
#define AUDIO_I2S_MCLK_PIN               GPIO_NUM_18   // 主时钟，模块已接
#define AUDIO_I2S_SCLK_PIN               GPIO_NUM_8    // 位时钟 BCLK
#define AUDIO_I2S_WS_PIN                 GPIO_NUM_1    // 帧时钟 LRCK（单声道）
#define AUDIO_I2S_DOUT_PIN               GPIO_NUM_45   // TX：主控→CODEC（接模块 DIN）
#define AUDIO_I2S_DIN_PIN                GPIO_NUM_21   // RX：CODEC→主控（接模块 DOUT）

// I2C 引脚（独立总线，与 EPD 屏幕检测的 I2C_NUM_1 互不冲突）
#define AUDIO_I2C_PORT                   (I2C_NUM_0)
#define AUDIO_I2C_SDA_PIN                GPIO_NUM_4
#define AUDIO_I2C_SCL_PIN                GPIO_NUM_5
#define AUDIO_I2C_FREQ_HZ                (100000)

// I2S 读写超时
#define AUDIO_I2S_TIMEOUT_MS             (1000)

// 软音量默认（0-100）
#define AUDIO_DEFAULT_VOLUME             (100)
// 输入增益默认（dB，写 ES8311 0x1C 寄存器，需按 datasheet 校准）
#define AUDIO_DEFAULT_INPUT_GAIN_DB      (30)

/*********************************************************************
* TYPEDEFS
*/

/*********************************************************************
 * CONSTANTS
 */

/*********************************************************************
 * LOCAL VARIABLES
 */

/*********************************************************************
 * GLOBAL VARIABLES
 */

/*********************************************************************
 * LOCAL FUNCTIONS
 */

/*********************************************************************
 * GLOBAL FUNCTIONS
 */

/**
 * @brief 初始化音频硬件（I2C/I2S + ES8311 codec）
 *
 * 初始化 I2C 总线并探测 ES8311，创建 I2S 标准全双工通道，
 * 软复位并配置 ES8311 寄存器。模块无外部 PA_EN/CODEC_EN 使能脚，
 * 上电后 codec 默认不输出，需通过 capture/playback start 启用对应通道。
 *
 * @return ESP_OK 成功；其它失败
 */
esp_err_t hal_audio_init(void);

/**
 * @brief 释放音频硬件资源
 */
void hal_audio_deinit(void);

/**
 * @brief 从 ADC 读取一帧采集数据（单声道 16bit）
 *
 * 仅在 hal_audio_capture_start() 之后调用有效。
 *
 * @param buf 输出缓冲（int16_t 数组）
 * @param frames 期望读取的帧数（每帧 = 1 个 int16 样本）
 * @return 实际读取的帧数；失败返回 -1
 */
int32_t hal_audio_read_stream(int16_t *buf, uint32_t frames);

/**
 * @brief 向 DAC 写入一帧播放数据（单声道 16bit，经软音量缩放）
 *
 * 仅在 hal_audio_playback_start() 之后调用有效。
 *
 * @param buf 输入缓冲（int16_t 数组）
 * @param frames 期望写入的帧数（每帧 = 1 个 int16 样本）
 * @return 实际写入的帧数；失败返回 -1
 */
int32_t hal_audio_write_stream(const int16_t *buf, uint32_t frames);

/**
 * @brief 开启 ADC 采集通道（启用 I2S RX）
 */
esp_err_t hal_audio_capture_start(void);

/**
 * @brief 关闭 ADC 采集通道（停用 I2S RX，省电）
 */
esp_err_t hal_audio_capture_stop(void);

/**
 * @brief 开启 DAC 播放通道（先用静音帧稳定时钟再启用，规避 POP）
 */
esp_err_t hal_audio_playback_start(void);

/**
 * @brief 关闭 DAC 播放通道（停止播放，规避 POP）
 */
esp_err_t hal_audio_playback_stop(void);

/**
 * @brief 设置 ES8311 输入增益（麦克风）
 *
 * @param db 增益（dB），默认 30dB
 */
void hal_audio_set_input_gain(uint8_t db);

/**
 * @brief 设置输出软音量
 *
 * @param vol 音量（0-100），默认 80
 */
void hal_audio_set_volume(uint8_t vol);

#ifdef __cplusplus
}
#endif

#endif /* SYS_FUNC_AUDIO_EN */

#endif /* __HAL_AUDIO_H__ */
