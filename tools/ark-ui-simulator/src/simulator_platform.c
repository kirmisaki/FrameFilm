#include <SDL2/SDL.h>
#include <stdio.h>
#include <string.h>
#include "lvgl.h"
#include "app_manager.h"
#include "app_boot_cfg.h"
#include "app_image.h"
#include "app_animation.h"
#include "service_param.h"
#include "service_file.h"
#include "service_wifi.h"
#include "service_ble.h"
#include "service_ble_gatts.h"
#include "service_monitor.h"
#include "hal_epd.h"
#include "hal_sd.h"
#include "hal_pwr.h"
#include "hal_bat.h"
#include "freertos/task.h"
#include "ui_core.h"
#include "ui_display.h"
#include "simulator.h"

typedef struct {
    uint32_t cmd;
    uint8_t len;
    uint8_t data[16];
} sim_request_t;

static sim_request_t requests[16];
static unsigned request_head, request_tail;
static lv_obj_t *root;
static const app_ui_ops_t *current_ops;
static const app_entry_t *current_entry;
static int boot_done;
static simulator_color_card_t color_card;
static app_boot_cfg_t boot_cfg = {APP_BOOT_PAGE_SHOW, APP_START_MENU, APP_LANGUAGE_ZH_CN};

ServiceParam_Def_t g_service_param = {
    .sys = {.led_mode = SERVICE_LED_MODE_ALWAYS},
    .sleep = {.sleep_mode = 1, .sleep_auto = 1, .sleep_time = 30},
    .network = {.wifi_enable = 1, .film_heartbeat_interval = 30},
    .ble = {.ble_enable = 1},
};

void simulator_show(const app_ui_ops_t *ops, const app_entry_t *entry)
{
    if(current_ops && current_ops->destroy) current_ops->destroy();
    lv_obj_t *old_root = root;
    current_ops = ops;
    current_entry = entry;
    if(!ops) { root = NULL; return; }
    root = lv_obj_create(NULL);
    lv_obj_remove_style_all(root);
    lv_obj_set_style_bg_color(root, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, LV_PART_MAIN);
    if(ops->create) ops->create(root);
    lv_screen_load(root);
    if(old_root) lv_obj_delete(old_root);
}

const app_entry_t *simulator_current_entry(void) { return current_entry; }
const app_ui_ops_t *simulator_current_ops(void) { return current_ops; }
int simulator_take_boot_done(void) { int value = boot_done; boot_done = 0; return value; }

int app_manager_post_ui_msg(uint32_t cmd, const void *data, uint8_t len)
{
    unsigned next = (request_tail + 1) % 16;
    if(next == request_head || len > sizeof(requests[0].data)) return -1;
    sim_request_t *r = &requests[request_tail];
    r->cmd = cmd; r->len = len;
    if(len && data) memcpy(r->data, data, len);
    request_tail = next;
    return 0;
}

void simulator_process_requests(void)
{
    while(request_head != request_tail) {
        sim_request_t r = requests[request_head];
        request_head = (request_head + 1) % 16;
        if(r.cmd == APP_UI_REQ_BOOT_DONE) { boot_done = 1; continue; }
        if(current_entry && current_entry->on_event) {
            app_event_t event = {.type = APP_EVT_UI_MSG, .cmd = r.cmd, .len = r.len};
            if(r.len) memcpy(event.payload, r.data, r.len);
            current_entry->on_event(&event);
        }
    }
}

int ui_core_post(uint32_t cmd, const void *data, uint8_t len)
{
    if(current_ops && current_ops->on_msg) current_ops->on_msg(cmd, data, len);
    return 0;
}
int ui_core_is_ready(void) { return 1; }
void simulator_reset_color_card(void) { memset(&color_card, 0, sizeof(color_card)); }
simulator_color_card_t simulator_color_card(void) { return color_card; }
int ui_display_set_color_card(const uint8_t *pixels, int x, int y, int w, int h)
{
    (void)pixels;
    color_card.calls++;
    color_card.x = x; color_card.y = y;
    color_card.w = w; color_card.h = h;
    return 0;
}
void ui_display_clear_color_card(void) { }
void ui_display_invalidate_all(void)
{
    lv_obj_t *screen = lv_screen_active();
    if(screen != NULL) lv_obj_invalidate(screen);
}
void ui_core_page_enter(uint8_t app_id, const app_ui_ops_t *ops) { (void)app_id; simulator_show(ops, NULL); }
void ui_core_page_exit(void) { simulator_show(NULL, NULL); }
void app_manager_sleep_show(const app_ui_ops_t *ops) { simulator_show(ops, NULL); }
void service_monitor_request_sleep(void) { }
void service_monitor_set_auto_sleep_timeout(uint32_t sec) { (void)sec; }

void hal_epd_mono_session_begin(void) { }
void hal_epd_mono_session_end(void) { }
int hal_pwr_wake_condition_met(void) { return 0; }
uint32_t hal_pwr_get_timer_wakeup(void)
{
    return g_service_param.sleep.sleep_auto ? g_service_param.sleep.sleep_time : 0;
}
int hal_bat_get_percent(void) { return 82; }
int hal_sd_get_status(void) { return SD_MOUNT; }
TickType_t xTaskGetTickCount(void) { return SDL_GetTicks(); }
void vTaskDelay(TickType_t ticks) { SDL_Delay(ticks); }
uint32_t service_file_get_count(void) { return 3; }
uint8_t service_wifi_get_connect_status(void) { return g_service_param.network.wifi_enable; }
uint8_t service_ble_gatts_get_connect(void) { return g_service_param.ble.ble_enable; }
void service_wifi_init(void) { }
void service_wifi_disconnect(void) { }
void service_wifi_deinit(void) { }
void service_ble_apply_enable(uint8_t on) { g_service_param.ble.ble_enable = on; }
void service_param_save(void) { }

/* The settings page stores a single versioned timeout selection in app4. */
static uint8_t settings_cfg_value, settings_cfg_version;

int service_param_app_load(uint8_t app_id, void *buf, uint16_t size, uint8_t ver)
{
    if(app_id != SERVICE_PARAM_APP_ID_SETTINGS || size != 1 ||
       settings_cfg_version == 0 || settings_cfg_version != ver) return -1;
    memcpy(buf, &settings_cfg_value, size);
    return 0;
}

int service_param_app_save(uint8_t app_id, const void *buf, uint16_t size, uint8_t ver)
{
    if(app_id != SERVICE_PARAM_APP_ID_SETTINGS || size != 1) return -1;
    memcpy(&settings_cfg_value, buf, size);
    settings_cfg_version = ver;
    return 0;
}

const app_boot_cfg_t *app_boot_cfg_get(void) { return &boot_cfg; }
void app_boot_cfg_set(uint8_t boot_page, uint8_t start_app)
{
    boot_cfg.boot_page = boot_page; boot_cfg.start_app = start_app;
}
uint8_t app_language_get(void) { return boot_cfg.language; }
void app_language_set(uint8_t language)
{
    if(language <= APP_LANGUAGE_EN) boot_cfg.language = language;
}

uint8_t app_manager_param_get(uint8_t app_id, uint8_t *out, uint8_t max)
{
    if(max < 7) return 0;
    out[0] = 1; out[1] = 1; out[2] = 0;
    out[3] = 2; out[4] = 2;
    if(app_id == APP_ID_ANIMATION) { out[5] = 1; out[6] = 0xF4; }
    else { out[5] = 0; out[6] = 30; }
    return 7;
}
void app_manager_param_set(uint8_t app_id, const uint8_t *tlv, uint8_t len)
{ (void)app_id; (void)tlv; (void)len; }
