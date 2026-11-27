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
typedef enum {
    WIFI_DOWNLOAD_IDLE = 0,
    WIFI_DOWNLOAD_DOWNLOADING,
    WIFI_DOWNLOAD_DONE,
    WIFI_DOWNLOAD_ERROR
} wifi_download_state_t;


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

extern void service_wifi_download_start(void);
extern void service_wifi_download_url(const char *url);
extern uint8_t service_wifi_download_get_progress(void);
extern wifi_download_state_t service_wifi_download_get_state(void);

extern void service_wifi_heartbeat_start(void);


#ifdef __cplusplus
}
#endif

#endif /* __SERVICE_WIFI_H__ */
