#ifndef __APP_INTERFACE_H__
#define __APP_INTERFACE_H__

/*********************************************************************
 * INCLUDES
 */
#include <stdint.h>
#include <stdbool.h>
#include "hal_input.h"
#include "sys_cfg.h"
#include "ui_core.h"    /* app_ui_ops_t 前向声明 + ui_core_* 声明（本身不含 lvgl） */

/*********************************************************************
 * CPPMIX
 */
#ifdef __cplusplus
extern "C" {
#endif

/*********************************************************************
 * MACROS
 */
#define APP_EVENT_PAYLOAD_MAX   (192)    // 事件内联负载上限（与 sys_event 对齐）

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
    APP_ID_SETTINGS,       // 系统设置（UI 层：设备信息 + 系统参数）
    APP_ID_MENU,           // 主菜单（UI 层：调度器的"根"状态，本身不在轮播列表里）
    APP_ID_PASS,           // 通行证（SD 个人档案，UI 层显示）
                           // ⚠ 追加在末尾而不是插进中间：这些 id 会落盘
                           // （service_param_app_current_set / app 参数 blob），
                           // 中间插入会让老设备上的 id 含义整体错位
    APP_ID_WALLET,         // 卡包（NFC，仅 n16r8；UI 层显示）
    APP_ID_MAX,
} app_id_t;

/**
 * @brief 主菜单轮播的条目数
 *
 * 轮播 = 5 个 app 卡片（图片 / 通行证 / 模板 / 时钟 / 动图）+ 系统设置；
 * APP_ID_MENU 自身不是可选项（它就是菜单）。
 * 显示顺序由 app_manager 的 m_menu_entries 定义，UI 页面侧的视觉表需与其一致。
 */
#if (SYS_NFC_ENABLE == 1)
#define APP_MENU_ENTRY_NUM      (7)
#else
#define APP_MENU_ENTRY_NUM      (6)
#endif

/**
 * @brief 显示层归属
 *
 * app 层内有两个互斥的显示层，由各 app 在 app_entry_t.layer 声明：
 * - DIRECT：直接显示层，app 自己把整帧交给 hal_epd 显示（图片/模板/动图）
 * - UI：UI 框架层，由 film_ui 承载 LVGL 页面（时钟及未来 UI app）
 *
 * 进入 DIRECT 层 app 时 UI 层会被挂起（释放面板），反之亦然。
 */
typedef enum {
    APP_LAYER_DIRECT = 0,  // 默认层，可省略不写
    APP_LAYER_UI,
} app_layer_t;

/**
 * @brief 休眠后画面
 *
 * 每个 app 声明自己睡下去以后屏上留什么。手动长按、闲置自动休眠、低电入睡
 * 三条路径都按此字段裁决：
 * - CARD：改画休眠卡（默认；菜单 / 模板 / 时钟 / 设置 / 通行证 / 卡包）
 * - KEEP：就着当前画面入睡（图片 / 动图 —— 屏上那幅内容本来就是用户要留的）
 *
 * 新增 app 不写即默认画卡；要改睡后表现只改自己的 app_entry_t，不用动调度器。
 */
typedef enum {
    APP_SLEEP_FACE_CARD = 0,   // 画休眠卡（默认）
    APP_SLEEP_FACE_KEEP,       // 保持当前画面
} app_sleep_face_t;

/**
 * @brief 应用事件类型
 */
typedef enum {
    APP_EVT_INPUT,       // hal_input 回调转发
    APP_EVT_TIMER,       // 定时器 tick（时钟/动图用）
    APP_EVT_SWITCH,      // 内部：请求切换 app（app_manager 消费）
    APP_EVT_SYS,         // 全局事件总线事件（sys_event，按 app_entry.events 过滤）
    APP_EVT_BOOT,        // 启动后仅投递一次给首个 app（开机自动行为，如自动切图/拉取）
    APP_EVT_UI_MSG,      // UI 页面在 ui_task 发来的请求（cmd + payload），由 app 在 app_task 消费
} app_evt_type_t;

/**
 * @brief UI 页面 <-> app 任务 的消息约定
 *
 * 上行（页面在 ui_task -> app 任务）：app_manager_post_ui_msg()，经 APP_EVT_UI_MSG 投递，
 * 由当前 app 的 on_event 消费 —— 设置页写参数必须走这条路，不能直接在 ui_task 调
 * service_param_*（该模块无内部锁，依赖"只在 app 任务串行调用"）。
 *
 * 下行（app 任务 -> 页面）：ui_core_post()，由页面的 ui_ops->on_msg 接收。
 */
/**
 * @brief 壳层状态栏数据（各 UI 页顶部状态栏共用）
 *
 * 由 app 在 app_task 侧采集（电池 / 参数服务），经 ui_core_post(APP_UI_MSG_STATUS)
 * 下发给页面；页面只渲染，不碰服务层。
 */
typedef struct {
    uint8_t bat_pct;    // 电量 0~100
    uint8_t wifi_on;    // WiFi 开关 0/1（协议栈是否已起）
    uint8_t wifi_conn;  // WiFi 是否已连上 0/1（wifi_on 为 0 时无意义）
    uint8_t bt_on;      // 蓝牙开关 0/1
    uint8_t bt_conn;    // 蓝牙是否已连接 0/1（bt_on 为 0 时无意义）
    uint8_t nfc_on;     // NFC 开关 0/1（无 NFC 的机型恒 0）
    uint8_t nfc_emu;    // NFC 正在模拟卡片 0/1
} app_status_t;

#define APP_UI_REQ_SETTINGS_APPLY   (0x01)  // 上行：payload = [row(1)][value(1)]
#define APP_UI_REQ_SETTINGS_SYNC    (0x02)  // 上行：无负载，请求下发起始快照（页面 create 时发出）
#define APP_UI_REQ_STATUS_SYNC      (0x03)  // 上行：无负载，请求下发状态栏数据（页面 create 时发出）
#define APP_UI_REQ_BOOT_DONE        (0x04)  // 上行：无负载，开机页进度走完，可以进主菜单了
#define APP_UI_MSG_MENU_SEL         (0x11)  // 下行：payload = [选中索引(1)]
#define APP_UI_MSG_SETTINGS_SNAPSHOT (0x13) // 下行：payload = settings_snapshot_t
#define APP_UI_MSG_STATUS           (0x14)  // 下行：payload = app_status_t
#define APP_UI_MSG_MENU_NOTICE      (0x15)  // 下行：payload = ASCII 提示文本（无 '\0'）；空负载 = 清除

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

    /**
     * @brief 进入前预检（可选，NULL 表示恒可进入）
     *
     * 返回 NULL 表示此刻可以进入；返回一段**静态**文案表示进不去（如动图没有可用帧）。
     * app_manager 会放弃这次切换、留在原页面，并把文案作为提示显示在主菜单上 ——
     * 否则用户看到"选了某个 app，画面却没变"，会误以为已经进去了。
     *
     * 调用点已在 app_do_switch() 里切好目标 app 的 data_dir 之后，故实现可以直接
     * 用 service_file_get_count() 判断"这个目录里有没有内容"。
     */
    const char *(*enter_block_reason)(void);

    /* Optional atomic open: resolve a basename after data_dir is ready, before
       stopping the old app. Keep the one-shot target outside persisted state.
       false must leave app state unchanged. Only IMAGE currently supports it. */
    bool (*prepare_open)(const char *filename);

    /* ---- 显示层归属 ---- */
    app_layer_t layer;              // 跑在哪一层；默认 APP_LAYER_DIRECT
    const app_ui_ops_t *ui_ops;     // layer == APP_LAYER_UI 时必填（页面契约）
    /* UI 层语义差异（由 app_manager 保证）：
     *   on_enter/on_exit  → 不调用，由 ui_ops->create/destroy 取代
     *   on_tick/tick_ms   → 不调用，页面的周期行为用页面内的 lv_timer 承担
     *   on_event          → 仍调用，但运行在 app_task，禁止触碰 lv_*，需用 ui_core_post() 投递
     *   keys              → 仅简易切换模式（SIMPLE）用其做按键避让，声明哪些键归页面自己
     */

    /* ---- 休眠后画面 ---- */
    app_sleep_face_t sleep_face;    // 默认 APP_SLEEP_FACE_CARD（省略不写即画休眠卡）

    /* ---- 状态持久化（框架负责 NVS 读写，app 零 NVS 代码） ---- */
    void *state;                 // 指向 app 状态结构体；NULL 或 state_size=0 表示不持久化
    uint16_t state_size;         // 状态结构体大小
    uint8_t state_ver;           // 状态结构体版本（字段变更时 +1，旧数据自动作废）
    const void *state_default;   // load 失败/版本不符时的默认值（同 state_size）

    /* ---- BLE 参数通道（上下隔离） ---- */
    uint8_t param_ch;            // 参数设置通道；查询通道为 param_ch + 1；0 表示无参数通道
    void (*on_param_set)(const uint8_t *tlv, uint8_t len); // BLE 参数设置（TLV 列表，可 NULL）
    uint8_t (*on_param_get)(uint8_t *out, uint8_t max);    // BLE 参数查询，返回写入字节数

    /* ---- 可选导航钩子（app_task 上下文） ----
     * on_back  ：BACK 键先交给 app；返回 true 表示已消费，false 交回框架回主菜单。
     * on_double：确认键双击先交给 app；返回 true 表示已消费。
     * 只有 n16r8 的卡包实现；其它 app 留 NULL。 */
    bool (*on_back)(void);
    bool (*on_double)(void);
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
