#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <assert.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_PARTITION_TYPE_DATA 1
#define ESP_PARTITION_MMAP_DATA 0
#define NVS_READWRITE 1
#define portMAX_DELAY 0
#define configASSERT assert
#define ESP_LOGI(...) ((void)0)
typedef struct {size_t size;} esp_partition_t;
typedef int esp_partition_mmap_handle_t;
typedef int nvs_handle_t;
typedef void *SemaphoreHandle_t;
SemaphoreHandle_t xSemaphoreCreateMutex(void);
void xSemaphoreTake(SemaphoreHandle_t handle,int timeout);
void xSemaphoreGive(SemaphoreHandle_t handle);
const esp_partition_t *esp_partition_find_first(int type,int subtype,const char *name);
int esp_partition_mmap(const esp_partition_t *p,size_t offset,size_t size,int memory,const void **ptr,int *handle);
void esp_partition_munmap(int handle);
int esp_partition_read(const esp_partition_t *p,size_t offset,void *data,size_t length);
int esp_partition_write(const esp_partition_t *p,size_t offset,const void *data,size_t length);
int esp_partition_erase_range(const esp_partition_t *p,size_t offset,size_t length);
uint32_t esp_crc32_le(uint32_t crc,const uint8_t *data,uint32_t size);
int nvs_open(const char *name,int mode,nvs_handle_t *handle);
void nvs_close(nvs_handle_t handle);
int nvs_get_u8(nvs_handle_t h,const char *key,uint8_t *value);
int nvs_set_u8(nvs_handle_t h,const char *key,uint8_t value);
int nvs_commit(nvs_handle_t h);
