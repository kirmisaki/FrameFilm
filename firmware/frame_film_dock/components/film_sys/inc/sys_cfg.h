#ifndef __SYS_CFG_H__
#define __SYS_CFG_H__

#ifdef __cplusplus
extern "C" {
#endif

/*********************************************************************
 * INCLUDES
 */


/*********************************************************************
 * MACROS
 */
// SYS CONFIG
#define SYS_DEVICE_NAME                "FRAMEFILMDOCK"
#define SYS_MANUFACTURER_NAME          "FRAMEFILMDOCK"

#define SYS_MODEL_NUMBER               "M1.0"
#define SYS_SERIAL_NUMBER              "FILM000001"             //SN号
#define SYS_HAREWARE_VERSION           "H1.0"                   //硬件版本号
#define SYS_FIRMWARE_VERSION           "1.0.0"                  //固件版本号
#define SYS_SYSTEM_ID                  "loveU"

#define SYS_BLE_DEFAULT_KEY            "FRAMEFILM_KEY"

#define SYS_M_NVS_NAMESPACE            "FRAMEFILM_NVS"
#define SYS_M_NVS_KEY_NAME             "FILMKEY"

// spiffs
#define BACE_PATH                      "/spiffs"

// 功能开关：音频模块（ES8311+NS4150B CODEC，dock 可选外设）
// 置 1：启用音频（采集/播放）；置 0：关闭（无音频模块的 dock 节省资源）
#define SYS_FUNC_AUDIO_EN             (0)
#if SYS_FUNC_AUDIO_EN
// 功能开关：USB 声卡（插入电脑后作为 USB 麦克风 + 扬声器）
// 置 1：启用（依赖 SYS_FUNC_AUDIO_EN，复用 IO19/IO20）
// 置 0：关闭
#define SYS_FUNC_AUDIO_USB_EN          (1)
#else
#define SYS_FUNC_AUDIO_USB_EN          (0)
#endif /* SYS_FUNC_AUDIO_EN */
// USB键盘功能开关，可将dock的按键变成一个pc的hid键盘设备
// 置 1：启用；置 0：关闭
// 说明：与 USB 声卡开关相互独立，可单独开启。
//       两者同时开启时组成 HID + UAC 复合设备；仅开键盘时 USB 只枚举出一个 HID 键盘。
#define SYS_FUNC_KEYBOARD_USB_EN       (0)

// USB虚拟串口(CDC-ACM)功能开关，供上位机配置设备与传输film文件
// 置 1：启用；置 0：关闭
// 说明：与 USB 声卡/键盘开关相互独立，可单独开启，也可组成 HID + UAC + CDC 复合设备。
#define SYS_FUNC_USB_CDC_EN            (0)

// USB 设备栈总开关（声卡/键盘/虚拟串口任一开启即需要，置 0 表示完全不使用 USB 设备）
#define SYS_FUNC_USB_DEV_EN           (SYS_FUNC_AUDIO_USB_EN || SYS_FUNC_KEYBOARD_USB_EN || SYS_FUNC_USB_CDC_EN)

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


#ifdef __cplusplus
extern "C"
}
#endif

#endif /* __SYS_CFG_H__ */