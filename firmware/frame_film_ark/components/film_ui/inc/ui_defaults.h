#ifndef __UI_DEFAULTS_H__
#define __UI_DEFAULTS_H__

/* 由 tools/ui-assets/gen_ui_assets.py 生成，请勿手改。
 * 内置默认图：1bpp 原始位图（1=黑 0=白，MSB 在左），
 * SD 卡上没有可用资源时由 ui_assets 回退使用。 */

#include <stdint.h>

/* 80x80, 800 B */
extern const uint8_t ui_def_icon_image[800];

/* 80x80, 800 B */
extern const uint8_t ui_def_icon_pass[800];

/* 80x80, 800 B */
extern const uint8_t ui_def_icon_wallet[800];

/* 80x80, 800 B */
extern const uint8_t ui_def_icon_template[800];

/* 80x80, 800 B */
extern const uint8_t ui_def_icon_clock[800];

/* 80x80, 800 B */
extern const uint8_t ui_def_icon_animation[800];

/* 80x80, 800 B */
extern const uint8_t ui_def_icon_settings[800];

/* 304x272, 10336 B */
extern const uint8_t ui_def_badge[10336];

#endif /* __UI_DEFAULTS_H__ */
