#ifndef __APP_ANIMATION_H__
#define __APP_ANIMATION_H__

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
 * @brief 动图 app 接口实例（独立目录 /sdcard/animation 的多帧循环播放）
 */
extern const app_entry_t g_app_animation_entry;

#ifdef __cplusplus
}
#endif

#endif /* __APP_ANIMATION_H__ */
