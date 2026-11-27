/*********************************************************************
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Copyright (c) 2026 kiritro
 *
 * FileName : /film_service/src/service_screen.c
 * Description: 信息屏薄客户端：拉取 FFSP → 校验 → 上屏 → 缓存/版本
 *
 *********************************************************************/

/*********************************************************************
 * INCLUDES
 */
#include <string.h>
#include <stdio.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_sntp.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "sys_log.h"
#include "sys_cfg.h"
#include "hal_epd.h"
#include "hal_input.h"
#include "hal_sd.h"
#include "service_param.h"
#include "service_wifi.h"
#include "service_monitor.h"
#include "service_ble_gatts.h"
#include "screen_pkg.h"
#include "storage_cache.h"
#include "service_screen.h"

/*********************************************************************
 * MACROS
 */
#define SCREEN_TAG                  "screen"

#define SCREEN_TASK_STACK           (8192)
#define SCREEN_TASK_PRIO            (5)
#define SCREEN_QUEUE_LEN            (4)

#define SCREEN_FB_BYTES             ((EPD_WIDTH * EPD_HEIGHT) / 8)
#define SCREEN_HTTP_BUF_SIZE        FFSP_MAX_PACKAGE_SIZE

#define SCREEN_WIFI_TIMEOUT_MS      (15000)
#define SCREEN_HTTP_TIMEOUT_MS      (20000)

#define SCREEN_NVS_KEY_PAGE         "sc_page"
#define SCREEN_NVS_KEY_VER          "sc_ver"

/*********************************************************************
 * TYPEDEFS
 */
typedef enum
{
    SCREEN_MSG_SYNC = 1,
    SCREEN_MSG_NEXT,
    SCREEN_MSG_PREV,
    SCREEN_MSG_FORCE,
} screen_msg_type_t;

typedef struct
{
    uint8_t type;
} screen_msg_t;

/*********************************************************************
 * LOCAL VARIABLES
 */
static TaskHandle_t m_task_hdl = NULL;
static QueueHandle_t m_queue = NULL;

static volatile bool m_busy = false;
static uint16_t m_page_id = 0;
static uint32_t m_version = 0;

static uint8_t *m_pkg_buf = NULL;
static uint8_t *m_frame = NULL;
static uint8_t *m_baseline = NULL;

static uint32_t m_http_len = 0;
static bool m_http_overflow = false;

/*********************************************************************
 * LOCAL FUNCTIONS
 */
static void screen_task(void *arg);
static void screen_msg_post(screen_msg_type_t type);
static bool screen_sync(uint16_t page, bool force_full);
static bool screen_wait_wifi(uint32_t timeout_ms);
static void screen_sntp_sync(void);
static void screen_state_load(void);
static void screen_state_save(void);
static esp_err_t screen_http_event(esp_http_client_event_t *evt);

static void screen_key_short(void)  { screen_msg_post(SCREEN_MSG_NEXT); }
static void screen_key_long(void)   { screen_msg_post(SCREEN_MSG_FORCE); }
static void screen_key_double(void) { screen_msg_post(SCREEN_MSG_PREV); }

static void *screen_alloc(size_t size)
{
    void *p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
    if (p == NULL)
    {
        p = heap_caps_malloc(size, MALLOC_CAP_8BIT);
    }
    return p;
}

/*********************************************************************
 * GLOBAL FUNCTIONS
 */

void service_screen_init(void)
{
    if (m_task_hdl != NULL)
    {
        return;
    }

    screen_state_load();

    m_pkg_buf = (uint8_t *)screen_alloc(SCREEN_HTTP_BUF_SIZE);
    m_frame = (uint8_t *)screen_alloc(SCREEN_FB_BYTES);
    m_baseline = (uint8_t *)screen_alloc(SCREEN_FB_BYTES);
    if (m_pkg_buf == NULL || m_frame == NULL || m_baseline == NULL)
    {
        sys_loge(SCREEN_TAG, "alloc buffers failed");
        return;
    }

    storage_cache_init();

    m_queue = xQueueCreate(SCREEN_QUEUE_LEN, sizeof(screen_msg_t));
    if (m_queue == NULL)
    {
        sys_loge(SCREEN_TAG, "create queue failed");
        return;
    }

    hal_input_register_cb(INPUT_PRESS_SHORT, screen_key_short);
    hal_input_register_cb(INPUT_PRESS_DOUBLE, screen_key_double);
    hal_input_register_cb(INPUT_PRESS_LONG, screen_key_long);

    if (xTaskCreate(screen_task, "screen_task", SCREEN_TASK_STACK, NULL,
                    SCREEN_TASK_PRIO, &m_task_hdl) != pdPASS)
    {
        sys_loge(SCREEN_TAG, "create task failed");
        m_task_hdl = NULL;
        return;
    }

    sys_logi(SCREEN_TAG, "screen service started (page=%u ver=%lu)",
             m_page_id, (unsigned long)m_version);

    screen_msg_post(SCREEN_MSG_SYNC);
}

void service_screen_request_sync(bool force_full)
{
    screen_msg_post(force_full ? SCREEN_MSG_FORCE : SCREEN_MSG_SYNC);
}

void service_screen_next_page(void)
{
    screen_msg_post(SCREEN_MSG_NEXT);
}

void service_screen_prev_page(void)
{
    screen_msg_post(SCREEN_MSG_PREV);
}

bool service_screen_is_busy(void)
{
    return m_busy;
}

uint16_t service_screen_get_page(void)
{
    return m_page_id;
}

uint32_t service_screen_get_version(void)
{
    return m_version;
}

/*********************************************************************
 * LOCAL FUNCTIONS
 */

static void screen_msg_post(screen_msg_type_t type)
{
    if (m_queue == NULL)
    {
        return;
    }

    screen_msg_t msg = { .type = (uint8_t)type };
    xQueueSend(m_queue, &msg, 0); /* 忙时丢弃，避免堆积 */
}

static void screen_task(void *arg)
{
    (void)arg;

    for (;;)
    {
        screen_msg_t msg;
        if (xQueueReceive(m_queue, &msg, portMAX_DELAY) != pdPASS)
        {
            continue;
        }

        switch (msg.type)
        {
        case SCREEN_MSG_NEXT:
            if (m_page_id < 0xFFFF)
            {
                m_page_id++;
            }
            m_version = 0; /* 新页面没有基准，请求全量 */
            screen_sync(m_page_id, true);
            break;

        case SCREEN_MSG_PREV:
            if (m_page_id > 0)
            {
                m_page_id--;
            }
            m_version = 0;
            screen_sync(m_page_id, true);
            break;

        case SCREEN_MSG_FORCE:
            screen_sync(m_page_id, true);
            break;

        case SCREEN_MSG_SYNC:
        default:
            screen_sync(m_page_id, false);
            break;
        }
    }
}

static bool screen_wait_wifi(uint32_t timeout_ms)
{
    uint32_t waited = 0;

    if (!g_service_param.network.wifi_enable)
    {
        sys_logw(SCREEN_TAG, "WiFi disabled");
        return false;
    }
    if (g_service_param.network.wifi_ssid[0] == '\0')
    {
        sys_logw(SCREEN_TAG, "WiFi SSID empty");
        return false;
    }

    if (!service_wifi_get_connect_status())
    {
        service_wifi_connect();
    }

    while (!service_wifi_get_connect_status() && waited < timeout_ms)
    {
        vTaskDelay(pdMS_TO_TICKS(200));
        waited += 200;
    }

    return service_wifi_get_connect_status() != 0;
}

static void screen_sntp_sync(void)
{
    time_t now = 0;
    int i;

    time(&now);
    if (now > 1700000000)
    {
        return; /* 已有有效时间 */
    }

    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();

    for (i = 0; i < 50; i++)
    {
        time(&now);
        if (now > 1700000000)
        {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    esp_sntp_stop();

    if (now > 1700000000)
    {
        sys_logi(SCREEN_TAG, "SNTP synced");
    }
    else
    {
        sys_logw(SCREEN_TAG, "SNTP timeout, HTTPS may fail");
    }
}

static void screen_state_load(void)
{
    nvs_handle_t handle;
    uint16_t page = 0;
    uint32_t ver = 0;

    if (nvs_open(SYS_M_NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK)
    {
        return;
    }

    if (nvs_get_u16(handle, SCREEN_NVS_KEY_PAGE, &page) == ESP_OK)
    {
        m_page_id = page;
    }
    if (nvs_get_u32(handle, SCREEN_NVS_KEY_VER, &ver) == ESP_OK)
    {
        m_version = ver;
    }

    nvs_close(handle);
}

static void screen_state_save(void)
{
    nvs_handle_t handle;

    if (nvs_open(SYS_M_NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK)
    {
        sys_logw(SCREEN_TAG, "nvs open failed");
        return;
    }

    nvs_set_u16(handle, SCREEN_NVS_KEY_PAGE, m_page_id);
    nvs_set_u32(handle, SCREEN_NVS_KEY_VER, m_version);
    nvs_commit(handle);
    nvs_close(handle);
}

static esp_err_t screen_http_event(esp_http_client_event_t *evt)
{
    if (evt == NULL)
    {
        return ESP_OK;
    }

    switch (evt->event_id)
    {
    case HTTP_EVENT_ON_CONNECTED:
        m_http_len = 0;
        m_http_overflow = false;
        break;

    case HTTP_EVENT_ON_DATA:
        if (evt->data_len > 0)
        {
            if (m_http_len + (uint32_t)evt->data_len <= SCREEN_HTTP_BUF_SIZE)
            {
                memcpy(m_pkg_buf + m_http_len, evt->data, evt->data_len);
                m_http_len += evt->data_len;
            }
            else
            {
                m_http_overflow = true;
            }
        }
        break;

    default:
        break;
    }

    return ESP_OK;
}

static bool screen_sync(uint16_t page, bool force_full)
{
    const char *base_url = g_service_param.network.film_api_url;
    char url[320];
    bool have_baseline = false;
    bool ok = false;
    int attempt;

    m_busy = true;

    if (base_url[0] == '\0')
    {
        sys_logw(SCREEN_TAG, "screen URL empty, skip sync");
        m_busy = false;
        return false;
    }
    if (!screen_wait_wifi(SCREEN_WIFI_TIMEOUT_MS))
    {
        sys_logw(SCREEN_TAG, "WiFi not connected, skip sync");
        m_busy = false;
        return false;
    }

    if (strncmp(base_url, "https://", 8) == 0)
    {
        screen_sntp_sync();
    }

    /* 只有页面与版本都匹配缓存时，才具备差分基准 */
    if (!force_full && m_version != 0)
    {
        uint32_t size = 0;
        uint32_t ver = 0;
        uint16_t cached_page = 0;

        if (storage_cache_load_last_frame(m_baseline, SCREEN_FB_BYTES,
                                          &size, &ver, &cached_page) == ESP_OK &&
            size == SCREEN_FB_BYTES && ver == m_version && cached_page == page)
        {
            have_baseline = true;
        }
    }

    for (attempt = 0; attempt < 2 && !ok; attempt++)
    {
        uint32_t base_version = have_baseline ? m_version : 0;
        const char *sep = strchr(base_url, '?') ? "&" : "?";
        const char *device_id = g_service_param.network.film_device_id[0]
                                ? g_service_param.network.film_device_id : "unknown";
        ffsp_package_t pkg;
        int status;
        esp_err_t err;

        snprintf(url, sizeof(url),
                 "%s%sdevice_id=%s&panel=0x%02X&page=%u&base=%lu",
                 base_url, sep, device_id, EPD_PANEL_ID, page,
                 (unsigned long)base_version);

        m_http_len = 0;
        m_http_overflow = false;

        esp_http_client_config_t config = {
            .url = url,
            .event_handler = screen_http_event,
            .timeout_ms = SCREEN_HTTP_TIMEOUT_MS,
            .buffer_size = 4096,
            .crt_bundle_attach = esp_crt_bundle_attach,
            .method = HTTP_METHOD_GET,
        };

        esp_http_client_handle_t client = esp_http_client_init(&config);
        if (client == NULL)
        {
            sys_loge(SCREEN_TAG, "http init failed");
            break;
        }

        esp_http_client_set_header(client, "Accept", "application/octet-stream");
        err = esp_http_client_perform(client);
        status = esp_http_client_get_status_code(client);
        esp_http_client_cleanup(client);

        if (err != ESP_OK)
        {
            sys_loge(SCREEN_TAG, "http perform failed: %s", esp_err_to_name(err));
            break;
        }

        if (status == 204)
        {
            sys_logi(SCREEN_TAG, "204 no change");
            ok = true;
            break;
        }
        if (status == 409)
        {
            sys_logw(SCREEN_TAG, "409 base mismatch, retry full");
            have_baseline = false;
            force_full = true;
            continue;
        }
        if (status != 200)
        {
            sys_loge(SCREEN_TAG, "http status %d", status);
            break;
        }
        if (m_http_overflow)
        {
            sys_loge(SCREEN_TAG, "package too large");
            break;
        }

        if (ffsp_parse(m_pkg_buf, m_http_len, &pkg) != ESP_OK)
        {
            sys_loge(SCREEN_TAG, "ffsp parse failed");
            break;
        }
        if (pkg.header.width != EPD_WIDTH || pkg.header.height != EPD_HEIGHT)
        {
            sys_loge(SCREEN_TAG, "panel size mismatch: %ux%u",
                     pkg.header.width, pkg.header.height);
            break;
        }

        bool need_full = ffsp_is_full(&pkg) || (base_version == 0) || force_full;
        uint32_t i;
        for (i = 0; i < pkg.header.region_count; i++)
        {
            if (pkg.regions[i].refresh == FFSP_REFRESH_FULL)
            {
                need_full = true;
            }
        }

        if (need_full)
        {
            if (ffsp_is_full(&pkg) || base_version == 0)
            {
                ffsp_composite(&pkg, m_frame);
            }
            else
            {
                /* 差分包中夹带 full 区域：在旧帧上合成后整屏刷新 */
                memcpy(m_frame, m_baseline, SCREEN_FB_BYTES);
                ffsp_apply_regions(&pkg, m_frame);
            }

            hal_epd_display_init();
            hal_epd_display_mono(m_frame);
            memcpy(m_baseline, m_frame, SCREEN_FB_BYTES);
            have_baseline = true;
        }
        else
        {
            if (!have_baseline)
            {
                sys_logw(SCREEN_TAG, "delta without baseline, retry full");
                force_full = true;
                continue;
            }

            hal_epd_display_init_partial();
            hal_epd_restore_baseline(m_baseline);

            for (i = 0; i < pkg.header.region_count; i++)
            {
                const ffsp_region_t *r = &pkg.regions[i];
                hal_epd_partial_update(0, r->x, r->y, r->w, r->h,
                                       ffsp_region_data(&pkg, r), 0);
            }
            hal_epd_refresh_partial();

            ffsp_apply_regions(&pkg, m_baseline);
        }

        hal_epd_pwroff();

        m_page_id = pkg.header.page_id;
        m_version = pkg.header.target_version;
        screen_state_save();
        storage_cache_save_last_frame(m_baseline, SCREEN_FB_BYTES, m_version, m_page_id);

        sys_logi(SCREEN_TAG, "display updated: page=%u ver=%lu",
                 m_page_id, (unsigned long)m_version);
        ok = true;
    }

    m_busy = false;

    if (ok)
    {
        if (g_service_param.sleep.sleep_mode && !service_ble_gatts_get_connect())
        {
            service_monitor_request_sleep();
        }
    }

    return ok;
}
