#ifndef __UI_DISPLAY_H__
#define __UI_DISPLAY_H__

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
 * GLOBAL FUNCTIONS
 */

/**
 * @brief 申请显示资源（延迟申请，须在 ui_task 上下文调用）
 *
 * 分配 I1 渲染缓冲与 mono 帧缓冲，创建 display 并注册 flush 回调。幂等。
 *
 * **按需申请、用完即还**：UI 层与 DIRECT 层互斥，两块缓冲（共 86KB）不应常驻——
 * 否则会与图片 app 需要的大块连续 PSRAM（8bpp film 需 345KB）抢内存。
 *
 * @return 0 成功；-1 失败（已在内部回收）
 */
int ui_display_acquire(void);

/**
 * @brief 归还显示资源（幂等）
 *
 * 删除 display 并释放两块缓冲，把 PSRAM/内部 RAM 还给其它子系统。
 */
void ui_display_release(void);

/**
 * @brief 打开/关闭"把帧推给面板"的闸门
 *
 * 关闭时 flush 仍然会被 LVGL 调用，但不推面板（仅 flush_ready），
 * 用于暂停期间避免 UI 覆盖直绘内容。
 *
 * @param enable 非 0 打开
 */
void ui_display_set_output(int enable);

/**
 * @brief 标记整屏为脏，触发下一次整屏重绘
 *
 * 用于 resume 后覆盖直绘内容。
 */
void ui_display_invalidate_all(void);

/**
 * @brief 最近一次真正推屏的时刻（esp_timer µs）；0 表示本页还没推过
 *
 * 供 ui_task 判断"UI 已经空闲多久"，据此关掉黑白快刷会话给面板断电
 * （会话期间帧间保持上电，见 hal_epd_mono_session_begin）。
 */
int64_t ui_display_last_flush_us(void);

/**
 * @brief 复制最近一次推屏的物理 mono 位图（截图用）
 *
 * 缓冲为 720x480、1bpp、MSB 在前、1=黑，与 .film Format 0x01 主体一致。
 * 可在 app 任务调用：内部用互斥锁与 ui_task 的帧转换互斥。
 *
 * @param dst 目标缓冲
 * @param len 目标缓冲长度（必须 ≥ UI_MONO_BYTES）
 * @return 0 成功；-1 UI 显示未申请/参数无效；-2 当前为彩色卡页面（mono 缓冲不是当前画面）
 */
int ui_display_snapshot_mono(uint8_t *dst, uint32_t len);

/* ui_task 专用。卡片像素由页面持有，remove 后方可释放。坐标为逻辑竖屏。 */
int ui_display_set_color_card(const uint8_t *pixels, int x, int y, int w, int h);
void ui_display_clear_color_card(void);

#ifdef __cplusplus
}
#endif

#endif /* __UI_DISPLAY_H__ */
