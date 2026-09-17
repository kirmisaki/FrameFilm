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
// 机型三选一
// #define FRAMEFILM_STD        1          // 基础版
#define FRAMEFILM_PRO        1          // Pro 版（默认）
// #define FRAMEFILM_MAX        1          // Max 版
#ifndef FRAMEFILM_STD
#define FRAMEFILM_STD        0
#endif
#ifndef FRAMEFILM_PRO
#define FRAMEFILM_PRO        0
#endif
#ifndef FRAMEFILM_MAX
#define FRAMEFILM_MAX        0
#endif
#if (FRAMEFILM_STD + FRAMEFILM_PRO + FRAMEFILM_MAX) != 1
#error "机型配置错误：只能选择一个机型"
#endif

// App 切换交互模式三选一
#define SYS_APP_SWITCH_NONE            0   // 关闭按键切换（BLE 远程切换仍有效，纯相框）
#define SYS_APP_SWITCH_SIMPLE          1   // 简易：图片 <-> 最近推送的 app，上/下回图片，确认键互切
#define SYS_APP_SWITCH_FULL            2   // 全功能：长按进封面菜单（需屏幕支持，否则自动降级为简易模式）
#define SYS_APP_SWITCH_MODE            SYS_APP_SWITCH_FULL
#if (SYS_APP_SWITCH_MODE != SYS_APP_SWITCH_NONE) && \
    (SYS_APP_SWITCH_MODE != SYS_APP_SWITCH_SIMPLE) && \
    (SYS_APP_SWITCH_MODE != SYS_APP_SWITCH_FULL)
#error "App 切换模式配置错误：SYS_APP_SWITCH_MODE 只能取 NONE/SIMPLE/FULL"
#endif

#if FRAMEFILM_STD == 1
#define SYS_DEVICE_NAME                "FRAMEFILM"
#define SYS_MANUFACTURER_NAME          "FRAMEFILM"
#define SYS_INPUT_HAS_NAV_ENTER        1   // 旋转编码器：A/B 导航 + 按键确认
#endif
#if FRAMEFILM_PRO == 1
#define SYS_DEVICE_NAME                "FRAMEFILMPRO"
#define SYS_MANUFACTURER_NAME          "FRAMEFILMPRO"
#define SYS_INPUT_HAS_NAV_ENTER        1   // 三按键：上/下/确认
#endif
#if FRAMEFILM_MAX == 1
#define SYS_DEVICE_NAME                "FRAMEFILMMAX"
#define SYS_MANUFACTURER_NAME          "FRAMEFILMMAX"
#define SYS_INPUT_HAS_NAV_ENTER        1   // 三按键：上/下/确认
#endif

#define SYS_MODEL_NUMBER               "M1.0"
#define SYS_SERIAL_NUMBER              "FILM000001"             //SN号
#define SYS_HAREWARE_VERSION           "H1.0"                   //硬件版本号
#define SYS_FIRMWARE_VERSION           "1.0.0"                  //固件版本号
#define SYS_SYSTEM_ID                  "loveU"

#define SYS_BLE_DEFAULT_KEY            "FRAMEFILM_KEY"

#define SYS_M_NVS_NAMESPACE            "FRAMEFILM_NVS"
#define SYS_M_NVS_KEY_NAME             "FILMKEY"

// app 状态持久化（与整包 ServiceParam_Def_t 隔离，避免改一个 app 状态就重写整包）
#define SYS_M_NVS_APP_NAMESPACE        "FRAMEFILM_APP"    // NVS namespace 上限 15 字符
#define SYS_M_NVS_APP_KEY_CURRENT      "cur_app"          // 框架当前 app id
#define SYS_M_NVS_APP_KEY_PREFIX       "app"              // app 状态 key 前缀：app0 ~ app3

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