#ifndef __SERVICE_CMD_H__
#define __SERVICE_CMD_H__

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
#define CMD_TAG                        "cmd_service"

#define SYS_OS_PRI_CMD_TASK            (7)
#define SYS_OS_SIZE_CMD_TASK           (4096)
#define SYS_OS_NAME_CMD_TASK           "cmd_task"

// 命令帧：HEAD(1) | CH(1) | LEN(1) | DATA(LEN) | SUM(1)，SUM 为前面所有字节之和
// 说明：BLE 与 USB(CDC) 共用同一套命令，解析与业务处理与具体链路无关
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

#define BLE_FILM_TRANS_CH_CTRL_FILM_HEARTBEAT_URL          (0x3E) // HTTP心跳地址设置
#define BLE_FILM_TRANS_CH_CTRL_FILM_HEARTBEAT_URL_GET      (0x3F) // HTTP心跳地址查询
#define BLE_FILM_TRANS_CH_CTRL_FILM_HEARTBEAT_INTERVAL     (0x40) // 心跳间隔设置（1字节，5-180秒）
#define BLE_FILM_TRANS_CH_CTRL_FILM_HEARTBEAT_INTERVAL_GET (0x41) // 心跳间隔查询
#define BLE_FILM_TRANS_CH_CTRL_SCREEN_RESOLUTION_GET       (0x42) // 屏幕面板ID与分辨率查询
// USB HID 键盘键值（单击/双击/长按）
#define BLE_FILM_TRANS_CH_CTRL_KEYBOARD_KEY_SET            (0x43) // 设置键值
#define BLE_FILM_TRANS_CH_CTRL_KEYBOARD_KEY_GET            (0x44) // 查询键值

/*********************************************************************
* TYPEDEFS
*/
// 命令来源链路（回包按来源原路返回）
typedef enum
{
    SERVICE_CMD_SRC_BLE = 0,
    SERVICE_CMD_SRC_USB,
    SERVICE_CMD_SRC_MAX,
} service_cmd_src_t;

// 输出回调：把已组好的应答帧从 src 链路发出
typedef void (*service_cmd_out_cb_t)(service_cmd_src_t src, const uint8_t *p_data, uint16_t len);

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
/**
 * @brief 初始化命令服务
 *
 * 创建命令解析任务与消息队列。需在注册各链路输出回调之前调用。
 */
extern void service_cmd_init(void);

/**
 * @brief 注册链路输出回调
 *
 * @param src   命令来源链路
 * @param cb    输出回调（把应答帧写回该链路），NULL 表示注销
 */
extern void service_cmd_src_register(service_cmd_src_t src, service_cmd_out_cb_t cb);

/**
 * @brief 向命令服务投递原始字节流
 *
 * 可在任意任务上下文调用（内部仅做拷贝入队）。字节流允许半包/粘包，
 * 由命令任务按帧头、长度与校验和完成组帧后分发。
 *
 * @param src   命令来源链路
 * @param p_data 原始数据
 * @param len   数据长度
 */
extern void service_cmd_input(service_cmd_src_t src, const uint8_t *p_data, uint16_t len);

/**
 * @brief 从指定链路发送一帧应答数据
 *
 * @param src   目标链路
 * @param p_data 已组好的完整帧（含帧头与校验和）
 * @param len   帧长度
 */
extern void service_cmd_send(service_cmd_src_t src, const uint8_t *p_data, uint16_t len);


#ifdef __cplusplus
}
#endif

#endif /* __SERVICE_CMD_H__ */
