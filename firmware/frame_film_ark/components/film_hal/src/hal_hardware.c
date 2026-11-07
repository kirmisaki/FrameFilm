#include "hal_hardware.h"
#include "sdkconfig.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_psram.h"
#include <string.h>

#if defined(CONFIG_ESPTOOLPY_FLASHSIZE_4MB) && defined(CONFIG_SPIRAM_MODE_QUAD)
#define ARK_BUILD_TARGET ARK_TARGET_N4R2
#define ARK_BUILD_FLASH (4u * 1024 * 1024)
#define ARK_BUILD_PSRAM (2u * 1024 * 1024)
#define ARK_BUILD_PSRAM_BUS 1
#elif defined(CONFIG_ESPTOOLPY_FLASHSIZE_16MB) && defined(CONFIG_SPIRAM_MODE_OCT)
#define ARK_BUILD_TARGET ARK_TARGET_N16R8
#define ARK_BUILD_FLASH (16u * 1024 * 1024)
#define ARK_BUILD_PSRAM (8u * 1024 * 1024)
#define ARK_BUILD_PSRAM_BUS 2
#else
#error "Ark firmware requires a declared n4r2 or n16r8 hardware target"
#endif

const __attribute__((section(".rodata_custom_desc"), used, aligned(4)))
ArkHardwareDescriptor_t ark_hardware_descriptor = {
    .magic = {'F', 'F', 'A', 'R', 'K', 'H', 'W', '1'}, .schema = 1, .size = ARK_HARDWARE_DESCRIPTOR_SIZE,
    .chip_id = 9, .flash_bytes = ARK_BUILD_FLASH, .psram_bytes = ARK_BUILD_PSRAM,
    .psram_bus = ARK_BUILD_PSRAM_BUS, .target_id = ARK_BUILD_TARGET
};

void hal_hardware_get_info(HalHardwareInfo_t *info)
{
    if(info == NULL) return;
    memset(info, 0, sizeof(*info));
    info->target_id = "";
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    info->chip_id = (uint16_t)chip.model;
    info->chip_revision = chip.revision;
    /* get_size() can report the image-header limit; use the physical ID API. */
    if(esp_flash_get_physical_size(NULL, &info->flash_bytes) != ESP_OK)
        info->flash_bytes = 0;
    info->psram_bytes = (uint32_t)esp_psram_get_size();
    if(info->psram_bytes != 0) info->psram_bus = ARK_BUILD_PSRAM_BUS;
    if(chip.model != CHIP_ESP32S3) return;
    if(info->flash_bytes == 4u * 1024 * 1024 && info->psram_bytes == 2u * 1024 * 1024 &&
       info->psram_bus == 1) info->target_id = ARK_TARGET_N4R2;
    else if(info->flash_bytes == 16u * 1024 * 1024 && info->psram_bytes == 8u * 1024 * 1024 &&
            info->psram_bus == 2) info->target_id = ARK_TARGET_N16R8;
}

bool hal_hardware_descriptor_matches(const ArkHardwareDescriptor_t *image)
{
    if(image == NULL) return false;
    HalHardwareInfo_t actual;
    hal_hardware_get_info(&actual);
    if(actual.target_id[0] == '\0') return false;
    const uint8_t zero[8] = {0};
    char target[32] = {0};
    memcpy(target, actual.target_id, strlen(actual.target_id));
    return memcmp(image->magic, "FFARKHW1", 8) == 0 && image->schema == 1 &&
        image->size == ARK_HARDWARE_DESCRIPTOR_SIZE && image->chip_id == actual.chip_id &&
        image->flash_bytes == actual.flash_bytes && image->psram_bytes == actual.psram_bytes &&
        image->psram_bus == actual.psram_bus && memcmp(image->target_id, target, sizeof(target)) == 0 &&
        memcmp(image->reserved, zero, sizeof(image->reserved)) == 0 &&
        memcmp(image->reserved_tail, zero, sizeof(image->reserved_tail)) == 0;
}
