#ifndef __SERVICE_SCREEN_H__
#define __SERVICE_SCREEN_H__

/*********************************************************************
 * INCLUDES
 */
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*********************************************************************
 * GLOBAL FUNCTIONS
 */

/** 初始化屏幕薄客户端（任务、缓存、按键、首次同步） */
void service_screen_init(void);

/** 请求同步；force_full=true 时强制向服务端请求全量 */
void service_screen_request_sync(bool force_full);

/** 下一页 / 上一页 */
void service_screen_next_page(void);
void service_screen_prev_page(void);

/** 是否正在拉取/上屏（休眠调度用） */
bool service_screen_is_busy(void);

/** 当前页面 / 版本（BLE 查询用） */
uint16_t service_screen_get_page(void);
uint32_t service_screen_get_version(void);

#ifdef __cplusplus
}
#endif

#endif /* __SERVICE_SCREEN_H__ */
