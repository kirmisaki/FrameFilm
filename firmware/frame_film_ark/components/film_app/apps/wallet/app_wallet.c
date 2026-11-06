/**
 * 卡包 app（仅 n16r8）：模拟 / 读卡复制 / 封面 / BLE 管理
 */
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_heap_caps.h"

#include "sys_cfg.h"
#include "sys_event.h"
#include "sys_log.h"

#include "hal_nfc.h"
#include "service_ble.h"
#include "service_param.h"

#include "ui_core.h"
#include "ui_ops.h"
#include "ui_fonts.h"

#include "app_shell.h"
#include "app_typography.h"
#include "app_language.h"
#include "app_manager.h"
#include "app_wallet.h"
#include "app_wallet_store.h"

#if (SYS_NFC_ENABLE == 1)

#define WALLET_TAG                  "app_wallet"

#define WALLET_MODE_EMU             0
#define WALLET_MODE_COPY            1

#define WALLET_SCAN_SCANNING        0
#define WALLET_SCAN_DONE            1
#define WALLET_SCAN_FAIL            2
#define WALLET_SCAN_UNSUPPORTED     3

#define WALLET_CMD_NEXT             1
#define WALLET_CMD_PREV             2
#define WALLET_CMD_ACTION           3
#define WALLET_CMD_SYNC             4

#define APP_UI_REQ_WALLET_CMD       0x21
#define APP_UI_MSG_WALLET_SNAP      0x22
#define APP_UI_MSG_WALLET_COVER     0x23

#define WALLET_UI_MAX_ROWS          5
#define WALLET_COVER_PIXELS         (WALLET_COVER_W * WALLET_COVER_H)

typedef struct {
    uint8_t mode;
    uint8_t scan;
    uint8_t count;
    uint8_t current;
    uint8_t kind;
    uint8_t uid_len;
    uint8_t uid[10];
    uint8_t has_cover;
    char name[24];
    char type[16];
    char status[40];
    char detail[48];
    uint8_t copy_kind;
    uint8_t copy_format;
    uint8_t copy_uid_len;
    uint8_t copy_uid[10];
    uint16_t copy_mem_len;
} wallet_snapshot_t;

typedef struct {
    lv_image_dsc_t dsc;
    uint8_t pixels[WALLET_COVER_PIXELS];
} wallet_cover_t;

typedef enum {
    WSCAN_OK = 0,
    WSCAN_TIMEOUT,
    WSCAN_UNSUPPORTED,
    WSCAN_CANCELLED,
    WSCAN_ERROR,
} wallet_scan_status_t;

static uint8_t m_mode = WALLET_MODE_EMU;
static uint8_t m_scan_state = WALLET_SCAN_FAIL;

static atomic_bool m_scan_cancel;
static atomic_bool m_emu_resync;    // 读卡期间离开了卡包：等 reader_end 完成后再恢复模拟
static _Atomic(wallet_card_t *) m_scan_result = NULL;
static TaskHandle_t m_scan_task = NULL;

static app_shell_t m_shell;
static wallet_snapshot_t m_snap;
static wallet_cover_t *m_cover = NULL;

static lv_obj_t *m_emu_page;
static lv_obj_t *m_copy_page;
static lv_obj_t *m_cover_img;
static lv_obj_t *m_default_panel;
static lv_obj_t *m_default_type;
static lv_obj_t *m_idx_label;
static lv_obj_t *m_name_label;
static lv_obj_t *m_type_label;
static lv_obj_t *m_count_label;
static lv_obj_t *m_status_label;
static lv_obj_t *m_row_obj[WALLET_UI_MAX_ROWS];
static lv_obj_t *m_row_idx[WALLET_UI_MAX_ROWS];
static lv_obj_t *m_row_name[WALLET_UI_MAX_ROWS];
static lv_obj_t *m_row_type[WALLET_UI_MAX_ROWS];
static lv_obj_t *m_copy_title;
static lv_obj_t *m_copy_sub;
static lv_obj_t *m_copy_action;
static lv_obj_t *m_copy_meta[4];
static lv_obj_t *m_copy_cta;

/* BLE 会话 */
typedef struct {
    bool active;
    bool cover;
    uint8_t card_id;
    uint8_t kind;
    uint8_t content_mode;
    uint8_t uid_len;
    uint8_t sak;
    uint8_t atqa[2];
    uint8_t uid[10];
    uint8_t name_len;
    char name[24];
    uint16_t total;
    uint16_t received;
    uint32_t crc_expected;
    uint32_t crc;
    uint8_t *buf;
    TickType_t last_tick;
} wallet_ble_sess_t;

static wallet_ble_sess_t m_sess;

/* NFC 总开关：持久化在 service_param 的 app8 槽位（默认开）。
   不放进 ServiceParam_Def_t —— 改结构体布局会触发整体重置。 */
#define WALLET_CFG_VER              (1)
typedef struct {
    uint8_t nfc_enable;
} wallet_cfg_t;
static wallet_cfg_t m_cfg;
static uint8_t m_cfg_loaded = 0;
static int m_emu_idx = -1;          // 当前正在模拟的卡下标；-1 = 未模拟

/**************************** helpers ****************************/
static void wallet_cfg_load(void)
{
    if(m_cfg_loaded) return;
    m_cfg_loaded = 1;
    m_cfg.nfc_enable = 1;
    if(service_param_app_load(SERVICE_PARAM_APP_ID_NFC_CFG, &m_cfg,
                              (uint16_t)sizeof(m_cfg), WALLET_CFG_VER) != 0 || m_cfg.nfc_enable > 1)
    {
        m_cfg.nfc_enable = 1;   // 无数据/损坏：默认开启，保持升级前"进卡包即模拟"的行为
    }
}

uint8_t app_wallet_nfc_enabled(void)
{
    wallet_cfg_load();
    return m_cfg.nfc_enable;
}

static void wallet_stop_emu(void)
{
    (void)hal_nfc_emu_stop();
    m_emu_idx = -1;
}

static uint32_t crc32_begin(void) { return 0xFFFFFFFFu; }

static uint32_t crc32_feed(uint32_t c, const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    while(len--)
    {
        c ^= *p++;
        for(int i = 0; i < 8; i++)
        {
            c = (c >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(c & 1u)));
        }
    }
    return c;
}

static uint32_t crc32_end(uint32_t c) { return c ^ 0xFFFFFFFFu; }

static uint16_t get16be(const uint8_t *p) { return (uint16_t)(((uint16_t)p[0] << 8) | p[1]); }
static uint32_t get32be(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static const char *wallet_type_name(uint8_t kind, uint8_t format)
{
    switch(kind)
    {
    case HAL_NFC_KIND_M1_UID:  return "M1 / UID";
    case HAL_NFC_KIND_NTAG213: return "NTAG213";
    case HAL_NFC_KIND_NTAG215: return "NTAG215";
    case HAL_NFC_KIND_NTAG216: return "NTAG216";
    case HAL_NFC_KIND_M1_1K:   return format == HAL_NFC_DATA_M1_BLOCKS ? "M1 1K" : "M1 / UID";
    case HAL_NFC_KIND_M1_2K:   return format == HAL_NFC_DATA_M1_BLOCKS ? "M1 2K" : "M1 / UID";
    case HAL_NFC_KIND_M1_4K:   return format == HAL_NFC_DATA_M1_BLOCKS ? "M1 4K" : "M1 / UID";
    default:                   return "CARD";
    }
}

static void wallet_free_card(wallet_card_t *c)
{
    if(c == NULL) return;
    if(c->mem != NULL) heap_caps_free(c->mem);
    heap_caps_free(c);
}

static bool wallet_kind_is_m1(uint8_t kind)
{
    return kind == HAL_NFC_KIND_M1_UID || kind == HAL_NFC_KIND_M1_1K ||
           kind == HAL_NFC_KIND_M1_2K || kind == HAL_NFC_KIND_M1_4K;
}

static void wallet_card_to_hal(const wallet_card_t *c, hal_nfc_card_t *h)
{
    memset(h, 0, sizeof(*h));
    h->kind = (hal_nfc_kind_t)c->kind;
    h->data_format = (hal_nfc_data_format_t)c->data_format;
    h->atqa[0] = c->atqa[0];
    h->atqa[1] = c->atqa[1];
    h->sak = c->sak;
    h->uid_len = c->uid_len;
    memcpy(h->uid, c->uid, sizeof(h->uid));
    h->mem = c->mem;
    h->mem_len = c->mem_len;
}

static void wallet_start_emu(int idx)
{
    wallet_card_t *c = wallet_store_get(idx);
    if(c == NULL)
    {
        wallet_stop_emu();
        return;
    }
    /* 已在模拟同一张卡：不重启，避免切 app / 刷快照时把射频打断 */
    if(idx == m_emu_idx && hal_nfc_emu_running())
    {
        return;
    }
    hal_nfc_card_t h;
    wallet_card_to_hal(c, &h);
    esp_err_t err = hal_nfc_emu_start(&h);
    m_emu_idx = (err == ESP_OK) ? idx : -1;
    if(err != ESP_OK)
    {
        sys_logw(WALLET_TAG, "emu start failed: %s", esp_err_to_name(err));
    }
    else
    {
        sys_logi(WALLET_TAG, "emu start idx=%d kind=%u uid_len=%u", idx,
                 (unsigned)c->kind, (unsigned)c->uid_len);
    }
}

/* 卡内容被改写（BLE 写入/覆盖）后必须真正重启一次，幂等判断会漏掉"同下标新内容" */
static void wallet_restart_emu(int idx)
{
    wallet_stop_emu();
    wallet_start_emu(idx);
}

void app_wallet_emu_sync(void)
{
    /* 读卡任务占用射频时不能启模拟：emu_start 会先 reader_end 把读卡场关掉，
       扫描会静默失败。等任务收尾后由 m_emu_resync 恢复。 */
    if(m_scan_task != NULL)
    {
        atomic_store(&m_emu_resync, true);
        return;
    }
    if(!app_wallet_nfc_enabled() || !hal_nfc_ready())
    {
        wallet_stop_emu();
        return;
    }
    if(!wallet_store_loaded())
    {
        (void)wallet_store_load();
    }
    if(wallet_store_count() <= 0)
    {
        wallet_stop_emu();
        return;
    }
    int cur = wallet_store_current();
    if(cur < 0 || cur >= wallet_store_count())
    {
        cur = 0;
    }
    wallet_start_emu(cur);
}

uint8_t app_wallet_emu_active(void)
{
    return (m_emu_idx >= 0 && hal_nfc_emu_running()) ? 1 : 0;
}

void app_wallet_set_nfc_enabled(uint8_t enable)
{
    wallet_cfg_load();
    enable = enable ? 1 : 0;
    if(m_cfg.nfc_enable == enable)
    {
        app_wallet_emu_sync();
        return;
    }
    m_cfg.nfc_enable = enable;
    (void)service_param_app_save(SERVICE_PARAM_APP_ID_NFC_CFG, &m_cfg,
                                 (uint16_t)sizeof(m_cfg), WALLET_CFG_VER);
    app_wallet_emu_sync();
    sys_logi(WALLET_TAG, "nfc %s", enable ? "on" : "off");
}

static void wallet_build_snapshot(void)
{
    memset(&m_snap, 0, sizeof(m_snap));
    m_snap.mode = m_mode;
    m_snap.scan = m_scan_state;
    m_snap.count = (uint8_t)wallet_store_count();
    m_snap.current = (uint8_t)(m_snap.count ? wallet_store_current() : 0);

    wallet_card_t *c = wallet_store_get(wallet_store_current());
    if(c != NULL)
    {
        m_snap.kind = c->kind;
        m_snap.uid_len = c->uid_len;
        memcpy(m_snap.uid, c->uid, sizeof(m_snap.uid));
        m_snap.has_cover = (c->flags & WALLET_FLAG_COVER) ? 1 : 0;
        snprintf(m_snap.name, sizeof(m_snap.name), "%s", c->name);
        snprintf(m_snap.type, sizeof(m_snap.type), "%s",
                 wallet_type_name(c->kind, c->data_format));
    }

    if(!hal_nfc_ready())
    {
        snprintf(m_snap.status, sizeof(m_snap.status), "%s", app_text("NFC 硬件未就绪", "NFC NOT READY"));
    }
    else if(!app_wallet_nfc_enabled())
    {
        snprintf(m_snap.status, sizeof(m_snap.status), "%s", app_text("NFC 已关闭", "NFC OFF"));
    }
    else if(m_mode == WALLET_MODE_COPY)
    {
        switch(m_scan_state)
        {
        case WALLET_SCAN_SCANNING: snprintf(m_snap.status, sizeof(m_snap.status), "%s",
                                           app_text("正在读卡…", "READING...")); break;
        case WALLET_SCAN_DONE:     snprintf(m_snap.status, sizeof(m_snap.status), "%s",
                                           app_text("读取成功", "READ OK")); break;
        case WALLET_SCAN_UNSUPPORTED: snprintf(m_snap.status, sizeof(m_snap.status), "%s",
                                              app_text("暂不支持", "UNSUPPORTED CARD")); break;
        default:                   snprintf(m_snap.status, sizeof(m_snap.status), "%s",
                                           app_text("读取失败", "READ FAILED")); break;
        }
        if(m_scan_state == WALLET_SCAN_DONE)
        {
            wallet_card_t *p = atomic_load(&m_scan_result);
            if(p != NULL)
            {
                m_snap.copy_kind = p->kind;
                m_snap.copy_format = p->data_format;
                m_snap.copy_uid_len = p->uid_len;
                memcpy(m_snap.copy_uid, p->uid, sizeof(m_snap.copy_uid));
                m_snap.copy_mem_len = p->mem_len;
            }
        }
    }
    else
    {
        snprintf(m_snap.status, sizeof(m_snap.status), "%s",
                 c ? app_text("模拟中 · 等待读卡器", "EMULATING / TAP READER")
                   : app_text("未选择卡片", "NO CARD SELECTED"));
    }
}

static wallet_cover_t *wallet_load_cover(int idx)
{
    uint8_t *payload = NULL;
    uint32_t crc = 0;
    if(wallet_store_cover_load(idx, &payload, &crc) != 0)
    {
        return NULL;
    }

    wallet_cover_t *c = (wallet_cover_t *)heap_caps_malloc(sizeof(*c), MALLOC_CAP_SPIRAM);
    if(c == NULL) c = (wallet_cover_t *)malloc(sizeof(*c));
    if(c == NULL)
    {
        heap_caps_free(payload);
        return NULL;
    }

    memset(c, 0, sizeof(*c));
    for(int i = 0; i < WALLET_COVER_PIXELS; i++)
    {
        uint8_t bit = (payload[i >> 3] >> (7 - (i & 7))) & 1;
        c->pixels[i] = bit ? 0x00 : 0xFF;
    }
    heap_caps_free(payload);

    c->dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    c->dsc.header.cf = LV_COLOR_FORMAT_L8;
    c->dsc.header.w = WALLET_COVER_W;
    c->dsc.header.h = WALLET_COVER_H;
    c->dsc.header.stride = WALLET_COVER_W;
    c->dsc.data_size = WALLET_COVER_PIXELS;
    c->dsc.data = c->pixels;
    return c;
}

static void wallet_post_snapshot(void)
{
    wallet_build_snapshot();
    (void)ui_core_post(APP_UI_MSG_WALLET_SNAP, &m_snap, sizeof(m_snap));

    if(m_snap.has_cover)
    {
        wallet_cover_t *c = wallet_load_cover(wallet_store_current());
        if(ui_core_post(APP_UI_MSG_WALLET_COVER, &c, sizeof(c)) != 0 && c != NULL)
        {
            heap_caps_free(c);
        }
    }
}

static void wallet_cancel_scan(void)
{
    atomic_store(&m_scan_cancel, true);
}

static void wallet_take_pending(void)
{
    wallet_card_t *p = atomic_exchange(&m_scan_result, NULL);
    if(p != NULL) wallet_free_card(p);
}

/**************************** 读卡任务 ****************************/
static int wallet_ntag_pages(uint8_t version)
{
    switch(version)
    {
    case 0x0F: return 45;
    case 0x11: return 135;
    case 0x13: return 231;
    default:   return 0;
    }
}

static wallet_card_t *wallet_read_card(hal_nfc_card_t *id, wallet_scan_status_t *status)
{
    *status = WSCAN_UNSUPPORTED;

    if(id->sak & 0x20)
    {
        return NULL;
    }

    if(id->sak == 0x08 || id->sak == 0x18 || id->sak == 0x38 ||
       id->sak == 0x88 || id->sak == 0x98)
    {
        wallet_card_t *c = (wallet_card_t *)heap_caps_malloc(sizeof(*c), MALLOC_CAP_SPIRAM);
        if(c == NULL) c = (wallet_card_t *)malloc(sizeof(*c));
        if(c == NULL) { *status = WSCAN_ERROR; return NULL; }
        memset(c, 0, sizeof(*c));
        c->kind = HAL_NFC_KIND_M1_UID;
        c->data_format = HAL_NFC_DATA_NONE;
        c->uid_len = id->uid_len;
        c->sak = id->sak;
        c->atqa[0] = id->atqa[0];
        c->atqa[1] = id->atqa[1];
        memcpy(c->uid, id->uid, sizeof(c->uid));
        c->name_len = 0;
        *status = WSCAN_OK;
        return c;
    }

    if(id->sak != 0x00)
    {
        return NULL;
    }

    uint8_t tx[2] = {0x60, 0x00};
    uint8_t rx[16] = {0};
    uint16_t rx_len = sizeof(rx);
    if(hal_nfc_reader_transceive(tx, 1, rx, &rx_len, 50) != ESP_OK || rx_len < 8 || rx[2] != 0x04)
    {
        return NULL;
    }

    uint8_t version = rx[6];
    int pages = wallet_ntag_pages(version);
    if(pages == 0)
    {
        return NULL;
    }

    uint16_t mem_len = (uint16_t)(pages * 4);
    uint8_t *mem = (uint8_t *)heap_caps_malloc(mem_len, MALLOC_CAP_SPIRAM);
    if(mem == NULL) mem = (uint8_t *)malloc(mem_len);
    if(mem == NULL) { *status = WSCAN_ERROR; return NULL; }

    for(int page = 0; page < pages; )
    {
        int start = page;
        if(start + 3 > pages - 1) start = pages - 4;
        tx[0] = 0x30;
        tx[1] = (uint8_t)start;
        rx_len = 16;
        if(hal_nfc_reader_transceive(tx, 2, rx, &rx_len, 50) != ESP_OK || rx_len < 16)
        {
            heap_caps_free(mem);
            return NULL;
        }
        int copy_pages = pages - start;
        if(copy_pages > 4) copy_pages = 4;
        memcpy(&mem[start * 4], rx, (size_t)copy_pages * 4);
        page = start + 4;
    }

    /* 页 0/2/3 基本校验（7 字节 UID：BCC0 = 0x88 ^ UID0..2，BCC1 = UID3..6） */
    uint8_t bcc0 = (uint8_t)(0x88 ^ mem[0] ^ mem[1] ^ mem[2]);
    uint8_t bcc1 = (uint8_t)(mem[4] ^ mem[5] ^ mem[6] ^ mem[7]);
    if(mem[3] != bcc0 || mem[8] != bcc1 || mem[12] != 0xE1)
    {
        heap_caps_free(mem);
        return NULL;
    }

    wallet_card_t *c = (wallet_card_t *)heap_caps_malloc(sizeof(*c), MALLOC_CAP_SPIRAM);
    if(c == NULL) c = (wallet_card_t *)malloc(sizeof(*c));
    if(c == NULL)
    {
        heap_caps_free(mem);
        *status = WSCAN_ERROR;
        return NULL;
    }
    memset(c, 0, sizeof(*c));
    c->kind = version == 0x0F ? HAL_NFC_KIND_NTAG213 :
              version == 0x11 ? HAL_NFC_KIND_NTAG215 : HAL_NFC_KIND_NTAG216;
    c->data_format = HAL_NFC_DATA_NTAG_PAGES;
    c->data_ver = WALLET_DATA_VER;
    c->uid_len = 7;
    c->sak = 0;
    c->atqa[0] = 0x44;
    c->atqa[1] = 0x00;
    memcpy(c->uid, mem, 3);
    memcpy(&c->uid[3], &mem[4], 4);
    c->mem_len = mem_len;
    c->mem = mem;
    c->name_len = 0;
    *status = WSCAN_OK;
    return c;
}

static void wallet_scan_task(void *arg)
{
    (void)arg;
    wallet_scan_status_t status = WSCAN_TIMEOUT;
    wallet_card_t *result = NULL;

    TickType_t start = xTaskGetTickCount();
    while(!atomic_load(&m_scan_cancel) &&
          (xTaskGetTickCount() - start) < pdMS_TO_TICKS(HAL_NFC_SCAN_TIMEOUT_MS))
    {
        hal_nfc_card_t id;
        esp_err_t err = hal_nfc_reader_poll(&id, 250);
        if(err == ESP_OK)
        {
            result = wallet_read_card(&id, &status);
            (void)hal_nfc_reader_halt();
            break;
        }
        if(err != ESP_ERR_NOT_FOUND)
        {
            status = WSCAN_ERROR;
            break;
        }
    }

    if(atomic_load(&m_scan_cancel)) status = WSCAN_CANCELLED;
    if(result == NULL && status == WSCAN_UNSUPPORTED) { /* keep */ }
    (void)hal_nfc_reader_end();
    sys_logi(WALLET_TAG, "scan end status=%u", (unsigned)status);

    /* 先清句柄：下面的 emu_sync 要看到"读卡已收尾"，否则会被自身的门闸挡回去 */
    m_scan_task = NULL;

    /* 读卡期间用户已离开卡包：reader 先收尾，再恢复常驻模拟（避免 reader_end 打断 PICC） */
    if(atomic_exchange(&m_emu_resync, false))
    {
        app_wallet_emu_sync();
    }

    if(!atomic_load(&m_scan_cancel))
    {
        atomic_store(&m_scan_result, result);
        uint8_t st = (uint8_t)status;
        (void)sys_event_publish(SYS_EVT_NFC_SCAN_DONE, &st, 1);
    }
    else if(result != NULL)
    {
        wallet_free_card(result);
    }

    vTaskDelete(NULL);
}

static void wallet_start_read(void)
{
    if(!app_wallet_nfc_enabled())
    {
        return;   // NFC 总开关关闭：不启动读卡
    }
    wallet_cancel_scan();
    wallet_take_pending();
    wallet_stop_emu();

    m_mode = WALLET_MODE_COPY;
    m_scan_state = WALLET_SCAN_SCANNING;
    m_scan_task = NULL;
    atomic_store(&m_scan_cancel, false);

    esp_err_t err = hal_nfc_reader_begin();
    if(err != ESP_OK)
    {
        sys_logw(WALLET_TAG, "reader begin failed: %s", esp_err_to_name(err));
        m_scan_state = WALLET_SCAN_FAIL;
        wallet_post_snapshot();
        return;
    }

    if(xTaskCreatePinnedToCore(wallet_scan_task, "wallet_scan", 4096, NULL, 4, &m_scan_task, 1) != pdPASS)
    {
        (void)hal_nfc_reader_end();
        m_scan_state = WALLET_SCAN_FAIL;
        sys_logw(WALLET_TAG, "scan task create failed");
    }
    else
    {
        sys_logi(WALLET_TAG, "reader begin ok");
    }
    wallet_post_snapshot();
}

static void wallet_back_to_emu(void)
{
    wallet_cancel_scan();
    wallet_take_pending();
    m_mode = WALLET_MODE_EMU;
    app_wallet_emu_sync();
}

/**************************** app 接口 ****************************/
static const char *wallet_enter_block_reason(void)
{
    if(!hal_nfc_ready())
    {
        return "NFC NOT READY";
    }

    (void)wallet_store_load();
    m_mode = WALLET_MODE_EMU;
    m_scan_state = WALLET_SCAN_FAIL;
    atomic_store(&m_scan_cancel, false);
    wallet_take_pending();

    /* 常驻模拟：开关开且有卡就以当前选中的卡模拟（无卡/关闭则停） */
    app_wallet_emu_sync();
    return NULL;
}

static void wallet_on_scan_done(uint8_t status)
{
    wallet_card_t *p = atomic_exchange(&m_scan_result, NULL);
    if(status == WSCAN_OK && p != NULL)
    {
        atomic_store(&m_scan_result, p);
        m_scan_state = WALLET_SCAN_DONE;
    }
    else
    {
        if(p != NULL) wallet_free_card(p);
        m_scan_state = (status == WSCAN_UNSUPPORTED) ? WALLET_SCAN_UNSUPPORTED : WALLET_SCAN_FAIL;
    }
    wallet_post_snapshot();
}

static void wallet_commit_pending(void)
{
    wallet_card_t *p = atomic_exchange(&m_scan_result, NULL);
    if(p == NULL) return;

    int idx = -1;
    if(wallet_store_upsert(p, &idx) == 0 && idx >= 0)
    {
        wallet_store_select(idx);
        (void)wallet_store_save();
        wallet_restart_emu(idx);   // 同 UID 覆盖时内容已变，必须重启模拟
    }
    else
    {
        m_scan_state = WALLET_SCAN_FAIL;
    }
    wallet_free_card(p);
    m_mode = WALLET_MODE_EMU;
    wallet_post_snapshot();
}

static void wallet_next_prev(int delta)
{
    int n = wallet_store_count();
    if(n <= 0) return;
    int cur = wallet_store_current();
    cur = (cur + delta + n) % n;
    wallet_store_select(cur);
    (void)wallet_store_save();
    wallet_start_emu(cur);
    wallet_post_snapshot();
}

static void wallet_on_event(const app_event_t *e)
{
    /* 状态栏（电量/WiFi/蓝牙/时间）与提示行复用 app_shell 的采集/回投逻辑 */
    app_shell_on_event(e);

    if(e->type == APP_EVT_UI_MSG && e->cmd == APP_UI_REQ_WALLET_CMD && e->len >= 1)
    {
        switch(e->payload[0])
        {
        case WALLET_CMD_NEXT: if(m_mode == WALLET_MODE_EMU) wallet_next_prev(1); break;
        case WALLET_CMD_PREV: if(m_mode == WALLET_MODE_EMU) wallet_next_prev(-1); break;
        case WALLET_CMD_SYNC: wallet_post_snapshot(); break;
        case WALLET_CMD_ACTION:
            if(m_mode != WALLET_MODE_COPY) break;
            if(m_scan_state == WALLET_SCAN_SCANNING)
            {
                wallet_back_to_emu();
                wallet_post_snapshot();
            }
            else if(m_scan_state == WALLET_SCAN_DONE)
            {
                wallet_commit_pending();
            }
            else
            {
                wallet_start_read();
            }
            break;
        default: break;
        }
        return;
    }

    if(e->type == APP_EVT_SYS && e->cmd == SYS_EVT_NFC_SCAN_DONE && e->len >= 1)
    {
        wallet_on_scan_done(e->payload[0]);
        return;
    }
}

static bool wallet_on_double(void)
{
    if(m_mode != WALLET_MODE_EMU) return false;
    if(!app_wallet_nfc_enabled()) return false;   // NFC 关闭时双击交回框架（退回主菜单）
    wallet_start_read();
    return true;
}

static bool wallet_on_back(void)
{
    if(m_mode != WALLET_MODE_COPY) return false;
    wallet_back_to_emu();
    wallet_post_snapshot();
    return true;
}

void app_wallet_leave(void)
{
    wallet_cancel_scan();
    wallet_take_pending();
    m_mode = WALLET_MODE_EMU;
    if(m_scan_task != NULL)
    {
        /* 读卡任务还在跑：先别动射频，等它 reader_end 完成后自行恢复模拟 */
        atomic_store(&m_emu_resync, true);
        return;
    }
    app_wallet_emu_sync();   // 离开卡包不停模拟：开关开且有卡就继续模拟当前卡
}

void app_wallet_sleep_prepare(void)
{
    wallet_cancel_scan();
    wallet_take_pending();
    atomic_store(&m_emu_resync, false);
    m_mode = WALLET_MODE_EMU;
    if(!wallet_store_loaded()) (void)wallet_store_load();
    app_wallet_emu_sync();
    if(!app_wallet_nfc_enabled())
    {
        /* 总开关关闭：连芯片一起卸掉，monitor 的 sleep_arm 会失败，
           入睡后不再被 NFC 场唤醒（也省掉场检测的待机电流）。 */
        hal_nfc_deinit();
    }
}

void app_wallet_deactivate(void)
{
    wallet_cancel_scan();
    wallet_take_pending();
    atomic_store(&m_emu_resync, false);
    wallet_stop_emu();
    m_mode = WALLET_MODE_EMU;
}

/**************************** UI ****************************/
static lv_obj_t *wallet_label(lv_obj_t *parent, const lv_font_t *font,
                              lv_color_t color, const char *text)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(l, color, LV_PART_MAIN);
    lv_label_set_text(l, text);
    return l;
}

static lv_obj_t *wallet_black_box(lv_obj_t *parent, int w, int h)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_scrollable(o, false);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, LV_PART_MAIN);
    return o;
}

static void wallet_ui_apply_snapshot(void)
{
    if(m_snap.mode == WALLET_MODE_EMU)
    {
        lv_obj_remove_flag(m_emu_page, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(m_copy_page, LV_OBJ_FLAG_HIDDEN);

        if(m_snap.count > 0)
        {
            lv_label_set_text_fmt(m_idx_label, "%02u", (unsigned)(m_snap.current + 1));
            lv_label_set_text(m_name_label, m_snap.name);
            lv_label_set_text(m_type_label, m_snap.type);
            lv_label_set_text_fmt(m_count_label, app_text("%u 张卡片", "%u CARDS"), (unsigned)m_snap.count);

            for(int i = 0; i < WALLET_UI_MAX_ROWS; i++)
            {
                int idx = i;
                if(idx >= m_snap.count)
                {
                    lv_obj_add_flag(m_row_obj[i], LV_OBJ_FLAG_HIDDEN);
                    continue;
                }
                lv_obj_remove_flag(m_row_obj[i], LV_OBJ_FLAG_HIDDEN);
                wallet_card_t *c = wallet_store_get(idx);
                if(c == NULL) continue;
                lv_label_set_text_fmt(m_row_idx[i], "%02u", (unsigned)(idx + 1));
                lv_label_set_text(m_row_name[i], c->name);
                lv_label_set_text(m_row_type[i], wallet_type_name(c->kind, c->data_format));
                if(idx == m_snap.current)
                {
                    lv_obj_set_style_bg_color(m_row_obj[i], lv_color_black(), LV_PART_MAIN);
                    lv_obj_set_style_bg_opa(m_row_obj[i], LV_OPA_COVER, LV_PART_MAIN);
                    lv_obj_set_style_text_color(m_row_idx[i], lv_color_white(), LV_PART_MAIN);
                    lv_obj_set_style_text_color(m_row_name[i], lv_color_white(), LV_PART_MAIN);
                    lv_obj_set_style_text_color(m_row_type[i], lv_color_white(), LV_PART_MAIN);
                }
                else
                {
                    lv_obj_set_style_bg_opa(m_row_obj[i], LV_OPA_TRANSP, LV_PART_MAIN);
                    lv_obj_set_style_text_color(m_row_idx[i], lv_color_black(), LV_PART_MAIN);
                    lv_obj_set_style_text_color(m_row_name[i], lv_color_black(), LV_PART_MAIN);
                    lv_obj_set_style_text_color(m_row_type[i], lv_color_black(), LV_PART_MAIN);
                }
            }
            if(m_snap.has_cover)
            {
                lv_obj_add_flag(m_default_panel, LV_OBJ_FLAG_HIDDEN);
            }
            else
            {
                lv_obj_add_flag(m_cover_img, LV_OBJ_FLAG_HIDDEN);
                lv_obj_remove_flag(m_default_panel, LV_OBJ_FLAG_HIDDEN);
                lv_label_set_text(m_default_type, m_snap.type);
            }
        }
        else
        {
            lv_obj_add_flag(m_cover_img, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(m_default_panel, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(m_default_type, app_text("无卡片", "NO CARD"));
            lv_label_set_text(m_idx_label, "--");
            lv_label_set_text(m_name_label, app_text("卡包", "WALLET"));
            lv_label_set_text(m_type_label, "");
            lv_label_set_text(m_count_label, app_text("0 张卡片", "0 CARDS"));
            for(int i = 0; i < WALLET_UI_MAX_ROWS; i++)
            {
                lv_obj_add_flag(m_row_obj[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
        lv_label_set_text(m_status_label, m_snap.status);
        app_shell_set_text(&m_shell,
                           app_text("上下切卡  双击读卡  返回退出  长按休眠",
                                    "UP/DOWN CARD  DOUBLE READ  BACK EXIT  HOLD SLEEP"),
                           app_text("卡包", "WALLET"));
        return;
    }

    /* COPY */
    lv_obj_add_flag(m_emu_page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(m_copy_page, LV_OBJ_FLAG_HIDDEN);
    app_shell_set_text(&m_shell,
                       app_text("单击取消/复制  返回退出  长按休眠",
                                "ENTER CANCEL/COPY  BACK EMU  HOLD SLEEP"),
                       app_text("卡包 读卡", "WALLET COPY"));

    const char *title = app_text("正在读卡…", "READING...");
    const char *sub = app_text("将卡片贴在设备 NFC 区域", "PLACE CARD ON NFC AREA");
    const char *action = app_text("单击取消", "ENTER CANCEL");
    if(m_snap.scan == WALLET_SCAN_DONE)
    {
        title = app_text("读取成功", "READ OK");
        sub = app_text("按确认键复制到卡包", "PRESS ENTER TO COPY");
        action = app_text("复制卡", "COPY CARD");
    }
    else if(m_snap.scan == WALLET_SCAN_UNSUPPORTED)
    {
        title = app_text("暂不支持", "UNSUPPORTED");
        sub = app_text("ISO14443-4 / 加密卡", "ISO14443-4 / SECURE CARD");
        action = app_text("单击重试", "ENTER RETRY");
    }
    else if(m_snap.scan == WALLET_SCAN_FAIL)
    {
        title = app_text("读取失败", "READ FAILED");
        sub = app_text("超时 / 无卡片", "TIMEOUT / NO CARD");
        action = app_text("单击重试", "ENTER RETRY");
    }
    lv_label_set_text(m_copy_title, title);
    lv_label_set_text(m_copy_sub, sub);
    lv_label_set_text(m_copy_action, action);

    if(m_snap.scan == WALLET_SCAN_DONE)
    {
        lv_label_set_text_fmt(m_copy_meta[0], app_text("类型   %s", "TYPE   %s"),
                              wallet_type_name(m_snap.copy_kind, m_snap.copy_format));
        char uid[64] = {0};
        for(int i = 0; i < m_snap.copy_uid_len; i++)
        {
            char t[4];
            snprintf(t, sizeof(t), "%02X ", m_snap.copy_uid[i]);
            strncat(uid, t, sizeof(uid) - strlen(uid) - 1);
        }
        lv_label_set_text_fmt(m_copy_meta[1], app_text("UID    %s", "UID    %s"), uid);
        lv_label_set_text_fmt(m_copy_meta[2], app_text("容量   %u 字节", "SIZE   %u BYTES"),
                              (unsigned)m_snap.copy_mem_len);
        lv_label_set_text(m_copy_meta[3], m_snap.copy_format == HAL_NFC_DATA_NONE ?
                          app_text("内容   仅卡号", "DATA   UID ONLY") :
                          app_text("内容   已读取整片镜像", "DATA   IMAGE READ"));
        lv_obj_remove_flag(m_copy_cta, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(m_copy_action, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        lv_obj_add_flag(m_copy_cta, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(m_copy_action, LV_OBJ_FLAG_HIDDEN);
        for(int i = 0; i < 4; i++) lv_label_set_text(m_copy_meta[i], "");
    }
}

static void wallet_ui_clear_cover(void)
{
    if(m_cover_img != NULL) lv_image_set_src(m_cover_img, NULL);
    if(m_cover != NULL)
    {
#if LV_CACHE_DEF_SIZE > 0
        lv_image_cache_drop(&m_cover->dsc);
#endif
        heap_caps_free(m_cover);
        m_cover = NULL;
    }
    if(m_cover_img != NULL) lv_obj_add_flag(m_cover_img, LV_OBJ_FLAG_HIDDEN);
}

static void wallet_ui_on_msg(uint32_t cmd, const void *data, uint8_t len)
{
    if(cmd == APP_UI_MSG_WALLET_SNAP && data != NULL && len >= sizeof(wallet_snapshot_t))
    {
        memcpy(&m_snap, data, sizeof(m_snap));
        if(!m_snap.has_cover) wallet_ui_clear_cover();
        wallet_ui_apply_snapshot();
        return;
    }
    if(cmd == APP_UI_MSG_WALLET_COVER && data != NULL && len == sizeof(wallet_cover_t *))
    {
        wallet_cover_t *next = *(wallet_cover_t *const *)data;
        if(m_cover_img != NULL)
        {
#if LV_CACHE_DEF_SIZE > 0
            if(m_cover != NULL) lv_image_cache_drop(&m_cover->dsc);
#endif
            lv_image_set_src(m_cover_img, NULL);
        }
        if(m_cover != NULL) heap_caps_free(m_cover);
        m_cover = next;
        if(m_cover != NULL)
        {
            lv_image_set_src(m_cover_img, &m_cover->dsc);
            lv_obj_remove_flag(m_cover_img, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(m_default_panel, LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_add_flag(m_cover_img, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(m_default_panel, LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }
    (void)app_shell_handle_msg(&m_shell, cmd, data, len);
}

static void wallet_ui_on_key(input_press_type_t key)
{
    if(m_snap.mode == WALLET_MODE_EMU)
    {
        if(key == INPUT_PRESS_UP) (void)app_manager_post_ui_msg(APP_UI_REQ_WALLET_CMD, &(uint8_t){WALLET_CMD_NEXT}, 1);
        else if(key == INPUT_PRESS_DOWN) (void)app_manager_post_ui_msg(APP_UI_REQ_WALLET_CMD, &(uint8_t){WALLET_CMD_PREV}, 1);
        return;
    }
    if(key == INPUT_PRESS_SHORT)
    {
        uint8_t cmd = WALLET_CMD_ACTION;
        (void)app_manager_post_ui_msg(APP_UI_REQ_WALLET_CMD, &cmd, 1);
    }
}

static void wallet_ui_create(lv_obj_t *root)
{
    app_shell_build(root,
                    app_text("上下切卡  双击读卡  返回退出  长按休眠",
                             "UP/DOWN CARD  DOUBLE READ  BACK EXIT  HOLD SLEEP"),
                    app_text("卡包", "WALLET"), &m_shell);
    app_shell_request_status();

    lv_obj_t *body = lv_obj_create(root);
    lv_obj_remove_style_all(body);
    lv_obj_set_scrollable(body, false);
    lv_obj_set_size(body, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_top(body, SHELL_BODY_TOP, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(body, SHELL_BODY_BOTTOM, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(body, SHELL_BODY_PAD_X, LV_PART_MAIN);

    /* ---- EMU ---- */
    m_emu_page = lv_obj_create(body);
    lv_obj_remove_style_all(m_emu_page);
    lv_obj_set_scrollable(m_emu_page, false);
    lv_obj_set_size(m_emu_page, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(m_emu_page, LV_FLEX_FLOW_COLUMN);

    m_cover_img = lv_image_create(m_emu_page);
    lv_obj_set_size(m_cover_img, WALLET_COVER_W, WALLET_COVER_H);
    lv_obj_add_flag(m_cover_img, LV_OBJ_FLAG_HIDDEN);

    m_default_panel = wallet_black_box(m_emu_page, WALLET_COVER_W, WALLET_COVER_H);
    lv_obj_set_flex_flow(m_default_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(m_default_panel, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    wallet_label(m_default_panel, &lv_font_unscii_16, lv_color_white(), "NFC");
    m_default_type = wallet_label(m_default_panel, app_font_small(), lv_color_white(),
                                  app_text("无封面", "NO COVER"));

    lv_obj_t *bar = wallet_black_box(m_emu_page, WALLET_COVER_W, 48);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(bar, 14, LV_PART_MAIN);
    lv_obj_set_style_pad_column(bar, 10, LV_PART_MAIN);
    m_idx_label = wallet_label(bar, app_font_small(), lv_color_white(), "01");
    m_name_label = wallet_label(bar, app_font_body(), lv_color_white(), app_text("卡包", "WALLET"));
    lv_obj_set_flex_grow(m_name_label, 1);
    m_type_label = wallet_label(bar, app_font_small(), lv_color_white(), "NTAG215");

    lv_obj_t *hint = lv_obj_create(m_emu_page);
    lv_obj_remove_style_all(hint);
    lv_obj_set_scrollable(hint, false);
    lv_obj_set_size(hint, WALLET_COVER_W, 36);
    lv_obj_set_style_border_side(hint, LV_BORDER_SIDE_TOP | LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
    lv_obj_set_style_border_width(hint, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(hint, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_flex_flow(hint, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hint, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(hint, 4, LV_PART_MAIN);
    wallet_label(hint, app_font_small(), lv_color_black(),
                 app_text("双击确认键 · 进入读卡", "DOUBLE ENTER - READ"));
    m_count_label = wallet_label(hint, app_font_small(), lv_color_black(), app_text("0 张卡片", "0 CARDS"));

    lv_obj_t *list = lv_obj_create(m_emu_page);
    lv_obj_remove_style_all(list);
    lv_obj_set_scrollable(list, false);
    lv_obj_set_size(list, WALLET_COVER_W, WALLET_UI_MAX_ROWS * 38);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);

    for(int i = 0; i < WALLET_UI_MAX_ROWS; i++)
    {
        m_row_obj[i] = lv_obj_create(list);
        lv_obj_remove_style_all(m_row_obj[i]);
        lv_obj_set_scrollable(m_row_obj[i], false);
        lv_obj_set_size(m_row_obj[i], WALLET_COVER_W, 38);
        lv_obj_set_style_border_side(m_row_obj[i], LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
        lv_obj_set_style_border_width(m_row_obj[i], 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(m_row_obj[i], lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_pad_hor(m_row_obj[i], 10, LV_PART_MAIN);
        lv_obj_set_flex_flow(m_row_obj[i], LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(m_row_obj[i], LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(m_row_obj[i], 10, LV_PART_MAIN);
        m_row_idx[i] = wallet_label(m_row_obj[i], app_font_small(), lv_color_black(), "01");
        m_row_name[i] = wallet_label(m_row_obj[i], app_font_body(), lv_color_black(), "CARD");
        lv_obj_set_flex_grow(m_row_name[i], 1);
        m_row_type[i] = wallet_label(m_row_obj[i], app_font_small(), lv_color_black(), "NTAG215");
    }

    m_status_label = wallet_label(m_emu_page, app_font_small(), lv_color_black(), "");

    /* ---- COPY ---- */
    m_copy_page = lv_obj_create(body);
    lv_obj_remove_style_all(m_copy_page);
    lv_obj_set_scrollable(m_copy_page, false);
    lv_obj_set_size(m_copy_page, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(m_copy_page, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(m_copy_page, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *panel = lv_obj_create(m_copy_page);
    lv_obj_remove_style_all(panel);
    lv_obj_set_scrollable(panel, false);
    lv_obj_set_size(panel, WALLET_COVER_W, 300);
    lv_obj_set_style_border_width(panel, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(panel, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(panel, 10, LV_PART_MAIN);
    wallet_label(panel, &lv_font_unscii_16, lv_color_black(), "NFC");
    m_copy_title = wallet_label(panel, app_font_body(), lv_color_black(),
                                app_text("正在读卡…", "READING..."));
    m_copy_sub = wallet_label(panel, app_font_small(), lv_color_black(),
                              app_text("将卡片贴在设备 NFC 区域", "PLACE CARD ON NFC AREA"));
    m_copy_action = wallet_label(panel, app_font_small(), lv_color_black(),
                                 app_text("单击取消", "ENTER CANCEL"));

    for(int i = 0; i < 4; i++)
    {
        m_copy_meta[i] = wallet_label(m_copy_page, app_font_small(), lv_color_black(), "");
    }

    m_copy_cta = wallet_black_box(m_copy_page, WALLET_COVER_W, 44);
    lv_obj_set_flex_flow(m_copy_cta, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(m_copy_cta, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(m_copy_cta, 18, LV_PART_MAIN);
    wallet_label(m_copy_cta, app_font_body(), lv_color_white(),
                 app_text("复制卡         ENTER", "COPY CARD  ENTER"));
    lv_obj_add_flag(m_copy_cta, LV_OBJ_FLAG_HIDDEN);

    wallet_ui_apply_snapshot();
    app_shell_start_tick(&m_shell, NULL);
    (void)app_manager_post_ui_msg(APP_UI_REQ_WALLET_CMD, &(uint8_t){WALLET_CMD_SYNC}, 1);
}

static void wallet_ui_destroy(void)
{
    app_shell_release(&m_shell);
    if(m_cover_img != NULL) lv_image_set_src(m_cover_img, NULL);
    if(m_cover != NULL)
    {
#if LV_CACHE_DEF_SIZE > 0
        lv_image_cache_drop(&m_cover->dsc);
#endif
        heap_caps_free(m_cover);
        m_cover = NULL;
    }
    m_cover_img = NULL;
    m_default_panel = NULL;
    m_emu_page = NULL;
    m_copy_page = NULL;
    m_copy_cta = NULL;
}

static const app_ui_ops_t g_wallet_ui_ops = {
    .create = wallet_ui_create,
    .destroy = wallet_ui_destroy,
    .on_msg = wallet_ui_on_msg,
    .on_key = wallet_ui_on_key,
};

static const uint16_t g_wallet_events[] = { SYS_EVT_NFC_SCAN_DONE, 0 };

const app_entry_t g_app_wallet_entry = {
    .id = APP_ID_WALLET,
    .name = "wallet",
    .data_dir = NULL,
    .keys = APP_KEY_UP | APP_KEY_DOWN | APP_KEY_SHORT,
    .tick_ms = 0,
    .events = g_wallet_events,
    .enter_block_reason = wallet_enter_block_reason,
    .layer = APP_LAYER_UI,
    .ui_ops = &g_wallet_ui_ops,
    .on_event = wallet_on_event,
    .on_back = wallet_on_back,
    .on_double = wallet_on_double,
};

/**************************** BLE ****************************/
/* 状态码与设计一致 */
#define WST_OK          0
#define WST_BUSY        1
#define WST_PARAM       2
#define WST_NOSD        3
#define WST_NOTFOUND    4
#define WST_NOMEM       5
#define WST_SAVE        6
#define WST_EMU_FAIL    7

static void sess_free(void)
{
    if(m_sess.buf != NULL)
    {
        heap_caps_free(m_sess.buf);
        m_sess.buf = NULL;
    }
    memset(&m_sess, 0, sizeof(m_sess));
}

static void sess_expire(void)
{
    if(m_sess.active &&
       (xTaskGetTickCount() - m_sess.last_tick) > pdMS_TO_TICKS(60000))
    {
        sess_free();
    }
}

static void resp(uint8_t ch, const uint8_t *data, uint8_t len)
{
    service_ble_send_resp(ch, data, len);
}

static uint8_t wallet_status_from_store(int rc)
{
    if(rc == 0) return WST_OK;
    if(rc == -2) return WST_PARAM;
    if(rc == -3) return WST_NOMEM;
    return WST_SAVE;
}

static void ble_card_begin(const uint8_t *d, uint8_t n)
{
    uint8_t rsp[3] = {WST_PARAM, 0, BLE_NFC_CHUNK_MAX};
    if(n < 13) { resp(BLE_FILM_TRANS_CH_NFC_CARD_BEGIN, rsp, 1); return; }

    uint8_t kind = d[0];
    uint8_t mode = d[1];
    uint8_t uid_len = d[2];
    uint8_t sak = d[3];
    if(uid_len == 0 || uid_len > 10 || (uint16_t)6 + uid_len + 1 > n)
    {
        resp(BLE_FILM_TRANS_CH_NFC_CARD_BEGIN, rsp, 1);
        return;
    }
    const uint8_t *uid = &d[6];
    uint8_t name_len = d[6 + uid_len];
    if(name_len > 23 || (uint16_t)6 + uid_len + 1 + name_len + 6 > n)
    {
        resp(BLE_FILM_TRANS_CH_NFC_CARD_BEGIN, rsp, 1);
        return;
    }
    uint16_t total = get16be(&d[7 + uid_len + name_len]);
    uint32_t crc = get32be(&d[9 + uid_len + name_len]);

    if(kind > HAL_NFC_KIND_M1_4K || total > HAL_NFC_MAX_CARD_BYTES)
    {
        resp(BLE_FILM_TRANS_CH_NFC_CARD_BEGIN, rsp, 1);
        return;
    }
    if(kind >= HAL_NFC_KIND_NTAG213 && kind <= HAL_NFC_KIND_NTAG216)
    {
        if(uid_len != 7 || sak != 0x00 || d[4] != 0x44 || d[5] != 0x00)
        {
            resp(BLE_FILM_TRANS_CH_NFC_CARD_BEGIN, rsp, 1);
            return;
        }
    }
    else
    {
        if(uid_len != 4) { resp(BLE_FILM_TRANS_CH_NFC_CARD_BEGIN, rsp, 1); return; }
        if(kind == HAL_NFC_KIND_M1_UID && (sak != 0x08 && sak != 0x18 && sak != 0x38))
        {
            resp(BLE_FILM_TRANS_CH_NFC_CARD_BEGIN, rsp, 1);
            return;
        }
    }

    uint16_t expect = 0;
    if(kind == HAL_NFC_KIND_M1_UID) expect = 0;
    else if(kind == HAL_NFC_KIND_NTAG213) expect = mode == 0 ? 180 : 0;
    else if(kind == HAL_NFC_KIND_NTAG215) expect = mode == 0 ? 540 : 0;
    else if(kind == HAL_NFC_KIND_NTAG216) expect = mode == 0 ? 924 : 0;
    else if(kind == HAL_NFC_KIND_M1_1K) expect = 1024;
    else if(kind == HAL_NFC_KIND_M1_2K) expect = 2048;
    else if(kind == HAL_NFC_KIND_M1_4K) expect = 4096;

    if(mode == 0 && expect != total) { resp(BLE_FILM_TRANS_CH_NFC_CARD_BEGIN, rsp, 1); return; }
    if(mode == 1 && (kind < HAL_NFC_KIND_NTAG213 || kind > HAL_NFC_KIND_NTAG216 || total == 0))
    {
        resp(BLE_FILM_TRANS_CH_NFC_CARD_BEGIN, rsp, 1); return;
    }
    if(mode == 2 && kind < HAL_NFC_KIND_M1_1K) { resp(BLE_FILM_TRANS_CH_NFC_CARD_BEGIN, rsp, 1); return; }

    sess_free();
    m_sess.buf = (uint8_t *)heap_caps_malloc(total ? total : 1, MALLOC_CAP_SPIRAM);
    if(m_sess.buf == NULL) m_sess.buf = (uint8_t *)malloc(total ? total : 1);
    if(m_sess.buf == NULL) { resp(BLE_FILM_TRANS_CH_NFC_CARD_BEGIN, &(uint8_t){WST_NOMEM}, 1); return; }

    m_sess.active = true;
    m_sess.cover = false;
    m_sess.kind = kind;
    m_sess.content_mode = mode;
    m_sess.uid_len = uid_len;
    m_sess.sak = sak;
    m_sess.atqa[0] = d[4];
    m_sess.atqa[1] = d[5];
    memcpy(m_sess.uid, uid, uid_len);
    m_sess.name_len = name_len;
    if(name_len) memcpy(m_sess.name, &d[7 + uid_len], name_len);
    m_sess.name[name_len] = '\0';
    m_sess.total = total;
    m_sess.received = 0;
    m_sess.crc_expected = crc;
    m_sess.crc = crc32_begin();
    m_sess.last_tick = xTaskGetTickCount();

    rsp[0] = WST_OK;
    resp(BLE_FILM_TRANS_CH_NFC_CARD_BEGIN, rsp, 3);
}

static void ble_card_data(const uint8_t *d, uint8_t n)
{
    uint8_t rsp[3] = {WST_PARAM, 0, 0};
    if(!m_sess.active || m_sess.cover || n < 3) { resp(BLE_FILM_TRANS_CH_NFC_CARD_DATA, rsp, 1); return; }
    uint16_t off = get16be(d);
    uint16_t len = (uint16_t)(n - 2);
    if(off != m_sess.received || (uint32_t)off + len > m_sess.total || len > BLE_NFC_CHUNK_MAX)
    {
        rsp[0] = WST_PARAM;
        rsp[1] = (uint8_t)(m_sess.received >> 8);
        rsp[2] = (uint8_t)m_sess.received;
        resp(BLE_FILM_TRANS_CH_NFC_CARD_DATA, rsp, 3);
        return;
    }
    memcpy(m_sess.buf + off, &d[2], len);
    m_sess.crc = crc32_feed(m_sess.crc, &d[2], len);
    m_sess.received = (uint16_t)(off + len);
    m_sess.last_tick = xTaskGetTickCount();
    rsp[0] = WST_OK;
    rsp[1] = (uint8_t)(m_sess.received >> 8);
    rsp[2] = (uint8_t)m_sess.received;
    resp(BLE_FILM_TRANS_CH_NFC_CARD_DATA, rsp, 3);
}

/* NDEF Text 记录 + TLV 写入 NTAG 用户区 */
static int build_ndef_ntag(uint8_t kind, const uint8_t *uid, uint8_t uid_len,
                           const uint8_t *text, uint16_t text_len,
                           uint8_t **out, uint16_t *out_len)
{
    if(uid == NULL || uid_len != 7) return -1;
    uint16_t total = (uint16_t)(kind == HAL_NFC_KIND_NTAG213 ? 180 :
                                kind == HAL_NFC_KIND_NTAG215 ? 540 : 924);
    uint8_t *mem = (uint8_t *)heap_caps_malloc(total, MALLOC_CAP_SPIRAM);
    if(mem == NULL) mem = (uint8_t *)malloc(total);
    if(mem == NULL) return -1;
    memset(mem, 0, total);

    /* 页 0..3：UID/BCC/CC */
    mem[0] = uid[0]; mem[1] = uid[1]; mem[2] = uid[2];
    mem[3] = (uint8_t)(0x88 ^ uid[0] ^ uid[1] ^ uid[2] ^ uid[3]);
    mem[4] = uid[3]; mem[5] = uid[4]; mem[6] = uid[5]; mem[7] = uid[6];
    mem[8] = (uint8_t)(uid[3] ^ uid[4] ^ uid[5] ^ uid[6]);
    mem[12] = 0xE1; mem[13] = 0x10;
    mem[14] = kind == HAL_NFC_KIND_NTAG213 ? 0x12 : kind == HAL_NFC_KIND_NTAG215 ? 0x3E : 0x6D;
    mem[15] = 0x00;

    uint16_t payload = (uint16_t)(3 + text_len);
    uint16_t msg_cap = (uint16_t)(text_len + 16);
    uint8_t *msg = (uint8_t *)malloc(msg_cap);
    if(msg == NULL) { heap_caps_free(mem); return -1; }

    uint16_t msg_len;
    if(payload < 256)
    {
        msg[0] = 0xD1; msg[1] = 0x01; msg[2] = (uint8_t)payload; msg[3] = 0x54;
        msg_len = 4;
    }
    else
    {
        msg[0] = 0xC1; msg[1] = 0x01;
        msg[2] = (uint8_t)(payload >> 24); msg[3] = (uint8_t)(payload >> 16);
        msg[4] = (uint8_t)(payload >> 8); msg[5] = (uint8_t)payload;
        msg[6] = 0x54; msg_len = 7;
    }
    msg[msg_len++] = 0x02; msg[msg_len++] = 'e'; msg[msg_len++] = 'n';
    memcpy(&msg[msg_len], text, text_len);
    msg_len += text_len;

    uint16_t pos = 16;
    if(msg_len < 255)
    {
        if((uint32_t)pos + 2 + msg_len + 1 > total)
        {
            free(msg); heap_caps_free(mem); return -1;
        }
        mem[pos++] = 0x03; mem[pos++] = (uint8_t)msg_len;
    }
    else
    {
        if((uint32_t)pos + 4 + msg_len + 1 > total)
        {
            free(msg); heap_caps_free(mem); return -1;
        }
        mem[pos++] = 0x03; mem[pos++] = 0xFF;
        mem[pos++] = (uint8_t)(msg_len >> 8); mem[pos++] = (uint8_t)msg_len;
    }
    memcpy(&mem[pos], msg, msg_len);
    pos += msg_len;
    mem[pos++] = 0xFE;
    free(msg);

    *out = mem;
    *out_len = total;
    return 0;
}

static void ble_card_commit(void)
{
    uint8_t rsp[16];
    memset(rsp, 0, sizeof(rsp));
    rsp[0] = WST_PARAM;
    if(!m_sess.active || m_sess.cover) { resp(BLE_FILM_TRANS_CH_NFC_CARD_COMMIT, rsp, 1); return; }
    if(m_sess.received != m_sess.total || crc32_end(m_sess.crc) != m_sess.crc_expected)
    {
        resp(BLE_FILM_TRANS_CH_NFC_CARD_COMMIT, rsp, 1);
        return;
    }

    wallet_card_t card;
    memset(&card, 0, sizeof(card));
    card.kind = m_sess.kind;
    card.data_ver = WALLET_DATA_VER;
    card.name_len = m_sess.name_len;
    memcpy(card.name, m_sess.name, sizeof(card.name));
    card.atqa[0] = m_sess.atqa[0];
    card.atqa[1] = m_sess.atqa[1];
    card.sak = m_sess.sak;

    uint8_t *owned_mem = NULL;
    if(m_sess.kind >= HAL_NFC_KIND_NTAG213 && m_sess.kind <= HAL_NFC_KIND_NTAG216)
    {
        card.data_format = HAL_NFC_DATA_NTAG_PAGES;
        card.uid_len = 7;
        if(m_sess.content_mode == 1)
        {
            if(build_ndef_ntag(m_sess.kind, m_sess.uid, m_sess.uid_len,
                               m_sess.buf, m_sess.total, &owned_mem, &card.mem_len) != 0)
            {
                rsp[0] = WST_NOMEM;
                resp(BLE_FILM_TRANS_CH_NFC_CARD_COMMIT, rsp, 1);
                return;
            }
            card.mem = owned_mem;
        }
        else
        {
            if(m_sess.uid_len != 7 ||
               memcmp(m_sess.buf, m_sess.uid, 3) != 0 ||
               memcmp(&m_sess.buf[4], &m_sess.uid[3], 4) != 0 ||
               m_sess.buf[3] != (uint8_t)(0x88 ^ m_sess.uid[0] ^ m_sess.uid[1] ^ m_sess.uid[2] ^ m_sess.uid[3]) ||
               m_sess.buf[8] != (uint8_t)(m_sess.uid[3] ^ m_sess.uid[4] ^ m_sess.uid[5] ^ m_sess.uid[6]))
            {
                rsp[0] = WST_PARAM;
                resp(BLE_FILM_TRANS_CH_NFC_CARD_COMMIT, rsp, 1);
                return;
            }
            card.mem = m_sess.buf;
            card.mem_len = m_sess.total;
        }
        memcpy(card.uid, m_sess.uid, 7);
    }
    else
    {
        card.data_format = m_sess.kind == HAL_NFC_KIND_M1_UID ? HAL_NFC_DATA_NONE : HAL_NFC_DATA_M1_BLOCKS;
        card.uid_len = 4;
        card.mem = m_sess.content_mode == 2 ? m_sess.buf : NULL;
        card.mem_len = m_sess.content_mode == 2 ? m_sess.total : 0;
        memcpy(card.uid, m_sess.uid, 4);
    }

    if(!wallet_store_loaded()) (void)wallet_store_load();
    int idx = -1;
    int rc = wallet_store_upsert(&card, &idx);
    if(rc == 0 && idx >= 0)
    {
        wallet_store_select(idx);
        rc = wallet_store_save();
    }
    if(card.mem != m_sess.buf && card.mem != NULL) heap_caps_free(card.mem);
    sess_free();

    if(rc != 0)
    {
        rsp[0] = wallet_status_from_store(rc);
        resp(BLE_FILM_TRANS_CH_NFC_CARD_COMMIT, rsp, 1);
        return;
    }

    if(app_wallet_nfc_enabled())
    {
        wallet_restart_emu(idx);   // 卡内容已写入，必须重启模拟
    }
    else
    {
        wallet_stop_emu();         // 开关关闭：卡入库但不模拟
    }
    if(app_manager_get_current() == APP_ID_WALLET) wallet_post_snapshot();

    rsp[0] = WST_OK;
    rsp[1] = wallet_store_get(idx)->id;
    rsp[2] = (uint8_t)wallet_store_count();
    rsp[3] = (uint8_t)wallet_store_current();
    rsp[4] = wallet_store_get(idx)->uid_len;
    memcpy(&rsp[5], wallet_store_get(idx)->uid, rsp[4]);
    resp(BLE_FILM_TRANS_CH_NFC_CARD_COMMIT, rsp, (uint8_t)(5 + rsp[4]));
}

static void ble_cover_begin(const uint8_t *d, uint8_t n)
{
    uint8_t rsp[3] = {WST_PARAM, 0, BLE_NFC_CHUNK_MAX};
    if(n < 12) { resp(BLE_FILM_TRANS_CH_NFC_COVER_BEGIN, rsp, 1); return; }
    uint8_t id = d[0];
    uint8_t fmt = d[1];
    uint16_t w = get16be(&d[2]);
    uint16_t h = get16be(&d[4]);
    uint16_t total = get16be(&d[6]);
    uint32_t crc = get32be(&d[8]);
    int idx = -1;
    for(int i = 0; i < wallet_store_count(); i++)
    {
        if(wallet_store_get(i)->id == id) { idx = i; break; }
    }
    if(idx < 0) { resp(BLE_FILM_TRANS_CH_NFC_COVER_BEGIN, &(uint8_t){WST_NOTFOUND}, 1); return; }
    if(fmt != 1 || w != WALLET_COVER_W || h != WALLET_COVER_H || total != WALLET_COVER_PAYLOAD)
    {
        resp(BLE_FILM_TRANS_CH_NFC_COVER_BEGIN, rsp, 1); return;
    }

    sess_free();
    m_sess.buf = (uint8_t *)heap_caps_malloc(total, MALLOC_CAP_SPIRAM);
    if(m_sess.buf == NULL) m_sess.buf = (uint8_t *)malloc(total);
    if(m_sess.buf == NULL) { resp(BLE_FILM_TRANS_CH_NFC_COVER_BEGIN, &(uint8_t){WST_NOMEM}, 1); return; }
    m_sess.active = true;
    m_sess.cover = true;
    m_sess.card_id = id;
    m_sess.total = total;
    m_sess.received = 0;
    m_sess.crc_expected = crc;
    m_sess.crc = crc32_begin();
    m_sess.last_tick = xTaskGetTickCount();
    rsp[0] = WST_OK;
    resp(BLE_FILM_TRANS_CH_NFC_COVER_BEGIN, rsp, 3);
}

static void ble_cover_data(const uint8_t *d, uint8_t n)
{
    uint8_t rsp[3] = {WST_PARAM, 0, 0};
    if(!m_sess.active || !m_sess.cover || n < 3) { resp(BLE_FILM_TRANS_CH_NFC_COVER_DATA, rsp, 1); return; }
    uint16_t off = get16be(d);
    uint16_t len = (uint16_t)(n - 2);
    if(off != m_sess.received || (uint32_t)off + len > m_sess.total || len > BLE_NFC_CHUNK_MAX)
    {
        rsp[1] = (uint8_t)(m_sess.received >> 8);
        rsp[2] = (uint8_t)m_sess.received;
        resp(BLE_FILM_TRANS_CH_NFC_COVER_DATA, rsp, 3);
        return;
    }
    memcpy(m_sess.buf + off, &d[2], len);
    m_sess.crc = crc32_feed(m_sess.crc, &d[2], len);
    m_sess.received = (uint16_t)(off + len);
    m_sess.last_tick = xTaskGetTickCount();
    rsp[0] = WST_OK;
    rsp[1] = (uint8_t)(m_sess.received >> 8);
    rsp[2] = (uint8_t)m_sess.received;
    resp(BLE_FILM_TRANS_CH_NFC_COVER_DATA, rsp, 3);
}

static void ble_cover_commit(void)
{
    uint8_t rsp[3] = {WST_PARAM, 0, 0};
    if(!m_sess.active || !m_sess.cover || m_sess.received != m_sess.total ||
       crc32_end(m_sess.crc) != m_sess.crc_expected)
    {
        resp(BLE_FILM_TRANS_CH_NFC_COVER_COMMIT, rsp, 1);
        return;
    }
    int idx = -1;
    for(int i = 0; i < wallet_store_count(); i++)
    {
        if(wallet_store_get(i)->id == m_sess.card_id) { idx = i; break; }
    }
    if(idx < 0) { resp(BLE_FILM_TRANS_CH_NFC_COVER_COMMIT, &(uint8_t){WST_NOTFOUND}, 1); sess_free(); return; }

    int rc = wallet_store_cover_save(idx, m_sess.buf, m_sess.crc_expected);
    if(rc == 0) rc = wallet_store_save();
    uint8_t id = m_sess.card_id;
    sess_free();
    if(rc != 0) { resp(BLE_FILM_TRANS_CH_NFC_COVER_COMMIT, &(uint8_t){WST_SAVE}, 1); return; }
    if(app_manager_get_current() == APP_ID_WALLET) wallet_post_snapshot();
    rsp[0] = WST_OK; rsp[1] = id; rsp[2] = 1;
    resp(BLE_FILM_TRANS_CH_NFC_COVER_COMMIT, rsp, 3);
}

static void ble_wallet_ctrl(const uint8_t *d, uint8_t n)
{
    if(n < 1) { resp(BLE_FILM_TRANS_CH_NFC_WALLET_CTRL, &(uint8_t){WST_PARAM}, 1); return; }
    uint8_t op = d[0];

    if(op == 0x00) /* LIST */
    {
        uint8_t start = n >= 2 ? d[1] : 0;
        uint8_t out[192];
        memset(out, 0, sizeof(out));
        out[0] = WST_OK;
        out[1] = (uint8_t)wallet_store_count();
        out[2] = (uint8_t)wallet_store_current();
        out[3] = start;
        uint8_t pos = 5;
        uint8_t cnt = 0;
        for(int i = start; i < wallet_store_count(); i++)
        {
            wallet_card_t *c = wallet_store_get(i);
            uint8_t name_len = c->name_len > 23 ? 23 : c->name_len;
            uint8_t need = (uint8_t)(15 + name_len);
            if(pos + need > sizeof(out)) break;
            out[pos++] = c->id;
            out[pos++] = c->kind;
            out[pos++] = c->uid_len;
            out[pos++] = c->sak;
            out[pos++] = c->atqa[0];
            out[pos++] = c->atqa[1];
            memcpy(&out[pos], c->uid, 7); pos += 7;
            out[pos++] = c->flags;
            out[pos++] = name_len;
            memcpy(&out[pos], c->name, name_len); pos += name_len;
            cnt++;
        }
        out[4] = cnt;
        resp(BLE_FILM_TRANS_CH_NFC_WALLET_CTRL, out, pos);
        return;
    }

    if(op == 0x04) /* STATS */
    {
        uint8_t out[4] = {WST_OK, (uint8_t)wallet_store_count(),
                          (uint8_t)wallet_store_current(), WALLET_MAX_CARDS};
        resp(BLE_FILM_TRANS_CH_NFC_WALLET_CTRL, out, sizeof(out));
        return;
    }

    if(n < 2 && op != 0x04) { resp(BLE_FILM_TRANS_CH_NFC_WALLET_CTRL, &(uint8_t){WST_PARAM}, 1); return; }
    uint8_t id = d[1];
    int idx = -1;
    for(int i = 0; i < wallet_store_count(); i++)
        if(wallet_store_get(i)->id == id) { idx = i; break; }

    if(op == 0x01) /* SELECT */
    {
        if(idx < 0) { resp(BLE_FILM_TRANS_CH_NFC_WALLET_CTRL, &(uint8_t){WST_NOTFOUND}, 1); return; }
        wallet_store_select(idx);
        (void)wallet_store_save();
        app_wallet_emu_sync();   // 常驻模拟：不在卡包时也立即切到新选中的卡
        if(app_manager_get_current() == APP_ID_WALLET) wallet_post_snapshot();
        uint8_t out[2] = {WST_OK, (uint8_t)wallet_store_current()};
        resp(BLE_FILM_TRANS_CH_NFC_WALLET_CTRL, out, 2);
        return;
    }
    if(op == 0x02) /* DELETE */
    {
        if(idx < 0) { resp(BLE_FILM_TRANS_CH_NFC_WALLET_CTRL, &(uint8_t){WST_NOTFOUND}, 1); return; }
        wallet_card_t *c = wallet_store_get(idx);
        uint8_t del_id = c->id;
        wallet_store_delete(idx);
        char path[64];
        snprintf(path, sizeof(path), WALLET_DIR "/cover_%u.bin", (unsigned)del_id);
        (void)remove(path);
        (void)wallet_store_save();
        app_wallet_emu_sync();   // 删到无卡即停模拟；仍有卡则模拟新的当前卡
        if(app_manager_get_current() == APP_ID_WALLET) wallet_post_snapshot();
        uint8_t out[3] = {WST_OK, (uint8_t)wallet_store_count(), (uint8_t)wallet_store_current()};
        resp(BLE_FILM_TRANS_CH_NFC_WALLET_CTRL, out, 3);
        return;
    }
    if(op == 0x03) /* GET */
    {
        if(idx < 0) { resp(BLE_FILM_TRANS_CH_NFC_WALLET_CTRL, &(uint8_t){WST_NOTFOUND}, 1); return; }
        wallet_card_t *c = wallet_store_get(idx);
        uint8_t out[192];
        memset(out, 0, sizeof(out));
        uint8_t pos = 0;
        out[pos++] = WST_OK;
        out[pos++] = c->id;
        out[pos++] = c->kind;
        out[pos++] = c->uid_len;
        out[pos++] = c->sak;
        out[pos++] = c->atqa[0];
        out[pos++] = c->atqa[1];
        memcpy(&out[pos], c->uid, 7); pos += 7;
        out[pos++] = c->flags;
        out[pos++] = c->name_len;
        memcpy(&out[pos], c->name, c->name_len); pos += c->name_len;
        out[pos++] = (uint8_t)(c->mem_len >> 8);
        out[pos++] = (uint8_t)c->mem_len;
        out[pos++] = (uint8_t)(c->cover_len >> 8);
        out[pos++] = (uint8_t)c->cover_len;
        resp(BLE_FILM_TRANS_CH_NFC_WALLET_CTRL, out, pos);
        return;
    }
    if(op == 0x05) /* RENAME */
    {
        if(idx < 0 || n < 4) { resp(BLE_FILM_TRANS_CH_NFC_WALLET_CTRL, &(uint8_t){WST_NOTFOUND}, 1); return; }
        uint8_t len = d[2];
        if(len == 0 || len > 23 || (uint8_t)(3 + len) > n)
        {
            resp(BLE_FILM_TRANS_CH_NFC_WALLET_CTRL, &(uint8_t){WST_PARAM}, 1); return;
        }
        if(wallet_store_rename(idx, (const char *)&d[3], len) != 0)
        {
            resp(BLE_FILM_TRANS_CH_NFC_WALLET_CTRL, &(uint8_t){WST_PARAM}, 1); return;
        }
        (void)wallet_store_save();
        if(app_manager_get_current() == APP_ID_WALLET) wallet_post_snapshot();
        uint8_t out[32];
        out[0] = WST_OK; out[1] = id; out[2] = len;
        memcpy(&out[3], &d[3], len);
        resp(BLE_FILM_TRANS_CH_NFC_WALLET_CTRL, out, (uint8_t)(3 + len));
        return;
    }
    if(op == 0x06) /* CLEAR_COVER */
    {
        if(idx < 0) { resp(BLE_FILM_TRANS_CH_NFC_WALLET_CTRL, &(uint8_t){WST_NOTFOUND}, 1); return; }
        (void)wallet_store_cover_delete(idx);
        (void)wallet_store_save();
        if(app_manager_get_current() == APP_ID_WALLET) wallet_post_snapshot();
        uint8_t out[2] = {WST_OK, id};
        resp(BLE_FILM_TRANS_CH_NFC_WALLET_CTRL, out, 2);
        return;
    }

    resp(BLE_FILM_TRANS_CH_NFC_WALLET_CTRL, &(uint8_t){WST_PARAM}, 1);
}

void app_wallet_on_ble_cmd(uint8_t ch, const uint8_t *data, uint8_t len)
{
    sess_expire();
    switch(ch)
    {
    case BLE_FILM_TRANS_CH_NFC_CARD_BEGIN:  ble_card_begin(data, len); break;
    case BLE_FILM_TRANS_CH_NFC_CARD_DATA:   ble_card_data(data, len); break;
    case BLE_FILM_TRANS_CH_NFC_CARD_COMMIT: ble_card_commit(); break;
    case BLE_FILM_TRANS_CH_NFC_WALLET_CTRL: ble_wallet_ctrl(data, len); break;
    case BLE_FILM_TRANS_CH_NFC_COVER_BEGIN: ble_cover_begin(data, len); break;
    case BLE_FILM_TRANS_CH_NFC_COVER_DATA:  ble_cover_data(data, len); break;
    case BLE_FILM_TRANS_CH_NFC_COVER_COMMIT: ble_cover_commit(); break;
    default: break;
    }
}

#endif /* SYS_NFC_ENABLE */
