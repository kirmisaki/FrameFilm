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

#define BLE_CMD_HEAD                   (0x55)

// 通道定义
// FILM文件传输 传输逻辑 START->FILENAME->FILELEN->FILEDATA-> STOP->END
#define BLE_FILM_TRANS_CH_FILE_NAME                    (0x00)
#define BLE_FILM_TRANS_CH_FILE_LEN                     (0x01)
#define BLE_FILM_TRANS_CH_FILE_DATA                    (0x02)
#define BLE_FILM_TRANS_CH_FILE_START                   (0x03)
#define BLE_FILM_TRANS_CH_FILE_STOP                    (0x04)
// FILM文件管理               
#define BLE_FILM_TRANS_CH_FILE_DELETE                  (0x05) // 删除id对应的文件
#define BLE_FILM_TRANS_CH_FILE_LIST                    (0x06) // 查询文件列表
#define BLE_FILM_TRANS_CH_FILE_DISPLAY                 (0x07) // 显示id对应的文件
// OTA               
#define BLE_FILM_TRANS_CH_OTA_LEN                      (0x10)
#define BLE_FILM_TRANS_CH_OTA_DATA                     (0x11)
#define BLE_FILM_TRANS_CH_OTA_START                    (0x12)
#define BLE_FILM_TRANS_CH_OTA_STOP                     (0x13)
// FILM控制               
#define BLE_FILM_TRANS_CH_CTRL_MODE                    (0x20) // Film模式切换 手动/自动
#define BLE_FILM_TRANS_CH_CTRL_RESET                   (0x21) // 重置设备到出厂
#define BLE_FILM_TRANS_CH_CTRL_PWRREAD                 (0x22) // 获取电量
#define BLE_FILM_TRANS_CH_CTRL_REBOOT                  (0x23) // 重启设备
#define BLE_FILM_TRANS_CH_CTRL_SLEEPONOFF              (0x24) // 休眠模式开关
#define BLE_FILM_TRANS_CH_CTRL_SLEEPMODE               (0x25) // 休眠模式 定时唤醒开关
#define BLE_FILM_TRANS_CH_CTRL_SLEEPMODE_TIME          (0x26) // 定时唤醒开关时间（单位分钟）

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

// 数据包构成 1byte 头 1byte 通道 1byte 数据长度 nbyte 数据(数据长度) 1byte 校验和(和校验)
typedef struct
{
    uint8_t ch;          //通道
    uint8_t len;         //数据长度
    uint8_t *pdata;
} ble_cmd_t;

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
