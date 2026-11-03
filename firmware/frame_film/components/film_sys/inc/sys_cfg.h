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
// 机型三选一：优先使用构建参数 -DFRAMEFILM_MODEL=std|pro|max（见 CMakePresets.json / build.ps1），
// 未指定时使用下面手改的默认值（直接 idf.py build 走这一分支）
#if defined(FRAMEFILM_MODEL_STD) || defined(FRAMEFILM_MODEL_PRO) || defined(FRAMEFILM_MODEL_MAX)
#if defined(FRAMEFILM_MODEL_STD)
#define FRAMEFILM_STD        1
#else
#define FRAMEFILM_STD        0
#endif
#if defined(FRAMEFILM_MODEL_PRO)
#define FRAMEFILM_PRO        1
#else
#define FRAMEFILM_PRO        0
#endif
#if defined(FRAMEFILM_MODEL_MAX)
#define FRAMEFILM_MAX        1
#else
#define FRAMEFILM_MAX        0
#endif
#else
#define FRAMEFILM_STD        0          // 基础版
#define FRAMEFILM_PRO        1          // Pro 版（默认）
#define FRAMEFILM_MAX        0          // Max 版
#endif
#if (FRAMEFILM_STD + FRAMEFILM_PRO + FRAMEFILM_MAX) != 1
#error "机型配置错误：只能选择一个机型"
#endif

#if FRAMEFILM_STD == 1
#define SYS_DEVICE_NAME                "FRAMEFILM"
#define SYS_MANUFACTURER_NAME          "FRAMEFILM"
#endif
#if FRAMEFILM_PRO == 1
#define SYS_DEVICE_NAME                "FRAMEFILMPRO"
#define SYS_MANUFACTURER_NAME          "FRAMEFILMPRO"
#endif
#if FRAMEFILM_MAX == 1
#define SYS_DEVICE_NAME                "FRAMEFILMMAX"
#define SYS_MANUFACTURER_NAME          "FRAMEFILMMAX"
#endif

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