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
/* ServiceParam_Def_t 布局版本：1 = 含 film 成员的旧布局；2 = 移除 film + app 参数外置；
   3 = 加入 tz_min（时区，随 BLE 0x4D 时间同步下发）；
   4 = 移除 ble_mode（"手动打开"策略从未实现，蓝牙现在只有 ble_enable 一个开关）；
   5 = sys 分组收拢（factory_flag / tz_min 移入）+ LED 开关改三态（led_enable → led_mode）；
   6 = 蓝牙名后缀并入 ble 分组（取消独立 NVS key ble_name） */
#define SERVICE_PARAM_VER                                          (6)

/* app 状态持久化：app_id 数值必须与 film_app/inc/app_interface.h 的 app_id_t 对齐
 * （service 层看不到 app_id_t，这里用裸数值常量）。
 * 追加新 app 时**只能往后加**：这些 id 是 NVS key（"app<id>"）的一部分，
 * 中间插入会让老设备上已落盘的数据被当成别的 app 的状态读出来。 */
#define SERVICE_PARAM_APP_ID_IMAGE                                 (0)
#define SERVICE_PARAM_APP_ID_TEMPLATE                              (1)
#define SERVICE_PARAM_APP_ID_CLOCK                                 (2)
#define SERVICE_PARAM_APP_ID_ANIMATION                             (3)
#define SERVICE_PARAM_APP_ID_SETTINGS                              (4)
#define SERVICE_PARAM_APP_ID_MENU                                  (5)
#define SERVICE_PARAM_APP_ID_PASS                                  (6)
/* 保留槽位：不属于任何 app，放 app 层自己的参数（当前是"开机行为"，见 app_boot_cfg）。
 * 借用同一套 blob 持久化（magic/size/version 校验 + "app<id>" key），复用现成基础设施。 */
#define SERVICE_PARAM_APP_ID_BOOT_CFG                              (7)
/* 保留槽位：NFC 总开关（卡包模块使用，见 app_wallet.c）。
 * 不放进 ServiceParam_Def_t —— 改结构体布局会触发整体重置，连带清掉 WiFi 配网与时区。 */
#define SERVICE_PARAM_APP_ID_NFC_CFG                               (8)
#define SERVICE_PARAM_APP_NUM                                      (9)

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

/* 蓝牙名后缀缓冲（不含 "FRAMEFILMARK-" 前缀）：1..16 字节 UTF-8 + NUL（缓冲 17B）。
   前缀 13B + 后缀 16B = 29B，与 service_ble.h 的 BLE_DEVICE_NAME_MAX_BYTES 对齐。 */
#define SERVICE_BLE_NAME_SUFFIX_MAX                                (16)

typedef struct
{
    uint8_t ble_enable;       // BLE开关 0：关闭 1：开启
    char ble_name[SERVICE_BLE_NAME_SUFFIX_MAX + 1]; // 蓝牙名后缀；空串 = 未配置，由蓝牙 MAC 派生默认名
} ServiceBle_Def_t;

/* LED 模式（ServiceSys_Def_t.led_mode）。
   候选顺序即设置页候选表顺序（"LED" 行），改这里要同步 app_settings.c。 */
#define SERVICE_LED_MODE_ALWAYS    (0)   // 常亮：一直呼吸
#define SERVICE_LED_MODE_OFF       (1)   // 关闭：不亮
#define SERVICE_LED_MODE_AUTO      (2)   // 闲置自动熄灭：无操作超时后熄灯，见 service_monitor.c
#define SERVICE_LED_MODE_NUM       (3)

typedef struct
{
    uint8_t led_mode;          // LED 模式：SERVICE_LED_MODE_*
    uint8_t factory_flag;
    int16_t tz_min;            // 本地时区（距 UTC 的分钟数，东为正：东八区 = +480，0 = UTC）
                                // 由连接端随"时间同步"下发（BLE 0x4D），落盘以免重启后丢
} ServiceSys_Def_t;

#pragma pack(4)
typedef struct
{
    uint8_t param_ver;         // 结构体版本，与 nvs 内不一致则重建默认值
    ServiceSys_Def_t sys;
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
