#ifndef __SYS_HW_H__
#define __SYS_HW_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SYS_HW_DESCRIPTOR_OFFSET 288
#define SYS_HW_DESCRIPTOR_SIZE 64
#define SYS_HW_TARGET_N4R2 "ark-esp32s3-n4r2"
#define SYS_HW_TARGET_N16R8 "ark-esp32s3-n16r8"

typedef struct {
    uint16_t chip_id;
    uint16_t chip_revision;
    uint32_t flash_bytes;
    uint32_t psram_bytes;
    uint8_t psram_bus; /* 0=unknown, 1=quad, 2=octal. */
    const char *target_id; /* Empty for an unsupported or unconfirmed combination. */
} SysHwInfo_t;

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
} SysHwDescriptor_t;

_Static_assert(sizeof(SysHwDescriptor_t) == 64, "Ark hardware descriptor size");
_Static_assert(offsetof(SysHwDescriptor_t, chip_id) == 10, "Ark hardware chip offset");
_Static_assert(offsetof(SysHwDescriptor_t, target_id) == 24, "Ark hardware target offset");

/* Build identity embedded in app.bin; kept by the linker with -u. */
extern const SysHwDescriptor_t g_sys_hw_descriptor;

void sys_hw_get_info(SysHwInfo_t *info);
bool sys_hw_descriptor_matches(const SysHwDescriptor_t *image);

#endif
