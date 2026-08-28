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
#define BLE_CMD_LEN_MIN                (4)

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
#define BLE_FILM_TRANS_CH_FILE_DISPLAY_GET             (0x08) // 查询当前显示的文件
// OTA               
#define BLE_FILM_TRANS_CH_OTA_LEN                      (0x10)
#define BLE_FILM_TRANS_CH_OTA_DATA                     (0x11)
#define BLE_FILM_TRANS_CH_OTA_START                    (0x12)
#define BLE_FILM_TRANS_CH_OTA_STOP                     (0x13)
// FILM控制               
#define BLE_FILM_TRANS_CH_CTRL_MODE                    (0x20) // Film模式切换 （0：手动，1：自动）
#define BLE_FILM_TRANS_CH_CTRL_MODE_GET                (0x21) // Film模式查询 （0：手动，1：自动）
#define BLE_FILM_TRANS_CH_CTRL_RESET                   (0x22) // 重置设备到出厂
#define BLE_FILM_TRANS_CH_CTRL_PWRREAD                 (0x23) // 获取电量
#define BLE_FILM_TRANS_CH_CTRL_REBOOT                  (0x24) // 重启设备
#define BLE_FILM_TRANS_CH_CTRL_SLEEPONOFF              (0x25) // 休眠模式开关设置
#define BLE_FILM_TRANS_CH_CTRL_SLEEPONOFF_GET          (0x26) // 休眠模式开关查询
#define BLE_FILM_TRANS_CH_CTRL_SLEEPMODE               (0x27) // 休眠模式 定时唤醒开关设置
#define BLE_FILM_TRANS_CH_CTRL_SLEEPMODE_GET           (0x28) // 休眠模式 定时唤醒开关查询
#define BLE_FILM_TRANS_CH_CTRL_SLEEPMODE_TIME          (0x29) // 定时唤醒开关时间设置（单位分钟）
#define BLE_FILM_TRANS_CH_CTRL_SLEEPMODE_TIME_GET      (0x2A) // 定时唤醒开关时间查询（单位分钟）
#define BLE_FILM_TRANS_CH_CTRL_SDRESET                 (0x2B) // SD卡格式化
// 网络控制
#define BLE_FILM_TRANS_CH_CTRL_WIFI_ENABLE             (0x30) // WiFi开关设置
#define BLE_FILM_TRANS_CH_CTRL_WIFI_ENABLE_GET         (0x31) // WiFi开关查询
#define BLE_FILM_TRANS_CH_CTRL_WIFI_SSID               (0x32) // WiFi SSID设置
#define BLE_FILM_TRANS_CH_CTRL_WIFI_SSID_GET           (0x33) // WiFi SSID查询
#define BLE_FILM_TRANS_CH_CTRL_WIFI_PASSWORD           (0x34) // WiFi 密码设置
#define BLE_FILM_TRANS_CH_CTRL_WIFI_PASSWORD_GET       (0x35) // WiFi 密码查询
#define BLE_FILM_TRANS_CH_CTRL_FILM_API_URL            (0x36) // HTTP下载film文件的API地址设置
#define BLE_FILM_TRANS_CH_CTRL_FILM_API_URL_GET        (0x37) // HTTP下载film文件的API地址查询
#define BLE_FILM_TRANS_CH_CTRL_WIFI_CONNECT            (0x38) // 连接WiFi
#define BLE_FILM_TRANS_CH_CTRL_WIFI_DISCONNECT         (0x39) // 断开WiFi连接
#define BLE_FILM_TRANS_CH_CTRL_WIFI_CONNECT_GET        (0x3A) // 查询WiFi连接状态 0：未连接 1：已连接
#define BLE_FILM_TRANS_CH_CTRL_WIFI_CLEAR              (0x3B) // 清除网络配置信息
#define BLE_FILM_TRANS_CH_CTRL_FILM_DOWNLOAD           (0x3C) // 开始下载film文件
#define BLE_FILM_TRANS_CH_CTRL_FILM_DOWNLOAD_STATE     (0x3D) // 查询下载状态

#define BLE_FILM_TRANS_CH_CTRL_FILM_HEARTBEAT_URL      (0x3E) // HTTP心跳地址设置
#define BLE_FILM_TRANS_CH_CTRL_FILM_HEARTBEAT_URL_GET  (0x3F) // HTTP心跳地址查询
#define BLE_FILM_TRANS_CH_CTRL_FILM_HEARTBEAT_INTERVAL     (0x40) // 心跳间隔设置（1字节，5-180秒）
#define BLE_FILM_TRANS_CH_CTRL_FILM_HEARTBEAT_INTERVAL_GET (0x41) // 心跳间隔查询
#define BLE_FILM_TRANS_CH_CTRL_SCREEN_RESOLUTION_GET       (0x42) // 屏幕分辨率查询（宽2字节+高2字节，大端）

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
    uint8_t sum;         //校验和
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
