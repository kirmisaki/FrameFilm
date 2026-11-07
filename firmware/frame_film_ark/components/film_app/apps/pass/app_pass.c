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
#define PASS_PROFILE_PATH     "/sdcard/app/pass/profile.bin"
#define PASS_PROFILE_W        (440u)
#define PASS_PROFILE_H        (608u)
#define PASS_PROFILE_ROW      (PASS_PROFILE_W / 8u)
#define PASS_PROFILE_BYTES    (PASS_PROFILE_W * PASS_PROFILE_H / 8u)
#define PASS_COLOR_BYTES      (PASS_PROFILE_W * PASS_PROFILE_H / 2u)
#define PASS_FFUI_HEADER      (16u)
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
    uint8_t color;
    uint8_t pixels[];
} pass_card_t;

/* pending 跨任务，exchange 明确转移所有权；active 仅 ui_task 持有。 */
static _Atomic(pass_card_t *) m_pending = NULL;
static atomic_int m_prepare_result = PASS_LOAD_MISSING;
static _Atomic uint32_t m_status_bits = 0;
static pass_card_t *m_active;
static app_shell_t m_shell;
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
    uint8_t header[PASS_FFUI_HEADER];
    uint8_t row[PASS_PROFILE_ROW];
    size_t payload_size;
    uint8_t color;
    FILE *fp;
    pass_card_t *card = NULL;
    pass_load_result_t result = PASS_LOAD_FAILED;

    *out = NULL;
    if(path == NULL) return PASS_LOAD_MISSING;
    fp = fopen(path, "rb");
    if(fp == NULL) return errno == ENOENT ? PASS_LOAD_MISSING : PASS_LOAD_FAILED;

    if(fread(header, 1, sizeof(header), fp) != sizeof(header) ||
       memcmp(header, "FFUI", 4) != 0 ||
       !((header[4] == 1 && header[5] == 1) ||
         (header[4] == 2 && header[5] == 2)) ||
       (unsigned)(header[6] | (header[7] << 8)) != PASS_PROFILE_W ||
       (unsigned)(header[8] | (header[9] << 8)) != PASS_PROFILE_H ||
       memcmp(&header[10], (const uint8_t[6]){0}, 6) != 0)
    {
        goto done;
    }
    color = header[4] == 2;
    payload_size = color ? PASS_COLOR_BYTES : PASS_PROFILE_BYTES;
    if(fseek(fp, 0, SEEK_END) != 0 ||
       ftell(fp) != (long)(PASS_FFUI_HEADER + payload_size) ||
       fseek(fp, PASS_FFUI_HEADER, SEEK_SET) != 0)
    {
        goto done;
    }

    card = (pass_card_t *)heap_caps_malloc(sizeof(*card) +
             (color ? PASS_COLOR_BYTES : PASS_PROFILE_W * PASS_PROFILE_H)
#ifdef ARK_UI_SIMULATOR
             + (color ? PASS_PROFILE_W * PASS_PROFILE_H * 3u : 0u)
#endif
             , MALLOC_CAP_SPIRAM);
    if(card == NULL) goto done;
    card->color = color;
    if(color)
    {
        if(fread(card->pixels, 1, payload_size, fp) != payload_size) goto done;
        for(size_t i = 0; i < payload_size; ++i)
        {
            const uint8_t v = card->pixels[i];
            const uint8_t a = v >> 4, b = v & 0x0F;
            if(!((a == 0 || a == 1 || a == 2 || a == 3 || a == 5 || a == 6) &&
                 (b == 0 || b == 1 || b == 2 || b == 3 || b == 5 || b == 6))) goto done;
        }
    }
    else
    {
        for(unsigned y = 0; y < PASS_PROFILE_H; ++y)
        {
            if(fread(row, 1, sizeof(row), fp) != sizeof(row)) goto done;
            for(unsigned x = 0; x < PASS_PROFILE_W; ++x)
            {
                const uint8_t bit = (row[x >> 3] >> (7u - (x & 7u))) & 1u;
                card->pixels[y * PASS_PROFILE_W + x] = bit ? 0x00 : 0xFF;
            }
        }
    }

    memset(&card->dsc, 0, sizeof(card->dsc));
    if(!color)
    {
        card->dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
        card->dsc.header.cf = LV_COLOR_FORMAT_L8;
        card->dsc.header.w = PASS_PROFILE_W;
        card->dsc.header.h = PASS_PROFILE_H;
        card->dsc.header.stride = PASS_PROFILE_W;
        card->dsc.data_size = PASS_PROFILE_W * PASS_PROFILE_H;
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
        uint8_t *out_rgb = card->pixels + PASS_COLOR_BYTES;
        for(size_t i = 0; i < PASS_PROFILE_W * PASS_PROFILE_H; ++i)
        {
            const uint8_t packed = card->pixels[i >> 1];
            const uint8_t code = (i & 1u) ? (packed & 0x0Fu) : (packed >> 4);
            memcpy(out_rgb + i * 3u, bgr[code], 3);
        }
        card->dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
        card->dsc.header.cf = LV_COLOR_FORMAT_RGB888;
        card->dsc.header.w = PASS_PROFILE_W;
        card->dsc.header.h = PASS_PROFILE_H;
        card->dsc.header.stride = PASS_PROFILE_W * 3u;
        card->dsc.data_size = PASS_PROFILE_W * PASS_PROFILE_H * 3u;
        card->dsc.data = out_rgb;
    }
#endif
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

static void pass_show_result(pass_load_result_t result)
{
    pass_card_t *next = atomic_exchange(&m_pending, NULL);
    if(next != NULL)
    {
        pass_card_t *old = m_active;
        if(next->color)
        {
            lv_area_t area;
            /* Resolve the final flex layout before caching the native overlay
               position. The empty panel must not take part in this layout. */
            lv_obj_set_hidden(m_empty_panel, true);
            lv_obj_set_hidden(m_card_img, false);
#ifdef ARK_UI_SIMULATOR
            lv_image_set_src(m_card_img, &next->dsc);
#else
            lv_image_set_src(m_card_img, NULL);
#endif
            lv_obj_set_size(m_card_img, PASS_PROFILE_W, PASS_PROFILE_H);
            lv_obj_update_layout(m_card_img);
            lv_obj_get_coords(m_card_img, &area);
            if(ui_display_set_color_card(next->pixels, area.x1, area.y1,
                                         PASS_PROFILE_W, PASS_PROFILE_H) != 0)
            {
                result = PASS_LOAD_FAILED;
#ifdef ARK_UI_SIMULATOR
                lv_image_set_src(m_card_img, old != NULL ? &old->dsc : NULL);
#else
                if(old != NULL && !old->color) lv_image_set_src(m_card_img, &old->dsc);
#endif
                heap_caps_free(next);
                next = NULL;
            }
            else app_shell_stop_tick(&m_shell);
        }
        else
        {
            ui_display_clear_color_card();
            lv_image_set_src(m_card_img, &next->dsc);
            lv_obj_set_size(m_card_img, PASS_PROFILE_W, PASS_PROFILE_H);
            if(m_shell.tick == NULL) app_shell_start_tick(&m_shell, NULL);
        }
        if(next != NULL) m_active = next;
        if(next != NULL && old != NULL) {
#if LV_CACHE_DEF_SIZE > 0
#ifdef ARK_UI_SIMULATOR
            lv_image_cache_drop(&old->dsc);
#else
            if(!old->color) lv_image_cache_drop(&old->dsc);
#endif
#endif
            heap_caps_free(old);
        }
    }

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
    app_shell_build(root, app_text(PASS_FOOT_HINT, PASS_FOOT_HINT_EN), app_text("通行证", "PASS"), &m_shell);

    body = lv_obj_create(root);
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
    if(m_active == NULL || !m_active->color) app_shell_start_tick(&m_shell, NULL);
}

static void pass_ui_destroy(void)
{
    app_shell_release(&m_shell);
    ui_display_clear_color_card();
    /* ui_core 会在 destroy 后删页面树，先断开 LVGL 的外部像素指针。 */
    if(m_card_img != NULL) lv_image_set_src(m_card_img, NULL);
    m_card_img = NULL;
    m_empty_panel = NULL;
    m_empty_note = NULL;
    m_result_note = NULL;
    if(m_active != NULL) {
#if LV_CACHE_DEF_SIZE > 0
#ifdef ARK_UI_SIMULATOR
        lv_image_cache_drop(&m_active->dsc);
#else
        if(!m_active->color) lv_image_cache_drop(&m_active->dsc);
#endif
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
};
