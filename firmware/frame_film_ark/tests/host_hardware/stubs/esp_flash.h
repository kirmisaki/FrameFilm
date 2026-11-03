#pragma once
#include <stdint.h>
#define ESP_OK 0
int esp_flash_get_physical_size(void *chip, uint32_t *size);
