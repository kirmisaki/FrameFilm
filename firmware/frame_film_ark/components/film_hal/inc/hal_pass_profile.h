/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef __HAL_PASS_PROFILE_H__
#define __HAL_PASS_PROFILE_H__

#include <stdio.h>
#include "hal_epd_abc9.h"

/* Shared by the SD loader and every staged profile.bin publisher. No allocation
 * or panel access. Native student bodies are already in physical orientation;
 * compact FFUI planes are logical row-major, 1 = black, MSB first. */
#ifndef PASS_PROFILE_FILE_PATH
#define PASS_PROFILE_FILE_PATH "/sdcard/app/pass/profile.bin"
#endif
#define PASS_COMPACT_W 440u
#define PASS_COMPACT_H 608u
#define PASS_COMPACT_I1_BYTES (PASS_COMPACT_W * PASS_COMPACT_H / 8u)
#define PASS_AVATAR_X 20u
#define PASS_AVATAR_Y 143u
#define PASS_AVATAR_W 128u
#define PASS_AVATAR_H 160u
#define PASS_AVATAR_PIXELS (PASS_AVATAR_W * PASS_AVATAR_H)
#define PASS_FAST_BODY_BYTES (720u * 480u)

typedef enum {
    PASS_FMT_MONO, PASS_FMT_SIX, PASS_FMT_ABC9, PASS_FMT_FAST,
    PASS_FMT_COMPACT_ABC9, PASS_FMT_COMPACT_FAST
} PassFormat_t;

typedef struct {
    PassFormat_t format;
    uint16_t width, height;
    uint8_t header_size, avatar_planes;
    uint32_t body_size;
} PassProfileInfo_t;

static inline uint16_t hal_pass_le16(const uint8_t *p)
{ return (uint16_t)p[0] | ((uint16_t)p[1] << 8); }

static inline int hal_pass_profile_header(const uint8_t *h, size_t bytes,
                                          size_t file_size, PassProfileInfo_t *out)
{
    if(h == NULL || out == NULL || bytes < 16) return 0;
    PassProfileInfo_t info = {PASS_FMT_MONO, 0, 0, 0, 0, 0};
    if(memcmp(h, "FFUI", 4) == 0)
    {
        info.header_size = 16;
        info.width = hal_pass_le16(h + 6);
        info.height = hal_pass_le16(h + 8);
        if(h[4] == 3)
        {
            if(info.width != PASS_COMPACT_W || info.height != PASS_COMPACT_H ||
               h[10] > 1 || memcmp(h + 11, (const uint8_t[5]){0}, 5)) return 0;
            info.avatar_planes = h[10];
            if(h[5] == 0x03) info.format = PASS_FMT_COMPACT_FAST;
#if defined(CONFIG_ARK_EPD_ABC_PREVIEW) || defined(CONFIG_ARK_EPD_ABC_DIAG)
            else if(h[5] == 0x7F) info.format = PASS_FMT_COMPACT_ABC9;
#endif
            else return 0;
            info.body_size = PASS_COMPACT_I1_BYTES + info.avatar_planes *
                PASS_AVATAR_PIXELS * (info.format == PASS_FMT_COMPACT_ABC9 ? 2u : 1u);
        }
        else
        {
            if(!((info.width == PASS_COMPACT_W && info.height == PASS_COMPACT_H) ||
                 (info.width == 480 && info.height == 720)) ||
               memcmp(h + 10, (const uint8_t[6]){0}, 6)) return 0;
            if(h[4] == 1 && h[5] == 1) info.format = PASS_FMT_MONO;
            else if(h[4] == 2 && h[5] == 2) info.format = PASS_FMT_SIX;
            else return 0;
            info.body_size = (uint32_t)info.width * info.height /
                             (info.format == PASS_FMT_MONO ? 8u : 2u);
        }
    }
    else
    {
        if(bytes < 32) return 0;
        info.header_size = 32;
        info.width = 480; info.height = 720; /* logical layout only */
        const uint32_t body = (uint32_t)h[0] | ((uint32_t)h[1] << 8) |
                              ((uint32_t)h[2] << 16) | ((uint32_t)h[3] << 24);
        if(h[8] == 0 && h[9] == 0x03 && body == PASS_FAST_BODY_BYTES &&
           hal_pass_le16(h + 4) == 720 && hal_pass_le16(h + 6) == 480 &&
           hal_pass_le16(h + 10) <= 1)
        { info.format = PASS_FMT_FAST; info.body_size = PASS_FAST_BODY_BYTES; }
#if defined(CONFIG_ARK_EPD_ABC_PREVIEW) || defined(CONFIG_ARK_EPD_ABC_DIAG)
        else if(hal_epd_abc9_header_valid(h))
        { info.format = PASS_FMT_ABC9; info.body_size = EPD_ABC9_BODY_BYTES; }
#endif
        else return 0;
    }
    if(file_size != (size_t)info.header_size + info.body_size) return 0;
    *out = info;
    return 1;
}

/* Chunk offsets are body-relative; LE16 chunks must begin/end on code bounds. */
static inline int hal_pass_profile_codes(const PassProfileInfo_t *info,
                                         const uint8_t *p, size_t offset, size_t n)
{
    if(info == NULL || p == NULL || offset > info->body_size ||
       n > info->body_size - offset) return 0;
    PassFormat_t fmt = info->format;
    if(fmt == PASS_FMT_MONO) return 1;
    if(fmt == PASS_FMT_COMPACT_ABC9 || fmt == PASS_FMT_COMPACT_FAST)
    {
        if(offset + n <= PASS_COMPACT_I1_BYTES) return 1;
        if(offset < PASS_COMPACT_I1_BYTES)
        { size_t skip = PASS_COMPACT_I1_BYTES - offset; p += skip; n -= skip; offset += skip; }
        fmt = fmt == PASS_FMT_COMPACT_ABC9 ? PASS_FMT_ABC9 : PASS_FMT_FAST;
    }
    if(fmt == PASS_FMT_ABC9)
        return !(offset & 1u) && hal_epd_abc9_codes_valid(p, n);
    for(size_t i = 0; i < n; ++i)
    {
        if(fmt == PASS_FMT_FAST) { if(p[i] > 63) return 0; }
        else
        {
            uint8_t a = p[i] >> 4, b = p[i] & 15;
            if(a > 6 || b > 6 || a == 4 || b == 4) return 0;
        }
    }
    return 1;
}

/* Inspect an open file, validate the exact body, and rewind to the body for the
 * loader. Staged commits use this same function, including actual file length. */
static inline int hal_pass_profile_inspect(FILE *fp, PassProfileInfo_t *out)
{
    uint8_t header[32], chunk[256];
    if(fp == NULL || out == NULL || fseek(fp, 0, SEEK_END)) return 0;
    long length = ftell(fp);
    if(length < 16 || fseek(fp, 0, SEEK_SET)) return 0;
    size_t got = fread(header, 1, sizeof(header), fp);
    if(!hal_pass_profile_header(header, got, (size_t)length, out) ||
       fseek(fp, out->header_size, SEEK_SET)) return 0;
    for(size_t pos = 0; pos < out->body_size;)
    {
        size_t n = out->body_size - pos;
        if(n > sizeof(chunk)) n = sizeof(chunk);
        if(fread(chunk, 1, n, fp) != n || !hal_pass_profile_codes(out, chunk, pos, n)) return 0;
        pos += n;
    }
    return fseek(fp, out->header_size, SEEK_SET) == 0;
}

/* Compact logical pixel code; caller must bound x/y and validate the planes.
 * Black/white use the same measured native codes as full-card quantization. */
static inline uint16_t hal_pass_compact_code(const uint8_t *body, int abc9,
                                             int avatar, unsigned x, unsigned y)
{
    if(avatar && x >= PASS_AVATAR_X && x < PASS_AVATAR_X + PASS_AVATAR_W &&
       y >= PASS_AVATAR_Y && y < PASS_AVATAR_Y + PASS_AVATAR_H)
    {
        size_t i = (size_t)(y - PASS_AVATAR_Y) * PASS_AVATAR_W + x - PASS_AVATAR_X;
        const uint8_t *p = body + PASS_COMPACT_I1_BYTES + i * (abc9 ? 2u : 1u);
        return abc9 ? hal_pass_le16(p) : *p;
    }
    size_t i = (size_t)y * PASS_COMPACT_W + x;
    int black = (body[i >> 3] >> (7u - (i & 7u))) & 1u;
    return black ? (abc9 ? 387 : 0) : (abc9 ? 75 : 21);
}
#endif
