#ifndef __SERVICE_MONITOR_H__
#define __SERVICE_MONITOR_H__

#include <stdbool.h>


/*********************************************************************
 * INCLUDES
 */
#include <stdint.h>

/*********************************************************************
 * CPPMIX
 */
#ifdef __cplusplus
extern "C" {
#endif

/*********************************************************************
 * MACROS
 */


/*********************************************************************
* TYPEDEFS
*/
enum
{
    MSG_LED_MANAGER = 0x01,
    MSG_BATTERY_MANAGER,
    MSG_AUTO_SLEEP_MANAGER,
    MSG_ENTER_SLEEP,        // 手动休眠（主菜单长按）：不经过休眠开关判定，直接进低功耗
};

typedef struct
{
    uint8_t ID;
} monitor_msg_t;

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
extern void service_monitor_init(void);

/**
 * @brief 请求立即进入低功耗（手动休眠）
 *
 * 只投一条消息，真正的 deinit + deep sleep 在 monitor 任务里做（那里是既有的、
 * 已验证过的入睡路径）。与自动休眠不同，**不看休眠模式开关** —— 用户明确按下的
 * 动作就该执行；定时唤醒参数仍按既有规则生效（sleep_auto && sleep_time > 0）。
 *
 * monitor 在卸载外设之前再次检查唤醒脚；仍按下则保留请求并等待释放，
 * 同时等待进行中的直传取消与恢复完成，避免刚入睡就被同一次长按唤醒。
 */
extern void service_monitor_request_sleep(void);
/* 入睡已请求时不再接受新的临时文件传输。 */
extern bool service_monitor_sleep_pending(void);

/* app 层注册：返回 true 仅表示 app 与 UI 已同步停止输出。 */
typedef bool (*service_monitor_sleep_prepare_cb_t)(void);
extern void service_monitor_set_sleep_prepare_cb(service_monitor_sleep_prepare_cb_t cb);

/**
 * @brief 设置"无操作多久后自动休眠"（秒）
 *
 * 由设置页从持久化参数载入；覆盖上电时按唤醒原因给的默认值（手动唤醒 60s /
 * 定时唤醒 30s），设置后两者统一使用该值。取值会被钳在 [10s, 3600s]。
 */
extern void service_monitor_set_auto_sleep_timeout(uint32_t sec);

#ifdef __cplusplus
}
#endif

#endif /* __SERVICE_MONITOR_H__ */
