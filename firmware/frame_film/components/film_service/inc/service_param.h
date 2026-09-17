#ifndef __SERVICE_PARAM_H__
#define __SERVICE_PARAM_H__


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
/* ServiceParam_Def_t 布局版本：1 = 含 film 成员的旧布局；2 = 移除 film + app 参数外置 */
#define SERVICE_PARAM_VER                                          (2)

/* app 状态持久化：app_id 数值必须与 film_app/inc/app_interface.h 的 app_id_t 对齐
 * （service 层看不到 app_id_t，这里用裸数值常量） */
#define SERVICE_PARAM_APP_ID_IMAGE                                 (0)
#define SERVICE_PARAM_APP_ID_TEMPLATE                              (1)
#define SERVICE_PARAM_APP_ID_CLOCK                                 (2)
#define SERVICE_PARAM_APP_ID_ANIMATION                             (3)
#define SERVICE_PARAM_APP_NUM                                      (4)

/* 单个 app 状态 blob 中 app 数据的最大字节数（不含 6 字节头）。
 * 各 app 的状态结构体大小不得超过该值，app 侧以 _Static_assert 自行校验。 */
#define SERVICE_PARAM_APP_DATA_MAX                                 (64)


/*********************************************************************
* TYPEDEFS
*/
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
    uint8_t param_ver;         // 结构体版本，与 nvs 内不一致则重建默认值
    uint8_t factory_flag;
    ServiceSleep_Def_t sleep;
    ServiceNetwork_Def_t network;
    ServiceBle_Def_t ble;
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

/* ---------------- app 状态持久化（blob = 6B 头 + app 数据） ----------------
 * app 侧只声明结构体指针/大小/版本/默认值，NVS 读写全部由这里的接口完成。
 * 校验失败（key 不存在 / magic 不符 / size 或 version 不匹配）一律视为“无数据”，
 * 静默回落到默认值并返回负值，不终止启动。
 *
 * 调用上下文约束：以上接口均无内部锁，依赖“只在 app 任务上下文串行调用”来保证
 * 一致性（框架已把 on_exit / 参数回调 / on_tick 全部收敛到 app 任务）。
 * 请勿在 BLE / 定时器 / 中断等其他任务上下文直接调用，否则会绕过该串行化保证。
 */
extern int service_param_app_load(uint8_t app_id, void *buf, uint16_t size, uint8_t ver);
extern int service_param_app_save(uint8_t app_id, const void *buf, uint16_t size, uint8_t ver);
extern int service_param_app_erase(uint8_t app_id);
extern void service_param_app_erase_all(void);
extern int service_param_app_current_get(void);
extern int service_param_app_current_set(uint8_t app_id);


#ifdef __cplusplus
}
#endif

#endif /* __SERVICE_PARAM_H__ */
