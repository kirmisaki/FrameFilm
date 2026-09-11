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
 * Description: USB 设备驱动（USB 声卡 / HID 键盘，可单独或复合）及 USB 描述符
 *              说明：开启 CONFIG_USB_DEVICE_UAC_AS_PART 后 usb_device_uac 组件不再
 *                    提供描述符与 mount/umount 回调，需由本文件自行实现。
 * ChangeLog: Change Notes
 *
 *********************************************************************/

/*********************************************************************
 * INCLUDES
 */
#include <string.h>

#include "sdkconfig.h"
// 注意：tusb.h 必须在 FreeRTOS 头文件之前包含
#include "tusb.h"
#include "uac_descriptors.h"

#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"

#include "sys_cfg.h"
#include "sys_log.h"
#include "hal_usb.h"

#if SYS_FUNC_AUDIO_USB_EN
#include "usb_device_uac.h"
#include "hal_audio.h"
#endif

#if SYS_FUNC_KEYBOARD_USB_EN && !SYS_FUNC_AUDIO_USB_EN
// 仅开键盘时需自行拉起 USB 设备栈（声卡模式下由 usb_device_uac 负责）
#include "freertos/task.h"
#include "esp_private/usb_phy.h"
#endif

#if SYS_FUNC_USB_DEV_EN

/*********************************************************************
 * MACROS
 */
#define USB_TAG                          "HAL_USB"

// USB 设备 VID/PID（与 usb_device_uac 默认配置保持一致）
#define USB_DEVICE_VID                   (0x303A)
#define USB_DEVICE_PID                   (0x8000)

// 端点地址
#if SYS_FUNC_AUDIO_USB_EN
#define EPNUM_AUDIO_OUT                  (0x01)
#define EPNUM_AUDIO_FB                   (0x81)
#define EPNUM_AUDIO_IN                   (0x82)
#endif
#if SYS_FUNC_KEYBOARD_USB_EN
#define EPNUM_HID_IN                     (0x83)
#endif

// 字符串描述符索引
#if SYS_FUNC_KEYBOARD_USB_EN
#define STRIDX_HID                       (4)
#define STRIDX_UAC                       (5)
#else
#define STRIDX_UAC                       (4)
#endif

// 配置描述符总长度
#if SYS_FUNC_KEYBOARD_USB_EN && SYS_FUNC_AUDIO_USB_EN
#define CONFIG_TOTAL_LEN                 (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN + CFG_TUD_AUDIO * TUD_AUDIO_DEVICE_DESC_LEN)
#elif SYS_FUNC_KEYBOARD_USB_EN
#define CONFIG_TOTAL_LEN                 (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN)
#else
#define CONFIG_TOTAL_LEN                 (TUD_CONFIG_DESC_LEN + CFG_TUD_AUDIO * TUD_AUDIO_DEVICE_DESC_LEN)
#endif

// 按键按下后自动释放的时间，形成完整的“按下-抬起”
#define HID_KEY_RELEASE_MS               (30)

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
static uint16_t s_desc_str[32];

#if SYS_FUNC_KEYBOARD_USB_EN
static TimerHandle_t s_hid_release_timer = NULL;

#if !SYS_FUNC_AUDIO_USB_EN
static usb_phy_handle_t s_usb_phy_hdl = NULL;
static bool s_usb_stack_inited = false;
static void usb_device_task(void *arg);
#endif /* !SYS_FUNC_AUDIO_USB_EN */
#endif /* SYS_FUNC_KEYBOARD_USB_EN */

/*********************************************************************
 * GLOBAL VARIABLES
 */
// 接口布局（UAC composite 模式下由工程定义，按功能开关自动排列）
enum {
#if SYS_FUNC_KEYBOARD_USB_EN
    ITF_NUM_HID = 0,
#endif
#if SYS_FUNC_AUDIO_USB_EN
    ITF_NUM_AUDIO_CONTROL,
    ITF_NUM_AUDIO_STREAMING_SPK,
    ITF_NUM_AUDIO_STREAMING_MIC,
#endif
    ITF_NUM_TOTAL
};

#if SYS_FUNC_AUDIO_USB_EN
// 音频流接口号：UAC composite 模式下需传给 uac_device_init
const int hal_usb_itf_num_spk = ITF_NUM_AUDIO_STREAMING_SPK;
const int hal_usb_itf_num_mic = ITF_NUM_AUDIO_STREAMING_MIC;
#endif

//--------------------------------------------------------------------+
// Device Descriptors
//--------------------------------------------------------------------+
tusb_desc_device_t const desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,

    // 组合了 UAC 时需要 IAD（复合设备）；仅 HID 时类信息在接口描述符中定义
#if SYS_FUNC_AUDIO_USB_EN
    .bDeviceClass       = TUSB_CLASS_MISC,
    .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol    = MISC_PROTOCOL_IAD,
#else
    .bDeviceClass       = TUSB_CLASS_UNSPECIFIED,
    .bDeviceSubClass    = 0,
    .bDeviceProtocol    = 0,
#endif
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,

    .idVendor           = USB_DEVICE_VID,
    .idProduct          = USB_DEVICE_PID,
    .bcdDevice          = 0x0100,

    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,

    .bNumConfigurations = 0x01
};

//--------------------------------------------------------------------+
// HID Report Descriptor
//--------------------------------------------------------------------+
// 注意：CFG_TUD_HID 恒为使能（UAC 组件源码依赖），HID 报告描述符与 HID 类回调
//       必须无条件提供；HID 接口是否出现在配置描述符中由 SYS_FUNC_KEYBOARD_USB_EN 决定。
uint8_t const desc_hid_report[] = {
    TUD_HID_REPORT_DESC_KEYBOARD()
};

//--------------------------------------------------------------------+
// Configuration Descriptor
//--------------------------------------------------------------------+
uint8_t const desc_configuration[] = {
    // 配置号, 接口数, 字符串索引, 总长度, 属性, 供电电流(mA)
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),
#if SYS_FUNC_KEYBOARD_USB_EN
    // 接口号, 字符串索引, 启动协议(键盘=1), 报告描述符长度, IN端点, 端点大小, 轮询间隔(ms)
    TUD_HID_DESCRIPTOR(ITF_NUM_HID, STRIDX_HID, 1, sizeof(desc_hid_report), EPNUM_HID_IN, 8, 10),
#endif
#if SYS_FUNC_AUDIO_USB_EN
    // 接口号, 字符串索引, EP Out & EP In 地址, 反馈端点
    TUD_AUDIO_DESCRIPTOR(ITF_NUM_AUDIO_CONTROL, STRIDX_UAC, EPNUM_AUDIO_OUT, EPNUM_AUDIO_IN, EPNUM_AUDIO_FB),
#endif
};

//--------------------------------------------------------------------+
// String Descriptors
//--------------------------------------------------------------------+
char const *string_desc_arr [] = {
    (const char[]) { 0x09, 0x04 },  // 0: 语言 ID（英语 0x0409）
    SYS_MANUFACTURER_NAME,          // 1: 厂商
    SYS_DEVICE_NAME,                // 2: 产品
    SYS_SERIAL_NUMBER,              // 3: 序列号
#if SYS_FUNC_KEYBOARD_USB_EN
    "keyboard",                     // 4: HID 接口
#endif
#if SYS_FUNC_AUDIO_USB_EN
    "usb uac",                      // 5/4: UAC 控制接口
#if SPEAK_CHANNEL_NUM
    "speaker",                      // 6/5: 扬声器接口
#endif
#if MIC_CHANNEL_NUM
    "microphone",                   // 7/6: 麦克风接口
#endif
#endif /* SYS_FUNC_AUDIO_USB_EN */
};

/*********************************************************************
 * LOCAL FUNCTIONS
 */
#if SYS_FUNC_AUDIO_USB_EN
static esp_err_t usb_uac_output_cb(uint8_t *buf, size_t len, void *cb_ctx);
static esp_err_t usb_uac_input_cb(uint8_t *buf, size_t len, size_t *bytes_read, void *cb_ctx);
static void usb_uac_set_mute_cb(uint32_t mute, void *cb_ctx);
static void usb_uac_set_volume_cb(uint32_t volume, void *cb_ctx);
#endif /* SYS_FUNC_AUDIO_USB_EN */

#if SYS_FUNC_KEYBOARD_USB_EN
static void hid_key_release_cb(TimerHandle_t xTimer);
#endif

/*********************************************************************
 * GLOBAL FUNCTIONS
 */
uint8_t const *tud_descriptor_device_cb(void)
{
    return (uint8_t const *)&desc_device;
}

uint8_t const *tud_descriptor_configuration_cb(uint8_t index)
{
    (void)index;
    return desc_configuration;
}

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    (void)langid;

    uint8_t chr_count;

    if (index == 0)
    {
        memcpy(&s_desc_str[1], string_desc_arr[0], 2);
        chr_count = 1;
    }
    else
    {
        if (!(index < sizeof(string_desc_arr) / sizeof(string_desc_arr[0])))
        {
            return NULL;
        }

        const char *str = string_desc_arr[index];

        chr_count = (uint8_t) strlen(str);
        if (chr_count > 31)
        {
            chr_count = 31;
        }

        for (uint8_t i = 0; i < chr_count; i++)
        {
            s_desc_str[1 + i] = str[i];
        }
    }

    s_desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));

    return s_desc_str;
}

//--------------------------------------------------------------------+
// Device Callbacks
//--------------------------------------------------------------------+
void tud_mount_cb(void)
{
    sys_logi(USB_TAG, "usb mounted");
}

void tud_umount_cb(void)
{
    sys_logi(USB_TAG, "usb unmounted");
}

void tud_suspend_cb(bool remote_wakeup_en)
{
    (void)remote_wakeup_en;
    sys_logi(USB_TAG, "usb suspended");
}

void tud_resume_cb(void)
{
    sys_logi(USB_TAG, "usb resumed");
}

//--------------------------------------------------------------------+
// HID Callbacks
//--------------------------------------------------------------------+
uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance)
{
    (void)instance;
    return desc_hid_report;
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t *buffer, uint16_t reqlen)
{
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)reqlen;
    return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t const *buffer, uint16_t bufsize)
{
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)bufsize;
}

#if SYS_FUNC_AUDIO_USB_EN

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

#endif /* SYS_FUNC_AUDIO_USB_EN */

#if SYS_FUNC_KEYBOARD_USB_EN

#if !SYS_FUNC_AUDIO_USB_EN
static void usb_device_task(void *arg)
{
    (void)arg;
    while (1)
    {
        tud_task();
    }
}
#endif /* !SYS_FUNC_AUDIO_USB_EN */

static void hid_key_release_cb(TimerHandle_t xTimer)
{
    (void)xTimer;

    uint8_t keycode[6] = { 0 };
    if (tud_hid_ready())
    {
        tud_hid_keyboard_report(0, 0, keycode);
    }
}

esp_err_t hal_usb_hid_init(void)
{
    if (s_hid_release_timer != NULL)
    {
        return ESP_OK;
    }

#if !SYS_FUNC_AUDIO_USB_EN
    // 仅键盘模式：自行初始化 USB PHY 与 TinyUSB 任务
    if (!s_usb_stack_inited)
    {
        usb_phy_config_t phy_conf = {
            .controller = USB_PHY_CTRL_OTG,
            .otg_mode = USB_OTG_MODE_DEVICE,
            .target = USB_PHY_TARGET_INT,
#if CONFIG_TINYUSB_RHPORT_HS
            .otg_speed = USB_PHY_SPEED_HIGH,
#endif
        };
        if (usb_new_phy(&phy_conf, &s_usb_phy_hdl) != ESP_OK)
        {
            sys_loge(USB_TAG, "usb phy init failed");
            return ESP_FAIL;
        }

        if (!tusb_init())
        {
            sys_loge(USB_TAG, "tinyusb init failed");
            return ESP_FAIL;
        }

        if (xTaskCreatePinnedToCore(usb_device_task, "TinyUSB", 4096, NULL,
                                    CONFIG_UAC_TINYUSB_TASK_PRIORITY, NULL, tskNO_AFFINITY) != pdPASS)
        {
            sys_loge(USB_TAG, "create tinyusb task failed");
            return ESP_FAIL;
        }
        s_usb_stack_inited = true;
    }
#endif /* !SYS_FUNC_AUDIO_USB_EN */

    s_hid_release_timer = xTimerCreate("hid_rel", pdMS_TO_TICKS(HID_KEY_RELEASE_MS), pdFALSE, NULL, hid_key_release_cb);
    if (s_hid_release_timer == NULL)
    {
        sys_loge(USB_TAG, "create hid release timer failed");
        return ESP_FAIL;
    }

    sys_logi(USB_TAG, "hid keyboard initialized");
    return ESP_OK;
}

esp_err_t hal_usb_hid_key_send(uint8_t modifier, const uint8_t keycode[6])
{
    if (s_hid_release_timer == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }

    if (!tud_hid_ready())
    {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t keys[6];
    memcpy(keys, keycode, sizeof(keys));

    if (!tud_hid_keyboard_report(0, modifier, keys))
    {
        return ESP_FAIL;
    }

    // 延时自动释放按键，形成一次完整的“按下-抬起”
    xTimerReset(s_hid_release_timer, 0);
    return ESP_OK;
}

#endif /* SYS_FUNC_KEYBOARD_USB_EN */

esp_err_t hal_usb_init(void)
{
    if (s_usb_initialized)
    {
        return ESP_OK;
    }

#if SYS_FUNC_AUDIO_USB_EN
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

#if CONFIG_USB_DEVICE_UAC_AS_PART
    // 复合设备模式下需显式告知 UAC 音频流接口号
    config.spk_itf_num = hal_usb_itf_num_spk;
    config.mic_itf_num = hal_usb_itf_num_mic;
#endif

    ret = uac_device_init(&config);
    if (ret != ESP_OK)
    {
        sys_loge(USB_TAG, "uac device init failed: %d", ret);
        hal_audio_duplex_stop();
        return ret;
    }
#endif /* SYS_FUNC_AUDIO_USB_EN */

#if SYS_FUNC_KEYBOARD_USB_EN
    // HID 键盘：与声卡共用同一 USB 设备栈；未开声卡时由该接口自行拉起设备栈
    esp_err_t hid_ret = hal_usb_hid_init();
    if (hid_ret != ESP_OK)
    {
        sys_logw(USB_TAG, "hid keyboard init failed: %d", hid_ret);
    }
#endif /* SYS_FUNC_KEYBOARD_USB_EN */

    s_usb_initialized = true;
    sys_logi(USB_TAG, "usb device initialized");
    return ESP_OK;
}

#endif /* SYS_FUNC_USB_DEV_EN */
