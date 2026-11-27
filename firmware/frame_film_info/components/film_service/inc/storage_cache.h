#ifndef __STORAGE_CACHE_H__
#define __STORAGE_CACHE_H__

/*********************************************************************
 * INCLUDES
 */
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*********************************************************************
 * MACROS
 */
#define STORAGE_CACHE_ROOT          "/sdcard/info"
#define STORAGE_CACHE_PAGES_DIR     "/sdcard/info/pages"
#define STORAGE_CACHE_LAST_FRAME    "/sdcard/info/pages/last.bin"

/*********************************************************************
 * GLOBAL FUNCTIONS
 */

/** 初始化缓存目录（SD 未挂载时安全返回） */
void storage_cache_init(void);

/** SD 是否已挂载可用 */
bool storage_cache_is_ready(void);

/**
 * @brief 保存最后一帧（用于下次差分基准）
 *
 * @param frame    800×480 1bpp 位图
 * @param size     位图字节数
 * @param version  对应的屏幕版本
 * @param page_id  页面编号
 */
esp_err_t storage_cache_save_last_frame(const uint8_t *frame, uint32_t size,
                                        uint32_t version, uint16_t page_id);

/**
 * @brief 读取最后一帧
 *
 * @param frame       输出缓冲
 * @param cap         缓冲容量
 * @param out_size    实际位图字节数
 * @param out_version 版本
 * @param out_page_id 页面编号
 * @return ESP_OK 成功；ESP_ERR_NOT_FOUND 无缓存；其他失败
 */
esp_err_t storage_cache_load_last_frame(uint8_t *frame, uint32_t cap,
                                        uint32_t *out_size, uint32_t *out_version,
                                        uint16_t *out_page_id);

#ifdef __cplusplus
}
#endif

#endif /* __STORAGE_CACHE_H__ */
