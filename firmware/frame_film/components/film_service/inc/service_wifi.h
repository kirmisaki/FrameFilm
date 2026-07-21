#ifndef __SERVICE_WIFI_H__
#define __SERVICE_WIFI_H__


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
extern void service_wifi_init(void);
extern void service_wifi_deinit(void);
extern void service_wifi_connect(void);
extern void service_wifi_disconnect(void);
extern uint8_t service_wifi_get_connect_status(void);
extern void service_wifi_clear_config(void);


#ifdef __cplusplus
}
#endif

#endif /* __SERVICE_WIFI_H__ */
