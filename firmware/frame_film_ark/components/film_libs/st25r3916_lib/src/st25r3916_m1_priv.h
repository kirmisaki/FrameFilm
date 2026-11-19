/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Shared bit/CRC helpers for the MIFARE Classic reader module.
 */
#pragma once

#include <stdint.h>
#include <string.h>

#include "st25r3916_crypto1.h"

/* CRC_A (ISO/IEC 14443-3): poly 0x1021 reflected (0x8408), init 0x6363,
 * transmitted low byte first. Bitwise form so it is obviously the standard
 * CRC-A (the "swap + shift" shortcut computes the non-reflected CCITT variant
 * and yields wrong values here). */
static inline uint16_t m1_crc16(const uint8_t *data, uint16_t len)
{
    uint16_t crc = 0x6363;
    for (uint16_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t b = 0; b < 8; b++) {
            crc = (crc & 1u) ? (uint16_t)((crc >> 1) ^ 0x8408u) : (uint16_t)(crc >> 1);
        }
    }
    return crc;
}

/* Data + parity bit after every byte, LSB first within each byte. */
static inline uint16_t m1_pack_bits(const uint8_t *data, const uint8_t *par, uint8_t nbytes, uint8_t *out)
{
    const uint16_t total = (uint16_t)(nbytes * 9u);
    memset(out, 0, (uint16_t)((total + 7u) / 8u));
    uint16_t bit = 0;
    for (uint8_t i = 0; i < nbytes; i++) {
        for (uint8_t b = 0; b < 8; b++) {
            if (data[i] & (1u << b)) {
                out[bit >> 3] |= (uint8_t)(1u << (bit & 7u));
            }
            bit++;
        }
        if (par[i] & 1u) {
            out[bit >> 3] |= (uint8_t)(1u << (bit & 7u));
        }
        bit++;
    }
    return total;
}

static inline uint8_t m1_unpack_bits(const uint8_t *in, uint16_t nbits, uint8_t *out, uint8_t max_bytes)
{
    uint8_t n = 0;
    uint16_t bit = 0;
    while ((uint16_t)(bit + 8u) <= nbits && n < max_bytes) {
        uint8_t v = 0;
        for (uint8_t b = 0; b < 8; b++) {
            if (in[bit >> 3] & (uint8_t)(1u << (bit & 7u))) {
                v |= (uint8_t)(1u << b);
            }
            bit++;
        }
        out[n++] = v;
        if (bit < nbits) {
            bit++; /* drop the parity bit */
        }
    }
    return n;
}

/* Encrypt after authentication: the LFSR is clocked with 0. */
static inline void m1_encrypt_zero(st25r_crypto1_state_t *c, const uint8_t *plain, uint8_t n, uint8_t *enc,
                                   uint8_t *par)
{
    for (uint8_t i = 0; i < n; i++) {
        enc[i] = (uint8_t)(st25r_crypto1_byte(c, 0, 0) ^ plain[i]);
        par[i] = (uint8_t)(st25r_crypto1_filter_bit(c) ^ st25r_oddparity(plain[i]));
    }
}
