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
// 0x20 / 0x21 已废弃：播放模式语义已下移到图片 app 参数通道（0x45），
// 命令号按“命令值一旦定义不再变更”原则保留，收到后忽略/回 0xFF
#define BLE_FILM_TRANS_CH_CTRL_MODE                    (0x20) // [已废弃] Film模式切换
#define BLE_FILM_TRANS_CH_CTRL_MODE_GET                (0x21) // [已废弃] Film模式查询
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

// 注意：0x43 / 0x44 归 dock 底座固件（USB HID 键盘键值设置/查询，见 frame_film_dock 的
// service_cmd.h），冰箱贴固件不使用这两个命令号。app 控制通道排在 app 参数通道之后，
// 使 0x45~0x4C 成为连续的 app 通道区间。
#define BLE_FILM_TRANS_CH_CTRL_APP_SWITCH                  (0x4B) // 切换 app（1字节 app_id，app 层消费）
#define BLE_FILM_TRANS_CH_CTRL_APP_CURRENT_GET             (0x4C) // 查询当前 app（返回 1字节 app_id）

// app 参数通道（0x45~0x4A）：payload 为 TLV 列表，BLE 层不解析语义，只整包上浮给 app 层
// 约定：设置通道 = param_ch，查询通道 = param_ch + 1
#define BLE_FILM_TRANS_CH_APP_IMAGE_PARAM                  (0x45) // 图片 app 参数设置
#define BLE_FILM_TRANS_CH_APP_IMAGE_PARAM_GET              (0x46) // 图片 app 参数查询
#define BLE_FILM_TRANS_CH_APP_TEMPLATE_PARAM               (0x47) // 模板 app 参数设置
#define BLE_FILM_TRANS_CH_APP_TEMPLATE_PARAM_GET           (0x48) // 模板 app 参数查询
#define BLE_FILM_TRANS_CH_APP_ANIM_PARAM                   (0x49) // 动图 app 参数设置
#define BLE_FILM_TRANS_CH_APP_ANIM_PARAM_GET               (0x4A) // 动图 app 参数查询

#define BLE_APP_PARAM_CH_FIRST                             (BLE_FILM_TRANS_CH_APP_IMAGE_PARAM)
#define BLE_APP_PARAM_CH_LAST                              (BLE_FILM_TRANS_CH_APP_ANIM_PARAM_GET)

/*********************************************************************
* TYPEDEFS
*/

/**
 * @brief 当前 app 查询回调（由 app 层注册）
 *
 * @return 当前 app_id（未注册时返回 0xFF）
 */
typedef uint8_t (*service_ble_app_id_get_cb_t)(void);

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

/**
 * @brief 按 BLE 帧格式回发一包数据（内部拼 0x55 / CH / LEN / 校验和）
 *
 * @param ch   通道号
 * @param data 数据负载，可为 NULL
 * @param len  数据长度，超过上限自动截断
 */
extern void service_ble_send_resp(uint8_t ch, const uint8_t *data, uint8_t len);

/**
 * @brief 注册当前 app 查询回调
 *
 * 供 APP_CURRENT_GET 命令回包使用。
 *
 * @param cb 回调函数指针（NULL 取消注册）
 */
extern void service_ble_set_app_id_get_cb(service_ble_app_id_get_cb_t cb);


#ifdef __cplusplus
extern "C"}
#endif

#endif /* __SERVICE_BLE_H__ */
