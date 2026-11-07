/*********************************************************************
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Copyright (c) 2026 kiritro
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * 
 * FileName : /film_service/src/service_wifi.c
 * Author: Kiritro  Version: v0.1  Date: 2026/7/20
 * Description: WiFi服务
 * ChangeLog: Change Notes
 *
 *********************************************************************/

/*********************************************************************
 * INCLUDES
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <sys/time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "freertos/portmacro.h"

#include "esp_http_client.h"
#include "esp_heap_caps.h"
#include "esp_memory_utils.h"
#include "esp_log.h"
#include "cJSON.h"

#include "sys_log.h"
#include "sys_event.h"
#include "sys_com.h"
#include "hal_bat.h"
#include "hal_wifi.h"
#include "service_param.h"
#include "service_file.h"
#include "service_wifi.h"
#include "service_ota.h"
#include "service_ble.h"       /* service_ble_apply_enable：心跳下发 ble_enable 时同款起停 */
#ifdef ARK_AP_FILE_BENCH
#include <errno.h>
#include "lwip/sockets.h"
#include "esp_ota_ops.h"
#include "ap_file_bench_secret.h"
#endif

/*********************************************************************
 * MACROS
 */
#define WIFI_SERVICE_TAG        "wifi_service"

/*********************************************************************
* TYPEDEFS
*/


/*********************************************************************
 * CONSTANTS
 */


/*********************************************************************
 * LOCAL VARIABLES
 */
static wifi_download_state_t g_download_state = WIFI_DOWNLOAD_IDLE;
static uint8_t g_download_progress = 0;
static int g_download_content_length = 0;
static int g_download_received = 0;
static char g_download_filename[256] = {0};
static uint8_t *g_download_buffer = NULL;

typedef struct {
    char ssid[33];
    char password[64];
    char url[512];
    char path[512];
    bool file;
    bool ota;
    bool softap;
    uint32_t size;
    uint8_t sha[32];
} wifi_direct_args_t;

static portMUX_TYPE g_direct_lock = portMUX_INITIALIZER_UNLOCKED;
static wifi_direct_status_t g_direct_status;
static bool g_direct_busy;
static bool g_direct_cancel;
static bool g_direct_apply;
static wifi_direct_args_t *g_softap_args;
static bool g_softap_submitted;
static bool g_softap_accepting;
static char g_softap_password[33];
static bool g_heartbeat_active;
static bool g_wifi_shutting_down;
static bool g_wifi_deinit_running;
static bool g_legacy_active;
static TaskHandle_t g_heartbeat_task_hdl;

/*********************************************************************
 * GLOBAL VARIABLES
 */


/*********************************************************************
 * LOCAL FUNCTIONS
 */
static void wifi_download_task(void *pvParameters);
static esp_err_t wifi_http_event_handler(esp_http_client_event_t *evt);
static void wifi_direct_task(void *arg);


/*********************************************************************
 * GLOBAL FUNCTIONS
 */

/**
 * [service_wifi_init 初始化WiFi]
 */
void service_wifi_init(void)
{
    if(service_wifi_direct_busy()) return;
    portENTER_CRITICAL(&g_direct_lock);
    if(g_wifi_deinit_running)
    {
        portEXIT_CRITICAL(&g_direct_lock);
        return;
    }
    g_wifi_shutting_down = false;
    portEXIT_CRITICAL(&g_direct_lock);
    if(hal_wifi_initialized())
    {
        sys_logi(WIFI_SERVICE_TAG, "WiFi already initialized");
        return;
    }

    if(!g_service_param.network.wifi_enable)
    {
        sys_logi(WIFI_SERVICE_TAG, "WiFi disabled, skip init");
        return;
    }

    if(hal_wifi_init(false) != ESP_OK) return;

    sys_logi(WIFI_SERVICE_TAG, "WiFi initialized");

    // 启动心跳任务（内部等待 WiFi 连接后再上报）
    service_wifi_heartbeat_start();

    if(strlen(g_service_param.network.wifi_ssid) > 0)
    {
        service_wifi_connect();
    }
}

/**
 * [service_wifi_deinit 反初始化WiFi]
 */
void service_wifi_deinit(void)
{
    portENTER_CRITICAL(&g_direct_lock);
    g_wifi_shutting_down = true;
    g_wifi_deinit_running = true;
    if(g_legacy_active) g_download_state = WIFI_DOWNLOAD_ERROR;
    portEXIT_CRITICAL(&g_direct_lock);
    service_wifi_direct_cancel();
    while(service_wifi_direct_busy()) vTaskDelay(pdMS_TO_TICKS(50));
    for(;;)
    {
        portENTER_CRITICAL(&g_direct_lock);
        bool active = g_legacy_active ||
                      (g_heartbeat_active && xTaskGetCurrentTaskHandle() != g_heartbeat_task_hdl);
        portEXIT_CRITICAL(&g_direct_lock);
        if(!active) break;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    if(!hal_wifi_initialized())
    {
        portENTER_CRITICAL(&g_direct_lock);
        g_wifi_deinit_running = false;
        g_wifi_shutting_down = false;
        portEXIT_CRITICAL(&g_direct_lock);
        return;
    }

    hal_wifi_deinit();

    portENTER_CRITICAL(&g_direct_lock);
    g_wifi_deinit_running = false;
    g_wifi_shutting_down = false;
    portEXIT_CRITICAL(&g_direct_lock);

    sys_logi(WIFI_SERVICE_TAG, "WiFi deinitialized");
}

/**
 * [service_wifi_connect 连接WiFi]
 */
void service_wifi_connect(void)
{
    if(service_wifi_direct_busy()) return;

    if(!hal_wifi_initialized())
    {
        service_wifi_init();
    }

    if(!hal_wifi_initialized())
    {
        sys_logw(WIFI_SERVICE_TAG, "WiFi init failed");
        return;
    }

    if(strlen(g_service_param.network.wifi_ssid) == 0)
    {
        sys_logw(WIFI_SERVICE_TAG, "WiFi SSID is empty");
        return;
    }

    if(hal_wifi_connect(g_service_param.network.wifi_ssid,
                        g_service_param.network.wifi_password) != ESP_OK)
        sys_logw(WIFI_SERVICE_TAG, "WiFi connect failed");

    sys_logi(WIFI_SERVICE_TAG, "Connecting to SSID: %s", g_service_param.network.wifi_ssid);
}

/**
 * [service_wifi_disconnect 断开WiFi]
 */
void service_wifi_disconnect(void)
{
    if(service_wifi_direct_busy()) return;
    if(!hal_wifi_initialized())
    {
        return;
    }

    hal_wifi_disconnect();

    sys_logi(WIFI_SERVICE_TAG, "WiFi disconnected");
}

/**
 * [service_wifi_get_connect_status 获取WiFi连接状态]
 * @return 0：未连接 1：已连接
 */
uint8_t service_wifi_get_connect_status(void)
{
    return hal_wifi_connected() ? 1 : 0;
}

/**
 * [service_wifi_clear_config 清除网络配置信息]
 */
void service_wifi_clear_config(void)
{
    if(service_wifi_direct_busy()) return;
    if(hal_wifi_initialized())
    {
        service_wifi_disconnect();
        service_wifi_deinit();
    }

    g_service_param.network.wifi_enable = 0;
    memset(g_service_param.network.wifi_ssid, 0, sizeof(g_service_param.network.wifi_ssid));
    memset(g_service_param.network.wifi_password, 0, sizeof(g_service_param.network.wifi_password));
    memset(g_service_param.network.film_api_url, 0, sizeof(g_service_param.network.film_api_url));

    service_param_save();

    sys_logi(WIFI_SERVICE_TAG, "WiFi config cleared");
}

bool service_wifi_direct_busy(void)
{
    portENTER_CRITICAL(&g_direct_lock);
    bool busy = g_direct_busy;
    portEXIT_CRITICAL(&g_direct_lock);
    return busy;
}

void service_wifi_direct_get_status(wifi_direct_status_t *out)
{
    if(!out) return;
    portENTER_CRITICAL(&g_direct_lock);
    *out = g_direct_status;
    portEXIT_CRITICAL(&g_direct_lock);
}

void service_wifi_direct_cancel(void)
{
    portENTER_CRITICAL(&g_direct_lock);
    if(g_direct_busy) g_direct_cancel = true;
    portEXIT_CRITICAL(&g_direct_lock);
}

static bool wifi_direct_cancelled(void)
{
    portENTER_CRITICAL(&g_direct_lock);
    bool cancelled = g_direct_cancel;
    portEXIT_CRITICAL(&g_direct_lock);
    return cancelled;
}

static void wifi_direct_status(uint8_t state, uint8_t error, uint32_t received, uint32_t total)
{
    portENTER_CRITICAL(&g_direct_lock);
    g_direct_status.state = state;
    g_direct_status.error = error;
    g_direct_status.received = received;
    g_direct_status.total = total;
    g_direct_status.progress = total ? (uint8_t)((uint64_t)received * 100 / total) : 0;
    portEXIT_CRITICAL(&g_direct_lock);
}

static uint8_t wifi_direct_start(const char *ssid, const char *password, const char *url,
                                 const char *path, uint32_t size, const uint8_t *sha)
{
    wifi_direct_args_t *args;
    if(!ssid || !password || !url || !ssid[0] ||
       strlen(ssid) > 32 || strlen(password) > 63 || strlen(url) >= 512 ||
       strncmp(url, "http://", 7) != 0)
        return 2;
    if(path && (!service_file_save_path_valid(path) || strlen(path) >= sizeof(args->path)))
        return 2;

    args = calloc(1, sizeof(*args));
    if(!args) return 3;
    strlcpy(args->ssid, ssid, sizeof(args->ssid));
    strlcpy(args->password, password, sizeof(args->password));
    strlcpy(args->url, url, sizeof(args->url));
    args->file = path != NULL;
    if(path) strlcpy(args->path, path, sizeof(args->path));

    portENTER_CRITICAL(&g_direct_lock);
    if(g_direct_busy || g_wifi_deinit_running || g_legacy_active)
    {
        uint8_t result = 1;
        if(g_direct_busy && g_softap_args && g_softap_accepting && !g_softap_submitted && !g_direct_cancel &&
           strcmp(ssid, g_softap_args->ssid) == 0 && strcmp(password, g_softap_args->password) == 0)
        {
            strlcpy(g_softap_args->url, args->url, sizeof(args->url));
            g_softap_args->file = path != NULL;
            if(path) strlcpy(g_softap_args->path, path, sizeof(args->path));
            g_softap_args->ota = sha != NULL;
            g_softap_args->size = size;
            if(sha) memcpy(g_softap_args->sha, sha, 32);
            g_softap_submitted = true;
            g_softap_accepting = false;
            result = 0;
        }
        portEXIT_CRITICAL(&g_direct_lock);
        free(args);
        return result;
    }
    g_direct_busy = true;
    g_direct_cancel = false;
    g_direct_apply = false;
    args->ota = sha != NULL;
    args->size = size;
    if(sha) memcpy(args->sha, sha, 32);
    g_wifi_shutting_down = false;
    g_direct_status = (wifi_direct_status_t){.state = WIFI_DIRECT_CONNECTING};
    portEXIT_CRITICAL(&g_direct_lock);

    if(xTaskCreate(wifi_direct_task, "wifi_direct", 6144, args, 5, NULL) != pdPASS)
    {
        portENTER_CRITICAL(&g_direct_lock);
        g_direct_status.state = WIFI_DIRECT_ERROR;
        g_direct_status.error = WIFI_DIRECT_ERR_RESOURCE;
        g_direct_busy = false;
        portEXIT_CRITICAL(&g_direct_lock);
        free(args);
        return 3;
    }
    return 0;
}

bool service_wifi_softap_waiting(void)
{
    portENTER_CRITICAL(&g_direct_lock);
    bool waiting = g_softap_args && g_softap_accepting && !g_softap_submitted && !g_direct_cancel;
    portEXIT_CRITICAL(&g_direct_lock);
    return waiting;
}

uint8_t service_wifi_softap_prepare(char ssid[33], char password[64])
{
    wifi_direct_args_t *args = calloc(1, sizeof(*args));
    if(!args) return 3;
    args->softap = true;
    if(hal_wifi_softap_identity(args->ssid, sizeof(args->ssid)) != ESP_OK) {
        free(args);
        return 3;
    }
    /* Stable for this boot: Android can reuse approval for the same device AP. */
    if(!g_softap_password[0]) {
        static const char hex[] = "0123456789ABCDEF";
        uint8_t random[16];
        esp_fill_random(random, sizeof(random));
        for(unsigned i = 0; i < sizeof(random); ++i) {
            g_softap_password[2*i] = hex[random[i] >> 4];
            g_softap_password[2*i+1] = hex[random[i] & 15];
        }
    }
    strlcpy(args->password, g_softap_password, sizeof(args->password));
    strlcpy(ssid, args->ssid, 33);
    strlcpy(password, args->password, 64);
    portENTER_CRITICAL(&g_direct_lock);
    if(g_direct_busy || g_wifi_deinit_running || g_legacy_active) {
        portEXIT_CRITICAL(&g_direct_lock);
        free(args);
        return 1;
    }
    g_direct_busy = true;
    g_direct_cancel = false;
    g_direct_apply = false;
    g_wifi_shutting_down = false;
    g_softap_submitted = false;
    g_softap_accepting = false;
    g_softap_args = args;
    g_direct_status = (wifi_direct_status_t){.state = WIFI_DIRECT_CONNECTING};
    portEXIT_CRITICAL(&g_direct_lock);
    if(xTaskCreate(wifi_direct_task, "wifi_direct", 6144, args, 5, NULL) != pdPASS) {
        portENTER_CRITICAL(&g_direct_lock);
        g_softap_args = NULL;
        g_direct_busy = false;
        g_direct_status.state = WIFI_DIRECT_ERROR;
        g_direct_status.error = WIFI_DIRECT_ERR_RESOURCE;
        portEXIT_CRITICAL(&g_direct_lock);
        free(args);
        return 3;
    }
    return 0;
}

uint8_t service_wifi_direct_start(const char *ssid, const char *password, const char *url)
{
    return wifi_direct_start(ssid, password, url, NULL, 0, NULL);
}
uint8_t service_wifi_direct_file_start(const char *ssid, const char *password, const char *url, const char *path)
{
    if(!path) return 2;
    return wifi_direct_start(ssid, password, url, path, 0, NULL);
}
uint8_t service_wifi_direct_ota_start(const char *ssid, const char *password, const char *url, uint32_t size,
                                      const uint8_t sha[32])
{
    if(!sha || !size || size > service_ota_direct_max())
        return 2;
    return wifi_direct_start(ssid, password, url, NULL, size, sha);
}
uint8_t service_wifi_direct_ota_apply(void)
{
    portENTER_CRITICAL(&g_direct_lock);
    bool ready = g_direct_busy && !g_direct_cancel && g_direct_status.state == WIFI_DIRECT_READY;
    if(ready)
        g_direct_status.state = WIFI_DIRECT_APPLYING;
    portEXIT_CRITICAL(&g_direct_lock);
    if(!ready)
        return 1;
    if(service_ota_direct_activate() != ESP_OK)
    {
        portENTER_CRITICAL(&g_direct_lock);
        g_direct_status.state = WIFI_DIRECT_READY;
        portEXIT_CRITICAL(&g_direct_lock);
        return 2;
    }
    portENTER_CRITICAL(&g_direct_lock);
    g_direct_apply = true;
    portEXIT_CRITICAL(&g_direct_lock);
    return 0;
}

static bool wifi_direct_wait_connected(uint32_t timeout_ms, bool allow_cancel)
{
    uint32_t start = xTaskGetTickCount();
    while(!hal_wifi_connected())
    {
        if((allow_cancel && wifi_direct_cancelled()) ||
           (xTaskGetTickCount() - start) >= pdMS_TO_TICKS(timeout_ms)) return false;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    return true;
}

/* Bounded receive window: file_task remains the only SD writer. */
#define WIFI_DIRECT_SLOT_COUNT 3
#define WIFI_DIRECT_SLOT_SIZE (16 * 1024)
typedef struct {
    QueueHandle_t completed;
    uint8_t *data;
    uint32_t len;
    bool busy;
    int result;
    int64_t write_us;
} wifi_direct_slot_t;

static void wifi_direct_write_done(void *context, int result, int64_t write_us)
{
    wifi_direct_slot_t *slot = context;
    slot->result = result;
    slot->write_us = write_us;
    /* At most SLOT_COUNT outstanding slots, each returned exactly once. */
    BaseType_t queued = xQueueSend(slot->completed, &slot, 0);
    configASSERT(queued == pdPASS);
}

static bool wifi_direct_reap(QueueHandle_t completed, TickType_t wait,
                             unsigned *pending, uint32_t *saved, int64_t *write_us,
                             uint8_t *error, uint32_t total)
{
    wifi_direct_slot_t *slot;
    if(xQueueReceive(completed, &slot, wait) != pdPASS) return false;
    --*pending;
    slot->busy = false;
    *write_us += slot->write_us;
    if(slot->result != 0) *error = WIFI_DIRECT_ERR_SAVE;
    else {
        *saved += slot->len;
        wifi_direct_status(WIFI_DIRECT_DOWNLOADING, 0, *saved, total);
    }
    return true;
}

typedef int (*wifi_direct_read_t)(void *context, char *buffer, int length);

static int wifi_direct_http_read(void *context, char *buffer, int length)
{
    return esp_http_client_read(context, buffer, length);
}

/* All borrowed slots are reaped on every exit, before the caller aborts/commits. */
static uint8_t wifi_direct_receive(wifi_direct_read_t read_source, void *context, const char *source,
                                   uint32_t total, uint32_t *saved_out)
{
    uint8_t error = WIFI_DIRECT_ERR_NONE;
    uint32_t network_bytes = 0, saved = 0;
    unsigned pending = 0, reads = 0, blocks = 0;
    /* These are overlapping stage times, not additive wall-clock components. */
    int64_t read_us = 0, wait_us = 0, write_us = 0;
    int64_t started = esp_timer_get_time();
    uint8_t *pool = heap_caps_malloc(WIFI_DIRECT_SLOT_COUNT * WIFI_DIRECT_SLOT_SIZE,
                                     MALLOC_CAP_SPIRAM);
    QueueHandle_t completed = xQueueCreate(WIFI_DIRECT_SLOT_COUNT, sizeof(wifi_direct_slot_t *));
    if(!pool || !completed) {
        if(pool) heap_caps_free(pool);
        if(completed) vQueueDelete(completed);
        return WIFI_DIRECT_ERR_RESOURCE;
    }
    wifi_direct_slot_t slots[WIFI_DIRECT_SLOT_COUNT] = {0};
    for(unsigned i = 0; i < WIFI_DIRECT_SLOT_COUNT; ++i) {
        slots[i].data = pool + i * WIFI_DIRECT_SLOT_SIZE;
        slots[i].completed = completed;
    }
    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(120000);
    while(network_bytes < total && !error) {
        while(wifi_direct_reap(completed, 0, &pending, &saved, &write_us, &error, total)) {}
        if(error) break;
        if(wifi_direct_cancelled()) { error = WIFI_DIRECT_ERR_CANCELLED; break; }
        wifi_direct_slot_t *slot = NULL;
        for(unsigned i = 0; i < WIFI_DIRECT_SLOT_COUNT; ++i)
            if(!slots[i].busy) { slot = &slots[i]; break; }
        if(!slot) {
            int64_t before = esp_timer_get_time();
            wifi_direct_reap(completed, pdMS_TO_TICKS(100), &pending, &saved,
                             &write_us, &error, total);
            wait_us += esp_timer_get_time() - before;
            continue;
        }
        uint32_t want = total - network_bytes;
        if(want > WIFI_DIRECT_SLOT_SIZE) want = WIFI_DIRECT_SLOT_SIZE;
        slot->len = 0;
        while(slot->len < want) {
            if(wifi_direct_cancelled()) { error = WIFI_DIRECT_ERR_CANCELLED; break; }
            if((int32_t)(xTaskGetTickCount() - deadline) >= 0) {
                error = WIFI_DIRECT_ERR_HTTP; break;
            }
            int64_t before = esp_timer_get_time();
            int got = read_source(context, (char *)slot->data + slot->len, (int)(want - slot->len));
            read_us += esp_timer_get_time() - before;
            ++reads;
            if(got == -ESP_ERR_HTTP_EAGAIN) {
                sys_logw(WIFI_SERVICE_TAG, "%s read timeout; retrying within idle deadline", source);
                continue;
            }
            if(got <= 0) { error = WIFI_DIRECT_ERR_HTTP; break; }
            slot->len += (uint32_t)got;
            network_bytes += (uint32_t)got;
            deadline = xTaskGetTickCount() + pdMS_TO_TICKS(120000);
        }
        if(error) break;
        slot->busy = true;
        int64_t before = esp_timer_get_time();
        int result = service_file_save_data_async(FILE_SAVE_WIFI, slot->data, slot->len,
                                                  wifi_direct_write_done, slot);
        wait_us += esp_timer_get_time() - before;
        if(result != 0) {
            slot->busy = false;
            error = WIFI_DIRECT_ERR_SAVE;
            break;
        }
        ++pending;
        ++blocks;
    }
    /* Never free/reuse a buffer or abort its file while file_task borrows it. */
    while(pending) {
        int64_t before = esp_timer_get_time();
        wifi_direct_reap(completed, portMAX_DELAY, &pending, &saved, &write_us, &error, total);
        wait_us += esp_timer_get_time() - before;
    }
    if(wifi_direct_cancelled()) error = WIFI_DIRECT_ERR_CANCELLED;
    if(!error && (network_bytes != total || saved != total)) error = WIFI_DIRECT_ERR_HTTP;
    *saved_out = saved;
    sys_logi(WIFI_SERVICE_TAG, "%s pipeline: net=%" PRIu32 " saved=%" PRIu32
             " reads=%u blocks=%u read_us=%" PRId64 " queue_wait_us=%" PRId64
             " write_us=%" PRId64 " elapsed_us=%" PRId64,
             source, network_bytes, saved, reads, blocks, read_us, wait_us, write_us,
             esp_timer_get_time() - started);
    vQueueDelete(completed);
    heap_caps_free(pool);
    return error;
}


#ifdef ARK_AP_FILE_BENCH
#define AP_FILE_BENCH_BYTES 47347232U
#define AP_FILE_BENCH_PATH "animation/HT27A001.film"

typedef struct {
    int socket;
    uint32_t network_bytes;
} ap_file_bench_source_t;

static int ap_file_bench_read(void *context, char *buffer, int length)
{
    ap_file_bench_source_t *source = context;
    int got = recv(source->socket, buffer, length, 0);
    if(got < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
        return -ESP_ERR_HTTP_EAGAIN;
    if(got > 0) source->network_bytes += (uint32_t)got;
    return got;
}

static void ap_file_bench_recovery(void *arg)
{
    (void)arg;
    /* Allow the pipeline 15s to drain borrowed file_task buffers and abort its part. */
    vTaskDelay(pdMS_TO_TICKS(285000));
    service_wifi_direct_cancel();
    vTaskDelay(pdMS_TO_TICKS(15000));
    sys_logw(WIFI_SERVICE_TAG, "AP file bench 300s limit: returning to normal Ark");
    sys_reboot();
}

bool service_wifi_ap_file_bench_prepare(void)
{
    static const uint8_t normal_sha[32] = { 0x7c, 0x2c, 0x78, 0xe1, 0xf1, 0xf8, 0x2d, 0x96, 0xad, 0x75, 0x38, 0x97, 0xab, 0x89, 0x19, 0xc4, 0xe4, 0xbe, 0x9f, 0xb4, 0xb6, 0xff, 0x02, 0x03, 0xeb, 0x40, 0x0b, 0xac, 0xc6, 0xce, 0xf3, 0x50 };
    const esp_partition_t *normal = esp_ota_get_next_update_partition(NULL);
    esp_app_desc_t desc;
    if(normal == NULL || normal == esp_ota_get_running_partition() ||
       esp_ota_get_partition_description(normal, &desc) != ESP_OK ||
       strcmp(desc.project_name, "frame_film_ark") != 0 || strcmp(desc.version, "3.2.5") != 0 ||
       memcmp(desc.app_elf_sha256, normal_sha, sizeof(normal_sha)) != 0) {
        sys_loge(WIFI_SERVICE_TAG, "AP file bench normal 7c2c78e1 image missing; diagnostic not started");
        return false;
    }
    if(esp_ota_set_boot_partition(normal) != ESP_OK) {
        sys_loge(WIFI_SERVICE_TAG, "AP file bench cannot arm normal next boot; diagnostic not started");
        return false;
    }
    /* Reserve before service init: ordinary STA init/heartbeat and auto-sleep stay out. */
    portENTER_CRITICAL(&g_direct_lock);
    g_direct_busy = true;
    g_direct_cancel = false;
    g_direct_status = (wifi_direct_status_t){.state = WIFI_DIRECT_CONNECTING};
    portEXIT_CRITICAL(&g_direct_lock);
    if(xTaskCreate(ap_file_bench_recovery, "ap_file_return", 2048, NULL, 1, NULL) != pdPASS) {
        sys_reboot();
        return false;
    }
    sys_logi(WIFI_SERVICE_TAG, "AP file bench next boot=normal 7c2c78e1; maximum runtime=300s");
    return true;
}

static void ap_file_bench_task(void *arg)
{
    (void)arg;
    int listener = -1, client = -1, commit_result = -1;
    uint32_t saved = 0;
    uint8_t error = WIFI_DIRECT_ERR_RESOURCE;
    bool saving = false;
    int64_t started = 0;
    ap_file_bench_source_t source = {.socket = -1};
    vTaskDelay(pdMS_TO_TICKS(12000));
    if(wifi_direct_cancelled()) goto done;
    if(!service_ble_ap_file_bench_stop()) goto done;
    char ssid[32];
    if(hal_wifi_ap_file_bench_start(ARK_AP_FILE_BENCH_PASSWORD, ssid, sizeof(ssid)) != ESP_OK)
        goto done;
    listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if(listener < 0) goto done;
    int reuse = 1;
    struct timeval timeout = {.tv_sec = 60};
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    if(setsockopt(listener, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) != 0) goto done;
    struct sockaddr_in address = {
        .sin_family = AF_INET, .sin_port = htons(5001), .sin_addr.s_addr = htonl(INADDR_ANY)
    };
    if(bind(listener, (struct sockaddr *)&address, sizeof(address)) != 0 || listen(listener, 1) != 0)
        goto done;
    sys_logi(WIFI_SERVICE_TAG, "AP file bench SSID=%s IP=192.168.4.1 channel=1 TCP=5001 bytes=%u; BLE off",
             ssid, (unsigned)AP_FILE_BENCH_BYTES);
    error = WIFI_DIRECT_ERR_CONNECT;
    client = accept(listener, NULL, NULL);
    if(client < 0 || wifi_direct_cancelled()) goto done;
    source.socket = client;
    timeout.tv_sec = 1;
    if(setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) != 0) goto done;
    timeout.tv_sec = 5;
    if(setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) != 0) goto done;
    started = esp_timer_get_time();
    error = WIFI_DIRECT_ERR_SAVE;
    if(service_file_save_start_to(FILE_SAVE_WIFI, AP_FILE_BENCH_PATH, AP_FILE_BENCH_BYTES) != 0)
        goto done;
    saving = true;
    wifi_direct_status(WIFI_DIRECT_DOWNLOADING, 0, 0, AP_FILE_BENCH_BYTES);
    error = wifi_direct_receive(ap_file_bench_read, &source, "AP TCP", AP_FILE_BENCH_BYTES, &saved);
    if(!error) {
        commit_result = service_file_save_stop(FILE_SAVE_WIFI, 0);
        if(commit_result != 0) error = WIFI_DIRECT_ERR_SAVE;
        else saving = false;
    }
done:
    if(saving) service_file_save_abort(FILE_SAVE_WIFI);
    if(wifi_direct_cancelled() && commit_result != 0) error = WIFI_DIRECT_ERR_CANCELLED;
    int64_t elapsed = started ? esp_timer_get_time() - started : 0;
    sys_logi(WIFI_SERVICE_TAG, "AP file bench net=%" PRIu32 " saved=%" PRIu32
             " elapsed_us=%" PRId64 " commit=%d error=%u",
             source.network_bytes, saved, elapsed, commit_result, (unsigned)error);
    if(client >= 0) {
        char reply[128];
        int length = snprintf(reply, sizeof(reply), "%s net=%" PRIu32 " saved=%" PRIu32
                              " elapsed_us=%" PRId64 " commit=%d error=%u\n",
                              error ? "ERR" : "OK", source.network_bytes, saved,
                              elapsed, commit_result, (unsigned)error);
        int sent = 0;
        while(sent < length) {
            int got = send(client, reply + sent, length - sent, 0);
            if(got <= 0) break;
            sent += got;
        }
        shutdown(client, SHUT_RDWR);
        close(client);
    }
    if(listener >= 0) close(listener);
    /* Keep g_direct_busy reserved until reboot so monitor cannot sleep in this gap. */
    wifi_direct_status(error ? WIFI_DIRECT_ERROR : WIFI_DIRECT_DONE, error, saved, AP_FILE_BENCH_BYTES);
    vTaskDelay(pdMS_TO_TICKS(500));
    sys_reboot();
    vTaskDelete(NULL);
}

void service_wifi_ap_file_bench_start(void)
{
    if(xTaskCreate(ap_file_bench_task, "ap_file_bench", 6144, NULL, 5, NULL) != pdPASS)
        sys_reboot();
}
#endif

#ifdef ARK_TRANSFER_BENCH
/* Receive the HTTP body into PSRAM and discard it without opening an SD file. */
static uint8_t wifi_direct_sink(esp_http_client_handle_t client, uint32_t total,
                                uint32_t *received_out)
{
    uint8_t *buffer = heap_caps_malloc(WIFI_DIRECT_SLOT_SIZE, MALLOC_CAP_SPIRAM);
    if(!buffer) return WIFI_DIRECT_ERR_RESOURCE;
    uint32_t received = 0, reads = 0;
    int64_t read_us = 0, first_byte_us = -1;
    int64_t started = esp_timer_get_time();
    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(120000);
    uint8_t error = WIFI_DIRECT_ERR_NONE;
    while(received < total && !error)
    {
        uint32_t want = total - received;
        if(want > WIFI_DIRECT_SLOT_SIZE) want = WIFI_DIRECT_SLOT_SIZE;
        uint32_t filled = 0;
        while(filled < want)
        {
            if(wifi_direct_cancelled()) { error = WIFI_DIRECT_ERR_CANCELLED; break; }
            if((int32_t)(xTaskGetTickCount() - deadline) >= 0)
            { error = WIFI_DIRECT_ERR_HTTP; break; }
            int64_t before = esp_timer_get_time();
            int got = esp_http_client_read(client, (char *)buffer + filled, (int)(want - filled));
            read_us += esp_timer_get_time() - before;
            ++reads;
            if(got == -ESP_ERR_HTTP_EAGAIN) {
                sys_logw(WIFI_SERVICE_TAG, "HTTP read timeout; retrying within idle deadline");
                continue;
            }
            if(got <= 0) { error = WIFI_DIRECT_ERR_HTTP; break; }
            if(first_byte_us < 0) first_byte_us = esp_timer_get_time() - started;
            filled += (uint32_t)got;
            received += (uint32_t)got;
            deadline = xTaskGetTickCount() + pdMS_TO_TICKS(120000);
        }
        if(filled) wifi_direct_status(WIFI_DIRECT_DOWNLOADING, 0, received, total);
    }
    if(wifi_direct_cancelled()) error = WIFI_DIRECT_ERR_CANCELLED;
    if(!error && received != total) error = WIFI_DIRECT_ERR_HTTP;
    *received_out = received;
    sys_logi(WIFI_SERVICE_TAG, "BENCH HTTP sink only, no SD save: net=%" PRIu32
             " reads=%" PRIu32 " first_byte_us=%" PRId64
             " read_us=%" PRId64 " elapsed_us=%" PRId64 " result=%u",
             received, reads, first_byte_us, read_us,
             esp_timer_get_time() - started, (unsigned)error);
    heap_caps_free(buffer);
    return error;
}
#endif

static uint8_t wifi_direct_download(const wifi_direct_args_t *args, uint32_t *received_out, uint32_t *total_out)
{
    const char *url = args->url;
    esp_http_client_config_t cfg = {.url = url, .timeout_ms = 5000, .buffer_size = 16 * 1024};
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if(!client) return WIFI_DIRECT_ERR_RESOURCE;

    uint8_t error = WIFI_DIRECT_ERR_HTTP;
    bool saving = false;
    uint32_t received = 0, total = 0;
    do {
        if(esp_http_client_open(client, 0) != ESP_OK) break;
        int64_t length = esp_http_client_fetch_headers(client);
        int status = esp_http_client_get_status_code(client);
        if(status != 200 || length < (args->file ? 1 : 32) || length > UINT32_MAX) break;
        total = (uint32_t)length;
        *total_out = total;
        wifi_direct_status(WIFI_DIRECT_DOWNLOADING, 0, 0, total);
        /* SDMMC needs an internal DMA bounce buffer even when file data lives in PSRAM. */
        sys_logi(WIFI_SERVICE_TAG, "Direct download: kind=%s total=%" PRIu32
                 " DMA free=%u largest=%u", args->file ? "file" : "film", total,
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA));

#ifdef ARK_TRANSFER_BENCH
        sys_logw(WIFI_SERVICE_TAG, "BENCH diagnostic: HTTP sink then isolated SD test; upload is not saved");
        error = wifi_direct_sink(client, total, &received);
        *received_out = received;
        if(error) break;
        int sd_result = service_file_benchmark_sd(total);
        sys_logi(WIFI_SERVICE_TAG, "BENCH SD result=%d; diagnostic upload is not saved", sd_result);
        error = WIFI_DIRECT_ERR_CANCELLED;
        break;
#endif
        if(args->file)
        {
            if(service_file_save_start_to(FILE_SAVE_WIFI, args->path, total) != 0)
            {
                error = WIFI_DIRECT_ERR_SAVE;
                break;
            }
        }
        else
        {
            const char *start = strrchr(url, '/');
            start = start ? start + 1 : "direct.film";
            const char *end = strchr(start, '?');
            size_t n = end ? (size_t)(end - start) : strlen(start);
            char filename[128];
            if(n < 5 || n >= sizeof(filename) ||
               memcmp(start + n - 5, ".film", 5) != 0)
                start = "direct.film", n = strlen(start);
            memcpy(filename, start, n);
            filename[n] = '\0';
            if(strchr(filename, '%') || strchr(filename, '\\'))
                strlcpy(filename, "direct.film", sizeof(filename));
            if(service_file_save_start(FILE_SAVE_WIFI, filename, total) != 0)
            {
                error = WIFI_DIRECT_ERR_SAVE;
                break;
            }
        }
        saving = true;
        error = wifi_direct_receive(wifi_direct_http_read, client, "HTTP", total, &received);
        *received_out = received;
        if(error) break;
        int64_t commit_start = esp_timer_get_time();
        int commit_result = service_file_save_stop(FILE_SAVE_WIFI, args->file ? 0 : 1);
        sys_logi(WIFI_SERVICE_TAG, "HTTP final commit_us=%" PRId64 " result=%d",
                 esp_timer_get_time() - commit_start, commit_result);
        if(commit_result != 0)
        {
            error = WIFI_DIRECT_ERR_SAVE;
            break;
        }
        saving = false;
        error = WIFI_DIRECT_ERR_NONE;
    } while(false);
    if(saving) service_file_save_abort(FILE_SAVE_WIFI);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return error;
}

static uint8_t wifi_direct_ota_download(const wifi_direct_args_t *args, uint32_t *received, uint32_t *total)
{
    esp_http_client_config_t cfg = {.url = args->url, .timeout_ms = 5000};
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if(!client)
        return WIFI_DIRECT_ERR_RESOURCE;
    uint8_t error = WIFI_DIRECT_ERR_HTTP;
    uint8_t *chunk = heap_caps_malloc(4096, MALLOC_CAP_SPIRAM);
    if(!chunk)
    {
        esp_http_client_cleanup(client);
        return WIFI_DIRECT_ERR_RESOURCE;
    }
    do
    {
        if(esp_http_client_open(client, 0) != ESP_OK)
            break;
        int64_t length = esp_http_client_fetch_headers(client);
        if(esp_http_client_get_status_code(client) != 200 || length != args->size)
        {
            error = WIFI_DIRECT_ERR_INTEGRITY;
            break;
        }
        *total = args->size;
        if(wifi_direct_cancelled())
        {
            error = WIFI_DIRECT_ERR_CANCELLED;
            break;
        }
        if(service_ota_direct_begin(args->size, args->sha) != ESP_OK)
        {
            error = WIFI_DIRECT_ERR_OTA;
            break;
        }
        TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(120000);
        while(*received < args->size)
        {
            if(wifi_direct_cancelled())
            {
                error = WIFI_DIRECT_ERR_CANCELLED;
                break;
            }
            if((int32_t)(xTaskGetTickCount() - deadline) >= 0)
                break;
            uint32_t remaining = args->size - *received;
            int got = esp_http_client_read(client, (char *)chunk, remaining < 4096 ? remaining : 4096);
            if(got == -ESP_ERR_HTTP_EAGAIN) {
                sys_logw(WIFI_SERVICE_TAG, "HTTP read timeout; retrying within idle deadline");
                continue;
            }
            if(got <= 0)
                break;
            if(service_ota_direct_write(chunk, got) != ESP_OK)
            {
                error = WIFI_DIRECT_ERR_OTA;
                break;
            }
            *received += got;
            deadline = xTaskGetTickCount() + pdMS_TO_TICKS(120000);
            wifi_direct_status(WIFI_DIRECT_DOWNLOADING, 0, *received, args->size);
        }
        if(wifi_direct_cancelled())
        {
            error = WIFI_DIRECT_ERR_CANCELLED;
            break;
        }
        if(*received != args->size)
            break;
        esp_err_t result = service_ota_direct_finish();
        error = result == ESP_OK                ? 0
                : result == ESP_ERR_INVALID_CRC ? WIFI_DIRECT_ERR_INTEGRITY
                                                : WIFI_DIRECT_ERR_OTA;
    } while(false);
    if(error)
        service_ota_direct_abort();
    free(chunk);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return error;
}

static void wifi_direct_task(void *arg)
{
    wifi_direct_args_t *args = arg;
    uint8_t error = WIFI_DIRECT_ERR_NONE;
    uint32_t received = 0, total = 0;
    bool was_initialized = false;
    bool was_connected = false;
    bool wifi_touched = false;
    wifi_config_t old_config = {0};
    bool have_config = false;
    wifi_ps_type_t old_ps = WIFI_PS_NONE;
    bool have_ps = false;
    bool ap_touched = false;
    bool ble_paused = false;

    /* A heartbeat may already be in its HTTP request. Let it finish before changing STA. */
    uint32_t wait_start = xTaskGetTickCount();
    while(true)
    {
        portENTER_CRITICAL(&g_direct_lock);
        bool active = g_heartbeat_active;
        portEXIT_CRITICAL(&g_direct_lock);
        if(!active || wifi_direct_cancelled()) break;
        if(xTaskGetTickCount() - wait_start >= pdMS_TO_TICKS(12000))
        { error = WIFI_DIRECT_ERR_RESOURCE; break; }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    if(wifi_direct_cancelled()) error = WIFI_DIRECT_ERR_CANCELLED;
    was_initialized = hal_wifi_initialized();
    was_connected = hal_wifi_connected();
    have_config = !was_initialized || hal_wifi_get_sta_config(&old_config) == ESP_OK;
    if(!have_config && !error) error = WIFI_DIRECT_ERR_RESOURCE;
    if(!error)
    {
        wifi_touched = true;
        if(was_initialized) hal_wifi_disconnect();
    }
    if(!error && (was_initialized ? hal_wifi_set_ram_storage(true) : hal_wifi_init(true)) != ESP_OK)
        error = WIFI_DIRECT_ERR_RESOURCE;
    if(!error)
    {
        esp_err_t ps_err = hal_wifi_get_power_save(&old_ps);
        have_ps = ps_err == ESP_OK;
        if(have_ps) ps_err = hal_wifi_set_power_save(WIFI_PS_NONE);
        if(ps_err != ESP_OK)
            sys_logw(WIFI_SERVICE_TAG, "Direct power-save boost unavailable: %s", esp_err_to_name(ps_err));
    }
    if(!error && args->softap)
    {
        ap_touched = true;
        if(hal_wifi_softap_start(args->ssid, args->password) != ESP_OK)
            error = WIFI_DIRECT_ERR_CONNECT;
        else {
            portENTER_CRITICAL(&g_direct_lock);
            g_softap_accepting = true;
            portEXIT_CRITICAL(&g_direct_lock);
            sys_logi(WIFI_SERVICE_TAG, "SoftAP prepared: %s; waiting for phone server", args->ssid);
            TickType_t started = xTaskGetTickCount();
            bool submitted = false;
            while(!error) {
                portENTER_CRITICAL(&g_direct_lock);
                submitted = g_softap_submitted;
                bool cancelled = g_direct_cancel;
                bool expired = !submitted && xTaskGetTickCount() - started >= pdMS_TO_TICKS(90000);
                if(cancelled || expired) g_softap_accepting = false;
                portEXIT_CRITICAL(&g_direct_lock);
                if(cancelled) error = WIFI_DIRECT_ERR_CANCELLED;
                else if(submitted) break;
                else if(expired) error = WIFI_DIRECT_ERR_CONNECT;
                else vTaskDelay(pdMS_TO_TICKS(50));
            }
            if(!error) {
                /* Let the BLE start ACK finish before intentionally disconnecting. */
                vTaskDelay(pdMS_TO_TICKS(350));
                ble_paused = true;
                if(!service_ble_wifi_pause()) error = WIFI_DIRECT_ERR_RESOURCE;
            }
        }
    }
    else if(!error)
    {
        if(hal_wifi_connect(args->ssid, args->password) != ESP_OK)
            error = WIFI_DIRECT_ERR_CONNECT;
        if(!error && !wifi_direct_wait_connected(25000, true))
            error = wifi_direct_cancelled() ? WIFI_DIRECT_ERR_CANCELLED : WIFI_DIRECT_ERR_CONNECT;
    }
    int64_t transfer_start = esp_timer_get_time();
    if(!error) error = args->ota ? wifi_direct_ota_download(args, &received, &total) :
                                wifi_direct_download(args, &received, &total);
    if(args->softap) sys_logi(WIFI_SERVICE_TAG, "SoftAP transfer saved=%" PRIu32
                             " total=%" PRIu32 " elapsed_us=%" PRId64 " error=%u",
                             received, total, esp_timer_get_time() - transfer_start, error);

    portENTER_CRITICAL(&g_direct_lock);
    if(args->softap) g_softap_accepting = false;
    portEXIT_CRITICAL(&g_direct_lock);
    wifi_direct_status(WIFI_DIRECT_RESTORING, error, received, total);
    if(ap_touched && hal_wifi_initialized() && hal_wifi_softap_stop() != ESP_OK)
        error = WIFI_DIRECT_ERR_RESTORE;
    if(have_ps && hal_wifi_set_power_save(old_ps) != ESP_OK)
        error = WIFI_DIRECT_ERR_RESTORE;
    if(wifi_touched && was_initialized)
    {
        hal_wifi_disconnect();
        if(have_config && hal_wifi_set_sta_config(&old_config) == ESP_OK)
        {
            if(was_connected && hal_wifi_reconnect() == ESP_OK)
            {
                if(!wifi_direct_wait_connected(25000, false)) error = WIFI_DIRECT_ERR_RESTORE;
            }
            else if(was_connected) error = WIFI_DIRECT_ERR_RESTORE;
        }
        else error = WIFI_DIRECT_ERR_RESTORE;
        if(hal_wifi_set_ram_storage(false) != ESP_OK) error = WIFI_DIRECT_ERR_RESTORE;
    }
    else if(wifi_touched && hal_wifi_initialized()) hal_wifi_deinit();
    if(ble_paused && !service_ble_wifi_resume()) {
        error = WIFI_DIRECT_ERR_RESTORE;
        /* Startup still restores ordinary BLE if a runtime stack restart fails. */
        if(!service_ble_wifi_resume()) {
            sys_loge(WIFI_SERVICE_TAG, "BLE restore failed; rebooting to recover connectivity");
            sys_reboot();
        }
    }

    if(args->ota && !error)
    {
        wifi_direct_status(WIFI_DIRECT_READY, 0, received, total);
        TickType_t start = xTaskGetTickCount();
        while(true)
        {
            portENTER_CRITICAL(&g_direct_lock);
            bool apply = g_direct_apply;
            bool activating = g_direct_status.state == WIFI_DIRECT_APPLYING;
            if(!activating && g_direct_cancel) error = WIFI_DIRECT_ERR_CANCELLED;
            if(!activating && !error && xTaskGetTickCount() - start >= pdMS_TO_TICKS(120000))
                error = WIFI_DIRECT_ERR_READY_TIMEOUT;
            /* Claim the terminal state under the same lock as apply, preventing
             * timeout/cancel from aborting a concurrently activated image. */
            if(error) g_direct_status.state = WIFI_DIRECT_RESTORING;
            portEXIT_CRITICAL(&g_direct_lock);
            if(apply)
            {
                vTaskDelay(pdMS_TO_TICKS(1500));
                sys_reboot();
                break;
            }
            if(error) break;
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }
    if(args->ota) service_ota_direct_abort();
    uint8_t state = error == WIFI_DIRECT_ERR_NONE ? WIFI_DIRECT_DONE :
                    error == WIFI_DIRECT_ERR_CANCELLED ? WIFI_DIRECT_CANCELLED : WIFI_DIRECT_ERROR;
    wifi_direct_status(state, error, received, total);
    sys_logi(WIFI_SERVICE_TAG, "Direct WiFi finished: state=%u bytes=%" PRIu32
             "/%" PRIu32 " error=%u", state, received, total, error);
    portENTER_CRITICAL(&g_direct_lock);
    if(g_softap_args == args) g_softap_args = NULL;
    g_direct_busy = false;
    portEXIT_CRITICAL(&g_direct_lock);
    /* Catch a local ON deferred in the last RESTORING tick. Resume is serialized. */
    if(ble_paused) (void)service_ble_wifi_resume();
    free(args);
    vTaskDelete(NULL);
}

/*********************************************************************
 * LOCAL FUNCTIONS
 */

/**
 * [wifi_http_event_handler HTTP事件处理]
 */
static esp_err_t wifi_http_event_handler(esp_http_client_event_t *evt)
{
    switch(evt->event_id)
    {
    case HTTP_EVENT_ON_CONNECTED:
        g_download_content_length = esp_http_client_get_content_length(evt->client);
        g_download_received = 0;
        g_download_progress = 0;
        if(g_download_buffer)
        {
            heap_caps_free(g_download_buffer);
            g_download_buffer = NULL;
        }
        sys_logi(WIFI_SERVICE_TAG, "HTTP connected, content length: %d", g_download_content_length);
        break;

    case HTTP_EVENT_ON_DATA:
        if(g_download_state != WIFI_DOWNLOAD_DOWNLOADING)
        {
            break;
        }
        if(evt->data_len > 0)
        {
            uint8_t *new_buf = (uint8_t *)heap_caps_realloc(g_download_buffer, g_download_received + evt->data_len, MALLOC_CAP_SPIRAM);
            if(new_buf)
            {
                g_download_buffer = new_buf;
                memcpy(g_download_buffer + g_download_received, evt->data, evt->data_len);
                g_download_received += evt->data_len;
                if(g_download_content_length > 0)
                {
                    g_download_progress = (uint8_t)(((uint32_t)g_download_received * 100) / g_download_content_length);
                }
            }
            else
            {
                sys_loge(WIFI_SERVICE_TAG, "Download buffer realloc failed");
                g_download_state = WIFI_DOWNLOAD_ERROR;
            }
        }
        break;

    case HTTP_EVENT_ON_FINISH:
    {
        sys_logi(WIFI_SERVICE_TAG, "HTTP download finished, received: %d bytes", g_download_received);
        int status = esp_http_client_get_status_code(evt->client);
        int64_t content_length = esp_http_client_get_content_length(evt->client);
        if(g_download_state != WIFI_DOWNLOAD_DOWNLOADING || status < 200 || status >= 300 ||
           (content_length >= 0 && g_download_received != content_length))
        {
            sys_loge(WIFI_SERVICE_TAG, "HTTP download incomplete: status=%d received=%d expected=%" PRId64,
                     status, g_download_received, content_length);
            if(g_download_buffer) heap_caps_free(g_download_buffer);
            g_download_buffer = NULL;
            g_download_state = WIFI_DOWNLOAD_ERROR;
            break;
        }
        if(g_download_buffer && g_download_received > 0)
        {
            if(service_file_save_start(FILE_SAVE_WIFI, g_download_filename, g_download_received) == 0)
            {
                int write_result = service_file_save_data(FILE_SAVE_WIFI,
                                                          g_download_buffer, g_download_received);
                /* save_data takes ownership even on failure. */
                g_download_buffer = NULL;
                int save_result = write_result == 0 ? service_file_save_stop(FILE_SAVE_WIFI, 1) : -1;
                if(save_result != 0)
                {
                    service_file_save_abort(FILE_SAVE_WIFI);
                    g_download_state = WIFI_DOWNLOAD_ERROR;
                    sys_loge(WIFI_SERVICE_TAG, "HTTP download save failed");
                }
                else
                {
                    g_download_state = WIFI_DOWNLOAD_DONE;
                    g_download_progress = 100;
                }
            }
            else
            {
                heap_caps_free(g_download_buffer);
                g_download_state = WIFI_DOWNLOAD_ERROR;
            }
            g_download_buffer = NULL;
        }
        else
        {
            g_download_state = WIFI_DOWNLOAD_ERROR;
        }
        /* 下载完成事件只在文件真正保存成功后发布。 */
        if(g_download_state == WIFI_DOWNLOAD_DONE)
            sys_event_publish(SYS_EVT_WIFI_DL_DONE, &g_download_received, sizeof(g_download_received));
        break;
    }

    case HTTP_EVENT_DISCONNECTED:
        sys_logi(WIFI_SERVICE_TAG, "HTTP disconnected");
        if(g_download_state == WIFI_DOWNLOAD_DOWNLOADING)
        {
            g_download_state = WIFI_DOWNLOAD_ERROR;
        }
        if(g_download_buffer)
        {
            heap_caps_free(g_download_buffer);
            g_download_buffer = NULL;
        }
        break;

    case HTTP_EVENT_ERROR:
        sys_loge(WIFI_SERVICE_TAG, "HTTP error");
        g_download_state = WIFI_DOWNLOAD_ERROR;
        if(g_download_buffer)
        {
            heap_caps_free(g_download_buffer);
            g_download_buffer = NULL;
        }
        break;

    default:
        break;
    }
    return ESP_OK;
}

/**
 * [wifi_download_task 下载任务]
 * @param pvParameters 下载 URL（strdup 拷贝，任务结束负责释放）
 */
static void wifi_download_task(void *pvParameters)
{
    const char *url = (const char *)pvParameters;

    if(url == NULL || strlen(url) == 0)
    {
        sys_loge(WIFI_SERVICE_TAG, "Download URL is empty");
        g_download_state = WIFI_DOWNLOAD_ERROR;
        free((void *)url);
        vTaskDelete(NULL);
        return;
    }

    // 从 URL 提取文件名（截断查询参数 ?...，如 latest.film?device_id=... 取 latest.film）
    const char *last_slash = strrchr(url, '/');
    const char *filename = last_slash ? (last_slash + 1) : "download.film";
    const char *query = strchr(filename, '?');
    size_t fname_len = query ? (size_t)(query - filename) : strlen(filename);
    if(fname_len <= 0 || fname_len >= sizeof(g_download_filename))
    {
        fname_len = strlen("download.film");
        filename = "download.film";
    }
    memcpy(g_download_filename, filename, fname_len);
    g_download_filename[fname_len] = '\0';

    esp_http_client_config_t config = {
        .url = url,
        .event_handler = wifi_http_event_handler,
        .timeout_ms = 5000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);

    g_download_state = WIFI_DOWNLOAD_DOWNLOADING;
    g_download_progress = 0;
    g_download_content_length = 0;
    g_download_received = 0;

    sys_logi(WIFI_SERVICE_TAG, "Starting download: %s -> %s", url, g_download_filename);

    esp_err_t err = esp_http_client_perform(client);

    if(err != ESP_OK)
    {
        sys_loge(WIFI_SERVICE_TAG, "HTTP download failed: %d", err);
        if(g_download_state == WIFI_DOWNLOAD_DOWNLOADING)
        {
            g_download_state = WIFI_DOWNLOAD_ERROR;
        }
    }

    esp_http_client_cleanup(client);
    free((void *)url);
    portENTER_CRITICAL(&g_direct_lock);
    g_legacy_active = false;
    portEXIT_CRITICAL(&g_direct_lock);
    vTaskDelete(NULL);
}

/**
 * [service_wifi_download_start 开始下载film文件（使用配置的 film_api_url）]
 */
void service_wifi_download_start(void)
{
    if(!g_service_param.network.wifi_enable)
    {
        sys_logw(WIFI_SERVICE_TAG, "WiFi disabled, cannot download");
        return;
    }

    if(!hal_wifi_connected())
    {
        sys_logw(WIFI_SERVICE_TAG, "WiFi not connected, cannot download");
        return;
    }

    if(g_download_state == WIFI_DOWNLOAD_DOWNLOADING)
    {
        sys_logw(WIFI_SERVICE_TAG, "Download already in progress");
        return;
    }

    if(strlen(g_service_param.network.film_api_url) == 0)
    {
        sys_logw(WIFI_SERVICE_TAG, "Film API URL is empty");
        return;
    }

    service_wifi_download_url(g_service_param.network.film_api_url);
}

/**
 * [service_wifi_download_url 按指定 URL 开始下载film文件]
 */
void service_wifi_download_url(const char *url)
{
    if(service_wifi_direct_busy() || g_wifi_shutting_down) return;
    if(!g_service_param.network.wifi_enable)
    {
        sys_logw(WIFI_SERVICE_TAG, "WiFi disabled, cannot download");
        return;
    }

    if(!hal_wifi_connected())
    {
        sys_logw(WIFI_SERVICE_TAG, "WiFi not connected, cannot download");
        return;
    }

    if(g_download_state == WIFI_DOWNLOAD_DOWNLOADING)
    {
        sys_logw(WIFI_SERVICE_TAG, "Download already in progress");
        return;
    }

    if(url == NULL || strlen(url) == 0)
    {
        sys_logw(WIFI_SERVICE_TAG, "Download URL is empty");
        return;
    }

    char *url_copy = strdup(url);
    if(url_copy == NULL)
    {
        sys_loge(WIFI_SERVICE_TAG, "Failed to alloc download URL");
        g_download_state = WIFI_DOWNLOAD_ERROR;
        return;
    }

    portENTER_CRITICAL(&g_direct_lock);
    if(g_legacy_active || g_direct_busy || g_wifi_shutting_down)
    {
        portEXIT_CRITICAL(&g_direct_lock);
        free(url_copy);
        return;
    }
    g_legacy_active = true;
    g_download_state = WIFI_DOWNLOAD_DOWNLOADING;
    portEXIT_CRITICAL(&g_direct_lock);

    // 创建下载任务
    if(pdPASS != xTaskCreate(wifi_download_task, "wifi_dl", 4096, url_copy, 5, NULL))
    {
        sys_loge(WIFI_SERVICE_TAG, "Failed to create download task");
        free(url_copy);
        g_download_state = WIFI_DOWNLOAD_ERROR;
        portENTER_CRITICAL(&g_direct_lock);
        g_legacy_active = false;
        portEXIT_CRITICAL(&g_direct_lock);
    }
}

/**
 * [service_wifi_download_get_progress 获取下载进度]
 */
uint8_t service_wifi_download_get_progress(void)
{
    return g_download_progress;
}

/**
 * [service_wifi_download_get_state 获取下载状态]
 */
wifi_download_state_t service_wifi_download_get_state(void)
{
    return g_download_state;
}

/*********************************************************************
 * 心跳（film-hub 服务端适配：定时上报 + 指令下发执行）
 *********************************************************************/
#define HEARTBEAT_RESP_MAX  2048
#define HEARTBEAT_URL_MAX   512

static char g_heartbeat_resp[HEARTBEAT_RESP_MAX];
static int g_heartbeat_resp_len = 0;

/**
 * [heartbeat_http_event_handler 心跳响应收集]
 */
static esp_err_t heartbeat_http_event_handler(esp_http_client_event_t *evt)
{
    switch(evt->event_id)
    {
    case HTTP_EVENT_ON_DATA:
        if(evt->data_len > 0 && g_heartbeat_resp_len + evt->data_len < (int)sizeof(g_heartbeat_resp))
        {
            memcpy(g_heartbeat_resp + g_heartbeat_resp_len, evt->data, evt->data_len);
            g_heartbeat_resp_len += evt->data_len;
        }
        break;
    case HTTP_EVENT_ON_FINISH:
    case HTTP_EVENT_DISCONNECTED:
        g_heartbeat_resp[g_heartbeat_resp_len] = '\0';
        break;
    default:
        break;
    }
    return ESP_OK;
}

/**
 * [wifi_heartbeat_exec_cmd 执行服务端下发的单条指令]
 */
static void wifi_heartbeat_exec_cmd(cJSON *cmd)
{
    cJSON *cmd_name = cJSON_GetObjectItem(cmd, "cmd");
    cJSON *params = cJSON_GetObjectItem(cmd, "params");
    if(!cJSON_IsString(cmd_name) || cmd_name->valuestring == NULL)
    {
        return;
    }
    const char *name = cmd_name->valuestring;

    if(strcmp(name, "download_film") == 0)
    {
        cJSON *url = params ? cJSON_GetObjectItem(params, "url") : NULL;
        if(cJSON_IsString(url) && url->valuestring != NULL && url->valuestring[0] != '\0')
        {
            sys_logi(WIFI_SERVICE_TAG, "Heartbeat cmd: download_film");
            service_wifi_download_url(url->valuestring);
        }
    }
    else if(strcmp(name, "set_config") == 0)
    {
        bool changed = false;
        if(cJSON_IsObject(params))
        {
            cJSON *item = NULL;
            item = cJSON_GetObjectItem(params, "wifi_enable");
            if(cJSON_IsNumber(item) && (item->valueint == 0 || item->valueint == 1))
            {
                if(g_service_param.network.wifi_enable != (uint8_t)item->valueint)
                {
                    g_service_param.network.wifi_enable = (uint8_t)item->valueint;
                    changed = true;
                    if(g_service_param.network.wifi_enable)
                    {
                        service_wifi_init();
                    }
                    else
                    {
                        service_wifi_disconnect();
                    }
                }
            }
            item = cJSON_GetObjectItem(params, "sleep_mode");
            if(cJSON_IsNumber(item) && (item->valueint == 0 || item->valueint == 1))
            {
                if(g_service_param.sleep.sleep_mode != (uint8_t)item->valueint)
                {
                    g_service_param.sleep.sleep_mode = (uint8_t)item->valueint;
                    changed = true;
                }
            }
            item = cJSON_GetObjectItem(params, "sleep_auto");
            if(cJSON_IsNumber(item) && (item->valueint == 0 || item->valueint == 1))
            {
                if(g_service_param.sleep.sleep_auto != (uint8_t)item->valueint)
                {
                    g_service_param.sleep.sleep_auto = (uint8_t)item->valueint;
                    changed = true;
                }
            }
            item = cJSON_GetObjectItem(params, "sleep_time");
            if(cJSON_IsNumber(item) && item->valueint >= 10 && item->valueint <= 2880)
            {
                if(g_service_param.sleep.sleep_time != (uint16_t)item->valueint)
                {
                    g_service_param.sleep.sleep_time = (uint16_t)item->valueint;
                    changed = true;
                }
            }
            item = cJSON_GetObjectItem(params, "ble_enable");
            if(cJSON_IsNumber(item) && (item->valueint == 0 || item->valueint == 1))
            {
                if(g_service_param.ble.ble_enable != (uint8_t)item->valueint)
                {
                    /* 与 WiFi 同款：置参数 + 起停协议栈（service_ble_apply_enable 内部写参数） */
                    service_ble_apply_enable((uint8_t)item->valueint);
                    changed = true;
                }
            }
        }
        if(changed)
        {
            sys_logi(WIFI_SERVICE_TAG, "Heartbeat cmd: set_config applied");
            service_param_save();
        }
    }
    else if(strcmp(name, "set_heartbeat") == 0)
    {
        cJSON *interval = params ? cJSON_GetObjectItem(params, "interval") : NULL;
        if(cJSON_IsNumber(interval) && interval->valueint >= 5 && interval->valueint <= 180)
        {
            sys_logi(WIFI_SERVICE_TAG, "Heartbeat cmd: set_heartbeat %d", interval->valueint);
            g_service_param.network.film_heartbeat_interval = (uint8_t)interval->valueint;
            service_param_save();
        }
    }
    else if(strcmp(name, "sync_time") == 0)
    {
        cJSON *ts = params ? cJSON_GetObjectItem(params, "timestamp") : NULL;
        if(cJSON_IsNumber(ts) && ts->valuedouble > 0)
        {
            struct timeval tv = { .tv_sec = (time_t)ts->valuedouble, .tv_usec = 0 };
            settimeofday(&tv, NULL);
            sys_logi(WIFI_SERVICE_TAG, "Heartbeat cmd: sync_time");
        }
    }
    else if(strcmp(name, "reboot") == 0)
    {
        sys_logi(WIFI_SERVICE_TAG, "Heartbeat cmd: reboot");
        vTaskDelay(pdMS_TO_TICKS(100));
        sys_reboot();
    }
    // clear_files 等其它指令：暂不支持，忽略
}

/**
 * [wifi_heartbeat_parse 解析心跳响应：token / 间隔 / 指令]
 */
static void wifi_heartbeat_parse(const char *body)
{
    if(service_wifi_direct_busy() || g_wifi_shutting_down) return;
    cJSON *root = cJSON_Parse(body);
    if(root == NULL)
    {
        sys_logw(WIFI_SERVICE_TAG, "Heartbeat response parse failed");
        return;
    }

    cJSON *data = cJSON_GetObjectItem(root, "data");
    if(!cJSON_IsObject(data))
    {
        cJSON_Delete(root);
        return;
    }

    // 首次心跳（或 token 轮换）下发 token
    cJSON *token = cJSON_GetObjectItem(data, "token");
    if(cJSON_IsString(token) && token->valuestring != NULL && token->valuestring[0] != '\0')
    {
        if(strcmp(token->valuestring, g_service_param.network.film_token) != 0)
        {
            strncpy(g_service_param.network.film_token, token->valuestring,
                    sizeof(g_service_param.network.film_token) - 1);
            sys_logi(WIFI_SERVICE_TAG, "Heartbeat token updated");
            service_param_save();
        }
    }

    // 服务端可动态调整心跳间隔
    cJSON *interval = cJSON_GetObjectItem(data, "heartbeat_interval");
    if(cJSON_IsNumber(interval) && interval->valueint >= 5 && interval->valueint <= 180)
    {
        if(g_service_param.network.film_heartbeat_interval != (uint8_t)interval->valueint)
        {
            g_service_param.network.film_heartbeat_interval = (uint8_t)interval->valueint;
            service_param_save();
        }
    }

    // 指令下发
    cJSON *cmds = cJSON_GetObjectItem(data, "commands");
    if(cJSON_IsArray(cmds))
    {
        int count = cJSON_GetArraySize(cmds);
        for(int i = 0; i < count; i++)
        {
            cJSON *cmd = cJSON_GetArrayItem(cmds, i);
            wifi_heartbeat_exec_cmd(cmd);
        }
    }

    cJSON_Delete(root);
}

/**
 * [wifi_heartbeat_once 单次心跳请求]
 */
static void wifi_heartbeat_once(void)
{
    char url[HEARTBEAT_URL_MAX];
    char auto_hb[192] = "";
    const char *hb_base = g_service_param.network.film_heartbeat_url;

    // 心跳地址：优先使用已配置的完整心跳接口；否则由服务器 API 地址自动拼接
    // （只配置一个 API 地址即可，如 http://192.168.1.219:8000）
    if(strstr(hb_base, "/api/v1/device/heartbeat") == NULL)
    {
        const char *api = g_service_param.network.film_api_url;
        int alen = (int)strlen(api);
        while(alen > 0 && api[alen - 1] == '/') alen--;  // 去掉末尾斜杠
        if(alen <= 0)
        {
            sys_logw(WIFI_SERVICE_TAG, "No API URL configured, skip heartbeat");
            return;
        }
        snprintf(auto_hb, sizeof(auto_hb), "%.*s/api/v1/device/heartbeat", alen, api);
        hb_base = auto_hb;
    }

    const char *token = g_service_param.network.film_token;

    int n = snprintf(url, sizeof(url),
        "%s?device_id=%s&token=%s&battery=%d&wifi_enable=%d"
        "&sleep_mode=%d&sleep_auto=%d&sleep_time=%d&ble_enable=%d"
        "&current_file_id=%lu&state=idle&heartbeat_interval=%d",
        hb_base,
        g_service_param.network.film_device_id,
        (token != NULL && token[0] != '\0') ? token : "",
        hal_bat_get_percent(),
        g_service_param.network.wifi_enable,
        g_service_param.sleep.sleep_mode,
        g_service_param.sleep.sleep_auto,
        g_service_param.sleep.sleep_time,
        g_service_param.ble.ble_enable,
        (unsigned long)service_file_get_current_id(),
        g_service_param.network.film_heartbeat_interval);

    if(n <= 0 || n >= (int)sizeof(url))
    {
        sys_logw(WIFI_SERVICE_TAG, "Heartbeat URL too long");
        return;
    }

    g_heartbeat_resp_len = 0;
    g_heartbeat_resp[0] = '\0';

    esp_http_client_config_t config = {
        .url = url,
        .event_handler = heartbeat_http_event_handler,
        .timeout_ms = 10000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if(client == NULL)
    {
        sys_loge(WIFI_SERVICE_TAG, "Heartbeat http client init failed");
        return;
    }

    esp_err_t err = esp_http_client_perform(client);
    if(err == ESP_OK)
    {
        wifi_heartbeat_parse(g_heartbeat_resp);
    }
    else
    {
        sys_logw(WIFI_SERVICE_TAG, "Heartbeat request failed: %d", err);
    }

    esp_http_client_cleanup(client);
}

/**
 * [wifi_heartbeat_task 心跳任务：WiFi 连接后定时上报]
 */
static void wifi_heartbeat_task(void *pvParameters)
{
    for(;;)
    {
        // 有 API 地址（用于拼接心跳接口）即视为已配置
        if(hal_wifi_connected() && strlen(g_service_param.network.film_api_url) > 0)
        {
            portENTER_CRITICAL(&g_direct_lock);
            bool run = !g_direct_busy && !g_wifi_shutting_down;
            if(run) g_heartbeat_active = true;
            portEXIT_CRITICAL(&g_direct_lock);
            if(run)
            {
                wifi_heartbeat_once();
                portENTER_CRITICAL(&g_direct_lock);
                g_heartbeat_active = false;
                portEXIT_CRITICAL(&g_direct_lock);
            }
        }

        uint32_t interval = g_service_param.network.film_heartbeat_interval;
        if(interval < 5) interval = 5;
        if(interval > 180) interval = 180;
        vTaskDelay(pdMS_TO_TICKS(interval * 1000));
    }
    vTaskDelete(NULL);
}

/**
 * [service_wifi_heartbeat_start 启动心跳任务]
 */
void service_wifi_heartbeat_start(void)
{
    if(g_heartbeat_task_hdl != NULL)
    {
        return;
    }

    if(pdPASS != xTaskCreate(wifi_heartbeat_task, "wifi_hb", 4096, NULL, 4, &g_heartbeat_task_hdl))
    {
        sys_loge(WIFI_SERVICE_TAG, "Failed to create heartbeat task");
    }
    else
    {
        sys_logi(WIFI_SERVICE_TAG, "Heartbeat task started");
    }
}
