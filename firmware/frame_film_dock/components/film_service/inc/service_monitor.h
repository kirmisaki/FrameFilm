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
};

// LED工作模式
typedef enum
{
    MONITOR_LED_MODE_NORMAL = 0,   // 常亮
    MONITOR_LED_MODE_REFRESH = 1,  // EPD刷新中，LED闪烁
} monitor_led_mode_t;

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

// 设置EPD刷新状态（1=刷新中LED闪烁，0=刷新完成常亮）
extern void service_monitor_set_film_refresh_state(uint8_t refreshing);

#ifdef __cplusplus
}
#endif

#endif /* __SERVICE_MONITOR_H__ */
