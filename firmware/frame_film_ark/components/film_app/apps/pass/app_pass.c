/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * 通行证个人资料图：app_task 读 SD，ui_task 接管已展开的像素。
 */
#include <errno.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "hal_epd_abc9.h"
#include "hal_pass_profile.h"
#include "sys_log.h"
#include "ui_ops.h"
#include "ui_display.h"
#include "ui_assets.h"
#include "ui_fonts.h"
#include "app_typography.h"
#include "app_shell.h"
#include "app_language.h"
#include "app_manager.h"
#include "app_pass.h"

#define APP_PASS_TAG          "app_pass"
#define PASS_PROFILE_PATH     PASS_PROFILE_FILE_PATH
#define PASS_PROFILE_W        (440u)
#define PASS_PROFILE_H        (608u)
#define PASS_FULLSCREEN_W     (480u)
#define PASS_FULLSCREEN_H     (720u)
#define PASS_PROFILE_ROW      (PASS_FULLSCREEN_W / 8u)
#define PASS_RULE_W          (240)
#define PASS_WORD_LS         (8)
#define PASS_FOOT_HINT       "单击刷新  双击确认退出  长按休眠"
#define PASS_FOOT_HINT_EN    "OK RELOAD  2X BACK  HOLD SLEEP"

typedef enum {
    PASS_LOAD_OK = 0,
    PASS_LOAD_MISSING,
    PASS_LOAD_FAILED,
} pass_load_result_t;

typedef struct {
    lv_image_dsc_t dsc;
    uint16_t width, height;
    uint8_t color;
    uint8_t abc9; /* Native 720x480 LE16 body, never converted through LVGL. */
    uint8_t fast, compact, avatar;
    uint32_t body_size;
    uint8_t pixels[];
} pass_card_t;

/* pending 跨任务，exchange 明确转移所有权；active 仅 ui_task 持有。 */
static _Atomic(pass_card_t *) m_pending = NULL;
static atomic_int m_prepare_result = PASS_LOAD_MISSING;
static _Atomic uint32_t m_status_bits = 0;
static pass_card_t *m_active;
static app_shell_t m_shell;
static lv_obj_t *m_shell_root;
static lv_obj_t *m_body;
static lv_obj_t *m_card_img;
static lv_obj_t *m_empty_panel;
static lv_obj_t *m_empty_note;
static lv_obj_t *m_result_note;

static const char *pass_profile_path(void)
{
#ifdef ARK_UI_SIMULATOR
    const char *path = getenv("ARK_PASS_PROFILE");
    return (path != NULL && path[0] != '\0') ? path : NULL;
#else
    return PASS_PROFILE_PATH;
#endif
}

static pass_load_result_t pass_load_card(pass_card_t **out)
{
    const char *path = pass_profile_path();
    PassProfileInfo_t info;
    uint8_t row[PASS_PROFILE_ROW];
    size_t payload_size, pixel_count;
    unsigned width, height;
    uint8_t color;
    FILE *fp;
    pass_card_t *card = NULL;
    pass_load_result_t result = PASS_LOAD_FAILED;

    *out = NULL;
    if(path == NULL) return PASS_LOAD_MISSING;
    fp = fopen(path, "rb");
    if(fp == NULL) return errno == ENOENT ? PASS_LOAD_MISSING : PASS_LOAD_FAILED;

    if(!hal_pass_profile_inspect(fp, &info)) goto done;
    if(info.format == PASS_FMT_ABC9 || info.format == PASS_FMT_FAST ||
       info.format == PASS_FMT_COMPACT_ABC9 || info.format == PASS_FMT_COMPACT_FAST)
    {
        const int compact = info.format == PASS_FMT_COMPACT_ABC9 || info.format == PASS_FMT_COMPACT_FAST;
        /* Keep compact planes packed; the display compositor supplies their
           pixels. Do not give I1 to LVGL's image decoder: it expands to ARGB8888
           (over 1MB scratch), defeating the N4R2 row-fed path. */
        card = heap_caps_malloc(sizeof(*card) + info.body_size, MALLOC_CAP_SPIRAM);
        if(card == NULL) goto done;
        memset(card, 0, sizeof(*card));
        card->width = info.width; card->height = info.height;
        card->color = 1;
        card->abc9 = info.format == PASS_FMT_ABC9;
        card->fast = info.format == PASS_FMT_FAST;
        card->compact = compact;
        card->avatar = info.avatar_planes;
        card->body_size = info.body_size;
        uint8_t *body = card->pixels;
        if(fread(body, 1, info.body_size, fp) != info.body_size ||
           !hal_pass_profile_codes(&info, body, 0, info.body_size)) goto done;
        if(compact)
        {
            /* Retain the selected compact mode independently of native ABC9. */
            card->fast = info.format == PASS_FMT_COMPACT_FAST;
        }
        else
        {
            /* Turn the full-screen student card to match the physical frame.
               Rotate only the loaded copy; preserve the stored byte order and LE16 codes. */
            const size_t stride = card->abc9 ? 2u : 1u;
            for(size_t left = 0, right = info.body_size - stride; left < right;
                left += stride, right -= stride)
            {
                for(size_t i = 0; i < stride; ++i)
                {
                    const uint8_t value = body[left + i];
                    body[left + i] = body[right + i];
                    body[right + i] = value;
                }
            }
        }
        goto loaded;
    }
    width = info.width; height = info.height;
    color = info.format == PASS_FMT_SIX;
    pixel_count = (size_t)width * height;
    payload_size = info.body_size;

    card = (pass_card_t *)heap_caps_malloc(sizeof(*card) +
             (color ? payload_size : pixel_count)
#ifdef ARK_UI_SIMULATOR
             + (color ? pixel_count * 3u : 0u)
#endif
             , MALLOC_CAP_SPIRAM);
    if(card == NULL) goto done;
    memset(card, 0, sizeof(*card));
    card->color = color;
    card->abc9 = 0;
    card->width = width;
    card->height = height;
    if(color)
    {
        if(fread(card->pixels, 1, payload_size, fp) != payload_size ||
           !hal_pass_profile_codes(&info, card->pixels, 0, payload_size)) goto done;
    }
    else
    {
        const size_t row_bytes = width / 8u;
        for(unsigned y = 0; y < height; ++y)
        {
            if(fread(row, 1, row_bytes, fp) != row_bytes) goto done;
            for(unsigned x = 0; x < width; ++x)
            {
                const uint8_t bit = (row[x >> 3] >> (7u - (x & 7u))) & 1u;
                card->pixels[y * width + x] = bit ? 0x00 : 0xFF;
            }
        }
    }

    memset(&card->dsc, 0, sizeof(card->dsc));
    if(!color)
    {
        card->dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
        card->dsc.header.cf = LV_COLOR_FORMAT_L8;
        card->dsc.header.w = width;
        card->dsc.header.h = height;
        card->dsc.header.stride = width;
        card->dsc.data_size = pixel_count;
        card->dsc.data = card->pixels;
    }
#ifdef ARK_UI_SIMULATOR
    else
    {
        /* ColorFast measured colors selected in ui_display.c; LVGL RGB888
           pixel memory order is B,G,R. This is a preview, not panel calibration. */
        static const uint8_t bgr[7][3] = {
            {0, 0, 0}, {255, 255, 255}, {57, 198, 219}, {32, 71, 180},
            {0, 0, 0}, {135, 60, 21}, {112, 149, 123}
        };
        uint8_t *out_rgb = card->pixels + payload_size;
        for(size_t i = 0; i < pixel_count; ++i)
        {
            const uint8_t packed = card->pixels[i >> 1];
            const uint8_t code = (i & 1u) ? (packed & 0x0Fu) : (packed >> 4);
            memcpy(out_rgb + i * 3u, bgr[code], 3);
        }
        card->dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
        card->dsc.header.cf = LV_COLOR_FORMAT_RGB888;
        card->dsc.header.w = width;
        card->dsc.header.h = height;
        card->dsc.header.stride = width * 3u;
        card->dsc.data_size = pixel_count * 3u;
        card->dsc.data = out_rgb;
    }
#endif
loaded:
    *out = card;
    card = NULL;
    result = PASS_LOAD_OK;

done:
    fclose(fp);
    if(card != NULL) heap_caps_free(card);
    if(result != PASS_LOAD_OK) sys_logw(APP_PASS_TAG, "profile unavailable: %s", path);
    return result;
}


static pass_load_result_t pass_prepare(void)
{
    pass_card_t *next = NULL;
    pass_card_t *old = atomic_exchange(&m_pending, NULL);
    if(old != NULL) heap_caps_free(old);

    pass_load_result_t result = pass_load_card(&next);
    app_status_t status;
    app_shell_status_snapshot(&status); /* app_task 侧，与本次卡片一同进入 UI。 */
    atomic_store(&m_status_bits, (uint32_t)status.bat_pct |
                 ((uint32_t)status.wifi_on << 8) |
                 ((uint32_t)status.wifi_conn << 9) |
                 ((uint32_t)status.bt_on << 10) |
                 ((uint32_t)status.bt_conn << 11) |
                 ((uint32_t)status.nfc_on << 12) |
                 ((uint32_t)status.nfc_emu << 13));
    atomic_store(&m_pending, next);
    atomic_store(&m_prepare_result, result);
    return result;
}

static void pass_apply_status(void)
{
    const uint32_t bits = atomic_load(&m_status_bits);
    const app_status_t status = {
        .bat_pct = (uint8_t)(bits & 0xFFu),
        .wifi_on = (uint8_t)((bits >> 8) & 1u),
        .wifi_conn = (uint8_t)((bits >> 9) & 1u),
        .bt_on = (uint8_t)((bits >> 10) & 1u),
        .bt_conn = (uint8_t)((bits >> 11) & 1u),
        .nfc_on = (uint8_t)((bits >> 12) & 1u),
        .nfc_emu = (uint8_t)((bits >> 13) & 1u),
    };
    app_shell_apply(&m_shell, &status);
}

static const char *pass_enter_block_reason(void)
{
    (void)pass_prepare();
    return NULL; /* 无资料时正常进入空状态。 */
}

static lv_obj_t *pass_label(lv_obj_t *parent, const lv_font_t *font,
                            lv_color_t color, const char *txt)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, color, LV_PART_MAIN);
    lv_label_set_text(label, txt);
    return label;
}

static void pass_rule(lv_obj_t *parent)
{
    lv_obj_t *rule = lv_obj_create(parent);
    lv_obj_remove_style_all(rule);
    lv_obj_set_scrollable(rule, false);
    lv_obj_set_size(rule, PASS_RULE_W, 1);
    lv_obj_set_style_bg_color(rule, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(rule, LV_OPA_COVER, LV_PART_MAIN);
}

static int pass_is_fullscreen(const pass_card_t *card)
{
    return card != NULL && card->width == PASS_FULLSCREEN_W && card->height == PASS_FULLSCREEN_H;
}

static void pass_layout(const pass_card_t *card)
{
    const int fullscreen = pass_is_fullscreen(card);
    lv_obj_set_hidden(m_shell_root, fullscreen);
    lv_obj_set_hidden(m_result_note, fullscreen);
    lv_obj_set_style_pad_top(m_body, fullscreen ? 0 : SHELL_BODY_TOP, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(m_body, fullscreen ? 0 : SHELL_BODY_BOTTOM, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(m_body, fullscreen ? 0 : SHELL_BODY_PAD_X, LV_PART_MAIN);
    lv_obj_set_size(m_card_img, card != NULL ? card->width : PASS_PROFILE_W,
                    card != NULL ? card->height : PASS_PROFILE_H);
}

static const lv_image_dsc_t *pass_image_source(const pass_card_t *card)
{
    if(card == NULL || card->abc9 || card->fast || card->compact) return NULL;
#ifndef ARK_UI_SIMULATOR
    if(card->color && !card->compact) return NULL;
#endif
    return &card->dsc;
}

static void pass_show_result(pass_load_result_t result)
{
    pass_card_t *next = atomic_exchange(&m_pending, NULL);
    if(next != NULL)
    {
        pass_card_t *old = m_active;
        pass_layout(next);
        if(!next->compact && (next->abc9 || next->fast))
        {
            int display_result = next->abc9 ?
                ui_display_set_abc9_card(next->pixels, next->body_size) :
                ui_display_set_fast_card(next->pixels, next->body_size);
            if(display_result != 0)
            {
                result = PASS_LOAD_FAILED;
                pass_layout(old);
                heap_caps_free(next);
                next = NULL;
            }
            else lv_image_set_src(m_card_img, NULL);
        }
        else if(next->color)
        {
            lv_area_t area;
            /* Resolve the final flex layout before caching the native overlay
               position. The empty panel must not take part in this layout. */
            lv_obj_set_hidden(m_empty_panel, true);
            lv_obj_set_hidden(m_card_img, false);
            lv_image_set_src(m_card_img, pass_image_source(next));
            lv_obj_update_layout(m_card_img);
            lv_obj_get_coords(m_card_img, &area);
            int display_result = next->compact ?
                ui_display_set_compact_card(next->pixels, next->body_size,
                                            area.x1, area.y1, !next->fast, next->avatar) :
                ui_display_set_color_card(next->pixels, area.x1, area.y1,
                                          next->width, next->height);
            if(display_result != 0)
            {
                result = PASS_LOAD_FAILED;
                pass_layout(old);
                lv_image_set_src(m_card_img, pass_image_source(old));
                heap_caps_free(next);
                next = NULL;
            }
        }
        else
        {
            ui_display_clear_color_card();
            lv_image_set_src(m_card_img, &next->dsc);
        }
        if(next != NULL) m_active = next;
        if(next != NULL && old != NULL) {
#if LV_CACHE_DEF_SIZE > 0
            if(pass_image_source(old) != NULL) lv_image_cache_drop(&old->dsc);
#endif
            heap_caps_free(old);
        }
    }

    if(m_active != NULL && (m_active->color || pass_is_fullscreen(m_active)))
        app_shell_stop_tick(&m_shell);
    else if(m_shell.tick == NULL)
        app_shell_start_tick(&m_shell, NULL);

    if(m_active != NULL)
    {
        lv_obj_set_hidden(m_empty_panel, true);
        lv_obj_set_hidden(m_card_img, false);
        lv_label_set_text(m_result_note, result == PASS_LOAD_OK ? "" : app_text("读取失败，保留当前通行证", "Read failed; showing current pass"));
    }
    else
    {
        lv_label_set_text(m_empty_note, result == PASS_LOAD_MISSING ?
                          app_text("尚未配置资料", "No profile configured") : app_text("资料读取失败", "Profile read failed"));
        lv_obj_set_hidden(m_card_img, true);
        lv_obj_set_hidden(m_empty_panel, false);
        lv_label_set_text(m_result_note, app_text("请用网页上传资料后刷新", "Upload profile on web, then reload"));
    }
}

static void pass_ui_create(lv_obj_t *root)
{
    lv_obj_t *body;
    lv_obj_t *title;
    lv_obj_t *icon;
    const void *src;

#ifdef ARK_UI_SIMULATOR
    /* 模拟器单线程直接调 create，没有 app_manager 入页预检。 */
    (void)pass_prepare();
#endif
    m_shell_root = lv_obj_create(root);
    lv_obj_remove_style_all(m_shell_root);
    lv_obj_set_scrollable(m_shell_root, false);
    lv_obj_set_size(m_shell_root, LV_PCT(100), LV_PCT(100));
    app_shell_build(m_shell_root, app_text(PASS_FOOT_HINT, PASS_FOOT_HINT_EN), app_text("通行证", "PASS"), &m_shell);

    body = m_body = lv_obj_create(root);
    lv_obj_remove_style_all(body);
    lv_obj_set_scrollable(body, false);
    lv_obj_set_size(body, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_top(body, SHELL_BODY_TOP, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(body, SHELL_BODY_BOTTOM, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(body, SHELL_BODY_PAD_X, LV_PART_MAIN);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(body, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    m_card_img = lv_image_create(body);
    m_empty_panel = lv_obj_create(body);
    lv_obj_remove_style_all(m_empty_panel);
    lv_obj_set_scrollable(m_empty_panel, false);
    lv_obj_set_flex_flow(m_empty_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(m_empty_panel, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(m_empty_panel, 18, LV_PART_MAIN);
    lv_obj_set_size(m_empty_panel, LV_PCT(100), LV_PCT(100));

    icon = lv_image_create(m_empty_panel);
    src = ui_assets_icon(UI_ICON_PASS);
    if(src != NULL) lv_image_set_src(icon, src);
    title = pass_label(m_empty_panel, app_font_hero(), lv_color_black(), app_text("通行证", "PASS"));
    lv_obj_set_style_text_letter_space(title, PASS_WORD_LS, LV_PART_MAIN);
    lv_obj_set_style_translate_x(title, PASS_WORD_LS / 2, LV_PART_MAIN);
    pass_rule(m_empty_panel);
    m_empty_note = pass_label(m_empty_panel, app_font_body(), lv_color_black(), app_text("尚未配置资料", "No profile configured"));
    (void)pass_label(m_empty_panel, app_font_small(), lv_color_black(), app_text("单击确认刷新", "Press Enter to reload"));

    m_result_note = pass_label(root, app_font_small(), lv_color_black(), "");
    lv_obj_set_pos(m_result_note, SHELL_BODY_PAD_X, 672);
    pass_show_result((pass_load_result_t)atomic_load(&m_prepare_result));
    pass_apply_status();
}

static void pass_ui_destroy(void)
{
    app_shell_release(&m_shell);
    ui_display_clear_color_card();
    /* ui_core 会在 destroy 后删页面树，先断开 LVGL 的外部像素指针。 */
    if(m_card_img != NULL) lv_image_set_src(m_card_img, NULL);
    m_card_img = NULL;
    m_shell_root = NULL;
    m_body = NULL;
    m_empty_panel = NULL;
    m_empty_note = NULL;
    m_result_note = NULL;
    if(m_active != NULL) {
#if LV_CACHE_DEF_SIZE > 0
        if(pass_image_source(m_active) != NULL) lv_image_cache_drop(&m_active->dsc);
#endif
        heap_caps_free(m_active);
    }
    m_active = NULL;
    pass_card_t *pending = atomic_exchange(&m_pending, NULL);
    if(pending != NULL) heap_caps_free(pending);
}

static void pass_ui_on_msg(uint32_t cmd, const void *data, uint8_t len)
{
    if(cmd == APP_UI_MSG_PASS_RELOAD)
    {
        if(data != NULL && len == 1)
        {
            pass_show_result(*(const uint8_t *)data);
            pass_apply_status();
            app_shell_refresh_time(&m_shell);
            if(m_active != NULL && m_active->color) ui_display_invalidate_all();
        }
        return;
    }
    (void)app_shell_handle_msg(&m_shell, cmd, data, len);
}

static void pass_ui_on_key(input_press_type_t key)
{
    if(key == INPUT_PRESS_SHORT &&
       app_manager_post_ui_msg(APP_UI_REQ_PASS_RELOAD, NULL, 0) != 0)
    {
        sys_logw(APP_PASS_TAG, "reload request queue full");
        lv_label_set_text(m_result_note, app_text("设备忙，请再按确认重试", "Busy; press Enter to retry"));
    }
}

static void pass_on_event(const app_event_t *event)
{
    if(event->type == APP_EVT_UI_MSG && event->cmd == APP_UI_REQ_PASS_RELOAD)
    {
        const uint8_t result = (uint8_t)pass_prepare();
        if(ui_core_post(APP_UI_MSG_PASS_RELOAD, &result, 1) != 0)
        {
            pass_card_t *pending = atomic_exchange(&m_pending, NULL);
            if(pending != NULL) heap_caps_free(pending);
            sys_logw(APP_PASS_TAG, "reload response queue full");
        }
        return;
    }
    app_shell_on_event(event);
}

static const app_ui_ops_t g_pass_ui_ops = {
    .create = pass_ui_create,
    .destroy = pass_ui_destroy,
    .on_msg = pass_ui_on_msg,
    .on_key = pass_ui_on_key,
};

const app_entry_t g_app_pass_entry = {
    .id = APP_ID_PASS,
    .name = "pass",
    .data_dir = NULL,
    .keys = APP_KEY_SHORT,
    .tick_ms = 0,
    .events = NULL,
    .enter_block_reason = pass_enter_block_reason,
    .layer = APP_LAYER_UI,
    .ui_ops = &g_pass_ui_ops,
    .on_event = pass_on_event,
    .sleep_face = APP_SLEEP_FACE_KEEP,
};
