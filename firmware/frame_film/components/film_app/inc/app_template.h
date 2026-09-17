#ifndef __APP_TEMPLATE_H__
#define __APP_TEMPLATE_H__

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
 * @brief 模板 app 接口实例（通用实时推送显示：蓝牙/WiFi 推送 film 到缓存并整屏显示）
 */
extern const app_entry_t g_app_template_entry;

#ifdef __cplusplus
}
#endif

#endif /* __APP_TEMPLATE_H__ */
