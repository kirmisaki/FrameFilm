#include "host_sdk.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "service_file.h"
static uint8_t flash[0xa0000], committed=255, staged=255;
static unsigned display_count;
static bool fail_commit,fail_write;
static const esp_partition_t part={sizeof(flash)};
SemaphoreHandle_t xSemaphoreCreateMutex(void) {return (void *)1;}
void xSemaphoreTake(SemaphoreHandle_t h,int timeout) {(void)h;(void)timeout;}
void xSemaphoreGive(SemaphoreHandle_t h) {(void)h;}
const esp_partition_t *esp_partition_find_first(int t,int s,const char *n) {(void)t;(void)s;(void)n;return &part;}
int esp_partition_mmap(const esp_partition_t *p,size_t o,size_t n,int m,const void **v,int *h) {
 (void)p;(void)m;assert(o+n<=sizeof(flash));*v=flash+o;*h=(int)o;return 0;
}
void esp_partition_munmap(int h) {(void)h;}
int esp_partition_read(const esp_partition_t *p,size_t o,void *d,size_t n) {(void)p;assert(o+n<=sizeof(flash));memcpy(d,flash+o,n);return 0;}
int esp_partition_write(const esp_partition_t *p,size_t o,const void *d,size_t n) {
 (void)p;if(fail_write)return -1;assert(o+n<=sizeof(flash));memcpy(flash+o,d,n);return 0;
}
int esp_partition_erase_range(const esp_partition_t *p,size_t o,size_t n) {(void)p;assert(o+n<=sizeof(flash));memset(flash+o,255,n);return 0;}
uint32_t esp_crc32_le(uint32_t crc,const uint8_t *data,uint32_t n) {
 crc=~crc;for(uint32_t i=0;i<n;i++){crc^=data[i];for(int k=0;k<8;k++)crc=(crc>>1)^(0xedb88320u & (0u-(crc&1)));}return ~crc;
}
int nvs_open(const char *n,int mode,nvs_handle_t *h) {(void)n;(void)mode;*h=1;return 0;}
void nvs_close(nvs_handle_t h) {(void)h;staged=committed;}
int nvs_get_u8(nvs_handle_t h,const char *k,uint8_t *v) {(void)h;(void)k;*v=committed;return 0;}
int nvs_set_u8(nvs_handle_t h,const char *k,uint8_t v) {(void)h;(void)k;staged=v;return 0;}
int nvs_commit(nvs_handle_t h) {(void)h;if(fail_commit)return -1;committed=staged;return 0;}
void service_film_display(uint32_t id) {assert(id==0);display_count++;}
static uint8_t film[212000];
static void make_film(uint8_t pixels) {
 memset(film,0,sizeof(film));film[1]=60;film[2]=3;film[5]=3;film[6]=40;film[7]=2;film[8]=4;
 film[16]=0;film[17]=255;film[18]=252;film[19]=224;memset(film+32,pixels,sizeof(film)-32);
}
static void upload(size_t count) {
 assert(service_file_save_start("test.film",sizeof(film))==0);
 for(size_t i=0;i<count;) {
  size_t n=count-i>192?192:count-i;uint8_t *copy=malloc(n);assert(copy);memcpy(copy,film+i,n);
  assert(service_file_save_data("test.film",sizeof(film),copy,(uint32_t)n)==0);i+=n;
 }
}
static void expect(uint8_t pixels) {assert(service_file_get_count()==1);assert(service_file_get_buffer()[32]==pixels);}
int main(void) {
 memset(flash,255,sizeof(flash));service_file_init();assert(service_file_get_count()==0);
 assert(service_file_save_start("../bad.film",212000)<0);
 assert(service_file_save_start("wrong.film",120032)<0);
 make_film(0x23);upload(sizeof(film));service_file_save_stop(1);expect(0x23);assert(display_count==1);
 service_file_init();expect(0x23);
 make_film(0x10);upload(1000);service_file_save_stop(1);expect(0x23);assert(display_count==1);
 make_film(0x44);upload(sizeof(film));service_file_save_stop(1);expect(0x23);
 make_film(0x10);upload(sizeof(film));fail_commit=true;service_file_save_stop(1);fail_commit=false;service_file_init();expect(0x23);
 make_film(0x10);upload(sizeof(film));service_file_save_stop(0);expect(0x10);assert(display_count==1);
 assert(service_file_get_size(0)==212000);assert(service_file_load(1)<0);
 char name[65];assert(service_file_get_filename_safe(0,name,sizeof(name))==0);assert(strcmp(name,"test.film")==0);
 assert(service_file_get_filename_safe(0,name,2)<0);
 assert(service_file_delete(0)==0);service_file_init();assert(service_file_get_count()==0);
 make_film(0x23);upload(sizeof(film));service_file_save_stop(1);expect(0x23);
 assert(service_file_save_start("bad-write.film",sizeof(film))==0);fail_write=true;
 uint8_t *copy=malloc(4);memset(copy,0,4);assert(service_file_save_data("bad-write.film",sizeof(film),copy,4)<0);
 fail_write=false;service_file_save_stop(1);expect(0x23);
 puts("PASS C3 actual store: chunk upload, reboot, short/invalid upload, commit/write failure, silent save, filename and deletion");
 return 0;
}
