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

/* 主菜单条目名 */
static inline const lv_font_t *app_font_menu_name(void)
{
    return app_language_get() == APP_LANGUAGE_EN ? &lv_font_montserrat_24 : &ui_font_24;
}

static inline const lv_font_t *app_font_title(void)
{
    return app_language_get() == APP_LANGUAGE_EN ? &lv_font_montserrat_24 : &ui_font_24;
}

static inline const lv_font_t *app_font_hero(void)
{
    return app_language_get() == APP_LANGUAGE_EN ? &lv_font_montserrat_36 : &ui_font_36;
}

/* Keep ASCII in Unscii8; fall back for symbols, or use Chinese for CJK text. */
const lv_font_t *app_font_small_text(const char *text);

#endif
