#ifndef __HAL_HARDWARE_H__
#define __HAL_HARDWARE_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ARK_HARDWARE_DESCRIPTOR_OFFSET 288
#define ARK_HARDWARE_DESCRIPTOR_SIZE 64
#define ARK_TARGET_N4R2 "ark-esp32s3-n4r2"
#define ARK_TARGET_N16R8 "ark-esp32s3-n16r8"

typedef struct {
    uint16_t chip_id;
    uint16_t chip_revision;
    uint32_t flash_bytes;
    uint32_t psram_bytes;
    uint8_t psram_bus; /* 0=unknown, 1=quad, 2=octal. */
    const char *target_id; /* Empty for an unsupported or unconfirmed combination. */
} HalHardwareInfo_t;

/* ESP32-S3 is little-endian. This descriptor is fixed at app.bin offset 288. */
typedef struct {
    char magic[8];
    uint8_t schema;
    uint8_t size;
    uint16_t chip_id;
    uint32_t flash_bytes;
    uint32_t psram_bytes;
    uint8_t psram_bus;
    uint8_t reserved[3];
    char target_id[32];
    uint8_t reserved_tail[8];
} ArkHardwareDescriptor_t;

_Static_assert(sizeof(ArkHardwareDescriptor_t) == 64, "Ark hardware descriptor size");
_Static_assert(offsetof(ArkHardwareDescriptor_t, chip_id) == 10, "Ark hardware chip offset");
_Static_assert(offsetof(ArkHardwareDescriptor_t, target_id) == 24, "Ark hardware target offset");

void hal_hardware_get_info(HalHardwareInfo_t *info);
bool hal_hardware_descriptor_matches(const ArkHardwareDescriptor_t *image);

#endif
