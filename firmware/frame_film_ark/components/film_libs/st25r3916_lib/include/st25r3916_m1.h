/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * MIFARE Classic (M1) helper API for the ST25R3916 reader.
 *
 * Card emulation is intentionally limited to the plain UID/ATQA/SAK passive
 * target path (st25r3916_picc.h); this module only exports the reader side
 * (Crypto1 authentication + block dump) and the sector geometry helpers.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "st25r3916_nfc.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Sector count for a block count (64 -> 16, 128 -> 32, 256 -> 40, else 0). */
uint8_t st25r_m1_sectors_for_blocks(uint16_t block_count);

/** @brief First block of a sector (sectors >= 32 hold 16 blocks). */
uint8_t st25r_m1_sector_first_block(uint8_t sector);

/** @brief Blocks per sector (4, or 16 for sectors >= 32). */
uint8_t st25r_m1_sector_block_count(uint8_t sector);

/* ------------------------------------------------------------------------- */
/* Reader side (dump an M1 card into a block image)                          */
/* ------------------------------------------------------------------------- */

/** @brief Dump configuration: candidate keys and the output image. */
typedef struct {
    uint8_t *blocks;      /**< output image, block_count * 16 bytes (zero-filled) */
    uint16_t block_count; /**< 64 (1K), 128 (2K) or 256 (4K) */
    const uint8_t *keys;  /**< key_count candidate keys, 6 bytes each, tried in order */
    uint8_t key_count;
    bool try_key_b;       /**< retry each key as Key B when Key A fails */
    uint16_t *blocks_read;  /**< optional: blocks successfully read */
    uint8_t *sectors_read;  /**< optional: sectors successfully authenticated */
} st25r_m1_dump_t;

/**
 * @brief Authenticate one block (fresh/plain AUTH; the card must be selected).
 *
 * Keeps the Crypto1 state for st25r_m1_reader_read_block(). On success the card
 * stays authenticated until st25r_m1_reader_end() or an encrypted HALT.
 */
esp_err_t st25r_m1_reader_auth(st25r_handle_t h, const st25r_card_t *card, uint8_t block,
                               const uint8_t key[6], bool key_b);

/** @brief Read one block over the authenticated session. */
esp_err_t st25r_m1_reader_read_block(st25r_handle_t h, uint8_t block, uint8_t out[16]);

/** @brief Drop the session and restore hardware parity settings. */
void st25r_m1_reader_end(st25r_handle_t h);

/**
 * @brief Dump a whole card: authenticate each sector (candidate keys), read all
 *        its blocks, and patch each trailer with the key that actually worked.
 *
 * Between sectors the card is halted (encrypted HLTA) and re-selected, because
 * a second plain AUTH is only valid on an unauthenticated card.
 *
 * @return ESP_OK when at least one sector was read, ESP_ERR_NOT_FOUND when no
 *         candidate key worked (UID is still available to the caller).
 */
esp_err_t st25r_m1_read_card(st25r_handle_t h, const st25r_card_t *card, const st25r_m1_dump_t *cfg);

#ifdef __cplusplus
}
#endif
