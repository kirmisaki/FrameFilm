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


/*********************************************************************
* TYPEDEFS
*/
enum
{
    MSG_LED_MANAGER = 0x01,
    MSG_BATTERY_MANAGER,
    MSG_AUTO_SLEEP_MANAGER,
    MSG_SLEEP_REQUEST,
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
 * @brief 请求进入低功耗（屏幕同步完成后由 service_screen 调用）
 *
 * 仅在休眠模式开启、BLE 未连接、屏幕服务空闲时生效。
 */
extern void service_monitor_request_sleep(void);

#ifdef __cplusplus
}
#endif

#endif /* __SERVICE_MONITOR_H__ */
