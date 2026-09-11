#ifndef __HAL_USB_H__
#define __HAL_USB_H__

/*********************************************************************
 * INCLUDES
 */
#include "esp_err.h"
#include "sys_cfg.h"

#if SYS_FUNC_USB_DEV_EN

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
 * @brief 初始化 USB 设备
 *
 * 按 SYS_FUNC_AUDIO_USB_EN / SYS_FUNC_KEYBOARD_USB_EN 的组合初始化：开声卡时先开启
 * I2S 全双工通道，再初始化 usb_device_uac（麦克风 + 扬声器）；开键盘时初始化 HID
 * 键盘，若未开声卡则由键盘侧自行拉起 USB 设备栈。
 * 插入电脑后系统会枚举出 USB 音频设备 / HID 键盘 / 两者复合设备。
 * 注意：占用 IO19(D-)/IO20(D+)。
 *
 * @return ESP_OK 成功；其它失败
 */
esp_err_t hal_usb_init(void);

#if SYS_FUNC_AUDIO_USB_EN
/* USB 音频流接口号（由 hal_usb.c 定义，UAC composite 模式需提供给 uac_device_init） */
extern const int hal_usb_itf_num_spk;
extern const int hal_usb_itf_num_mic;
#endif /* SYS_FUNC_AUDIO_USB_EN */

#if SYS_FUNC_KEYBOARD_USB_EN
/**
 * @brief 初始化 HID 键盘
 *
 * 与声卡同时开启时，USB 设备栈由 usb_device_uac 初始化，此处仅准备按键上报；
 * 仅开键盘时，此处会自行初始化 USB PHY 与 TinyUSB 任务。
 *
 * @return ESP_OK 成功；其它失败
 */
esp_err_t hal_usb_hid_init(void);

/**
 * @brief 发送一组 HID 键盘按键（支持修饰键与多键组合）
 *
 * 内部发送一次按键报告，并在 HID_KEY_RELEASE_MS 后自动发送空报告释放按键，
 * 形成一次完整的“按下-抬起”。组合键通过 modifier + 多个 keycode 同时按下实现。
 *
 * @param modifier HID 修饰键位掩码（bit0 Ctrl / bit1 Shift / bit2 Alt / bit3 GUI）
 * @param keycode  6 字节 HID 键码（Usage ID），不足的用 0 补齐
 * @return ESP_OK 成功；ESP_ERR_INVALID_STATE 未初始化或 USB 未就绪；ESP_FAIL 发送失败
 */
esp_err_t hal_usb_hid_key_send(uint8_t modifier, const uint8_t keycode[6]);
#endif /* SYS_FUNC_KEYBOARD_USB_EN */

#ifdef __cplusplus
}
#endif

#endif /* SYS_FUNC_USB_DEV_EN */

#endif /* __HAL_USB_H__ */
