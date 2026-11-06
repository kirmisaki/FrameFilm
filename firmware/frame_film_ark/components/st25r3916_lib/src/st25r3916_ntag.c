/*
 * SPDX-License-Identifier: MIT
 *
 * NTAG21x emulation (NFC-A, 106 kbit/s): GET_VERSION, READ, FAST_READ,
 * WRITE, COMPAT_WRITE, READ_CNT, READ_SIG and NDEF helpers.
 *
 * The tag image is plain memory: either an application supplied buffer or the
 * handle's own storage. Readers can write pages; the application is notified
 * through st25r_ntag_cb_t, so the image can be persisted outside the library.
 */
#include "st25r3916_priv.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_rom_sys.h"

static const char *TAG = "st25r3916/ntag";

/* Command bytes (as they appear in the FIFO, parity bit already stripped) */
#define NTAG_CMD_GET_VERSION 0x60
#define NTAG_CMD_READ        0x30
#define NTAG_CMD_FAST_READ   0x3A
#define NTAG_CMD_WRITE       0xA2
#define NTAG_CMD_COMP_WRITE  0x3E
#define NTAG_CMD_READ_CNT    0x39
#define NTAG_CMD_READ_SIG    0x3C
#define NTAG_CMD_PWD_AUTH    0x1B

typedef struct {
    uint8_t pages;
    uint8_t last_user;
    uint8_t cc_size;   /* capability container byte 2 */
    uint8_t version_sz; /* GET_VERSION storage size byte */
} ntag_info_t;

static const ntag_info_t k_ntag_info[] = {
    {45, 39, 0x12, 0x0F},    /* NTAG213: 144 B user */
    {135, 129, 0x3E, 0x11},  /* NTAG215: 504 B user */
    {231, 225, 0x6D, 0x13},  /* NTAG216: 888 B user */
};

static const char *const k_ntag_name[] = {"NTAG213", "NTAG215", "NTAG216"};

/* ------------------------------------------------------------------------- */
/* Helpers                                                                   */
/* ------------------------------------------------------------------------- */
static const ntag_info_t *ntag_info(st25r_handle_t h)
{
    return &k_ntag_info[h->ntag.type];
}

static uint8_t bit_count8(uint8_t v)
{
    uint8_t c = 0;
    for (; v; v >>= 1) {
        c = (uint8_t)(c + (v & 1u));
    }
    return c;
}

/* Frame with @p bits data bits plus one odd parity bit (ACK/NAK). */
static bool ntag_tx_bits(st25r_handle_t h, uint8_t value, uint8_t bits)
{
    const uint8_t mask = (uint8_t)((1u << bits) - 1u);
    const uint8_t payload = (uint8_t)(value & mask);
    const uint8_t parity = (uint8_t)((bit_count8(payload) & 1u) ? 0u : 1u);
    const uint8_t frame = (uint8_t)(payload | (uint8_t)(parity << bits));

    st25r_reg_write8(h, ST25R_REG_ISO14443A, ST25R_ISO_NO_TX_PAR);
    const bool ok = st25r_cmd(h, ST25R_CMD_CLEAR_FIFO) && st25r_fifo_write(h, &frame, 1) &&
                    st25r_tx_bytes(h, 0, (uint8_t)(bits + 1u)) &&
                    st25r_cmd(h, ST25R_CMD_TRANSMIT_WITHOUT_CRC);
    st25r_reg_write8(h, ST25R_REG_ISO14443A, 0x00);
    /* A transmit that is not byte aligned leaves the FIFO bit pointer in the
     * middle of a byte; clear it before the next reception. */
    st25r_cmd(h, ST25R_CMD_CLEAR_FIFO);
    return ok;
}

static bool ntag_tx_ack(st25r_handle_t h)
{
    return ntag_tx_bits(h, 0x0A, 4);
}

static bool ntag_tx_nak(st25r_handle_t h)
{
    return ntag_tx_bits(h, 0x00, 4);
}

/* Byte aligned frame, CRC appended by the chip. */
static bool ntag_tx_data(st25r_handle_t h, const uint8_t *data, uint16_t len)
{
    st25r_reg_write8(h, ST25R_REG_ISO14443A, 0x00);
    return st25r_cmd(h, ST25R_CMD_CLEAR_FIFO) && st25r_fifo_write(h, data, len) &&
           st25r_tx_bytes(h, len, 0) && st25r_cmd(h, ST25R_CMD_TRANSMIT_WITH_CRC);
}

static void ntag_notify(st25r_handle_t h, st25r_ntag_event_t evt, uint8_t page, uint16_t len)
{
    if (h->ntag.cb) {
        h->ntag.cb(h, evt, page, len, h->ntag.ctx);
    }
}

/* Rebuild UID pages, capability container and an empty user area. */
static void ntag_format(st25r_handle_t h, const uint8_t uid[7])
{
    const ntag_info_t *info = ntag_info(h);
    uint8_t *m = h->ntag.mem;

    memset(m, 0, h->ntag.size);
    m[0] = uid[0];
    m[1] = uid[1];
    m[2] = uid[2];
    m[3] = (uint8_t)(0x88 ^ uid[0] ^ uid[1] ^ uid[2]); /* BCC0 */
    m[4] = uid[3];
    m[5] = uid[4];
    m[6] = uid[5];
    m[7] = uid[6];
    m[8] = (uint8_t)(uid[3] ^ uid[4] ^ uid[5] ^ uid[6]); /* BCC1 */
    m[9] = 0x48;                                         /* internal */
    /* m[10], m[11]: static lock bytes */
    m[12] = 0xE1; /* capability container */
    m[13] = 0x10;
    m[14] = info->cc_size;
    m[15] = 0x00;

    /* Last config page: AUTH0 = 0xFF (no password protection). */
    m[(uint16_t)(info->pages - 1) * 4 + 0] = 0xFF;
}

/* ------------------------------------------------------------------------- */
/* Command dispatch (runs in the ACTIVE state, lock held)                    */
/* ------------------------------------------------------------------------- */
static bool ntag_handle(st25r_handle_t h, const uint8_t *d, uint16_t len)
{
    if (!h->ntag.active || !h->ntag.mem || len < 1) {
        return false;
    }
    const ntag_info_t *info = ntag_info(h);
    uint8_t *m = h->ntag.mem;

    switch (d[0]) {
        case NTAG_CMD_GET_VERSION: {
            const uint8_t version[8] = {0x00, 0x04, 0x04, 0x02, 0x01, 0x00, info->version_sz, 0x03};
            const bool ok = ntag_tx_data(h, version, sizeof(version));
            ESP_LOGD(TAG, "GET_VERSION -> %02X %02X %02X ok=%d", version[5], version[6], version[7], (int)ok);
            return ok;
        }
        case NTAG_CMD_READ: {
            if (len < 2 || d[1] > info->pages - 1) {
                return ntag_tx_nak(h);
            }
            const uint8_t page = d[1];
            uint8_t buf[16];
            for (uint16_t i = 0; i < sizeof(buf); ++i) {
                const uint16_t off = (uint16_t)(page * 4u + i);
                buf[i] = (off < h->ntag.size) ? m[off] : 0x00;
            }
            /* M5 参考实现直接回 16 字节，不带软件 ACK / 额外延时：
               芯片的被动目标逻辑会处理 ACK 与 FDT，软件再插 ACK 反而会让读数失败。 */
            const bool ok = ntag_tx_data(h, buf, sizeof(buf));
            ESP_LOGD(TAG, "READ p=%u -> %02X %02X %02X %02X ok=%d", page,
                     buf[0], buf[1], buf[2], buf[3], (int)ok);
            return ok;
        }
        case NTAG_CMD_FAST_READ: {
            if (len < 3 || d[1] > d[2] || d[2] > info->pages - 1) {
                return ntag_tx_nak(h);
            }
            const uint16_t off = (uint16_t)(d[1] * 4);
            const uint16_t count = (uint16_t)(((uint16_t)d[2] - d[1] + 1u) * 4u);
            /* The chip FIFO holds 512 bytes; a longer range cannot be sent in
             * one frame, so refuse it instead of overflowing the FIFO. */
            if (count > 500u) {
                return ntag_tx_nak(h);
            }
            const bool ok = ntag_tx_data(h, m + off, count);
            ESP_LOGD(TAG, "FAST_READ %u..%u (%u B) ok=%d", d[1], d[2], count, (int)ok);
            return ok;
        }
        case NTAG_CMD_WRITE: {
            if (len < 6 || d[1] < 4 || d[1] > info->last_user) {
                return ntag_tx_nak(h);
            }
            if (!h->ntag.writable) {
                return ntag_tx_nak(h);
            }
            const uint8_t page = d[1];
            memcpy(m + (uint16_t)page * 4, d + 2, 4);
            if (!ntag_tx_ack(h)) {
                return false;
            }
            ntag_notify(h, ST25R_NTAG_EVT_PAGE_WRITTEN, page, 4);
            return true;
        }
        case NTAG_CMD_COMP_WRITE: {
            if (len < 3 || d[1] < 4 || d[1] > info->last_user || !h->ntag.writable) {
                return ntag_tx_nak(h);
            }
            const uint8_t page = d[1];
            m[(uint16_t)page * 4] = d[2];
            if (!ntag_tx_ack(h)) {
                return false;
            }
            ntag_notify(h, ST25R_NTAG_EVT_PAGE_WRITTEN, page, 1);
            return true;
        }
        case NTAG_CMD_READ_CNT: {
            if (len < 2 || d[1] > 2) {
                return ntag_tx_nak(h);
            }
            const uint8_t counter[3] = {0x00, 0x00, 0x00};
            return ntag_tx_data(h, counter, sizeof(counter));
        }
        case NTAG_CMD_READ_SIG: {
            uint8_t sig[32] = {0};
            return ntag_tx_data(h, sig, sizeof(sig));
        }
        case NTAG_CMD_PWD_AUTH: {
            /* AUTH0 = 0xFF in the image, so password authentication is never
             * required; answer with a NAK if a reader asks anyway. */
            return ntag_tx_nak(h);
        }
        default:
            ESP_LOGD(TAG, "unsupported command 0x%02X", d[0]);
            return ntag_tx_nak(h);
    }
}

static bool ntag_on_picc_event(st25r_handle_t h, st25r_picc_event_t evt, const uint8_t *data, uint16_t len,
                               void *ctx)
{
    (void)ctx;
    if (evt != ST25R_PICC_EVT_FRAME) {
        return false;
    }
    return ntag_handle(h, data, len);
}

/* ------------------------------------------------------------------------- */
/* Public API                                                                */
/* ------------------------------------------------------------------------- */
esp_err_t st25r_ntag_begin(st25r_handle_t h, const st25r_ntag_config_t *cfg)
{
    if (!h || !h->ready || !cfg) {
        return ESP_ERR_INVALID_ARG;
    }
    if (cfg->type < ST25R_NTAG_213 || cfg->type > ST25R_NTAG_216) {
        return ESP_ERR_INVALID_ARG;
    }
    if (h->ntag.active || h->emu.running || h->reader_active) {
        return ESP_ERR_INVALID_STATE;
    }

    const ntag_info_t *info = &k_ntag_info[cfg->type];
    const uint16_t size = (uint16_t)(info->pages * 4);

    h->ntag.type = cfg->type;
    h->ntag.pages = info->pages;
    h->ntag.last_user = info->last_user;
    h->ntag.size = size;
    h->ntag.writable = cfg->writable;
    h->ntag.cb = cfg->callback;
    h->ntag.ctx = cfg->user_ctx;
    h->ntag.mem = cfg->memory ? cfg->memory : h->ntag.storage;

    ntag_format(h, cfg->uid);

    st25r_picc_config_t picc = {
        .card =
            {
                .tech = ST25R_TECH_NFC_A,
                .atqa = 0x0044, /* NTAG21x */
                .sak = 0x00,
                .uid_len = 7,
            },
        .callback = ntag_on_picc_event,
        .user_ctx = NULL,
        .task_stack_size = cfg->task_stack_size,
        .task_priority = cfg->task_priority,
        .task_core = cfg->task_core,
    };
    memcpy(picc.card.uid, cfg->uid, sizeof(picc.card.uid));

    h->ntag.active = true;
    const esp_err_t err = st25r_picc_begin(h, &picc);
    if (err != ESP_OK) {
        h->ntag.active = false;
        h->ntag.mem = NULL;
        return err;
    }
    ESP_LOGI(TAG, "%s emulation started (UID %02X%02X%02X%02X%02X%02X%02X)", k_ntag_name[cfg->type],
             cfg->uid[0], cfg->uid[1], cfg->uid[2], cfg->uid[3], cfg->uid[4], cfg->uid[5], cfg->uid[6]);
    return ESP_OK;
}

esp_err_t st25r_ntag_end(st25r_handle_t h)
{
    if (!h) {
        return ESP_ERR_INVALID_ARG;
    }
    const esp_err_t err = st25r_picc_end(h);
    h->ntag.active = false;
    return err;
}

bool st25r_ntag_is_running(st25r_handle_t h)
{
    return h && h->ntag.active && h->emu.running;
}

esp_err_t st25r_ntag_set_ndef(st25r_handle_t h, const uint8_t *message, uint16_t len)
{
    if (!h || !h->ntag.active || (!message && len)) {
        return ESP_ERR_INVALID_ARG;
    }

    st25r_lock(h);
    const uint16_t user_bytes = (uint16_t)((h->ntag.last_user - 4u + 1u) * 4u);
    const uint16_t header = (len < 0xFFu) ? 2u : 4u;
    if ((uint32_t)len + header + 1u > user_bytes) {
        st25r_unlock(h);
        return ESP_ERR_INVALID_SIZE;
    }

    uint8_t *m = h->ntag.mem + 16; /* user area starts at page 4 */
    uint16_t pos = 0;
    m[pos++] = 0x03; /* NDEF TLV */
    if (len < 0xFFu) {
        m[pos++] = (uint8_t)len;
    } else {
        m[pos++] = 0xFF;
        m[pos++] = (uint8_t)(len >> 8);
        m[pos++] = (uint8_t)(len & 0xFF);
    }
    if (len) {
        memcpy(m + pos, message, len);
        pos = (uint16_t)(pos + len);
    }
    m[pos++] = 0xFE; /* terminator TLV */
    memset(m + pos, 0, (size_t)(user_bytes - pos));
    st25r_unlock(h);

    ntag_notify(h, ST25R_NTAG_EVT_NDEF_CHANGED, 0, len);
    return ESP_OK;
}

esp_err_t st25r_ntag_set_ndef_text(st25r_handle_t h, const char *text)
{
    if (!h || !text) {
        return ESP_ERR_INVALID_ARG;
    }
    const size_t tlen = strlen(text);
    if (tlen > 200) {
        return ESP_ERR_INVALID_SIZE;
    }

    uint8_t msg[7 + 200];
    msg[0] = 0xD1; /* MB | ME | SR | TNF = well known */
    msg[1] = 0x01; /* type length */
    msg[2] = (uint8_t)(3 + tlen);
    msg[3] = 'T';
    msg[4] = 0x02; /* UTF-8, language length 2 */
    msg[5] = 'e';
    msg[6] = 'n';
    memcpy(msg + 7, text, tlen);
    return st25r_ntag_set_ndef(h, msg, (uint16_t)(7 + tlen));
}

esp_err_t st25r_ntag_read_memory(st25r_handle_t h, uint8_t page, uint8_t *data, uint8_t pages)
{
    if (!h || !h->ntag.active || !data || !pages) {
        return ESP_ERR_INVALID_ARG;
    }
    if ((uint16_t)(page + pages) > h->ntag.pages) {
        return ESP_ERR_INVALID_ARG;
    }
    st25r_lock(h);
    memcpy(data, h->ntag.mem + (uint16_t)page * 4, (size_t)pages * 4);
    st25r_unlock(h);
    return ESP_OK;
}

esp_err_t st25r_ntag_write_memory(st25r_handle_t h, uint8_t page, const uint8_t *data, uint8_t pages)
{
    if (!h || !h->ntag.active || !data || !pages) {
        return ESP_ERR_INVALID_ARG;
    }
    if (page < 4 || (uint16_t)(page + pages - 1) > h->ntag.last_user) {
        return ESP_ERR_INVALID_ARG;
    }
    st25r_lock(h);
    memcpy(h->ntag.mem + (uint16_t)page * 4, data, (size_t)pages * 4);
    st25r_unlock(h);
    return ESP_OK;
}
