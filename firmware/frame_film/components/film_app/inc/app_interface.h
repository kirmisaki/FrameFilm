#ifndef __APP_INTERFACE_H__
#define __APP_INTERFACE_H__

/*********************************************************************
 * INCLUDES
 */
#include <stdint.h>
#include "hal_input.h"

/*********************************************************************
 * CPPMIX
 */
#ifdef __cplusplus
extern "C" {
#endif

/*********************************************************************
 * MACROS
 */
#define APP_EVENT_PAYLOAD_MAX   (16)    // 事件内联负载上限（与 sys_event 对齐）

/**
 * @brief 按键占用掩码（用于 app_entry_t.keys）
 *
 * 声明式：每个 app 声明自己会消费哪些按键（位序同 input_press_type_t），
 * 简易切换模式（SYS_APP_SWITCH_SIMPLE）下调度器只在 app 未占用的键上做切换，
 * 避免打断 app 自身的按键功能（如图片翻页、动图播放模式切换）。
 */
#define APP_KEY_NONE    (0u)
#define APP_KEY_SHORT   (1u << INPUT_PRESS_SHORT)
#define APP_KEY_LONG    (1u << INPUT_PRESS_LONG)
#define APP_KEY_UP      (1u << INPUT_PRESS_UP)
#define APP_KEY_DOWN    (1u << INPUT_PRESS_DOWN)

#define APP_KEY_IS_BUSY(keys, k)  (((keys) & (1u << (k))) != 0u)

/*********************************************************************
 * TYPEDEFS
 */

/**
 * @brief 应用 ID
 *
 * 统一应用枚举，注册表/调度器/连接端（BLE 0x42 ScreenResolution）均使用。
 */
typedef enum {
    APP_ID_IMAGE = 0,      // 图片显示（本地 TF / BLE / WiFi）
    APP_ID_TEMPLATE,       // 模板显示（蓝牙/WiFi 实时推送内容到缓存显示：天气/日历等）
    APP_ID_CLOCK,          // 时钟
    APP_ID_ANIMATION,      // 动图
    APP_ID_MAX,
} app_id_t;

/**
 * @brief 应用事件类型
 */
typedef enum {
    APP_EVT_INPUT,       // hal_input 回调转发
    APP_EVT_TIMER,       // 定时器 tick（时钟/动图用）
    APP_EVT_SWITCH,      // 内部：请求切换 app（app_manager 消费）
    APP_EVT_SYS,         // 全局事件总线事件（sys_event，按 app_entry.events 过滤）
    APP_EVT_BOOT,        // 启动后仅投递一次给首个 app（开机自动行为，如自动切图/拉取）
} app_evt_type_t;

/**
 * @brief 应用事件
 */
typedef struct {
    app_evt_type_t type;
    input_press_type_t input;   // APP_EVT_INPUT 时有效
    uint32_t cmd;               // APP_EVT_SYS=sys_event_id_t
    void *data;                 // 附加数据（APP_EVT_SWITCH 为 app_id_t 值）
    uint8_t payload[APP_EVENT_PAYLOAD_MAX]; // APP_EVT_SYS 事件负载（值语义）
    uint8_t len;                // payload 有效长度
} app_event_t;

/**
 * @brief 统一 app 接口
 *
 * 每个 app 实现一个 app_entry_t，注册到 app_manager 供调度。
 */
typedef struct {
    app_id_t id;
    const char *name;
    const char *data_dir;   // 该 app 的数据目录（NULL 表示不参与目录切换）
    uint8_t keys;           // APP_KEY_* 掩码：该 app 占用的按键（切换调度需避让）
    uint32_t tick_ms;       // on_tick 周期（毫秒，0 表示不接收 tick）
    const uint16_t *events; // 关注的全局事件 ID 列表（sys_event_id_t，0 结尾）；NULL 表示不订阅
    void (*on_enter)(void);                 // 切到该 app
    void (*on_exit)(void);                  // 离开该 app
    void (*on_event)(const app_event_t *e); // 按键/BLE/网络/下载事件
    void (*on_tick)(void);                  // 周期性刷新（可选，时钟/动图用）

    /* ---- 状态持久化（框架负责 NVS 读写，app 零 NVS 代码） ---- */
    void *state;                 // 指向 app 状态结构体；NULL 或 state_size=0 表示不持久化
    uint16_t state_size;         // 状态结构体大小
    uint8_t state_ver;           // 状态结构体版本（字段变更时 +1，旧数据自动作废）
    const void *state_default;   // load 失败/版本不符时的默认值（同 state_size）

    /* ---- BLE 参数通道（上下隔离） ---- */
    uint8_t param_ch;            // 参数设置通道；查询通道为 param_ch + 1；0 表示无参数通道
    void (*on_param_set)(const uint8_t *tlv, uint8_t len); // BLE 参数设置（TLV 列表，可 NULL）
    uint8_t (*on_param_get)(uint8_t *out, uint8_t max);    // BLE 参数查询，返回写入字节数
} app_entry_t;

/*********************************************************************
 * TLV HELPERS（BLE 参数通道统一约定）
 *
 * payload = [TAG(1B)][LEN(1B)][VALUE(LEN B)] ...
 * 各 app 只解析自己那张 TAG 表：不认识的 TAG 跳过；LEN 不符跳过该项但不整包丢弃。
 * 多字节整数一律 Big-Endian（与 BLE 协议一致）。
 *********************************************************************/
#define APP_TLV_HDR_LEN   (2)   /* TAG + LEN */

/* TLV 项视图 */
typedef struct {
    uint8_t tag;
    uint8_t len;
    const uint8_t *val;
} app_tlv_t;

/**
 * @brief 取 TLV 列表中的下一项
 *
 * @param buf TLV 列表起始
 * @param len 列表总长度
 * @param off 游标（输入当前偏移，成功时输出下一项偏移）
 * @param out 输出项
 * @return 1 成功；0 已到末尾或剩余字节不足以构成一项
 */
static inline int app_tlv_next(const uint8_t *buf, uint8_t len, uint8_t *off, app_tlv_t *out)
{
    uint8_t o = *off;
    if((uint16_t)o + APP_TLV_HDR_LEN > len)
    {
        return 0;
    }
    uint8_t vlen = buf[o + 1];
    if((uint16_t)o + APP_TLV_HDR_LEN + vlen > len)
    {
        return 0;
    }
    out->tag = buf[o];
    out->len = vlen;
    out->val = &buf[o + APP_TLV_HDR_LEN];
    *off = (uint8_t)(o + APP_TLV_HDR_LEN + vlen);
    return 1;
}

static inline uint16_t app_tlv_be16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

static inline uint32_t app_tlv_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

/* 组包：返回写入字节数（out 需保证足够空间） */
static inline uint8_t app_tlv_put_u8(uint8_t *out, uint8_t tag, uint8_t v)
{
    out[0] = tag; out[1] = 1; out[2] = v;
    return 3;
}

static inline uint8_t app_tlv_put_u16(uint8_t *out, uint8_t tag, uint16_t v)
{
    out[0] = tag; out[1] = 2;
    out[2] = (uint8_t)(v >> 8); out[3] = (uint8_t)v;
    return 4;
}

static inline uint8_t app_tlv_put_u32(uint8_t *out, uint8_t tag, uint32_t v)
{
    out[0] = tag; out[1] = 4;
    out[2] = (uint8_t)(v >> 24); out[3] = (uint8_t)(v >> 16);
    out[4] = (uint8_t)(v >> 8);  out[5] = (uint8_t)v;
    return 6;
}

/*********************************************************************
 * CONSTANTS
 */

/*********************************************************************
 * GLOBAL FUNCTIONS
 */

#ifdef __cplusplus
}
#endif

#endif /* __APP_INTERFACE_H__ */
