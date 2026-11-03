#pragma once
#include <stdint.h>
#define CHIP_ESP32S3 9
typedef struct { int model; uint16_t revision; } esp_chip_info_t;
void esp_chip_info(esp_chip_info_t *info);
