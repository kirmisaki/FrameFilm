#ifndef __APP_TYPOGRAPHY_H__
#define __APP_TYPOGRAPHY_H__

#include "sdkconfig.h"

#include "app_boot_cfg.h"
#include "ui_fonts.h"

/* Page typography. Numeric readouts and brand artwork retain their own fonts. */
static inline const lv_font_t *app_font_small(void)
{
    return app_language_get() == APP_LANGUAGE_EN ? &lv_font_unscii_8 : &ui_font_14;
}

static inline const lv_font_t *app_font_body(void)
{
    return app_language_get() == APP_LANGUAGE_EN ? &lv_font_unscii_8 : &ui_font_18;
}

/* 主菜单条目名：中文在 n4r2 上用 18px 字体（24px 标题字体已裁剪） */
static inline const lv_font_t *app_font_menu_name(void)
{
    if(app_language_get() == APP_LANGUAGE_EN) return &lv_font_montserrat_24;
#if CONFIG_ARK_UI_CJK_TITLE_FONT
    return &ui_font_24;
#else
    return &ui_font_18;
#endif
}

static inline const lv_font_t *app_font_title(void)
{
    if(app_language_get() == APP_LANGUAGE_EN) return &lv_font_montserrat_24;
#if CONFIG_ARK_UI_CJK_TITLE_FONT
    return &ui_font_24;
#else
    return &ui_font_18;   /* n4r2：用 18px 中文正文，避免 24px 标题字形 */
#endif
}

static inline const lv_font_t *app_font_hero(void)
{
    if(app_language_get() == APP_LANGUAGE_EN) return &lv_font_montserrat_36;
#if CONFIG_ARK_UI_CJK_TITLE_FONT
    return &ui_font_36;
#else
    return &ui_font_18;   /* n4r2：用 18px 中文正文，避免 36px 标题字形 */
#endif
}

/* Keep ASCII in Unscii8; fall back for symbols, or use Chinese for CJK text. */
const lv_font_t *app_font_small_text(const char *text);

#endif
