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
