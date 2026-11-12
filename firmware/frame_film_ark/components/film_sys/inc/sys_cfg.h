#ifndef __SYS_CFG_H__
#define __SYS_CFG_H__

#ifdef __cplusplus
extern "C" {
#endif

/*********************************************************************
 * INCLUDES
 */
#include "sdkconfig.h"


/*********************************************************************
 * MACROS
 */
// SYS CONFIG
// 本固件为**单机型**（FRAMEFILMARK / 通行证版）：
// 屏幕固定 E6 3.70" 720×480、输入为三按键（上6/下4/确认5，低有效），
// n16r8 变体额外有 GPIO18 返回键与 ST25R3916 NFC（见 CONFIG_ARK_*）；
// 有电池检测 / SD 卡检测 / WS2812 LED、唤醒脚 GPIO5（低电平）。
// 老的三机型（STD/PRO/MAX）与屏幕切换分支已删除，代码里不再出现 FRAMEFILM_*_MODEL 宏。
// 需要多机型请用 firmware/frame_film/（保留了三机型支持的那套）。

// NFC 只在 n16r8 上存在；n4r2 没有 NFC，开关派生为 0。
#if CONFIG_ARK_HAS_NFC
#define SYS_NFC_ENABLE                 1
#else
#define SYS_NFC_ENABLE                 0
#endif


// App 切换交互模式三选一
#define SYS_APP_SWITCH_NONE            0   // 关闭按键切换（BLE 远程切换仍有效，纯相框）
#define SYS_APP_SWITCH_SIMPLE          1   // 简易：图片 <-> 最近推送的 app，上/下回图片，确认键互切
#define SYS_APP_SWITCH_FULL            2   // 全功能：主菜单（上电落在主菜单，其余 app 双击退出回它；需 UI 层支持，否则自动降级为简易模式）
#define SYS_APP_SWITCH_MODE            SYS_APP_SWITCH_FULL
#if (SYS_APP_SWITCH_MODE != SYS_APP_SWITCH_NONE) && \
    (SYS_APP_SWITCH_MODE != SYS_APP_SWITCH_SIMPLE) && \
    (SYS_APP_SWITCH_MODE != SYS_APP_SWITCH_FULL)
#error "App 切换模式配置错误：SYS_APP_SWITCH_MODE 只能取 NONE/SIMPLE/FULL"
#endif

// UI 框架层（LVGL）编译开关：0 = 整层裁掉（ui_core_* 退化为空实现，LVGL 不参与链接）
// 运行期还会再按面板能力判定（需 MonoFast，即 3.7" 屏）；两者都满足 UI 层才真正可用。
#define SYS_UI_ENABLE                  1

#define SYS_DEVICE_NAME                "FRAMEFILMARK"
#define SYS_MANUFACTURER_NAME          "FRAMEFILMARK"
#define SYS_INPUT_HAS_NAV_ENTER        1   // 三按键：上/下/确认

#define SYS_MODEL_NUMBER               "M1.0"
#define SYS_SERIAL_NUMBER              "FILM000001"             //SN号
#define SYS_HAREWARE_VERSION           "H1.0"                   //硬件版本号
#define SYS_FIRMWARE_VERSION           "3.2.6"                  //固件版本号
#define SYS_SYSTEM_ID                  "loveU"

#define SYS_BLE_DEFAULT_KEY            "FRAMEFILM_KEY"

#define SYS_M_NVS_NAMESPACE            "FRAMEFILM_NVS"
#define SYS_M_NVS_KEY_NAME             "FILMKEY"

// app 状态持久化（与整包 ServiceParam_Def_t 隔离，避免改一个 app 状态就重写整包）
#define SYS_M_NVS_APP_NAMESPACE        "FRAMEFILM_APP"    // NVS namespace 上限 15 字符
#define SYS_M_NVS_APP_KEY_CURRENT      "cur_app"          // 框架当前 app id
#define SYS_M_NVS_APP_KEY_PREFIX       "app"              // app 状态 key 前缀：app0 ~ app5

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
