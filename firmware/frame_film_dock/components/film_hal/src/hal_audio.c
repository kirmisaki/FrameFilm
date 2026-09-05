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
 * FileName : /film_hal/src/hal_audio.c
 * Author: Kiritro  Version: v0.2  Date: 2026/9/5
 * Description: ES8311+NS4150B 音频模块 HAL 驱动实现
 *
 * v0.2: 内部改为封装乐鑫 esp_codec_dev 组件（es8311_codec_new + esp_codec_dev）。
 *       删除手写寄存器表与裸 I2S/I2C 读写，寄存器初始化、时钟、
 *       增益/音量映射全部交由组件处理；对外接口保持不变。
 * ChangeLog: Change Notes
 *
 *********************************************************************/

/*********************************************************************
 * INCLUDES
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"

#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"

#include "hal_audio.h"
#include "sys_log.h"

#if SYS_FUNC_AUDIO_EN

/*********************************************************************
 * MACROS
 */
#define AUDIO_TAG                        "HAL_AUDIO"

// 软复位等待时间
#define ES8311_RESET_DELAY_MS             (5)

/*********************************************************************
* TYPEDEFS
*/

/*********************************************************************
 * CONSTANTS
 */

/*********************************************************************
 * LOCAL VARIABLES
 */
static i2c_master_bus_handle_t s_i2c_bus = NULL;
static i2s_chan_handle_t s_tx_chan = NULL;
static i2s_chan_handle_t s_rx_chan = NULL;

static const audio_codec_data_if_t *s_data_if = NULL;   // I2S 数据接口
static const audio_codec_ctrl_if_t *s_ctrl_if = NULL;   // I2C 控制接口
static const audio_codec_gpio_if_t *s_gpio_if = NULL;   // GPIO 接口
static const audio_codec_if_t *s_codec_if = NULL;       // ES8311 编解码接口
static esp_codec_dev_handle_t s_dev = NULL;             // 统一 codec 设备

static bool s_initialized = false;
static bool s_capture_active = false;
static bool s_playback_active = false;
static uint8_t s_volume = AUDIO_DEFAULT_VOLUME;         // 音量 0-100
static uint8_t s_input_gain_db = AUDIO_DEFAULT_INPUT_GAIN_DB; // 输入增益 dB

/*********************************************************************
 * GLOBAL VARIABLES
 */

/*********************************************************************
 * LOCAL FUNCTIONS
 */
static esp_err_t audio_i2c_bus_init(void);
static esp_err_t audio_i2s_channel_init(void);
static esp_err_t audio_codec_create(void);
static void audio_update_device_state(void);

/*********************************************************************
 * GLOBAL FUNCTIONS
 */

/**
 * @brief 创建 I2C 主机总线（供 esp_codec_dev 的控制接口使用），并探测 ES8311
 */
static esp_err_t audio_i2c_bus_init(void)
{
    i2c_master_bus_config_t bus_cfg =
    {
        .i2c_port = AUDIO_I2C_PORT,
        .sda_io_num = AUDIO_I2C_SDA_PIN,
        .scl_io_num = AUDIO_I2C_SCL_PIN,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    esp_err_t ret = i2c_new_master_bus(&bus_cfg, &s_i2c_bus);
    if (ret != ESP_OK)
    {
        sys_loge(AUDIO_TAG, "create i2c bus failed: %d", ret);
        return ret;
    }

    // 探测确认 ES8311 在位
    ret = i2c_master_probe(s_i2c_bus, AUDIO_I2C_ADDR, AUDIO_I2S_TIMEOUT_MS);
    if (ret != ESP_OK)
    {
        sys_loge(AUDIO_TAG, "ES8311 not found (0x%02x): %d", AUDIO_I2C_ADDR, ret);
        i2c_del_master_bus(s_i2c_bus);
        s_i2c_bus = NULL;
        return ret;
    }

    sys_logi(AUDIO_TAG, "i2c bus ready, ES8311 found @0x%02x", AUDIO_I2C_ADDR);
    return ESP_OK;
}

/**
 * @brief 创建 I2S 标准全双工通道（供 esp_codec_dev 的数据接口使用）
 *
 * 初始不 enable 通道：由 esp_codec_dev open/close 统一管理，规避
 * 模块功放常开导致的上电 POP 音。
 */
static esp_err_t audio_i2s_channel_init(void)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(AUDIO_I2S_HOST, I2S_ROLE_MASTER);

    esp_err_t ret = i2s_new_channel(&chan_cfg, &s_tx_chan, &s_rx_chan);
    if (ret != ESP_OK)
    {
        sys_loge(AUDIO_TAG, "i2s new channel failed: %d", ret);
        return ret;
    }

    i2s_std_config_t std_cfg =
    {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg =
        {
            .mclk = AUDIO_I2S_MCLK_PIN,
            .bclk = AUDIO_I2S_SCLK_PIN,
            .ws   = AUDIO_I2S_WS_PIN,
            .dout = AUDIO_I2S_DOUT_PIN,
            .din  = AUDIO_I2S_DIN_PIN,
        },
    };

    ret = i2s_channel_init_std_mode(s_tx_chan, &std_cfg);
    if (ret != ESP_OK)
    {
        sys_loge(AUDIO_TAG, "i2s init std (tx) failed: %d", ret);
        return ret;
    }

    ret = i2s_channel_init_std_mode(s_rx_chan, &std_cfg);
    if (ret != ESP_OK)
    {
        sys_loge(AUDIO_TAG, "i2s init std (rx) failed: %d", ret);
        return ret;
    }

    sys_logi(AUDIO_TAG, "i2s std channel ready");
    return ESP_OK;
}

/**
 * @brief 组装 esp_codec_dev：数据/控制/GPIO 接口 + ES8311 编解码接口
 *
 * 软复位（0x00=0x1F）与寄存器初始化由组件驱动（es8311_codec_new）内部处理。
 */
static esp_err_t audio_codec_create(void)
{
    // 数据接口：I2S 全双工
    audio_codec_i2s_cfg_t i2s_cfg =
    {
        .port = AUDIO_I2S_HOST,
        .rx_handle = s_rx_chan,
        .tx_handle = s_tx_chan,
    };
    s_data_if = audio_codec_new_i2s_data(&i2s_cfg);
    if (s_data_if == NULL)
    {
        sys_loge(AUDIO_TAG, "create i2s data if failed");
        return ESP_ERR_NO_MEM;
    }

    // 控制接口：I2C（共享上面创建的主机总线）
    // 注意：esp_codec_dev 的 audio_codec_i2c_cfg_t.addr 期望 8-bit 地址
    //（内部 >>1 得到 7-bit），而 AUDIO_I2C_ADDR 是 7-bit（0x18），故左移一位传 0x30。
    audio_codec_i2c_cfg_t i2c_cfg =
    {
        .port = AUDIO_I2C_PORT,
        .addr = (AUDIO_I2C_ADDR << 1),
        .bus_handle = s_i2c_bus,
    };
    s_ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    if (s_ctrl_if == NULL)
    {
        sys_loge(AUDIO_TAG, "create i2c ctrl if failed");
        return ESP_ERR_NO_MEM;
    }

    s_gpio_if = audio_codec_new_gpio();
    if (s_gpio_if == NULL)
    {
        sys_loge(AUDIO_TAG, "create gpio if failed");
        return ESP_ERR_NO_MEM;
    }

    // ES8311 编解码接口（寄存器初始化序列在组件内部）
    es8311_codec_cfg_t es8311_cfg = { 0 };
    es8311_cfg.ctrl_if = s_ctrl_if;
    es8311_cfg.gpio_if = s_gpio_if;
    es8311_cfg.codec_mode = ESP_CODEC_DEV_WORK_MODE_BOTH;
    es8311_cfg.pa_pin = GPIO_NUM_NC;                 // 模块无 PA_EN/CODEC_EN 使能脚
    es8311_cfg.use_mclk = true;
    es8311_cfg.hw_gain.pa_voltage = 5.0;             // NS4150B 功放供电 5V
    es8311_cfg.hw_gain.codec_dac_voltage = 3.3;      // ES8311 DAC 供电 3.3V
    es8311_cfg.pa_reverted = false;

    s_codec_if = es8311_codec_new(&es8311_cfg);
    if (s_codec_if == NULL)
    {
        sys_loge(AUDIO_TAG, "create es8311 codec if failed");
        return ESP_ERR_NO_MEM;
    }

    sys_logi(AUDIO_TAG, "esp_codec_dev codec created");
    return ESP_OK;
}

/**
 * @brief 按采集/播放状态动态创建或释放 codec 设备
 *
 * 任一方向激活时创建并 open（设置采样率/位深/声道 + 应用增益与音量），
 * 全部停止时 close 并删除，从而停掉 I2S 时钟，规避常开功放的底噪/POP。
 */
static void audio_update_device_state(void)
{
    bool need_open = (s_capture_active || s_playback_active);

    if (need_open && s_dev == NULL)
    {
        esp_codec_dev_cfg_t dev_cfg =
        {
            .dev_type = ESP_CODEC_DEV_TYPE_IN_OUT,
            .codec_if = s_codec_if,
            .data_if = s_data_if,
        };
        s_dev = esp_codec_dev_new(&dev_cfg);
        if (s_dev == NULL)
        {
            sys_loge(AUDIO_TAG, "esp_codec_dev_new failed");
            return;
        }

        esp_codec_dev_sample_info_t fs =
        {
            .bits_per_sample = 16,
            .channel = 1,
            .channel_mask = 0,
            .sample_rate = (uint32_t)AUDIO_SAMPLE_RATE,
            .mclk_multiple = 0,
        };
        esp_err_t ret = esp_codec_dev_open(s_dev, &fs);
        if (ret != ESP_OK)
        {
            sys_loge(AUDIO_TAG, "esp_codec_dev_open failed: %d", ret);
            esp_codec_dev_delete(s_dev);
            s_dev = NULL;
            return;
        }

        esp_codec_dev_set_in_gain(s_dev, s_input_gain_db);
        esp_codec_dev_set_out_vol(s_dev, s_volume);
    }
    else if (!need_open && s_dev != NULL)
    {
        esp_codec_dev_close(s_dev);
        esp_codec_dev_delete(s_dev);
        s_dev = NULL;
    }
}

esp_err_t hal_audio_init(void)
{
    if (s_initialized)
    {
        return ESP_OK;
    }

    esp_err_t ret = audio_i2c_bus_init();
    if (ret != ESP_OK)
    {
        return ret;
    }

    ret = audio_i2s_channel_init();
    if (ret != ESP_OK)
    {
        return ret;
    }

    ret = audio_codec_create();
    if (ret != ESP_OK)
    {
        return ret;
    }

    s_initialized = true;
    sys_logi(AUDIO_TAG, "audio initialized (esp_codec_dev)");
    return ESP_OK;
}

void hal_audio_deinit(void)
{
    if (!s_initialized)
    {
        return;
    }

    if (s_dev != NULL)
    {
        esp_codec_dev_close(s_dev);
        esp_codec_dev_delete(s_dev);
        s_dev = NULL;
    }
    if (s_codec_if != NULL)
    {
        audio_codec_delete_codec_if(s_codec_if);
        s_codec_if = NULL;
    }
    if (s_gpio_if != NULL)
    {
        audio_codec_delete_gpio_if(s_gpio_if);
        s_gpio_if = NULL;
    }
    if (s_ctrl_if != NULL)
    {
        audio_codec_delete_ctrl_if(s_ctrl_if);
        s_ctrl_if = NULL;
    }
    if (s_data_if != NULL)
    {
        audio_codec_delete_data_if(s_data_if);
        s_data_if = NULL;
    }

    if (s_tx_chan != NULL)
    {
        i2s_del_channel(s_tx_chan);
        s_tx_chan = NULL;
    }
    if (s_rx_chan != NULL)
    {
        i2s_del_channel(s_rx_chan);
        s_rx_chan = NULL;
    }
    if (s_i2c_bus != NULL)
    {
        i2c_del_master_bus(s_i2c_bus);
        s_i2c_bus = NULL;
    }

    s_initialized = false;
    sys_logi(AUDIO_TAG, "audio deinitialized");
}

esp_err_t hal_audio_capture_start(void)
{
    if (!s_initialized)
    {
        return ESP_ERR_INVALID_STATE;
    }
    // 采集与播放互斥：同一时刻单一数据流
    if (s_playback_active)
    {
        return ESP_ERR_INVALID_STATE;
    }
    if (!s_capture_active)
    {
        s_capture_active = true;
        audio_update_device_state();
        if (s_dev == NULL)
        {
            s_capture_active = false;
            return ESP_ERR_INVALID_STATE;
        }

        // 录音时静音 DAC，切断"喇叭→麦克风"声学回授，
        // 避免每次录音把喇叭声音录进去、再次回放/再次录音逐次叠加而成啸叫。
        esp_codec_dev_set_out_mute(s_dev, true);
    }
    return ESP_OK;
}

esp_err_t hal_audio_capture_stop(void)
{
    if (!s_initialized)
    {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_capture_active)
    {
        s_capture_active = false;
        audio_update_device_state();
    }
    return ESP_OK;
}

esp_err_t hal_audio_playback_start(void)
{
    if (!s_initialized)
    {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_capture_active)
    {
        return ESP_ERR_INVALID_STATE;
    }
    if (!s_playback_active)
    {
        s_playback_active = true;
        audio_update_device_state();
        if (s_dev == NULL)
        {
            s_playback_active = false;
            return ESP_ERR_INVALID_STATE;
        }

        // 先输出一段静音帧，稳定 I2S 时钟后再由上层写入有效数据，
        // 规避功放常开导致的 POP 音。
        int16_t silence[32] = { 0 };
        esp_codec_dev_write(s_dev, silence, sizeof(silence));
    }
    return ESP_OK;
}

esp_err_t hal_audio_playback_stop(void)
{
    if (!s_initialized)
    {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_playback_active)
    {
        // 播放结束先输出静音帧再停 DAC
        int16_t silence[16] = { 0 };
        esp_codec_dev_write(s_dev, silence, sizeof(silence));
        s_playback_active = false;
        audio_update_device_state();
    }
    return ESP_OK;
}

int32_t hal_audio_read_stream(int16_t *buf, uint32_t frames)
{
    if (!s_initialized || !s_capture_active || buf == NULL || s_dev == NULL)
    {
        return -1;
    }

    esp_err_t ret = esp_codec_dev_read(s_dev, buf, frames * sizeof(int16_t));
    if (ret != ESP_OK)
    {
        return -1;
    }
    return (int32_t)frames;
}

int32_t hal_audio_write_stream(const int16_t *buf, uint32_t frames)
{
    if (!s_initialized || !s_playback_active || buf == NULL || s_dev == NULL)
    {
        return -1;
    }

    // 音量交由 esp_codec_dev 硬件音量设置，此处原样写入 PCM
    esp_err_t ret = esp_codec_dev_write(s_dev, (void *)buf, frames * sizeof(int16_t));
    if (ret != ESP_OK)
    {
        return -1;
    }
    return (int32_t)frames;
}

void hal_audio_set_input_gain(uint8_t db)
{
    s_input_gain_db = db;
    if (s_initialized && s_dev != NULL)
    {
        esp_codec_dev_set_in_gain(s_dev, db);
    }
    sys_logi(AUDIO_TAG, "input gain set to %d dB", db);
}

void hal_audio_set_volume(uint8_t vol)
{
    if (vol > 100)
    {
        vol = 100;
    }
    s_volume = vol;
    if (s_initialized && s_dev != NULL)
    {
        esp_codec_dev_set_out_vol(s_dev, vol);
    }
    sys_logi(AUDIO_TAG, "volume set to %d", vol);
}

#endif /* SYS_FUNC_AUDIO_EN */
