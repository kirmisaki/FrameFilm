#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "hal_hardware.h"
#include "esp_chip_info.h"
static uint32_t physical_flash, initialized_psram;
static int flash_error, detected_chip = CHIP_ESP32S3, physical_calls;
void esp_chip_info(esp_chip_info_t *info) { info->model = detected_chip; info->revision = 101; }
int esp_flash_get_physical_size(void *chip, uint32_t *size) {
    assert(chip == NULL); physical_calls++; *size = physical_flash; return flash_error;
}
size_t esp_psram_get_size(void) { return initialized_psram; }
/* No get_size() stub: image-header capacity must never substitute for physical size. */
#include "../../components/film_hal/src/hal_hardware.c"

int main(void) {
    physical_flash = ARK_BUILD_FLASH; initialized_psram = ARK_BUILD_PSRAM;
    HalHardwareInfo_t info;
    hal_hardware_get_info(&info);
    assert(strcmp(info.target_id, ARK_BUILD_TARGET) == 0 && info.chip_revision == 101);
    assert(info.flash_bytes == physical_flash && info.psram_bytes == initialized_psram);
    assert(hal_hardware_descriptor_matches(&ark_hardware_descriptor));
    ArkHardwareDescriptor_t other = ark_hardware_descriptor;
    memset(other.target_id, 0, sizeof(other.target_id));
    if(ARK_BUILD_PSRAM_BUS == 1) {
        other.flash_bytes = 16u * 1024 * 1024; other.psram_bytes = 8u * 1024 * 1024;
        other.psram_bus = 2; memcpy(other.target_id, ARK_TARGET_N16R8, sizeof(ARK_TARGET_N16R8));
    } else {
        other.flash_bytes = 4u * 1024 * 1024; other.psram_bytes = 2u * 1024 * 1024;
        other.psram_bus = 1; memcpy(other.target_id, ARK_TARGET_N4R2, sizeof(ARK_TARGET_N4R2));
    }
    assert(!hal_hardware_descriptor_matches(&other));
    ArkHardwareDescriptor_t bad = ark_hardware_descriptor;
    bad.magic[0] = 0; assert(!hal_hardware_descriptor_matches(&bad));
    bad = ark_hardware_descriptor; bad.schema = 2; assert(!hal_hardware_descriptor_matches(&bad));
    bad = ark_hardware_descriptor; bad.size = 63; assert(!hal_hardware_descriptor_matches(&bad));
    bad = ark_hardware_descriptor; bad.reserved[0] = 1; assert(!hal_hardware_descriptor_matches(&bad));
    bad = ark_hardware_descriptor; bad.reserved_tail[7] = 1; assert(!hal_hardware_descriptor_matches(&bad));
    bad = ark_hardware_descriptor; bad.target_id[31] = 'X'; assert(!hal_hardware_descriptor_matches(&bad));
    bad = (ArkHardwareDescriptor_t){0}; assert(!hal_hardware_descriptor_matches(&bad));
    assert(!hal_hardware_descriptor_matches(NULL));
    physical_flash = other.flash_bytes;
    hal_hardware_get_info(&info); assert(info.target_id[0] == 0);
    assert(!hal_hardware_descriptor_matches(&ark_hardware_descriptor));
    physical_flash = ARK_BUILD_FLASH; initialized_psram = 0;
    hal_hardware_get_info(&info); assert(info.target_id[0] == 0 && info.psram_bus == 0);
    initialized_psram = ARK_BUILD_PSRAM; flash_error = -1;
    hal_hardware_get_info(&info); assert(info.target_id[0] == 0 && info.flash_bytes == 0);
    flash_error = 0; detected_chip = 5;
    hal_hardware_get_info(&info); assert(info.target_id[0] == 0);
    assert(physical_calls > 0);
    puts("PASS: " ARK_BUILD_TARGET " physical identity, cross-target rejection and descriptor boundaries");
    return 0;
}
