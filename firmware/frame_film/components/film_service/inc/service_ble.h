#ifndef __SERVICE_BLE_H__
#define __SERVICE_BLE_H__

#ifdef __cplusplus
extern "C"{
#endif

/*********************************************************************
 * INCLUDES
 */
#include <stdbool.h>
#include <stdint.h>

/*********************************************************************
 * MACROS
 */
#define SYS_OS_PRI_BLE_TASK            (8)
#define SYS_OS_SIZE_BLE_TASK           (4096)
#define SYS_OS_NAME_BLE_TASK           "ble_task"

/*********************************************************************
* TYPEDEFS
*/
typedef struct
{
    uint8_t ID;
    uint8_t subID;
    uint8_t len;
    uint8_t *pdata;
} ble_msg_t;

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
extern void service_ble_init(void);
extern void service_ble_msg_send(void *p_msg, bool in_isr);
extern void service_ble_msg_gatts_cmd_send( uint8_t const *p_data, uint16_t len );
extern void service_ble_msg_gatts_data_send( uint8_t const *p_data, uint16_t len, uint8_t ch);


#ifdef __cplusplus
extern "C"}
#endif

#endif /* __SERVICE_BLE_H__ */
