#ifndef __SERVICE_BLE_H__
#define __SERVICE_BLE_H__

#ifdef __cplusplus
extern "C"{
#endif

/*********************************************************************
 * INCLUDES
 */
#include <stdbool.h>
#include <stddef.h>
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
#define BLE_FILM_TRANS_CH_CTRL_APP_SWITCH                  (0x4B) // app_id; optional IMAGE basename + NUL (targeted-open response: app_id,status)
#define BLE_FILM_TRANS_CH_CTRL_APP_CURRENT_GET             (0x4C) // 查询当前 app（返回 1字节 app_id）

// 时间同步：4字节大端 Unix 秒（UTC）+ 2字节大端时区（距 UTC 分钟数，东为正，东八区=+480）。
// 设备应用后**回显同样的 6 字节**，连接端据此确认。
// 注：dock 固件的命令表是独立一份（service_cmd.c），未实现此号。
#define BLE_FILM_TRANS_CH_CTRL_TIME_SYNC                   (0x4D) // 时间 + 时区同步

// 远程按键注入（模拟本机按键，让连接端当遥控器用）：1 字节键值，取值见下方
// BLE_KEY_* 。设备把它当作一次真实按键事件投给 app 层（走与 HAL 输入完全相同的
// 通路，因此菜单导航 / 双击退回 / 长按休眠等语义一并生效），**回显同样 1 字节**。
// 注意：不带"按下/抬起"概念 —— 这里发的每个值都是一个已结算的按键事件，
// 与 HAL 上报的粒度一致（长按/双击都由输入层判定完毕后上报一次）。
// 注：dock 固件没有 app 层（按键是 PC 键盘），未实现此号。
#define BLE_FILM_TRANS_CH_CTRL_KEY_INJECT                  (0x4E) // 远程按键注入

// Ark 临时直传：凭据仅在本次 RAM 会话使用，不改持久化 WiFi 配网。
#define BLE_FILM_TRANS_CH_DIRECT_START                     (0x50)
#define BLE_FILM_TRANS_CH_DIRECT_STATUS                    (0x51)
#define BLE_FILM_TRANS_CH_DIRECT_CANCEL                    (0x52)
#define BLE_FILM_TRANS_CH_PROFILE_READ                     (0x53) // Ark 通行证资料 JSON 分块读取
#define BLE_FILM_TRANS_CH_DEVICE_NAME_GET                  (0x54) // 查询已配置完整名称
#define BLE_FILM_TRANS_CH_DEVICE_NAME_SET                  (0x55) // 设置 UTF-8 后缀 + NUL，下次 BLE 初始化生效
#define BLE_FILM_TRANS_CH_BUILD_INFO_GET (0x56)
#define BLE_FILM_TRANS_CH_DIRECT_OTA_START (0x57)
#define BLE_FILM_TRANS_CH_DIRECT_OTA_APPLY (0x58)
#define BLE_FILM_TRANS_CH_CAPABILITIES_GET (0x59) // Ark version + flags: bit0=color pass, bit1=WiFi file, bit2=catalog, bit3=atomic IMAGE open, bit4=SoftAP, bit5=ABC9 preview, bit6=hardware identity, bit7=NFC wallet
#define BLE_FILM_TRANS_CH_STORAGE_CATALOG (0x5B) // Ark read-only storage/catalog
#define BLE_FILM_TRANS_CH_DIRECT_FILE_START (0x5A) // SSID\0PASSWORD\0URL\0相对路径\0
#define BLE_FILM_TRANS_CH_DIRECT_AP_PREPARE (0x5C) // Empty request; NFC_CARD_BEGIN uses a payload
#define BLE_FILM_TRANS_CH_HARDWARE_INFO_GET (0x5D) // Empty request; NFC_CARD_DATA uses a payload
#define BLE_DIRECT_START_DATA_MAX                         (192)

/* NFC 卡包（仅 n16r8）：建卡 0x5C~0x5E、管理 0x5F、封面 0x60~0x62 */
#define BLE_FILM_TRANS_CH_NFC_CARD_BEGIN                  (0x5C)
#define BLE_FILM_TRANS_CH_NFC_CARD_DATA                   (0x5D)
#define BLE_FILM_TRANS_CH_NFC_CARD_COMMIT                 (0x5E)
#define BLE_FILM_TRANS_CH_NFC_WALLET_CTRL                 (0x5F)
#define BLE_FILM_TRANS_CH_NFC_COVER_BEGIN                 (0x60)
#define BLE_FILM_TRANS_CH_NFC_COVER_DATA                  (0x61)
#define BLE_FILM_TRANS_CH_NFC_COVER_COMMIT                (0x62)
#define BLE_NFC_CHUNK_MAX                                 (180)

/* 蓝牙设备名 = 编译期前缀 "FRAMEFILMARK-" (13B) + 可配置后缀 (1..16B)，完整名上限 29B
   （不含结尾 NUL，缓冲 30B）。后缀持久化在 ServiceBle_Def_t.ble_name（service_param.h）。 */
#define BLE_DEVICE_NAME_MAX_BYTES                         (29)

/* KEY_INJECT 的键值。与 input_press_type_t 无关：协议层不依赖 HAL 枚举，
 * 映射在 app 层完成（见 app_manager 的按键注入分支）。
 * 覆盖本机全部按键语义：上下选择 / 确认单击 / 确认双击（退回）/ 确认长按（休眠）。 */
#define BLE_KEY_SHORT                                      (0x00) // 确认键单击
#define BLE_KEY_LONG                                       (0x01) // 确认键长按（= 手动休眠）
#define BLE_KEY_UP                                         (0x02) // 上
#define BLE_KEY_DOWN                                       (0x03) // 下
#define BLE_KEY_DOUBLE                                     (0x04) // 确认键双击（= 退回主菜单）
#define BLE_KEY_BACK                                       (0x05) // 显式返回（n16r8 的 BACK）
#define BLE_KEY_MAX                                        (BLE_KEY_BACK)

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

/**
 * @brief 蓝牙开关：运行期起停协议栈（参考 WiFi 的开关语义）
 *
 * 关：停广播并注销服务；开：重新注册并恢复广播（从未拉起过则整栈初始化）。
 * 与 service_ble_init() 一致：**不开启就不初始化**。
 * 内部会写 g_service_param.ble.ble_enable，但**不落盘**，调用方自行 service_param_save()。
 */
extern void service_ble_apply_enable(uint8_t on);
extern void service_ble_msg_send(void *p_msg, bool in_isr);
extern void service_ble_msg_gatts_cmd_send( uint8_t const *p_data, uint16_t len );
extern void service_ble_transfer_disconnected(void); // GATT 回调中仅标记，不阻塞
/* 入睡屏障：拒收新命令，等待 BLE 任务取消并关闭其文件写入。 */
extern bool service_ble_prepare_sleep(void);
/* Transfer-only stack shutdown. Does not change the saved BLE setting. */
extern bool service_ble_wifi_pause(void);
extern bool service_ble_wifi_resume(void);
extern bool service_ble_wifi_paused(void);
#ifdef ARK_AP_FILE_BENCH
extern bool service_ble_ap_file_bench_stop(void);
#endif
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

/**
 * @brief 加载/刷新当前蓝牙设备名（0x54/0x55 与广播名共用）
 *
 * 读 g_service_param.ble.ble_name 里的后缀；未配置或存值非法时用蓝牙 MAC 派生并写回内存参数。
 * 由 gatts 在蓝牙初始化/重初始化时调用：GATT Device Name(0x2A00) 与广播数据都在那时才建立，
 * 所以 0x55 改名后要重启设备或关开蓝牙才会反映到广播名上。
 */
extern void service_ble_device_name_init(void);

/** @brief 取当前完整设备名（指向静态缓冲，调用方不得释放或修改） */
extern const char *service_ble_device_name_get(void);

/**
 * @brief 设置设备名后缀（UTF-8 + NUL，1..16 字节），校验通过后写参数并落盘
 *
 * @return 0 = 已保存，1 = 参数非法（当前名称与已存参数均不变）
 */
extern uint8_t service_ble_device_name_set(const uint8_t *suffix, size_t payload_len);


#ifdef __cplusplus
extern "C"}
#endif

#endif /* __SERVICE_BLE_H__ */
