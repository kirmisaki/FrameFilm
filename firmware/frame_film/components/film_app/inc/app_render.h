#ifndef __APP_RENDER_H__
#define __APP_RENDER_H__

/*********************************************************************
 * INCLUDES
 */
#include <stdint.h>
#include "app_interface.h"

/*********************************************************************
 * CPPMIX
 */
#ifdef __cplusplus
extern "C" {
#endif

/*********************************************************************
 * TYPEDEFS
 */

/*********************************************************************
 * CONSTANTS
 */

/*********************************************************************
 * GLOBAL FUNCTIONS
 */

/**
 * @brief 查询当前屏的渲染能力位
 *
 * 返回 hal_epd_get_capabilities() 的 EPD_CAP_* 组合，供 app 层运行时
 * 选择渲染路径（全屏 film / 8bpp 索引色 / MonoFast 差分局刷）。
 *
 * @return EPD_CAP_* 组合
 */
uint32_t app_render_get_capabilities(void);

/**
 * @brief 封面菜单是否可用
 *
 * 运行期按屏参数判定：仅 3.7" 屏（EPD_PANEL_ID 0x02，具备 MonoFast 快刷）
 * 能整屏绘制 app 封面并快速翻页；其余屏返回 0，切换模式应降级为简易模式。
 *
 * @return 1 可用，0 不可用
 */
int app_render_has_cover_menu(void);

/**
 * @brief 当前屏是否支持 8bpp 索引色（ColorQual / ColorFast）
 *
 * @return 1 支持，0 不支持
 */
int app_render_has_8bpp(void);

/**
 * @brief 当前屏是否支持 MonoFast 差分局刷（时钟 / 切换封面用）
 *
 * @return 1 支持，0 不支持
 */
int app_render_has_monofast(void);

/**
 * @brief 渲染完整 .film 文件（v1/v2 单帧或首帧）
 *
 * 由文件头 Format 自动分派到对应驱动（4bpp / 8bpp / mono），
 * 内部完成初始化 + 完整刷新 + 关电。
 *
 * @param filmData .film 文件数据指针（含 32 字节文件头）
 */
void app_render_display_full(const unsigned char *filmData);

/**
 * @brief 渲染 8bpp 索引色数据（仅 3.7 屏）
 *
 * @param index8Data 8bpp 颜色索引缓冲（W*H 字节）
 * @param mode 刷新模式：0=ColorFast（2 相），1=ColorQual（3 相）
 */
void app_render_display_8bpp(const unsigned char *index8Data, uint8_t mode);

/**
 * @brief 渲染 1bpp MonoFast 位图（差分局刷，仅 3.7 屏）
 *
 * @param mono_bitmap 1bpp 位图缓冲（W*H/8 字节，MSB 在前，1 黑 0 白）
 */
void app_render_display_mono(const unsigned char *mono_bitmap);

/**
 * @brief 清屏为空白
 */
void app_render_clear(void);

/**
 * @brief 渲染应用切换菜单（仅 3.7 屏切换态调用）
 *
 * 以 app 的封面 .film 图（TF 卡 `/sdcard/app/<appname>/cover.film`）
 * 作为菜单项，读入后按文件头 Format 整屏渲染；封面缺失时降级为日志
 * 提示（不绘制，不阻塞切换）。UP/DOWN 滚动时刷新为对应 app 的封面。
 * 内部缓存最近一次封面，重复高亮同一 app 时不再重复读取。
 *
 * @param app_name 当前高亮 app 的名称（app_entry_t.name）
 */
void app_render_switch_menu(const char *app_name);

#ifdef __cplusplus
}
#endif

#endif /* __APP_RENDER_H__ */
