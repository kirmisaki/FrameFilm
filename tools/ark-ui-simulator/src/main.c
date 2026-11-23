#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include "lvgl.h"
#include "sys_cfg.h"
#include "app_boot.h"
#include "app_menu.h"
#include "app_settings.h"
#include "app_sleep.h"
#include "app_pass.h"
#include "app_clock.h"
#include "app_interface.h"
#include "app_boot_cfg.h"
#include "app_shell.h"
#include "simulator.h"

static uint8_t menu_sel;
static bool sleep_keep;

static void show_menu(void)
{
    /* 与固件 app_menu_open 一致：返回时对齐刚离开的应用。 */
    static const app_id_t entries[APP_MENU_ENTRY_NUM] = {
        APP_ID_IMAGE, APP_ID_PASS, APP_ID_TEMPLATE,
        APP_ID_CLOCK, APP_ID_ANIMATION, APP_ID_SETTINGS
    };
    const app_entry_t *previous = simulator_current_entry();
    if(previous)
    {
        for(uint8_t i = 0; i < APP_MENU_ENTRY_NUM; i++)
            if(entries[i] == previous->id) { menu_sel = i; break; }
    }
    simulator_show(g_app_menu_entry.ui_ops, &g_app_menu_entry);
    g_app_menu_entry.ui_ops->on_msg(APP_UI_MSG_MENU_SEL, &menu_sel, 1);
}

static void show_boot(void)
{
    static const app_boot_tele_t tele[] = {
        {"PANEL", "720x480 E6"}, {"PSRAM", "1824 KB FREE"},
        {"STORAGE", "SD MOUNTED"}, {"FIRMWARE", SYS_FIRMWARE_VERSION}
    };
    app_boot_set_telemetry(tele, 4);
    simulator_show(app_boot_ops(), NULL);
}

static void show_sleep(void)
{
    app_sleep_set_info(1, 30);
    simulator_show(app_sleep_ops(), NULL);
}

static void activate_menu_selection(void)
{
    if(menu_sel == 1) simulator_show(g_app_pass_entry.ui_ops, &g_app_pass_entry);
    else if(menu_sel == 3) simulator_show(g_app_clock_entry.ui_ops, &g_app_clock_entry);
    else if(menu_sel == 5) simulator_show(g_app_settings_entry.ui_ops, &g_app_settings_entry);
    else fprintf(stderr, "Entry %u is not connected to this first desktop preview.\n", menu_sel);
}

static void press_key(input_press_type_t key)
{
    const app_ui_ops_t *ops = simulator_current_ops();
    const app_entry_t *entry = simulator_current_entry();
    if(sleep_keep) {
        if(key == INPUT_PRESS_SHORT) {
            lv_display_enable_invalidation(lv_display_get_default(), true);
            sleep_keep = false;
            show_boot();
        }
        return;
    }
    if(ops == app_sleep_ops() && key == INPUT_PRESS_SHORT) { show_boot(); return; }
    if(key == INPUT_PRESS_LONG) {
        if(entry && entry->sleep_face == APP_SLEEP_FACE_KEEP) {
            lv_display_t *display = lv_display_get_default();
            lv_refr_now(display);
            lv_display_enable_invalidation(display, false);
            sleep_keep = true;
        }
        else show_sleep();
        return;
    }
    if(key == INPUT_PRESS_DOUBLE) { show_menu(); return; }
    if(entry == &g_app_menu_entry) {
        if(key == INPUT_PRESS_UP) menu_sel = (menu_sel + 1) % APP_MENU_ENTRY_NUM;
        else if(key == INPUT_PRESS_DOWN) menu_sel = (menu_sel + APP_MENU_ENTRY_NUM - 1) % APP_MENU_ENTRY_NUM;
        else if(key == INPUT_PRESS_SHORT) { activate_menu_selection(); return; }
        ops->on_msg(APP_UI_MSG_MENU_SEL, &menu_sel, 1);
    }
    else if(ops && ops->on_key) ops->on_key(key);
}

static int save_frame(lv_display_t *display, const char *dir, const char *name)
{
    char path[1024];
    if(snprintf(path, sizeof(path), "%s/%s.bmp", dir, name) >= (int)sizeof(path)) return -1;
    for(int i = 0; i < 12; ++i) { lv_timer_handler(); simulator_process_requests(); SDL_Delay(10); }
    SDL_Renderer *renderer = lv_sdl_window_get_renderer(display);
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, 480, 720, 32, SDL_PIXELFORMAT_ARGB8888);
    if(!renderer || !surface) return -1;
    if(SDL_RenderReadPixels(renderer, NULL, SDL_PIXELFORMAT_ARGB8888,
                            surface->pixels, surface->pitch) != 0) { SDL_FreeSurface(surface); return -1; }
    int result = SDL_SaveBMP(surface, path);
    SDL_FreeSurface(surface);
    fprintf(stderr, "snapshot %s: %s\n", name, result == 0 ? path : SDL_GetError());
    return result;
}

static int ensure_preview_dir(const char *dir)
{
    char path[1024];
    size_t len = strlen(dir);
    if(len == 0 || len >= sizeof(path)) return -1;
    memcpy(path, dir, len + 1);
    for(char *p = path + 1; ; p++) {
        if(*p != '/' && *p != '\\' && *p != '\0') continue;
        char saved = *p;
        *p = '\0';
        if(mkdir(path, 0777) != 0 && errno != EEXIST) return -1;
        if(saved == '\0') break;
        *p = saved;
    }
    return 0;
}

static uint8_t preview_i1_buf[8 + 480 * 720 / 8];

static void preview_i1_flush(lv_display_t *display, const lv_area_t *area, uint8_t *pixels)
{
    (void)area;
    (void)pixels;
    lv_display_flush_ready(display);
}

static int save_i1_frame(lv_display_t *display, const char *dir, const char *name)
{
    char path[1024];
    if(snprintf(path, sizeof(path), "%s/%s.bmp", dir, name) >= (int)sizeof(path)) return -1;
    for(int i = 0; i < 12; i++) { lv_timer_handler(); simulator_process_requests(); SDL_Delay(10); }
    lv_refr_now(display);
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, 480, 720, 8, SDL_PIXELFORMAT_INDEX8);
    if(!surface) return -1;
    const SDL_Color colors[2] = {{0, 0, 0, 255}, {255, 255, 255, 255}};
    if(SDL_SetPaletteColors(surface->format->palette, colors, 0, 2) != 0) {
        SDL_FreeSurface(surface);
        return -1;
    }
    const uint8_t *pixels = preview_i1_buf + 8;
    for(int y = 0; y < 720; y++) {
        uint8_t *row = (uint8_t *)surface->pixels + y * surface->pitch;
        for(int x = 0; x < 480; x++)
            row[x] = (pixels[y * 60 + x / 8] >> (7 - x % 8)) & 1;
    }
    int result = SDL_SaveBMP(surface, path);
    SDL_FreeSurface(surface);
    fprintf(stderr, "I1 %s: %s\n", name, result == 0 ? path : SDL_GetError());
    return result;
}

static lv_display_t *create_i1_display(void)
{
    lv_tick_set_cb(SDL_GetTicks);
    lv_display_t *display = lv_display_create(480, 720);
    if(!display) { fputs("LVGL I1 display failed\n", stderr); return NULL; }
    lv_display_set_color_format(display, LV_COLOR_FORMAT_I1);
    lv_display_set_flush_cb(display, preview_i1_flush);
    lv_display_set_buffers(display, preview_i1_buf, NULL, sizeof(preview_i1_buf), LV_DISPLAY_RENDER_MODE_FULL);
    return display;
}

static int save_menu_preview(const char *dir)
{
    if(ensure_preview_dir(dir) != 0) {
        fprintf(stderr, "Cannot create preview directory: %s\n", dir);
        return 1;
    }
    lv_display_t *display = create_i1_display();
    if(!display) return 1;
    int result = 0;
    for(int lang = 0; lang < 2; lang++) {
        app_language_set(lang ? APP_LANGUAGE_EN : APP_LANGUAGE_ZH_CN);
        show_menu();
        for(uint8_t i = 0; i < APP_MENU_ENTRY_NUM; i++) {
            char name[32];
            menu_sel = i;
            g_app_menu_entry.ui_ops->on_msg(APP_UI_MSG_MENU_SEL, &menu_sel, 1);
            snprintf(name, sizeof(name), "menu-%s-%u", lang ? "en" : "zh", (unsigned)i);
            if(save_i1_frame(display, dir, name) != 0) { result = 1; break; }
        }
        if(result) break;
    }
    simulator_show(NULL, NULL);
    lv_display_delete(display);
    fprintf(stderr, "menu I1 preview %s\n", result ? "FAIL" : "PASS");
    return result;
}


static int save_font_preview(const char *dir)
{
    if(ensure_preview_dir(dir) != 0) {
        fprintf(stderr, "Cannot create preview directory: %s\n", dir);
        return 1;
    }
    lv_display_t *display = create_i1_display();
    if(!display) return 1;
    int result = 0;
    char name[80];
    for(int lang = 0; lang < 2 && !result; lang++) {
        const char *language = lang ? "en" : "zh";
        app_language_set(lang ? APP_LANGUAGE_EN : APP_LANGUAGE_ZH_CN);
        for(int egg = 0; egg <= 5 && !result; egg++) {
            if(lang && egg == 1) continue;
            char value[2] = {(char)('0' + egg), '\0'};
            if(setenv("ARK_BOOT_EGG", value, 1) != 0) { result = 1; break; }
            show_boot();
            snprintf(name, sizeof(name), "font-boot-%d-%s", egg, language);
            result |= save_i1_frame(display, dir, name);
            show_sleep();
            snprintf(name, sizeof(name), "font-sleep-%d-%s", egg, language);
            result |= save_i1_frame(display, dir, name);
        }
        show_menu();
        for(uint8_t i = 0; i < APP_MENU_ENTRY_NUM && !result; i++) {
            menu_sel = i;
            g_app_menu_entry.ui_ops->on_msg(APP_UI_MSG_MENU_SEL, &menu_sel, 1);
            snprintf(name, sizeof(name), "font-menu-%u-%s", (unsigned)i, language);
            result |= save_i1_frame(display, dir, name);
        }
        simulator_show(g_app_settings_entry.ui_ops, &g_app_settings_entry);
        for(int row = 0; row < SETTINGS_ROW_NUM && !result; row++) {
            snprintf(name, sizeof(name), "font-settings-%d-%s", row, language);
            result |= save_i1_frame(display, dir, name);
            g_app_settings_entry.ui_ops->on_key(INPUT_PRESS_UP);
        }
        simulator_show(g_app_clock_entry.ui_ops, &g_app_clock_entry);
        snprintf(name, sizeof(name), "font-clock-%s", language);
        result |= save_i1_frame(display, dir, name);
        if(setenv("ARK_PASS_PROFILE", "/missing-ark-font-preview.bin", 1) != 0) {
            result = 1;
            break;
        }
        simulator_show(g_app_pass_entry.ui_ops, &g_app_pass_entry);
        snprintf(name, sizeof(name), "font-pass-empty-%s", language);
        result |= save_i1_frame(display, dir, name);
    }
    simulator_show(NULL, NULL);
    lv_display_delete(display);
    fprintf(stderr, "font I1 preview %s\n", result ? "FAIL" : "PASS");
    return result ? 1 : 0;
}

static int save_prts_preview(lv_display_t *display, const char *dir)
{
    char name[32];
    char egg[2];
    int result = 0;

    if(ensure_preview_dir(dir) != 0) {
        fprintf(stderr, "Cannot create preview directory: %s\n", dir);
        return 1;
    }
    for(int lang = 0; lang < 2; lang++) {
        int english = lang == 1;
        app_language_set(english ? APP_LANGUAGE_EN : APP_LANGUAGE_ZH_CN);
        for(int i = 0; i <= 5; i++) {
            if(english && i == 1) continue; /* no verified English quote 1 */
            egg[0] = (char)('0' + i);
            egg[1] = '\0';
            if(setenv("ARK_BOOT_EGG", egg, 1) != 0) return 1;
            show_boot();
            if(i == 0) snprintf(name, sizeof(name), english ? "boot-normal-en" : "boot-normal");
            else snprintf(name, sizeof(name), english ? "boot-egg-%d-en" : "boot-egg-%d", i);
            result |= save_frame(display, dir, name);
            show_sleep();
            if(i == 0) snprintf(name, sizeof(name), english ? "sleep-normal-en" : "sleep-normal");
            else snprintf(name, sizeof(name), english ? "sleep-egg-%d-en" : "sleep-egg-%d", i);
            result |= save_frame(display, dir, name);
        }
        show_menu();
        result |= save_frame(display, dir, english ? "menu-en" : "menu-zh");
    }
    fprintf(stderr, "PRTS preview %s\n", result == 0 ? "PASS" : "FAIL");
    return result == 0 ? 0 : 1;
}

static int check_color_card(const char *step, int expected_calls)
{
    const simulator_color_card_t card = simulator_color_card();
    const int expected_y = SHELL_BODY_TOP +
        (720 - SHELL_BODY_TOP - SHELL_BODY_BOTTOM - 608) / 2;
    if(card.calls == expected_calls &&
       (expected_calls == 0 || (card.x == SHELL_BODY_PAD_X && card.y == expected_y &&
                                card.w == 440 && card.h == 608))) {
        fprintf(stderr, "%s: %d call(s) at (%d,%d) %dx%d\n",
                step, card.calls, card.x, card.y, card.w, card.h);
        return 0;
    }
    fprintf(stderr, "%s: expected %d centered overlay call(s), got %d at (%d,%d) %dx%d\n",
            step, expected_calls, card.calls, card.x, card.y, card.w, card.h);
    return 1;
}

static int check_pass_layout(lv_display_t *display, const char *preview_dir)
{
    char path[] = "/tmp/ark-pass-layout-XXXXXX";
    char missing_path[sizeof(path) + 8];
    const uint8_t header[16] = {'F', 'F', 'U', 'I', 2, 2, 0xB8, 0x01, 0x60, 0x02};
    uint8_t row[440 / 2];
    int fd = mkstemp(path);
    if(fd < 0) { perror("mkstemp"); return 1; }
    FILE *fp = fdopen(fd, "wb");
    if(!fp) { close(fd); unlink(path); return 1; }
    int result = fwrite(header, 1, sizeof(header), fp) != sizeof(header);
    for(int y = 0; y < 608 && !result; y++) {
        for(int x = 0; x < 440; x += 2) {
            const int border = x < 3 || x >= 438 || y < 3 || y >= 605;
            const int corner = (x < 22 || x >= 418) && (y < 22 || y >= 586);
            const uint8_t code = border ? 0 : corner ? (y < 22 ? 3 : 5) : 1;
            row[x / 2] = (uint8_t)((code << 4) | code);
        }
        result = fwrite(row, 1, sizeof(row), fp) != sizeof(row);
    }
    if(fclose(fp) != 0) result = 1;
    if(result) { unlink(path); return 1; }

    setenv("ARK_PASS_PROFILE", path, 1);
    simulator_reset_color_card();
    simulator_show(g_app_pass_entry.ui_ops, &g_app_pass_entry);
    result |= check_color_card("first color", 1);
    if(preview_dir != NULL) {
        if(ensure_preview_dir(preview_dir) != 0) result = 1;
        else result |= save_frame(display, preview_dir, "pass-color-layout");
    }

    snprintf(missing_path, sizeof(missing_path), "%s.missing", path);
    setenv("ARK_PASS_PROFILE", missing_path, 1);
    simulator_reset_color_card();
    simulator_show(g_app_pass_entry.ui_ops, &g_app_pass_entry);
    result |= check_color_card("empty", 0);

    setenv("ARK_PASS_PROFILE", path, 1);
    g_app_pass_entry.ui_ops->on_key(INPUT_PRESS_SHORT);
    simulator_process_requests();
    result |= check_color_card("empty to color", 1);

    simulator_reset_color_card();
    g_app_pass_entry.ui_ops->on_key(INPUT_PRESS_SHORT);
    simulator_process_requests();
    result |= check_color_card("color reload", 1);

    simulator_show(NULL, NULL);
    unlink(path);
    fprintf(stderr, "pass layout %s\n", result ? "FAIL" : "PASS");
    return result ? 1 : 0;
}

int main(int argc, char **argv)
{
    lv_init();
    if(argc >= 2 && strcmp(argv[1], "--menu-preview") == 0) {
        if(argc != 3 || argv[2][0] == '\0') {
            fputs("Usage: ark-ui-simulator --menu-preview <dir>\n", stderr);
            return 1;
        }
        return save_menu_preview(argv[2]);
    }
    if(argc >= 2 && strcmp(argv[1], "--font-preview") == 0) {
        if(argc != 3 || argv[2][0] == '\0') {
            fputs("Usage: ark-ui-simulator --font-preview <dir>\n", stderr);
            return 1;
        }
        return save_font_preview(argv[2]);
    }
    lv_display_t *display = lv_sdl_window_create(480, 720);
    if(!display) { fputs("LVGL SDL window failed\n", stderr); return 1; }
    lv_sdl_window_set_title(display, "FrameFilm Ark UI simulator");
    if(argc >= 2 && argc <= 3 && strcmp(argv[1], "--pass-layout-check") == 0)
        return check_pass_layout(display, argc == 3 ? argv[2] : NULL);
    if(argc == 3 && strcmp(argv[1], "--prts-preview") == 0) {
        return save_prts_preview(display, argv[2]);
    }
    show_boot();

    if(argc == 3 && strcmp(argv[1], "--smoke") == 0) {
        int result = save_frame(display, argv[2], "boot");
        show_menu();
        result |= save_frame(display, argv[2], "menu");
        press_key(INPUT_PRESS_UP);
        press_key(INPUT_PRESS_SHORT);
        if(simulator_current_entry() != &g_app_pass_entry) result = -1;
        result |= save_frame(display, argv[2], "pass");
        const char *profile_path = getenv("ARK_PASS_PROFILE");
        if(profile_path && profile_path[0]) {
            char *saved_path = strdup(profile_path);
            if(!saved_path) return 1;
            setenv("ARK_PASS_PROFILE", "/missing-ark-pass-test.bin", 1);
            press_key(INPUT_PRESS_SHORT);
            simulator_process_requests();
            result |= save_frame(display, argv[2], "pass-reload-failed");
            setenv("ARK_PASS_PROFILE", saved_path, 1);
            free(saved_path);
            press_key(INPUT_PRESS_SHORT);
            simulator_process_requests();
            result |= save_frame(display, argv[2], "pass-reload-retry");
        }
        press_key(INPUT_PRESS_DOUBLE);
        if(simulator_current_entry() != &g_app_menu_entry || menu_sel != 1) result = -1;
        result |= save_frame(display, argv[2], "menu-return");
        press_key(INPUT_PRESS_SHORT);
        if(simulator_current_entry() != &g_app_pass_entry) result = -1;
        press_key(INPUT_PRESS_LONG);
        press_key(INPUT_PRESS_DOUBLE);
        if(!sleep_keep || simulator_current_entry() != &g_app_pass_entry) result = -1;
        result |= save_frame(display, argv[2], "pass-sleep");
        press_key(INPUT_PRESS_SHORT);
        if(sleep_keep || simulator_current_ops() != app_boot_ops()) result = -1;
        simulator_show(g_app_settings_entry.ui_ops, &g_app_settings_entry);
        simulator_process_requests();
        press_key(INPUT_PRESS_UP);
        press_key(INPUT_PRESS_SHORT);
        simulator_process_requests();
        result |= save_frame(display, argv[2], "settings");
        /* From WIFI, five UP presses reach LANGUAGE in SYSTEM PARAMETERS. */
        for(int i = 0; i < 5; i++) press_key(INPUT_PRESS_UP);
        press_key(INPUT_PRESS_SHORT);
        simulator_process_requests();
        if(app_language_get() != APP_LANGUAGE_EN) result = -1;
        result |= save_frame(display, argv[2], "settings-en");
        press_key(INPUT_PRESS_DOUBLE);
        if(menu_sel != 5) result = -1;
        result |= save_frame(display, argv[2], "menu-en");
        simulator_show(g_app_clock_entry.ui_ops, &g_app_clock_entry);
        result |= save_frame(display, argv[2], "clock");
        press_key(INPUT_PRESS_DOUBLE);
        if(menu_sel != 3) result = -1;
        press_key(INPUT_PRESS_LONG);
        if(sleep_keep || simulator_current_ops() != app_sleep_ops()) result = -1;
        result |= save_frame(display, argv[2], "sleep");
        press_key(INPUT_PRESS_SHORT);
        if(simulator_current_ops() != app_boot_ops()) result = -1;
        fprintf(stderr, "smoke %s\n", result == 0 ? "PASS" : "FAIL");
        return result == 0 ? 0 : 1;
    }

    Uint32 enter_down_at = 0, enter_pending_at = 0;
    int old_up = 0, old_down = 0, old_enter = 0, old_shortcut = 0;
    int enter_long_sent = 0;
    int running = 1;
    while(running) {
        lv_timer_handler();
        simulator_process_requests();
        if(simulator_take_boot_done()) show_menu();
        const Uint8 *keys = SDL_GetKeyboardState(NULL);
        int up = keys[SDL_SCANCODE_UP], down = keys[SDL_SCANCODE_DOWN];
        int enter = keys[SDL_SCANCODE_RETURN] || keys[SDL_SCANCODE_KP_ENTER];
        Uint32 now = SDL_GetTicks();
        if(up && !old_up) press_key(INPUT_PRESS_UP);
        if(down && !old_down) press_key(INPUT_PRESS_DOWN);
        if(enter && !old_enter) { enter_down_at = now; enter_long_sent = 0; }
        if(enter && !enter_long_sent && now - enter_down_at >= 1000) {
            enter_long_sent = 1;
            enter_pending_at = 0;
            press_key(INPUT_PRESS_LONG);
        }
        if(!enter && old_enter) {
            if(enter_long_sent) enter_long_sent = 0;
            else if(enter_pending_at && now - enter_pending_at < 350) {
                enter_pending_at = 0; press_key(INPUT_PRESS_DOUBLE);
            }
            else enter_pending_at = now;
        }
        if(enter_pending_at && now - enter_pending_at >= 350) {
            enter_pending_at = 0; press_key(INPUT_PRESS_SHORT);
        }
        int shortcut = keys[SDL_SCANCODE_1] ? 1 : keys[SDL_SCANCODE_2] ? 2 :
                       keys[SDL_SCANCODE_3] ? 3 : keys[SDL_SCANCODE_4] ? 4 :
                       keys[SDL_SCANCODE_5] ? 5 : keys[SDL_SCANCODE_6] ? 6 : 0;
        if(!sleep_keep && shortcut && shortcut != old_shortcut) {
            if(shortcut == 1) show_boot();
            if(shortcut == 2) show_menu();
            if(shortcut == 3) simulator_show(g_app_settings_entry.ui_ops, &g_app_settings_entry);
            if(shortcut == 4) show_sleep();
            if(shortcut == 5) simulator_show(g_app_pass_entry.ui_ops, &g_app_pass_entry);
            if(shortcut == 6) simulator_show(g_app_clock_entry.ui_ops, &g_app_clock_entry);
        }
        if(keys[SDL_SCANCODE_ESCAPE]) running = 0;
        old_up = up; old_down = down; old_enter = enter; old_shortcut = shortcut;
        SDL_Delay(10);
    }
    simulator_show(NULL, NULL);
    lv_deinit();
    return 0;
}
