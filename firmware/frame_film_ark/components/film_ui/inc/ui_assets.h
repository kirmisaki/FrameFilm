#ifndef __UI_ASSETS_H__
#define __UI_ASSETS_H__

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
/* 资源槽位尺寸（资源规格）：SD 卡替换必须精确匹配这两组尺寸，固件不做缩放 */
#define UI_ICON_W           (80)
#define UI_ICON_H           (80)
/* 徽章横向 304、纵向 272：由 tools/ui-assets/gen_ui_assets.py 按源图 icon1.png 的
 * 宽高比（641:573）从 300 取整到 8 的倍数而得。改这里必须同步改脚本的 BADGE_W/H。 */
#define UI_BADGE_W          (304)
#define UI_BADGE_H          (272)

/* SD 卡资源根目录与容器头长度（FFUI 容器，见 tools/ui-assets/gen_ui_assets.py） */
#define UI_ASSET_ROOT       "/sdcard/app"

/*********************************************************************
* TYPEDEFS
*/
/**
 * @brief 图标槽位
 *
 * 顺序与主菜单轮播一致（app_manager 的 m_menu_entries）。
 * 图标本身可被 SD 卡上的同尺寸资源替换。
 */
typedef enum {
    UI_ICON_IMAGE = 0,
    UI_ICON_PASS,          // 通行证（占位 app 的卡片图标，源图 tools/ui-assets/png/pass.png）
    UI_ICON_WALLET,        // 卡包（NFC，仅 n16r8）
    UI_ICON_TEMPLATE,
    UI_ICON_CLOCK,
    UI_ICON_ANIMATION,
    UI_ICON_SETTINGS,
    UI_ICON_NUM,
} ui_icon_id_t;

/*********************************************************************
 * GLOBAL FUNCTIONS
 */

/**
 * @brief 取某个 app 图标的 LVGL 图像源
 *
 * 优先读 /sdcard/app/<app>/icon.bin；文件缺失/头部非法/尺寸不符时回退内置默认图。
 * 结果会缓存（含展开后的 L8 缓冲），后续调用直接返回同一指针。
 *
 * @param id 图标槽位
 * @return LVGL 图像源（lv_image_dsc_t*），可直接喂 lv_image_set_src()；失败返回 NULL
 */
const void *ui_assets_icon(ui_icon_id_t id);

/**
 * @brief 取反色版图标（菜单选中项用）
 *
 * 选中项是"实心黑底 + 反白图标"，反色由固件对位图取反得到，
 * 因此每种图标**只需提供一份资源**，不必画两套。
 *
 * @param id 图标槽位
 * @return LVGL 图像源（lv_image_dsc_t*）；失败返回 NULL
 */
const void *ui_assets_icon_inverted(ui_icon_id_t id);

/**
 * @brief 取开机徽章的 LVGL 图像源（/sdcard/app/_ui/badge.bin）
 *
 * @return LVGL 图像源（lv_image_dsc_t*）；失败返回 NULL
 */
const void *ui_assets_badge(void);

/**
 * @brief 释放所有已展开的图缓冲（幂等）
 *
 * 页面退出时调用，把 PSRAM/内部 RAM 还给其它子系统。
 */
void ui_assets_release(void);

#ifdef __cplusplus
}
#endif

#endif /* __UI_ASSETS_H__ */
