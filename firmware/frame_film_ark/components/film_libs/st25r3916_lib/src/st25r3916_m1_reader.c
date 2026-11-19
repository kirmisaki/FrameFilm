/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * MIFARE Classic reader side: Crypto1 authentication and encrypted block reads.
 *
 * Mirrors the requirements of the listener module: the first AUTH and the Nt
 * reply use the chip's byte framing and parity, while nr/ar, At and every
 * later frame carry encrypted parity, so they are moved bit-for-bit over FIFO
 * with NO_TX_PAR / NO_RX_PAR (the same trick used by the Bruce firmware).
 *
 * Post-authentication the LFSR is clocked with 0 on both sides (nr is the last
 * value fed in the clear), matching the MIFARE Crypto1 usage in Proxmark3.
 */
#include "st25r3916_m1.h"

#include <string.h>

#include "esp_log.h"
#include "esp_random.h"
#include "st25r3916_m1_priv.h"
#include "st25r3916_priv.h"

static const char *TAG = "st25r3916/m1rd";

#define M1_RD_TIMEOUT_MS 20
#define M1_RD_RESELECT_TRIES 20

typedef struct {
    bool authed;
    st25r_crypto1_state_t cipher;
} m1_reader_ctx_t;

static m1_reader_ctx_t s_rd;

/* ------------------------------------------------------------------------- */
/* Sector geometry                                                           */
/* ------------------------------------------------------------------------- */
uint8_t st25r_m1_sectors_for_blocks(uint16_t block_count)
{
    switch (block_count) {
        case 64:
            return 16; /* MIFARE Classic 1K: 16 sectors x 4 blocks */
        case 128:
            return 32; /* MIFARE Classic 2K: 32 sectors x 4 blocks */
        case 256:
            return 40; /* MIFARE Classic 4K: 32 x 4 + 8 x 16 */
        default:
            return 0;
    }
}

uint8_t st25r_m1_sector_first_block(uint8_t sector)
{
    if (sector < 32) {
        return (uint8_t)(sector * 4);
    }
    return (uint8_t)(128 + (sector - 32) * 16);
}

uint8_t st25r_m1_sector_block_count(uint8_t sector)
{
    return (sector < 32) ? 4 : 16;
}

static uint32_t rd_uid32(const uint8_t *uid, uint8_t uid_len)
{
    const uint8_t off = (uid_len > 4) ? (uint8_t)(uid_len - 4) : 0;
    return ((uint32_t)uid[off] << 24) | ((uint32_t)uid[off + 1] << 16) | ((uint32_t)uid[off + 2] << 8) |
           (uint32_t)uid[off + 3];
}

static uint32_t rd_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static bool rd_write_fwt(st25r_handle_t h, uint32_t ms)
{
    uint8_t timer = 0;
    if (!st25r_reg_read8(h, ST25R_REG_TIMER_EMV, &timer)) {
        return false;
    }
    return st25r_reg_write16(h, ST25R_REG_NO_RESP_TIMER1,
                             st25r_calc_nrt(ms, (timer & ST25R_TIMER_NRT_STEP) != 0));
}

static bool rd_wait_rx(st25r_handle_t h, uint32_t timeout_ms, uint32_t *irq)
{
    bool timed_out = false;
    uint32_t v = st25r_wait_irq_ex(h, ST25R_IRQ_RXE | ST25R_IRQ_RXS | ST25R_IRQ_PAR | ST25R_IRQ_CRC |
                                          ST25R_IRQ_ERR1 | ST25R_IRQ_ERR2,
                                   timeout_ms, &timed_out);
    if (!(v & ST25R_IRQ_RXE) && !timed_out) {
        v |= st25r_wait_irq_ex(h, ST25R_IRQ_RXE | ST25R_IRQ_PAR | ST25R_IRQ_CRC | ST25R_IRQ_ERR1 |
                                      ST25R_IRQ_ERR2,
                               timeout_ms, &timed_out);
    }
    *irq = v;
    return (v & ST25R_IRQ_RXE) != 0;
}

/* Re-select the card between sectors. A single missed WUPA/anticollision used
 * to abort the whole dump, so retry a few times before giving up. */
static bool rd_reselect(st25r_handle_t h, st25r_card_t *card)
{
    for (uint8_t i = 0; i < M1_RD_RESELECT_TRIES; i++) {
        if (st25r_reader_poll(h, card, 100) == ESP_OK) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return false;
}

/* Plain AUTH command: HW CRC + HW parity; the Nt reply has no CRC. */
static esp_err_t rd_auth_frame(st25r_handle_t h, const uint8_t cmd[2], uint8_t *rx, uint16_t *rx_len)
{
    if (!rd_write_fwt(h, M1_RD_TIMEOUT_MS) ||
        !st25r_reg_write8(h, ST25R_REG_ISO14443A, 0x00) ||
        !st25r_reg_set(h, ST25R_REG_AUX_DEF, ST25R_AUX_NO_CRC_RX) || !st25r_irq_clear(h) ||
        !st25r_cmd(h, ST25R_CMD_CLEAR_FIFO) || !st25r_fifo_write(h, cmd, 2) ||
        !st25r_tx_bytes(h, 2, 0) || !st25r_cmd(h, ST25R_CMD_TRANSMIT_WITH_CRC)) {
        return ESP_FAIL;
    }

    uint32_t irq = 0;
    if (!rd_wait_rx(h, M1_RD_TIMEOUT_MS, &irq)) {
        return ESP_ERR_TIMEOUT;
    }
    if (irq & (ST25R_IRQ_PAR | ST25R_IRQ_ERR1 | ST25R_IRQ_ERR2)) {
        st25r_cmd(h, ST25R_CMD_CLEAR_FIFO);
        return ESP_ERR_INVALID_RESPONSE;
    }

    uint16_t bytes = 0;
    uint8_t bits = 0;
    if (!st25r_fifo_size(h, &bytes, &bits) || bytes == 0) {
        return ESP_ERR_TIMEOUT;
    }
    if (bytes > *rx_len) {
        bytes = *rx_len;
    }
    if (!st25r_fifo_read(h, rx, bytes)) {
        return ESP_FAIL;
    }
    *rx_len = bytes;
    return ESP_OK;
}

/* Encrypted frame: software parity, keep RX parity bits, ignore CRC/PAR flags. */
static esp_err_t rd_raw(st25r_handle_t h, const uint8_t *bits, uint16_t nbits, uint8_t *rx, uint16_t rx_cap,
                        uint16_t *rx_bits, uint32_t timeout_ms)
{
    if (!rd_write_fwt(h, timeout_ms) ||
        !st25r_reg_set(h, ST25R_REG_ISO14443A, ST25R_ISO_NO_TX_PAR | ST25R_ISO_NO_RX_PAR) ||
        !st25r_reg_set(h, ST25R_REG_AUX_DEF, ST25R_AUX_NO_CRC_RX) || !st25r_irq_clear(h) ||
        !st25r_cmd(h, ST25R_CMD_CLEAR_FIFO) ||
        !st25r_fifo_write(h, bits, (uint16_t)((nbits + 7u) / 8u)) ||
        !st25r_tx_bytes(h, (uint16_t)(nbits / 8u), (uint8_t)(nbits % 8u)) ||
        !st25r_cmd(h, ST25R_CMD_TRANSMIT_WITHOUT_CRC)) {
        return ESP_FAIL;
    }

    uint32_t irq = 0;
    if (!rd_wait_rx(h, timeout_ms, &irq)) {
        return ESP_ERR_TIMEOUT;
    }

    uint16_t bytes = 0;
    uint8_t lb = 0;
    if (!st25r_fifo_size(h, &bytes, &lb) || bytes == 0) {
        return ESP_ERR_TIMEOUT;
    }
    if (bytes > rx_cap) {
        bytes = rx_cap;
    }
    if (!st25r_fifo_read(h, rx, bytes)) {
        return ESP_FAIL;
    }
    *rx_bits = (uint16_t)((bytes - 1) * 8u + (lb ? lb : 8u));
    return ESP_OK;
}

/* Encrypted HALT: no reply, so only wait for TXE. */
static void rd_tx_only(st25r_handle_t h, const uint8_t *bits, uint16_t nbits)
{
    if (!rd_write_fwt(h, 5) ||
        !st25r_reg_set(h, ST25R_REG_ISO14443A, ST25R_ISO_NO_TX_PAR | ST25R_ISO_NO_RX_PAR) ||
        !st25r_reg_set(h, ST25R_REG_AUX_DEF, ST25R_AUX_NO_CRC_RX) || !st25r_irq_clear(h) ||
        !st25r_cmd(h, ST25R_CMD_CLEAR_FIFO) ||
        !st25r_fifo_write(h, bits, (uint16_t)((nbits + 7u) / 8u)) ||
        !st25r_tx_bytes(h, (uint16_t)(nbits / 8u), (uint8_t)(nbits % 8u)) ||
        !st25r_cmd(h, ST25R_CMD_TRANSMIT_WITHOUT_CRC)) {
        return;
    }
    (void)st25r_wait_irq(h, ST25R_IRQ_TXE, 5);
}

esp_err_t st25r_m1_reader_auth(st25r_handle_t h, const st25r_card_t *card, uint8_t block,
                               const uint8_t key[6], bool key_b)
{
    if (!h || !card || !key) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!h->ready || !h->reader_active) {
        return ESP_ERR_INVALID_STATE;
    }
    if (card->uid_len != 4 && card->uid_len != 7) {
        return ESP_ERR_INVALID_ARG;
    }

    s_rd.authed = false;
    const uint8_t cmd[2] = {(uint8_t)(key_b ? 0x61 : 0x60), block};
    uint8_t rx[8] = {0};
    uint16_t rx_len = sizeof(rx);

    st25r_lock(h);
    esp_err_t err = rd_auth_frame(h, cmd, rx, &rx_len);
    st25r_unlock(h);
    if (err != ESP_OK) {
        return err;
    }
    if (rx_len < 4) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    const uint32_t nt = rd_be32(rx);
    uint64_t k = 0;
    for (uint8_t i = 0; i < 6; i++) {
        k = (k << 8) | key[i];
    }
    st25r_crypto1_init(&s_rd.cipher, k);
    (void)st25r_crypto1_word(&s_rd.cipher, rd_uid32(card->uid, card->uid_len) ^ nt, 0);

    const uint32_t nr = esp_random();
    const uint32_t ar = st25r_prng_successor(nt, 64);
    uint8_t plain[8];
    uint8_t enc[8];
    uint8_t par[8];
    for (uint8_t i = 0; i < 4; i++) {
        plain[i] = (uint8_t)(nr >> (24 - 8 * i));
        plain[4 + i] = (uint8_t)(ar >> (24 - 8 * i));
    }
    for (uint8_t i = 0; i < 4; i++) { /* nr is the last value fed into the LFSR */
        enc[i] = (uint8_t)(st25r_crypto1_byte(&s_rd.cipher, plain[i], 0) ^ plain[i]);
        par[i] = (uint8_t)(st25r_crypto1_filter_bit(&s_rd.cipher) ^ st25r_oddparity(plain[i]));
    }
    for (uint8_t i = 0; i < 4; i++) { /* from ar on: clocked with 0 */
        enc[4 + i] = (uint8_t)(st25r_crypto1_byte(&s_rd.cipher, 0, 0) ^ plain[4 + i]);
        par[4 + i] = (uint8_t)(st25r_crypto1_filter_bit(&s_rd.cipher) ^ st25r_oddparity(plain[4 + i]));
    }

    uint8_t bits[16] = {0};
    const uint16_t nbits = m1_pack_bits(enc, par, 8, bits);
    uint8_t rxb[8] = {0};
    uint16_t rbits = 0;
    st25r_lock(h);
    err = rd_raw(h, bits, nbits, rxb, sizeof(rxb), &rbits, M1_RD_TIMEOUT_MS);
    st25r_unlock(h);
    if (err != ESP_OK) {
        return err;
    }

    uint8_t at[4] = {0};
    if (m1_unpack_bits(rxb, rbits, at, 4) < 4) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    uint32_t atv = 0;
    for (uint8_t i = 0; i < 4; i++) {
        atv = (atv << 8) | (uint8_t)(at[i] ^ st25r_crypto1_byte(&s_rd.cipher, 0, 0));
    }
    if (atv != st25r_prng_successor(nt, 96)) {
        ESP_LOGW(TAG, "At mismatch blk=%u key=%s nt=%08lX got=%08lX exp=%08lX (rbits=%u)", block,
                 key_b ? "B" : "A", (unsigned long)nt, (unsigned long)atv,
                 (unsigned long)st25r_prng_successor(nt, 96), (unsigned)rbits);
        return ESP_ERR_INVALID_RESPONSE;
    }

    ESP_LOGD(TAG, "auth ok blk=%u key=%s nt=%08lX", block, key_b ? "B" : "A", (unsigned long)nt);
    s_rd.authed = true;
    return ESP_OK;
}

esp_err_t st25r_m1_reader_read_block(st25r_handle_t h, uint8_t block, uint8_t out[16])
{
    if (!h || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_rd.authed) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Authenticated commands carry their CRC inside the encrypted payload
     * (2 command bytes + 2 CRC bytes), exactly like Bruce/Proxmark. */
    uint8_t cmd[4] = {0x30, block, 0, 0};
    const uint16_t cmd_crc = m1_crc16(cmd, 2);
    cmd[2] = (uint8_t)(cmd_crc & 0xFFu);
    cmd[3] = (uint8_t)(cmd_crc >> 8);

    uint8_t enc[4];
    uint8_t par[4];
    m1_encrypt_zero(&s_rd.cipher, cmd, 4, enc, par);

    uint8_t bits[8] = {0};
    const uint16_t nbits = m1_pack_bits(enc, par, 4, bits);
    uint8_t rx[32] = {0};
    uint16_t rbits = 0;

    st25r_lock(h);
    const esp_err_t err = rd_raw(h, bits, nbits, rx, sizeof(rx), &rbits, M1_RD_TIMEOUT_MS);
    st25r_unlock(h);
    if (err != ESP_OK) {
        return err;
    }

    uint8_t raw[24] = {0};
    if (m1_unpack_bits(rx, rbits, raw, sizeof(raw)) < 18) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    for (uint8_t i = 0; i < 18; i++) {
        raw[i] = (uint8_t)(raw[i] ^ st25r_crypto1_byte(&s_rd.cipher, 0, 0));
    }
    const uint16_t crc = m1_crc16(raw, 16);
    if (raw[16] != (uint8_t)(crc & 0xFFu) || raw[17] != (uint8_t)(crc >> 8)) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    memcpy(out, raw, 16);
    return ESP_OK;
}

void st25r_m1_reader_end(st25r_handle_t h)
{
    if (h && h->ready) {
        st25r_lock(h);
        (void)st25r_reg_clear(h, ST25R_REG_ISO14443A, ST25R_ISO_NO_TX_PAR | ST25R_ISO_NO_RX_PAR);
        st25r_unlock(h);
    }
    s_rd.authed = false;
}

static void rd_halt_encrypted(st25r_handle_t h)
{
    /* HLTA is the 2-byte command 50 00 plus its (encrypted) CRC. */
    uint8_t cmd[4] = {ST25R_AC_HLTA, 0x00, 0, 0};
    const uint16_t crc = m1_crc16(cmd, 2);
    cmd[2] = (uint8_t)(crc & 0xFFu);
    cmd[3] = (uint8_t)(crc >> 8);

    uint8_t enc[4];
    uint8_t par[4];
    m1_encrypt_zero(&s_rd.cipher, cmd, 4, enc, par);
    uint8_t bits[8] = {0};
    const uint16_t nbits = m1_pack_bits(enc, par, 4, bits);
    st25r_lock(h);
    rd_tx_only(h, bits, nbits);
    st25r_unlock(h);
    s_rd.authed = false;
}

esp_err_t st25r_m1_read_card(st25r_handle_t h, const st25r_card_t *card, const st25r_m1_dump_t *cfg)
{
    if (!h || !card || !cfg || !cfg->blocks || !cfg->keys || cfg->key_count == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!h->reader_active) {
        return ESP_ERR_INVALID_STATE;
    }
    const uint8_t sectors = st25r_m1_sectors_for_blocks(cfg->block_count);
    if (sectors == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(cfg->blocks, 0, (size_t)cfg->block_count * 16u);
    uint16_t blocks_read = 0;
    uint8_t sectors_read = 0;

    for (uint8_t s = 0; s < sectors; s++) {
        const uint8_t first = st25r_m1_sector_first_block(s);
        const uint8_t count = st25r_m1_sector_block_count(s);
        if ((uint16_t)first + count > cfg->block_count) {
            break;
        }

        const uint8_t *used = NULL;
        bool used_b = false;
        for (uint8_t k = 0; k < cfg->key_count && used == NULL; k++) {
            const uint8_t *key = cfg->keys + (uint16_t)k * 6u;
            if (st25r_m1_reader_auth(h, card, first, key, false) == ESP_OK) {
                used = key;
                break;
            }
            /* A failed authentication leaves the card mute: re-select before
             * the same key as Key B and before the next candidate key. */
            st25r_card_t again;
            if (cfg->try_key_b && rd_reselect(h, &again) &&
                st25r_m1_reader_auth(h, card, first, key, true) == ESP_OK) {
                used = key;
                used_b = true;
                break;
            }
            if ((uint8_t)(k + 1u) < cfg->key_count && !rd_reselect(h, &again)) {
                ESP_LOGW(TAG, "re-select failed before key %u of sector %u", (unsigned)(k + 1u),
                         (unsigned)s);
                break;
            }
        }

        if (used != NULL) {
            bool ok = true;
            for (uint8_t i = 0; i < count; i++) {
                const uint8_t blk = (uint8_t)(first + i);
                const esp_err_t rerr = st25r_m1_reader_read_block(h, blk, cfg->blocks + (uint16_t)blk * 16u);
                if (rerr != ESP_OK) {
                    ESP_LOGD(TAG, "sector %u block %u read failed (%s)", s, blk, esp_err_to_name(rerr));
                    ok = false;
                    break;
                }
            }
            if (ok) {
                /* Patch the trailer with the key that actually worked (Key A
                 * always reads as zeros on a real card). */
                uint8_t *trailer = cfg->blocks + (uint16_t)(first + count - 1u) * 16u;
                if (used_b) {
                    memcpy(trailer + 10, used, 6);
                } else {
                    memcpy(trailer, used, 6);
                }
                blocks_read = (uint16_t)(blocks_read + count);
                sectors_read++;
                ESP_LOGD(TAG, "sector %u ok (Key %s)", s, used_b ? "B" : "A");
            }
            rd_halt_encrypted(h);
        } else {
            ESP_LOGD(TAG, "sector %u: no candidate key", s);
            (void)st25r_reader_halt(h);
        }

        if ((uint8_t)(s + 1u) < sectors) {
            /* A second plain AUTH needs an unauthenticated card: re-select. */
            st25r_card_t again;
            if (!rd_reselect(h, &again)) {
                ESP_LOGW(TAG, "re-select failed after sector %u, stop at %u/%u blocks", s,
                         blocks_read, cfg->block_count);
                break;
            }
        }
    }

    st25r_m1_reader_end(h);
    if (cfg->blocks_read) {
        *cfg->blocks_read = blocks_read;
    }
    if (cfg->sectors_read) {
        *cfg->sectors_read = sectors_read;
    }
    ESP_LOGI(TAG, "dump: %u/%u blocks, %u/%u sectors", blocks_read, cfg->block_count, sectors_read, sectors);
    return (blocks_read > 0) ? ESP_OK : ESP_ERR_NOT_FOUND;
}
