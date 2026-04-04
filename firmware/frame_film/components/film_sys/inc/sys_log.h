#ifndef __SYS_LOG_H__
#define __SYS_LOG_H__


/*********************************************************************
 * INCLUDES
 */
#include "esp_log.h"
#include "sys_err.h"
#include "sys_build.h"

/*********************************************************************
 * CPPMIX
 */
#ifdef __cplusplus
extern "C" {
#endif

/*********************************************************************
 * MACROS
 */
// 日志级别
#define SYS_LOG_LEVEL_NONE        (0)
#define SYS_LOG_LEVEL_ERROR       (1)
#define SYS_LOG_LEVEL_WARN        (2)
#define SYS_LOG_LEVEL_INFO        (3)
#define SYS_LOG_LEVEL_DEBUG       (4)

// 编译选项
#ifndef SYS_RELEASE
#define SYS_LOG_LEVEL             SYS_LOG_LEVEL_INFO   // 日志级别，可根据需要调整
#else
#define SYS_LOG_LEVEL             SYS_LOG_LEVEL_NONE   // 日志级别，可根据需要调整
#endif

// 日志输出宏定义
#ifndef LOG_USE_PRINTF
#define sys_loge(tag, format, ...) if (SYS_LOG_LEVEL >= SYS_LOG_LEVEL_ERROR) { ESP_LOGE(tag, "<ERROR> %s[%d]: " format, __FUNCTION__, __LINE__, ## __VA_ARGS__); }
#define sys_logw(tag, format, ...) if (SYS_LOG_LEVEL >= SYS_LOG_LEVEL_WARN)  { ESP_LOGW(tag, "<WARN> %s[%d]: " format, __FUNCTION__, __LINE__, ## __VA_ARGS__); }
#define sys_logi(tag, format, ...) if (SYS_LOG_LEVEL >= SYS_LOG_LEVEL_INFO)  { ESP_LOGI(tag, "<INFO> %s[%d]: " format, __FUNCTION__, __LINE__, ## __VA_ARGS__); }
#define sys_logd(tag, format, ...) if (SYS_LOG_LEVEL >= SYS_LOG_LEVEL_DEBUG) { ESP_LOGD(tag, "<DEBUG> %s[%d]: " format, __FUNCTION__, __LINE__, ## __VA_ARGS__); }
#else
#define sys_loge(tag, format, ...) if (SYS_LOG_LEVEL >= SYS_LOG_LEVEL_ERROR) { printf("<ERROR> %s %s[%d]: " format, tag, __FUNCTION__, __LINE__, ## __VA_ARGS__); printf("\n"); }
#define sys_logw(tag, format, ...) if (SYS_LOG_LEVEL >= SYS_LOG_LEVEL_WARN)  { printf("<WARN> %s %s[%d]: " format, tag, __FUNCTION__, __LINE__, ## __VA_ARGS__); printf("\n"); }
#define sys_logi(tag, format, ...) if (SYS_LOG_LEVEL >= SYS_LOG_LEVEL_INFO)  { printf("<INFO> %s %s[%d]: " format, tag, __FUNCTION__, __LINE__, ## __VA_ARGS__); printf("\n"); }
#define sys_logd(tag, format, ...) if (SYS_LOG_LEVEL >= SYS_LOG_LEVEL_DEBUG) { printf("<DEBUG> %s %s[%d]: " format, tag, __FUNCTION__, __LINE__, ## __VA_ARGS__); printf("\n"); }
#endif

/*********************************************************************
* TYPEDEFS
*/


/*********************************************************************
 * CONSTANTS
 */


/*********************************************************************
 * LOCAL VARIABLES
 */


/*********************************************************************
 * GLOBAL VARIABLES
 */


/*********************************************************************
 * LOCAL FUNCTIONS
 */


/*********************************************************************
 * GLOBAL FUNCTIONS
 */



#ifdef __cplusplus
}
#endif

#endif /* __SYS_LOG_H__ */
