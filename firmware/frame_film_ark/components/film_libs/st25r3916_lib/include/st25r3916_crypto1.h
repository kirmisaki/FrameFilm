/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Crypto1 stream cipher for MIFARE Classic (ST25R3916 listener and reader).
 *
 * The algorithm itself is public ("Dismantling MIFARE Classic", Garcia et al.
 * 2008, and the Crapto1 reference implementation). This expression follows the
 * Bruce firmware / Crapto1 lineage (GPL-3.0); see THIRD_PARTY.md.
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Cipher state: two 24-bit LFSR halves, one bit per parity tap. */
typedef struct {
    uint32_t odd;
    uint32_t even;
} st25r_crypto1_state_t;

/** @brief Initialise the cipher from a 48-bit key (top 16 bits ignored). */
void st25r_crypto1_init(st25r_crypto1_state_t *s, uint64_t key);

/** @brief Clock one bit; returns the keystream bit. @p is_encrypted feeds ciphertext back. */
uint8_t st25r_crypto1_bit(st25r_crypto1_state_t *s, uint8_t in, int is_encrypted);

/** @brief Clock one byte (LSB first); returns the keystream byte. */
uint8_t st25r_crypto1_byte(st25r_crypto1_state_t *s, uint8_t in, int is_encrypted);

/** @brief Clock one 32-bit word (big-endian bit order); returns the keystream word. */
uint32_t st25r_crypto1_word(st25r_crypto1_state_t *s, uint32_t in, int is_encrypted);

/** @brief Next keystream bit without clocking the cipher (used for encrypted parity). */
uint8_t st25r_crypto1_filter_bit(const st25r_crypto1_state_t *s);

/** @brief MIFARE nonce PRNG successor: advance @p x by @p n cycles. */
uint32_t st25r_prng_successor(uint32_t x, uint32_t n);

/** @brief ISO14443-A odd parity bit of @p b (1 when the byte has an even number of ones). */
uint8_t st25r_oddparity(uint8_t b);

#ifdef __cplusplus
}
#endif
