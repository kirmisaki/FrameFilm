#ifndef __SERVICE_OTA_H__
#define __SERVICE_OTA_H__


/*********************************************************************
 * INCLUDES
 */
#include <stdint.h>
#include "esp_partition.h"

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
    OTA_STATE_IDLE = 0,
    OTA_STATE_STARTED,
    OTA_STATE_RECEIVING,
    OTA_STATE_COMPLETED,
    OTA_STATE_FAILED,
} ota_state_t;

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
extern void service_ota_start(void);
extern void service_set_length(uint32_t len);
extern void service_ota_write(uint8_t *data, uint16_t len);
extern void service_ota_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* __SERVICE_OTA_H__ */