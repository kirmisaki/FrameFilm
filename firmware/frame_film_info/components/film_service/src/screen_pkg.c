/*********************************************************************
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Copyright (c) 2026 kiritro
 *
 * FileName : /film_service/src/screen_pkg.c
 * Description: FFSP 屏幕包解析与校验
 *
 *********************************************************************/

/*********************************************************************
 * INCLUDES
 */
#include <string.h>

#include "esp_rom_crc.h"

#include "sys_log.h"
#include "screen_pkg.h"

/*********************************************************************
 * MACROS
 */
#define PKG_TAG     "screen_pkg"

/*********************************************************************
 * LOCAL FUNCTIONS
 */
static uint32_t ffsp_crc32(const uint8_t *data, uint32_t len)
{
    return esp_rom_crc32_le(0, data, len);
}

/*********************************************************************
 * GLOBAL FUNCTIONS
 */

esp_err_t ffsp_parse(const uint8_t *data, uint32_t len, ffsp_package_t *pkg)
{
    ffsp_header_t header;
    uint32_t table_size;
    uint32_t i;

    if (data == NULL || pkg == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }
    if (len < FFSP_HEADER_SIZE)
    {
        sys_loge(PKG_TAG, "package too short: %lu", (unsigned long)len);
        return ESP_ERR_INVALID_SIZE;
    }

    memcpy(&header, data, sizeof(header));

    if (memcmp(header.magic, FFSP_MAGIC, 4) != 0)
    {
        sys_loge(PKG_TAG, "bad magic");
        return ESP_ERR_INVALID_RESPONSE;
    }
    if (header.version != FFSP_VERSION)
    {
        sys_loge(PKG_TAG, "unsupported version %u", header.version);
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (header.pixel_format != FFSP_PIXEL_MONO1)
    {
        sys_loge(PKG_TAG, "unsupported pixel format %u", header.pixel_format);
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (header.flags & FFSP_FLAG_COMPRESSED)
    {
        sys_loge(PKG_TAG, "compressed package not supported in v1");
        return ESP_ERR_NOT_SUPPORTED;
    }
    if ((header.flags & (FFSP_FLAG_FULL | FFSP_FLAG_DELTA)) == 0 ||
        (header.flags & (FFSP_FLAG_FULL | FFSP_FLAG_DELTA)) ==
            (FFSP_FLAG_FULL | FFSP_FLAG_DELTA))
    {
        sys_loge(PKG_TAG, "invalid flags 0x%02X", header.flags);
        return ESP_ERR_INVALID_RESPONSE;
    }
    if (header.width == 0 || header.height == 0 ||
        (header.width % 8) != 0 ||
        header.region_count == 0 ||
        header.region_count > FFSP_MAX_REGIONS)
    {
        sys_loge(PKG_TAG, "invalid dimensions/count: %ux%u regions=%u",
                 header.width, header.height, header.region_count);
        return ESP_ERR_INVALID_RESPONSE;
    }

    {
        ffsp_header_t crc_header = header;
        crc_header.header_crc32 = 0;
        if (ffsp_crc32((const uint8_t *)&crc_header, sizeof(crc_header)) != header.header_crc32)
        {
            sys_loge(PKG_TAG, "header crc mismatch");
            return ESP_ERR_INVALID_CRC;
        }
    }

    table_size = FFSP_HEADER_SIZE + (uint32_t)header.region_count * FFSP_REGION_SIZE;
    if (table_size > len || header.payload_size > (len - table_size))
    {
        sys_loge(PKG_TAG, "truncated package: table=%lu payload=%lu len=%lu",
                 (unsigned long)table_size, (unsigned long)header.payload_size,
                 (unsigned long)len);
        return ESP_ERR_INVALID_SIZE;
    }

    pkg->header = header;
    pkg->regions = (const ffsp_region_t *)(data + FFSP_HEADER_SIZE);
    pkg->payload = data + table_size;
    pkg->total_size = len;

    for (i = 0; i < header.region_count; i++)
    {
        const ffsp_region_t *r = &pkg->regions[i];
        uint32_t stride = (r->w + 7) / 8;
        uint32_t expect_size = stride * r->h;

        if (r->w == 0 || r->h == 0 || (r->x % 8) != 0 ||
            (uint32_t)r->x + r->w > header.width ||
            (uint32_t)r->y + r->h > header.height)
        {
            sys_loge(PKG_TAG, "region %lu out of range", (unsigned long)i);
            return ESP_ERR_INVALID_RESPONSE;
        }
        if (r->codec != FFSP_CODEC_RAW)
        {
            sys_loge(PKG_TAG, "region %lu unsupported codec %u", (unsigned long)i, r->codec);
            return ESP_ERR_NOT_SUPPORTED;
        }
        if (r->refresh > FFSP_REFRESH_FULL)
        {
            sys_loge(PKG_TAG, "region %lu invalid refresh %u", (unsigned long)i, r->refresh);
            return ESP_ERR_INVALID_RESPONSE;
        }
        if (r->data_offset > header.payload_size ||
            r->data_size != expect_size ||
            r->data_size > header.payload_size - r->data_offset)
        {
            sys_loge(PKG_TAG, "region %lu bad size/offset", (unsigned long)i);
            return ESP_ERR_INVALID_SIZE;
        }
        if (ffsp_crc32(pkg->payload + r->data_offset, r->data_size) != r->data_crc32)
        {
            sys_loge(PKG_TAG, "region %lu crc mismatch", (unsigned long)i);
            return ESP_ERR_INVALID_CRC;
        }
    }

    sys_logi(PKG_TAG, "package ok: %ux%u page=%u base=%lu target=%lu regions=%u payload=%lu",
             header.width, header.height, header.page_id,
             (unsigned long)header.base_version, (unsigned long)header.target_version,
             header.region_count, (unsigned long)header.payload_size);
    return ESP_OK;
}

const uint8_t *ffsp_region_data(const ffsp_package_t *pkg, const ffsp_region_t *region)
{
    if (pkg == NULL || region == NULL)
    {
        return NULL;
    }
    return pkg->payload + region->data_offset;
}

void ffsp_composite(const ffsp_package_t *pkg, uint8_t *fb)
{
    if (pkg == NULL || fb == NULL)
    {
        return;
    }

    memset(fb, 0x00, (pkg->header.width / 8) * pkg->header.height); /* 0 = 白 */
    ffsp_apply_regions(pkg, fb);
}

void ffsp_apply_regions(const ffsp_package_t *pkg, uint8_t *fb)
{
    uint32_t fb_stride;
    uint32_t i;

    if (pkg == NULL || fb == NULL)
    {
        return;
    }

    fb_stride = pkg->header.width / 8;

    for (i = 0; i < pkg->header.region_count; i++)
    {
        const ffsp_region_t *r = &pkg->regions[i];
        const uint8_t *src = ffsp_region_data(pkg, r);
        uint32_t stride = (r->w + 7) / 8;
        uint32_t row;

        for (row = 0; row < r->h; row++)
        {
            memcpy(fb + (r->y + row) * fb_stride + r->x / 8,
                   src + row * stride,
                   stride);
        }
    }
}
