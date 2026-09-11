#ifndef __SERVICE_PARAM_H__
#define __SERVICE_PARAM_H__


/*********************************************************************
 * INCLUDES
 */


/*********************************************************************
 * CPPMIX
 */
#ifdef __cplusplus
extern "C" {
#endif

/*********************************************************************
 * MACROS
 */
#define SERVICE_KEY_MAX_NUM            (6)   // 单次组合键最多同时按下的键数


/*********************************************************************
* TYPEDEFS
*/

// USB HID 键盘键值事件（与单击/双击/长按一一对应）
typedef enum
{
    SERVICE_KEY_EVENT_SHORT = 0,   // 单击
    SERVICE_KEY_EVENT_DOUBLE,      // 双击
    SERVICE_KEY_EVENT_LONG,        // 长按
    SERVICE_KEY_EVENT_MAX,
} service_key_event_t;

// USB HID 键盘单组键值（单键或组合键）
typedef struct
{
    uint8_t modifier;                     // HID 修饰键位掩码（bit0 Ctrl / bit1 Shift / bit2 Alt / bit3 GUI）
    uint8_t key_num;                      // 有效键码数量 0 - SERVICE_KEY_MAX_NUM
    uint8_t keycode[SERVICE_KEY_MAX_NUM]; // HID 键码（Usage ID），组合键时多个键同时按下
} ServiceKey_Def_t;

// USB HID 键盘键值配置（单击/双击/长按各一组）
typedef struct
{
    ServiceKey_Def_t short_press;   // 单击键值
    ServiceKey_Def_t double_press;  // 双击键值
    ServiceKey_Def_t long_press;    // 长按键值
} ServiceKeyCfg_Def_t;

typedef struct
{
    uint32_t current_file_id;  // 当前显示的文件ID
    uint8_t load_complete;     // 加载完成标志
    uint8_t play_mode;         // FILM模式（0：手动，1：本地切换 2.网络拉取）
} ServiceFilm_Def_t;

typedef struct
{
    uint8_t sleep_mode;        // 休眠模式开关 0：关闭 1：开启
    uint8_t sleep_auto;        // 定时唤醒开关 0：关闭 1：开启
    uint16_t sleep_time;       // 定时唤醒时间（单位分钟 10min - 48h(48*60min)）
} ServiceSleep_Def_t;

typedef struct
{
    uint8_t wifi_enable;       // WiFi开关 0：关闭 1：开启
    uint8_t film_heartbeat_interval; // 心跳间隔（单位秒 5s - 180s）
    char wifi_ssid[64];        // WiFi SSID（最大63字符）
    char wifi_password[64];    // WiFi 密码（最大63字符）
    char film_api_url[128];    // HTTP下载film文件的API地址
    char film_heartbeat_url[128]; // HTTP心跳地址，用于检查服务是否正常以及是否发起film下载
    char film_device_id[32];   // 设备唯一ID（MAC地址派生，与服务端 device_id 对应）
    char film_token[64];       // 服务端注册token（首次心跳下发，持久化避免认领后失联）
} ServiceNetwork_Def_t;

typedef struct
{
    uint8_t ble_enable;       // BLE开关 0：关闭 1：开启
    uint8_t ble_mode;         // BLE模式 0：常开 1：手动打开（休眠按键双击）
} ServiceBle_Def_t;

#pragma pack(4)
typedef struct
{
    uint8_t factory_flag;
    ServiceFilm_Def_t film;
    ServiceSleep_Def_t sleep;
    ServiceNetwork_Def_t network;
    ServiceBle_Def_t ble;
    ServiceKeyCfg_Def_t key;   // USB HID 键盘键值（单击/双击/长按）
} ServiceParam_Def_t; /*服务参数*/
#pragma pack()

/*********************************************************************
 * CONSTANTS
 */


/*********************************************************************
 * LOCAL VARIABLES
 */


/*********************************************************************
 * GLOBAL VARIABLES
 */
extern ServiceParam_Def_t g_service_param;

/*********************************************************************
 * LOCAL FUNCTIONS
 */


/*********************************************************************
 * GLOBAL FUNCTIONS
 */
extern void service_param_init(void);
extern void service_param_save(void);
extern void service_param_reset(void);
extern void service_param_ensure_device_id(void);


#ifdef __cplusplus
}
#endif

#endif /* __SERVICE_PARAM_H__ */
