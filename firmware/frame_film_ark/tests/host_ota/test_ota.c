/* Platform and hash API doubles; production staged OTA functions are included below. */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include "../../components/film_sys/inc/sys_hw.h"
typedef int esp_err_t;
typedef int esp_ota_handle_t;
typedef struct { uint32_t address, size; } esp_partition_t;
typedef struct { int chip_id; } esp_image_header_t;
typedef struct { char project_name[32]; } esp_app_desc_t;
typedef struct { unsigned sum; } mbedtls_sha256_context;
#define sys_logi(...) ((void)0)
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_STATE -2
#define ESP_ERR_INVALID_SIZE -3
#define ESP_ERR_INVALID_CRC -4
#define ESP_ERR_INVALID_ARG -5
#define ESP_CHIP_ID_ESP32S3 9
static esp_partition_t part = {4096, 1024}, run = {0, 1024};
static int aborted, activated, chip = 9;
static bool hardware_matches = true, hardware_missing;
bool sys_hw_descriptor_matches(const SysHwDescriptor_t *image) {
    (void)image; return hardware_matches;
}
static char project[32] = "frame_film_ark";
static const esp_partition_t *esp_ota_get_next_update_partition(void *p) { (void)p; return &part; }
static const esp_partition_t *esp_ota_get_running_partition(void) { return &run; }
static int esp_ota_abort(int h) { (void)h; aborted++; return 0; }
static int esp_ota_begin(const esp_partition_t *p, uint32_t n, int *h) { (void)p; (void)n; *h = 1; return 0; }
static int esp_ota_write(int h, const void *p, size_t n) { (void)h; (void)p; (void)n; return 0; }
static int esp_ota_end(int h) { (void)h; return 0; }
static int esp_ota_set_boot_partition(const esp_partition_t *p) { (void)p; activated++; return 0; }
static int esp_partition_read(const esp_partition_t *p, int off, void *out, size_t n) {
    (void)p;
    if(off == SYS_HW_DESCRIPTOR_OFFSET) {
        if(hardware_missing) return ESP_FAIL;
        memset(out, 0, n); return ESP_OK;
    }
    ((esp_image_header_t *)out)->chip_id = chip; return 0;
}
static int esp_ota_get_partition_description(const esp_partition_t *p, esp_app_desc_t *d) {
    (void)p; memcpy(d->project_name, project, 32); return 0;
}
static void mbedtls_sha256_init(mbedtls_sha256_context *c) { c->sum = 0; }
static void mbedtls_sha256_free(mbedtls_sha256_context *c) { (void)c; }
static int mbedtls_sha256_starts(mbedtls_sha256_context *c, int f) { (void)f; c->sum = 0; return 0; }
static int mbedtls_sha256_update(mbedtls_sha256_context *c, const uint8_t *p, size_t n) {
    while(n--) c->sum += *p++;
    return 0;
}
static int mbedtls_sha256_finish(mbedtls_sha256_context *c, uint8_t *p) {
    memset(p, 0, 32); p[0] = c->sum; return 0;
}
#include "actual_ota.inc"
int main(void) {
    uint8_t data[] = {1, 2, 3}, sha[32] = {6};
    assert(service_ota_direct_max() == 1024);
    assert(service_ota_direct_activate() == ESP_ERR_INVALID_STATE);
    assert(service_ota_direct_begin(0, sha) == ESP_ERR_INVALID_SIZE);
    assert(service_ota_direct_begin(1025, sha) == ESP_ERR_INVALID_SIZE);
    assert(service_ota_direct_begin(3, sha) == 0);
    assert(service_ota_direct_write(data, 4) == ESP_ERR_INVALID_SIZE);
    assert(service_ota_direct_write(data, 1) == 0 && service_ota_direct_write(data + 1, 2) == 0);
    assert(service_ota_direct_finish() == 0 && activated == 0);
    assert(service_ota_direct_begin(3, sha) == ESP_ERR_INVALID_STATE);
    assert(service_ota_direct_activate() == 0 && activated == 1);
    service_ota_direct_abort();
    assert(service_ota_direct_activate() == ESP_ERR_INVALID_STATE);
    sha[0] = 7;
    assert(service_ota_direct_begin(3, sha) == 0 && service_ota_direct_write(data, 3) == 0);
    assert(service_ota_direct_finish() == ESP_ERR_INVALID_CRC);
    service_ota_direct_abort();
    assert(aborted == 1);
    sha[0] = 6; chip = 8;
    assert(service_ota_direct_begin(3, sha) == 0 && service_ota_direct_write(data, 3) == 0);
    assert(service_ota_direct_finish() == ESP_ERR_INVALID_ARG);
    service_ota_direct_abort();
    chip = 9; memcpy(project, "frame_film_dock", 16);
    assert(service_ota_direct_begin(3, sha) == 0 && service_ota_direct_write(data, 3) == 0);
    assert(service_ota_direct_finish() == ESP_ERR_INVALID_ARG);
    service_ota_direct_abort();
    memcpy(project, "frame_film_ark", 15);
    assert(service_ota_direct_begin(3, sha) == 0 && service_ota_direct_write(data, 2) == 0);
    assert(service_ota_direct_finish() == ESP_ERR_INVALID_CRC);
    service_ota_direct_abort();
    assert(aborted == 2 && activated == 1);
    hardware_matches = false;
    assert(service_ota_direct_begin(3, sha) == 0 && service_ota_direct_write(data, 3) == 0);
    assert(service_ota_direct_finish() == ESP_ERR_INVALID_ARG);
    assert(service_ota_direct_activate() == ESP_ERR_INVALID_STATE && activated == 1);
    service_ota_direct_abort();
    hardware_matches = true; hardware_missing = true;
    assert(service_ota_direct_begin(3, sha) == 0 && service_ota_direct_write(data, 3) == 0);
    assert(service_ota_direct_finish() == ESP_ERR_INVALID_ARG);
    assert(service_ota_direct_activate() == ESP_ERR_INVALID_STATE && activated == 1);
    service_ota_direct_abort();
    puts("PASS: OTA length/digest/target/abort/activate and hardware descriptor rejection");
    return 0;
}
