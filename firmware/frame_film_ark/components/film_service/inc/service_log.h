#ifndef __SERVICE_LOG_H__
#define __SERVICE_LOG_H__

#ifdef __cplusplus
extern "C"{
#endif

/*********************************************************************
 * INCLUDES
 */
#include <stdint.h>
#include <stdbool.h>

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
 * GLOBAL FUNCTIONS
 */

/**
 * @brief 实时日志流开关（蓝牙终端用）
 *
 * 关闭时零开销：不安装 esp_log 钩子、不建任务、不分配缓冲；日志完全走原有
 * UART 输出。打开时安装钩子（日志同时进环形缓冲 + UART），由后台任务按 BLE
 * notify 分帧推给连接端；**不改变全局日志级别**，只采集当前已启用的日志。
 *
 * @param on 0=关闭 1=打开
 */
extern void service_log_stream_set(uint8_t on);

/**
 * @brief 查询当前是否处于日志流模式
 *
 * @return 1=打开 0=关闭
 */
extern uint8_t service_log_stream_get(void);

#ifdef __cplusplus
}
#endif

#endif /* __SERVICE_LOG_H__ */
