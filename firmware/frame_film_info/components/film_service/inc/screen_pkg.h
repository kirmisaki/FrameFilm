#ifndef __SCREEN_PKG_H__
#define __SCREEN_PKG_H__

/*********************************************************************
 * INCLUDES
 */
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*********************************************************************
 * MACROS
 */
#define FFSP_MAGIC                  "FFSP"
#define FFSP_VERSION                (1)
#define FFSP_HEADER_SIZE            (32)
#define FFSP_REGION_SIZE            (24)
#define FFSP_MAX_REGIONS            (32)
#define FFSP_MAX_PACKAGE_SIZE       (256 * 1024)

#define FFSP_FLAG_FULL              (1u << 0)
#define FFSP_FLAG_DELTA             (1u << 1)
#define FFSP_FLAG_COMPRESSED        (1u << 2)

#define FFSP_PIXEL_MONO1            (0)

#define FFSP_CODEC_RAW              (0)
#define FFSP_CODEC_RLE              (1)

#define FFSP_REFRESH_WINDOW         (0)
#define FFSP_REFRESH_DIFF           (1)
#define FFSP_REFRESH_FULL           (2)

/*********************************************************************
 * TYPEDEFS
 */
#pragma pack(push, 1)
typedef struct
{
    uint8_t  magic[4];          /* "FFSP" */
    uint8_t  version;           /* 包格式版本 */
    uint8_t  flags;             /* bit0 full / bit1 delta / bit2 compressed */
    uint8_t  pixel_format;      /* 0 = mono1 */
    uint8_t  panel_id;          /* 0x09 */
    uint16_t width;
    uint16_t height;
    uint16_t page_id;
    uint16_t region_count;
    uint32_t base_version;      /* 增量包基于的版本，全量为 0 */
    uint32_t target_version;    /* 应用后的版本 */
    uint32_t payload_size;
    uint32_t header_crc32;      /* header（本字段置 0）的 CRC32 */
} ffsp_header_t;

typedef struct
{
    uint16_t x;
    uint16_t y;
    uint16_t w;
    uint16_t h;
    uint8_t  codec;
    uint8_t  refresh;
    uint16_t reserved;
    uint32_t data_offset;
    uint32_t data_size;
    uint32_t data_crc32;
} ffsp_region_t;
#pragma pack(pop)

typedef struct
{
    ffsp_header_t        header;
    const ffsp_region_t *regions;
    const uint8_t       *payload;
    uint32_t             total_size;
} ffsp_package_t;

/*********************************************************************
 * GLOBAL FUNCTIONS
 */

/**
 * @brief 解析并校验 FFSP 屏幕包
 *
 * @param data 包数据
 * @param len  数据长度
 * @param pkg  输出解析结果（指向 data 内部）
 * @return ESP_OK 成功；其他失败
 */
esp_err_t ffsp_parse(const uint8_t *data, uint32_t len, ffsp_package_t *pkg);

/**
 * @brief 取某区域的数据指针
 */
const uint8_t *ffsp_region_data(const ffsp_package_t *pkg, const ffsp_region_t *region);

/**
 * @brief 把包内所有区域合成到整帧 1bpp 缓冲（白底）
 *
 * @param pkg 已解析的包
 * @param fb  目标缓冲，大小 = width*height/8
 */
void ffsp_composite(const ffsp_package_t *pkg, uint8_t *fb);

/**
 * @brief 把包内区域叠加到已有整帧（不清屏），用于增量更新基准
 */
void ffsp_apply_regions(const ffsp_package_t *pkg, uint8_t *fb);

/**
 * @brief 判断包是否为全量包
 */
static inline bool ffsp_is_full(const ffsp_package_t *pkg)
{
    return (pkg->header.flags & FFSP_FLAG_FULL) != 0;
}

#ifdef __cplusplus
}
#endif

#endif /* __SCREEN_PKG_H__ */
