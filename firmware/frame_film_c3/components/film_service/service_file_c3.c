// SPDX-License-Identifier: GPL-3.0-or-later
// One logical file, two atomic Flash banks. EPD reads mapped Flash; no PSRAM.
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_partition.h"
#include "esp_crc.h"
#include "esp_log.h"
#include "nvs.h"
#include "service_file.h"
#include "service_film.h"
#define BANK_SIZE 0x50000u
#define DATA_OFFSET 0x10000u
#define FILE_BYTES 212000u
#define MAGIC 0x43334646u
typedef struct { uint32_t magic, size, crc; char name[65]; } bank_meta_t;
static const esp_partition_t *partition;
static const uint8_t *mapped[2];
static esp_partition_mmap_handle_t handles[2];
static bank_meta_t meta[2];
static SemaphoreHandle_t mutex;
static int active=-1, receiving=-1;
static uint32_t written, write_crc;
static bool failed;
static nvs_handle_t nvs;
static bool nvs_ready;
void c3_storage_lock(void) {
    if(!mutex) mutex=xSemaphoreCreateMutex();
    configASSERT(mutex);
    xSemaphoreTake(mutex,portMAX_DELAY);
}
void c3_storage_unlock(void) { xSemaphoreGive(mutex); }
static unsigned le16(const uint8_t *p) { return p[0]|((unsigned)p[1]<<8); }
static uint32_t le32(const uint8_t *p) { return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
static bool valid_film(const uint8_t *p) {
    if(!p || le32(p)!=FILE_BYTES-32 || le16(p+4)!=768 || le16(p+6)!=552 ||
       p[8]<2 || p[8]>6 || p[9]!=0 || le16(p+10)>1) return false;
    const uint8_t palette[]={0,255,252,224,3,28};
    for(unsigned i=0;i<p[8];i++) if(p[16+i]!=palette[i]) return false;
    for(unsigned i=32;i<FILE_BYTES;i++) if((p[i]>>4)>=p[8] || (p[i]&15)>=p[8]) return false;
    return true;
}
void service_file_init(void) {
    c3_storage_lock();
    active=-1;receiving=-1;failed=false;written=0;
    for(int i=0;i<2;i++) if(mapped[i]) {
        esp_partition_munmap(handles[i]);mapped[i]=NULL;
    }
    if(nvs_ready) nvs_close(nvs);
    partition=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,0x40,"films");
    nvs_ready=nvs_open("c3_films",NVS_READWRITE,&nvs)==ESP_OK;
    uint8_t selected=255;
    if(nvs_ready) nvs_get_u8(nvs,"active",&selected);
    if(partition && partition->size>=2*BANK_SIZE) {
        for(int i=0;i<2;i++) {
            const void *ptr=NULL;
            if(esp_partition_mmap(partition,i*BANK_SIZE+DATA_OFFSET,FILE_BYTES,
                ESP_PARTITION_MMAP_DATA,&ptr,&handles[i])==ESP_OK) mapped[i]=ptr;
            esp_partition_read(partition,i*BANK_SIZE,&meta[i],sizeof(meta[i]));
        }
        if(selected<2 && meta[selected].magic==MAGIC && meta[selected].size==FILE_BYTES &&
           memchr(meta[selected].name,0,sizeof(meta[selected].name)) && valid_film(mapped[selected]) &&
           esp_crc32_le(0,mapped[selected],FILE_BYTES)==meta[selected].crc) active=selected;
    }
    ESP_LOGI("C3_file","single file store ready=%d active=%d",mapped[0]&&mapped[1]&&nvs_ready,active);
    c3_storage_unlock();
}
void service_file_refresh_list(void) {}
int service_file_save_start(const char *name,uint32_t size) {
    c3_storage_lock();
    receiving=-1; failed=false; written=0;write_crc=0;
    if(!nvs_ready || !partition || !mapped[0] || !mapped[1] || !name || size!=FILE_BYTES ||
       !strlen(name) || strlen(name)>64 || strchr(name,'/') || strchr(name,'\\')) goto error;
    receiving=active==0?1:0;
    if(esp_partition_erase_range(partition,receiving*BANK_SIZE,BANK_SIZE)!=ESP_OK) goto error;
    memset(&meta[receiving],0,sizeof(meta[receiving]));
    memcpy(meta[receiving].name,name,strlen(name)+1);
    c3_storage_unlock();return 0;
error:
    receiving=-1;failed=true;c3_storage_unlock();return -1;
}
int service_file_save_data(const char *name,uint32_t size,uint8_t *data,uint32_t length) {
    (void)name;
    c3_storage_lock();
    int result=-1;
    if(!failed && receiving>=0 && data && length && size==FILE_BYTES && length<=FILE_BYTES-written &&
       esp_partition_write(partition,receiving*BANK_SIZE+DATA_OFFSET+written,data,length)==ESP_OK) {
        written+=length;write_crc=esp_crc32_le(write_crc,data,length);result=0;
    } else failed=true;
    c3_storage_unlock();
    // Upstream BLE transfers ownership of its pvPortMalloc copy to this API.
    free(data);return result;
}
void service_file_save_stop(uint8_t auto_load) {
    c3_storage_lock();
    bool committed=false;
    if(!failed && receiving>=0 && written==FILE_BYTES && valid_film(mapped[receiving]) &&
       esp_crc32_le(0,mapped[receiving],FILE_BYTES)==write_crc) {
        bank_meta_t *m=&meta[receiving];m->magic=MAGIC;m->size=FILE_BYTES;m->crc=write_crc;
        if(esp_partition_write(partition,receiving*BANK_SIZE,m,sizeof(*m))==ESP_OK &&
           nvs_set_u8(nvs,"active",(uint8_t)receiving)==ESP_OK && nvs_commit(nvs)==ESP_OK) {
            active=receiving;committed=true;
        }
    }
    receiving=-1;
    ESP_LOGI("C3_file","save committed=%d bytes=%lu",committed,(unsigned long)written);
    c3_storage_unlock();
    if(committed && auto_load) service_film_display(0);
}
uint32_t service_file_get_count(void) { return active>=0?1:0; }
int service_file_load(uint32_t id) { return id==0 && active>=0?0:-1; }
void service_file_load_next(void) {}
int service_file_get_filename_safe(uint32_t id,char *out,uint32_t capacity) {
    c3_storage_lock();int result=-1;
    if(id==0 && active>=0 && out && capacity>strlen(meta[active].name)) {
        memcpy(out,meta[active].name,strlen(meta[active].name)+1);result=0;
    }
    c3_storage_unlock();return result;
}
uint8_t *service_file_get_buffer(void) { return active>=0?(uint8_t *)mapped[active]:NULL; }
uint32_t service_file_get_buffer_size(void) { return active>=0?FILE_BYTES:0; }
uint32_t service_file_get_current_id(void) { return 0; }
uint8_t service_file_get_load_complete(void) { return active>=0?FILE_LOAD_STATE_DONE:FILE_LOAD_STATE_NONE; }
uint32_t service_file_get_size(uint32_t id) { return id==0 && active>=0?FILE_BYTES:0; }
int service_file_delete(uint32_t id) {
    c3_storage_lock();int result=-1;
    if(id==0 && active>=0 && nvs_set_u8(nvs,"active",255)==ESP_OK && nvs_commit(nvs)==ESP_OK) {
        active=-1;result=0;
    }
    c3_storage_unlock();return result;
}
