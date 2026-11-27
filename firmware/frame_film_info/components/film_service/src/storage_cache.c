/*********************************************************************
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Copyright (c) 2026 kiritro
 *
 * FileName : /film_service/src/storage_cache.c
 * Description: SD 缓存（最后一帧、离线页基础）
 *
 *********************************************************************/

/*********************************************************************
 * INCLUDES
 */
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

#include "esp_rom_crc.h"

#include "sys_log.h"
#include "hal_sd.h"
#include "storage_cache.h"

/*********************************************************************
 * MACROS
 */
#define CACHE_TAG           "storage_cache"
#define CACHE_MAGIC         (0x464C4646u)   /* "FFLF" */
#define CACHE_FMT_VERSION   (1)
#define CACHE_TMP_PATH      "/sdcard/info/pages/last.tmp"

/*********************************************************************
 * TYPEDEFS
 */
#pragma pack(push, 1)
typedef struct
{
    uint32_t magic;
    uint16_t fmt_version;
    uint16_t page_id;
    uint32_t version;
    uint32_t size;
    uint32_t crc32;
} cache_header_t;
#pragma pack(pop)

/*********************************************************************
 * LOCAL FUNCTIONS
 */
static void cache_ensure_dirs(void)
{
    if (mkdir(STORAGE_CACHE_ROOT, 0777) != 0 && errno != EEXIST)
    {
        sys_logw(CACHE_TAG, "mkdir %s failed: %d", STORAGE_CACHE_ROOT, errno);
    }
    if (mkdir(STORAGE_CACHE_PAGES_DIR, 0777) != 0 && errno != EEXIST)
    {
        sys_logw(CACHE_TAG, "mkdir %s failed: %d", STORAGE_CACHE_PAGES_DIR, errno);
    }
}

/*********************************************************************
 * GLOBAL FUNCTIONS
 */

void storage_cache_init(void)
{
    if (!storage_cache_is_ready())
    {
        sys_logw(CACHE_TAG, "SD not mounted, cache disabled");
        return;
    }
    cache_ensure_dirs();
    sys_logi(CACHE_TAG, "cache ready at %s", STORAGE_CACHE_ROOT);
}

bool storage_cache_is_ready(void)
{
    return hal_sd_get_status() == SD_MOUNT;
}

esp_err_t storage_cache_save_last_frame(const uint8_t *frame, uint32_t size,
                                        uint32_t version, uint16_t page_id)
{
    cache_header_t header;
    FILE *file;
    size_t written;

    if (!storage_cache_is_ready())
    {
        return ESP_ERR_INVALID_STATE;
    }
    if (frame == NULL || size == 0)
    {
        return ESP_ERR_INVALID_ARG;
    }

    cache_ensure_dirs();

    header.magic = CACHE_MAGIC;
    header.fmt_version = CACHE_FMT_VERSION;
    header.page_id = page_id;
    header.version = version;
    header.size = size;
    header.crc32 = esp_rom_crc32_le(0, frame, size);

    file = fopen(CACHE_TMP_PATH, "wb");
    if (file == NULL)
    {
        sys_loge(CACHE_TAG, "open tmp failed");
        return ESP_FAIL;
    }

    written = fwrite(&header, 1, sizeof(header), file);
    if (written == sizeof(header) && size > 0)
    {
        written = fwrite(frame, 1, size, file);
    }
    fclose(file);

    if (written != size)
    {
        sys_loge(CACHE_TAG, "write failed: %u/%lu", (unsigned)written, (unsigned long)size);
        remove(CACHE_TMP_PATH);
        return ESP_FAIL;
    }

    remove(STORAGE_CACHE_LAST_FRAME);
    if (rename(CACHE_TMP_PATH, STORAGE_CACHE_LAST_FRAME) != 0)
    {
        sys_loge(CACHE_TAG, "rename failed");
        remove(CACHE_TMP_PATH);
        return ESP_FAIL;
    }

    sys_logi(CACHE_TAG, "last frame saved: page=%u ver=%lu size=%lu",
             page_id, (unsigned long)version, (unsigned long)size);
    return ESP_OK;
}

esp_err_t storage_cache_load_last_frame(uint8_t *frame, uint32_t cap,
                                        uint32_t *out_size, uint32_t *out_version,
                                        uint16_t *out_page_id)
{
    cache_header_t header;
    FILE *file;
    size_t read_size;

    if (!storage_cache_is_ready())
    {
        return ESP_ERR_INVALID_STATE;
    }
    if (frame == NULL || out_size == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    file = fopen(STORAGE_CACHE_LAST_FRAME, "rb");
    if (file == NULL)
    {
        return ESP_ERR_NOT_FOUND;
    }

    read_size = fread(&header, 1, sizeof(header), file);
    if (read_size != sizeof(header) ||
        header.magic != CACHE_MAGIC ||
        header.fmt_version != CACHE_FMT_VERSION ||
        header.size == 0 ||
        header.size > cap)
    {
        fclose(file);
        sys_logw(CACHE_TAG, "invalid cache header");
        return ESP_ERR_INVALID_RESPONSE;
    }

    read_size = fread(frame, 1, header.size, file);
    fclose(file);

    if (read_size != header.size)
    {
        sys_logw(CACHE_TAG, "short read: %u/%lu", (unsigned)read_size, (unsigned long)header.size);
        return ESP_ERR_INVALID_SIZE;
    }
    if (esp_rom_crc32_le(0, frame, header.size) != header.crc32)
    {
        sys_logw(CACHE_TAG, "cache crc mismatch");
        return ESP_ERR_INVALID_CRC;
    }

    *out_size = header.size;
    if (out_version)
    {
        *out_version = header.version;
    }
    if (out_page_id)
    {
        *out_page_id = header.page_id;
    }

    sys_logi(CACHE_TAG, "last frame loaded: page=%u ver=%lu size=%lu",
             header.page_id, (unsigned long)header.version, (unsigned long)header.size);
    return ESP_OK;
}
