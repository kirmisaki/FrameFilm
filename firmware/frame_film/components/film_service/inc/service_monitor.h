#ifndef __SERVICE_MONITOR_H__
#define __SERVICE_MONITOR_H__


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
// LED管理参数
#define MONITOR_LED_UPDATE_INTERVAL_MS      (500)    // LED状态更新间隔 500ms

// 电池管理参数
#define MONITOR_BAT_CHECK_INTERVAL_MS       (30000)  // 电池检测间隔 30s
#define MONITOR_BAT_LOW_THRESHOLD           (10)     // 低电量阈值 10%
#define MONITOR_BAT_CRITICAL_THRESHOLD      (5)      // 极低电量阈值 5%

// 自动休眠管理参数
#define MONITOR_SLEEP_CHECK_INTERVAL_MS     (200)    // 休眠检测间隔 200ms
#define MONITOR_AUTO_SLEEP_TIMEOUT_SEC      (180)    // 自动休眠超时时间 3min (180s)


/*********************************************************************
* TYPEDEFS
*/
enum
{
    MSG_LED_MANAGER = 0x01,
    MSG_BATTERY_MANAGER,
    MSG_AUTO_SLEEP_MANAGER,
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

#ifdef __cplusplus
}
#endif

#endif /* __SERVICE_MONITOR_H__ */
