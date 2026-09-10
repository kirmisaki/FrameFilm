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
 * FileName : /film_hal/src/hal_usb.c
 * Author: Kiritro  Version: v0.1  Date: 2026/9/10
 * Description: USB 声卡驱动（usb_device_uac），对接 hal_audio 全双工
 * ChangeLog: Change Notes
 *
 *********************************************************************/

/*********************************************************************
 * INCLUDES
 */
#include "usb_device_uac.h"

#include "hal_usb.h"
#include "hal_audio.h"
#include "sys_log.h"

#if SYS_FUNC_AUDIO_USB_EN

/*********************************************************************
 * MACROS
 */
#define USB_TAG                          "HAL_USB"

/*********************************************************************
* TYPEDEFS
*/

/*********************************************************************
 * CONSTANTS
 */

/*********************************************************************
 * LOCAL VARIABLES
 */
static bool s_usb_initialized = false;

/*********************************************************************
 * GLOBAL VARIABLES
 */

/*********************************************************************
 * LOCAL FUNCTIONS
 */
static esp_err_t usb_uac_output_cb(uint8_t *buf, size_t len, void *cb_ctx);
static esp_err_t usb_uac_input_cb(uint8_t *buf, size_t len, size_t *bytes_read, void *cb_ctx);
static void usb_uac_set_mute_cb(uint32_t mute, void *cb_ctx);
static void usb_uac_set_volume_cb(uint32_t volume, void *cb_ctx);

/*********************************************************************
 * GLOBAL FUNCTIONS
 */

/**
 * @brief 扬声器回调：USB 主机下发的 PCM → I2S DAC
 *
 * 运行在 UAC 扬声器任务中，len 为字节数。
 */
static esp_err_t usb_uac_output_cb(uint8_t *buf, size_t len, void *cb_ctx)
{
    (void)cb_ctx;

    int32_t written = hal_audio_write_stream((const int16_t *)buf, len / sizeof(int16_t));
    return (written < 0) ? ESP_FAIL : ESP_OK;
}

/**
 * @brief 麦克风回调：I2S ADC 采集的 PCM → USB 主机
 *
 * 运行在 UAC 麦克风任务中，需在 len 字节范围内填充数据并回填实际字节数。
 * hal_audio_read_stream 阻塞等待一帧数据，符合组件允许阻塞回调的要求。
 */
static esp_err_t usb_uac_input_cb(uint8_t *buf, size_t len, size_t *bytes_read, void *cb_ctx)
{
    (void)cb_ctx;

    int32_t frames = hal_audio_read_stream((int16_t *)buf, len / sizeof(int16_t));
    *bytes_read = (frames < 0) ? 0 : (size_t)frames * sizeof(int16_t);
    return ESP_OK;
}

/**
 * @brief 静音回调：USB 主机静音控件触发
 */
static void usb_uac_set_mute_cb(uint32_t mute, void *cb_ctx)
{
    (void)cb_ctx;
    hal_audio_set_mute(mute != 0);
}

/**
 * @brief 音量回调：USB 主机音量控件触发
 *
 * usb_device_uac 已将音量归一化到 0-100，直接透传给 hal_audio。
 */
static void usb_uac_set_volume_cb(uint32_t volume, void *cb_ctx)
{
    (void)cb_ctx;
    hal_audio_set_volume((uint8_t)volume);
}

esp_err_t hal_usb_init(void)
{
    if (s_usb_initialized)
    {
        return ESP_OK;
    }

    // USB 声卡需同时录音与播放，先开启 I2S 全双工
    esp_err_t ret = hal_audio_duplex_start();
    if (ret != ESP_OK)
    {
        sys_loge(USB_TAG, "audio duplex start failed: %d", ret);
        return ret;
    }

    uac_device_config_t config = { 0 };
    config.output_cb = usb_uac_output_cb;
    config.input_cb = usb_uac_input_cb;
    config.set_mute_cb = usb_uac_set_mute_cb;
    config.set_volume_cb = usb_uac_set_volume_cb;

    ret = uac_device_init(&config);
    if (ret != ESP_OK)
    {
        sys_loge(USB_TAG, "uac device init failed: %d", ret);
        hal_audio_duplex_stop();
        return ret;
    }

    s_usb_initialized = true;
    sys_logi(USB_TAG, "usb uac sound card initialized");
    return ESP_OK;
}

#endif /* SYS_FUNC_AUDIO_USB_EN */
