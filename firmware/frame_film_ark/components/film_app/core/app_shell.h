#ifndef __APP_SHELL_H__
#define __APP_SHELL_H__

/*********************************************************************
 * INCLUDES
 */
#include <stdint.h>

#include "lvgl.h"

#include "app_interface.h"

/*********************************************************************
 * CPPMIX
 */
#ifdef __cplusplus
extern "C" {
#endif

/*********************************************************************
 * MACROS
 */
/* 外壳尺寸（逻辑竖屏 480x720，与 tools/ui-mockup 的 .status/.foot/.body 一一对应）。
   页面的"正文"排在这两条之间，故对外只暴露边界，不暴露内部实现。 */
#define SHELL_STATUS_H          (30)     // 顶部状态栏高度
#define SHELL_FOOT_H            (28)     // 底部提示行高度
#define SHELL_PAD_X             (14)     // 状态栏/提示行左右内边距
#define SHELL_BODY_PAD_Y        (18)     // 正文上下留白
#define SHELL_BODY_PAD_X        (20)     // 正文左右安全边距

/* 正文可用区域（页面据此排自己的内容） */
#define SHELL_BODY_TOP          (SHELL_STATUS_H + SHELL_BODY_PAD_Y)
#define SHELL_BODY_BOTTOM       (SHELL_FOOT_H + SHELL_BODY_PAD_Y)

/* 状态栏周期刷新间隔（毫秒）。
 * 为什么是 10s 而不是 60s：时间精确到分钟，靠 60s 的定时器去追会在分钟边界上
 * 最多晚一分钟；10s 能把"显示跳分钟"的误差压到 10s 内。
 * 代价由变更检测兜住 —— app_shell_apply() 与时间标签都只在**内容真的变了**时才写控件，
 * 所以实际刷屏频率 ≤ 1 次/分钟（分钟跳一次），不会因为 tick 短而多刷。 */
#define SHELL_TICK_MS           (10000)

/*********************************************************************
* TYPEDEFS
*/
/**
 * @brief 周期刷新钩子（页面自定义"定期要做什么"）
 *
 * 默认（传 NULL）走 app_shell_request_status()：重新采集电量/WiFi/蓝牙。
 * 设置页那种"状态栏数据来自自己的快照"的页面，传自己的回调（发快照同步请求）。
 */
typedef void (*app_shell_tick_cb_t)(void);

/**
 * @brief 状态栏里需要动态刷新的控件
 */
typedef struct {
    lv_obj_t *bat_label;    // "82%"，位于状态栏最右侧
    lv_obj_t *wifi_label;   // "WIFI"（关闭时整项隐藏）
    lv_obj_t *wifi_pip;
    lv_obj_t *bt_label;     // "BT"
    lv_obj_t *bt_pip;
    lv_obj_t *nfc_label;    // "NFC"（关闭时整项隐藏；无 NFC 机型恒隐藏）
    lv_obj_t *nfc_pip;
    lv_obj_t *time_label;   // 居中 "HH:MM"（由 tick 维护）
    lv_obj_t *hint_label;
    lv_obj_t *page_label;

    /* ---- 以下为内部状态，页面不要写 ---- */
    app_status_t last_status;   // 上次已上屏的数据（用于变更检测，避免无谓全帧刷新）
    uint8_t      status_valid;
    char         last_time[6];  // 上次已上屏的 "HH:MM"
    lv_timer_t  *tick;
    app_shell_tick_cb_t tick_cb;
} app_shell_t;

/*********************************************************************
 * GLOBAL FUNCTIONS
 */

/**
 * @brief 建外壳：顶部状态栏 + 底部提示行（正文由页面自己在两者之间排）
 *
 * 只建"壳"，不建正文容器——各页面的正文布局差异较大，硬套一种容器反而更绕。
 * 页面用 SHELL_BODY_TOP / SHELL_BODY_BOTTOM 决定内容的起止。
 *
 * @param root 页面根对象
 * @param hint 底部左侧操作提示（如 "UP/DOWN SELECT   ENTER OPEN"）
 * @param page 底部右侧页面名（如 "MENU"）
 * @param out  输出：状态栏里需要动态刷新的控件（可为 NULL）
 */
void app_shell_build(lv_obj_t *root, const char *hint, const char *page, app_shell_t *out);
void app_shell_set_text(app_shell_t *s, const char *hint, const char *page);

/**
 * @brief 请求刷新状态栏数据（页面 create 时调用一次即可）
 *
 * 上行到 app 任务采集，再由 app_shell_on_event() 回投；页面本身不读服务层。
 */
void app_shell_request_status(void);

/**
 * @brief 启动状态栏周期刷新（电量 / WiFi / 蓝牙 / 居中时间）
 *
 * 电子纸每次上屏都是全帧（约 940ms 且会闪），所以这里**不是"定期重绘"**：
 * tick 只负责"定期取一次最新值"，真正写控件前会比对上次的值（app_shell_apply 与
 * 时间标签都带去重），内容没变就完全不碰 LVGL，也就不会产生任何刷新。
 *
 * 时间由本模块自己算（`localtime_r` 与时钟页同一套，ui_task 读它没有跨任务问题），
 * 不依赖 app 任务回投。
 *
 * @param s  外壳实例（app_shell_build 的输出）
 * @param cb 周期钩子；传 NULL 表示"重新采集状态栏数据"（app_shell_request_status）
 */
void app_shell_start_tick(app_shell_t *s, app_shell_tick_cb_t cb);
void app_shell_stop_tick(app_shell_t *s);
/* 页面显式刷新时可更新时钟；彩色页不启动周期 tick。 */
void app_shell_refresh_time(app_shell_t *s);

/**
 * @brief 释放外壳（页面 destroy 里必须调）
 *
 * 周期定时器不在 root 的对象树里，页面销毁不会自动删；漏了会留下挂在已销毁
 * 控件上的野指针定时器。
 */
void app_shell_release(app_shell_t *s);
/* 共用 logo + 10% 概率彩蛋，仅 ui_task 使用。 */
#define APP_SHELL_BRAND_H 304
lv_obj_t *app_shell_brandmark(lv_obj_t *parent);

/**
 * @brief 把状态栏数据落到控件（页面 on_msg 里调用）
 */
void app_shell_apply(app_shell_t *s, const app_status_t *st);

/**
 * @brief 处理状态栏消息（页面 on_msg 里统一转调）
 *
 * @return 1 已消费；0 与本模块无关（页面继续处理自己的 cmd）
 */
int app_shell_handle_msg(app_shell_t *s, uint32_t cmd, const void *data, uint8_t len);

/**
 * @brief 采集并回投状态栏数据（app 的 on_event 里转调）
 *
 * 采集必须留在 app 任务侧：电池 / g_service_param 没有跨任务保证。
 */
void app_shell_on_event(const app_event_t *e);
/* app_task 侧采集一次状态；用于通行证首帧/手动重载合并刷新。 */
void app_shell_status_snapshot(app_status_t *st);

#ifdef __cplusplus
}
#endif

#endif /* __APP_SHELL_H__ */
