#ifndef __HAL_EPD_ABC9_H__
#define __HAL_EPD_ABC9_H__

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Existing private .film container, also used verbatim by the I006 pass profile.
 * Pure validation only: no file I/O, allocation, panel access or code remapping. */
#define EPD_ABC9_HEADER_BYTES 32u
#define EPD_ABC9_BODY_BYTES   (720u * 480u * 2u)
#define EPD_ABC9_FILE_BYTES   (EPD_ABC9_HEADER_BYTES + EPD_ABC9_BODY_BYTES)

static inline int hal_epd_abc9_header_valid(const uint8_t *header)
{
    if(header == NULL) return 0;
    const uint32_t body = (uint32_t)header[0] | ((uint32_t)header[1] << 8) |
                          ((uint32_t)header[2] << 16) | ((uint32_t)header[3] << 24);
    return body == EPD_ABC9_BODY_BYTES &&
           header[4] == 0xD0 && header[5] == 0x02 && /* 720, LE16 */
           header[6] == 0xE0 && header[7] == 0x01 && /* 480, LE16 */
           header[9] == 0x7F && header[10] == 1 && header[11] == 0 &&
           memcmp(header + 12, "ABC9", 4) == 0;
}

/* May also validate aligned chunks while publishing a streamed upload. */
static inline int hal_epd_abc9_codes_valid(const uint8_t *pixels, size_t size)
{
    if(pixels == NULL || (size & 1u)) return 0;
    for(size_t i = 1; i < size; i += 2)
        if(pixels[i] & 0xFEu) return 0;
    return 1;
}

#endif /* __HAL_EPD_ABC9_H__ */
