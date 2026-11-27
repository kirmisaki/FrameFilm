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
// 本工程为 **FrameFilmInfo 基础工程**（单机型 / 信息屏），由 frame_film 移植而来。
// 硬件为 FrameFilmCommon 主板（ESP32-S3-WROOM-1-N16R8）：
//   屏幕 GDEM075T41WT 7.5" 800×480 黑白 1bpp（U8 单面板 4 线 SPI）
//   输入 单按键 GPIO4（低有效，EXT0 deep sleep 唤醒）
//   LED  WS2812 GPIO5；电池采样 GPIO6 / 采样使能 GPIO7
//   外设电源 3V3_C 使能 GPIO9；TF 卡 4-bit 无检测脚
// 详见 hardware/model/FrameFilmCommon/README.md。
// 工程内不再有 STD/PRO/MAX 机型宏与多屏幕选择，硬件规格按编译期常量固定。

#define SYS_DEVICE_NAME                "FRAMEFILMINFO"
#define SYS_MANUFACTURER_NAME          "FRAMEFILMINFO"

#define SYS_MODEL_NUMBER               "I1.0"
#define SYS_SERIAL_NUMBER              "INFO000001"             //SN号
#define SYS_HAREWARE_VERSION           "H1.0"                   //硬件版本号
#define SYS_FIRMWARE_VERSION           "0.1.0"                  //固件版本号
#define SYS_SYSTEM_ID                  "loveU"

#define SYS_BLE_DEFAULT_KEY            "FRAMEFILM_KEY"

#define SYS_M_NVS_NAMESPACE            "FILMINFO_NVS"           // NVS namespace 上限 15 字符
#define SYS_M_NVS_KEY_NAME             "FILMKEY"

// spiffs
#define BACE_PATH                      "/spiffs"


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