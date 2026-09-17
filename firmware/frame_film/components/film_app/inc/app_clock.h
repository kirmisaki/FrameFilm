#ifndef __APP_CLOCK_H__
#define __APP_CLOCK_H__

/*********************************************************************
 * INCLUDES
 */
#include "app_interface.h"

/*********************************************************************
 * CPPMIX
 */
#ifdef __cplusplus
extern "C" {
#endif

/*********************************************************************
 * GLOBAL VARIABLES
 */

/**
 * @brief 时钟 app 接口实例（设备端时间，MonoFast 差分局刷，WiFi/蓝牙校时 + RTC 兜底）
 */
extern const app_entry_t g_app_clock_entry;

#ifdef __cplusplus
}
#endif

#endif /* __APP_CLOCK_H__ */
