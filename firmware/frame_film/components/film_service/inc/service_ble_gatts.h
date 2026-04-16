#ifndef __service_ble_gatts_H__
#define __service_ble_gatts_H__

#ifdef __cplusplus
extern "C"{
#endif

/*********************************************************************
 * INCLUDES
 */
#include <stdint.h>

/*********************************************************************
 * MACROS
 */
#define GATTS_CH1_NOTIFY_ENABLE_BIT       (uint8_t)(0x01<<0)
#define GATTS_CH2_NOTIFY_ENABLE_BIT       (uint8_t)(0x01<<1)
#define GATTS_CH3_NOTIFY_ENABLE_BIT       (uint8_t)(0x01<<2)

#define BLE_DAFAULT_LATENCY               (0)
#define BLE_DEFAULT_MAX_INT               (0x20)   // max_int = 0x20*1.25ms = 40ms
#define BLE_DEFAULT_MIN_INT               (0x10)
#define BLE_DEFAULT_TIMEOUT               (400)    // timeout = 400*10ms = 4000ms
#define BLE_ADV_MIN_INT                   (0x200)  //最小广播间隔
#define BLE_ADV_MAX_INT                   (0x400)  //最大广播间隔

#define GATTC_GAP_SCAN_SECONDS            (3)      //复联扫描时间
#define GATTC_GAP_START_SCAN_SECONDS      (5)      //开启扫描时间

#define ADV_CONGIG_FLAG                   (1 << 0)
#define SCAN_RSP_CONFIG_FLAG              (1 << 1)

#define BLE_NOTIFY_SEND_CH1               (1)
#define BLE_NOTIFY_SEND_CH2               (2)
#define BLE_NOTIFY_SEND_CH3               (3)


/*********************************************************************
* TYPEDEFS
*/
typedef void (*service_ble_gatts_cmd_cb_t)(uint8_t const *p_data, uint16_t len);

enum
{
    BLE_GATT_MODE_S = 0,       //GATT SERVER
};

enum
{
    BLE_GATTS_DISCONNECT = 0,  //GATTS DISCONNECT
    BLE_GATTS_CONNECT,         //GATTS CONNECT
};

enum
{
    BLE_GATTS_UNINIT = 0,      //GATTS UNINIT
    BLE_GATTS_INIT,            //GATTS INIT
};

/* Attributes State Machine */
enum
{
    IDX_SVC_DEV,
    IDX_CHAR_1,
    IDX_CHAR_VAL_1,

    IDX_CHAR_2,
    IDX_CHAR_VAL_2,

    IDX_CHAR_3,
    IDX_CHAR_VAL_3,

    IDX_CHAR_4,
    IDX_CHAR_VAL_4,

    IDX_CHAR_5,
    IDX_CHAR_VAL_5,

    IDX_CHAR_6,
    IDX_CHAR_VAL_6,

    IDX_CHAR_7,
    IDX_CHAR_VAL_7,

    DEV_IDX_NB,
};

enum
{
    IDX_M_SVC,
    IDX_M_CHAR_1,
    IDX_M_CHAR_VAL_1,
    IDX_M_CHAR_CFG_1,

    IDX_M_CHAR_2,
    IDX_M_CHAR_VAL_2,
    IDX_M_CHAR_CFG_2,

    IDX_M_CHAR_3,
    IDX_M_CHAR_VAL_3,
    IDX_M_CHAR_CFG_3,

    DEV_M_IDX_NB,
};

enum
{
    MSG_BLE_CH1_IN_CMD = 1,
    MSG_BLE_CH1_OUT_DATA,
    MSG_BLE_CH2_OUT_DATA,
    MSG_BLE_CH3_OUT_DATA,
    MSG_BLE_GAP_DISCONNECT,
};


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
extern void service_ble_gatt_server_init(void);
extern void service_ble_gatt_server_uninit(void);
extern void service_ble_gatt_server_reinit(void);
extern void service_ble_gatts_dev_disconnect(void);
extern void service_ble_send_notify_data(uint8_t ch, uint8_t *buf, uint16_t len);
extern void service_ble_gatts_cmd_register_cb(service_ble_gatts_cmd_cb_t cb);
extern uint8_t service_ble_gatts_get_connect(void);


#ifdef __cplusplus
extern "C"}
#endif

#endif /* __service_ble_gatts_H__ */
