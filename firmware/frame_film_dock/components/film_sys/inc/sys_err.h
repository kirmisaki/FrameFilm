#ifndef __SYS_ERR_H__
#define __SYS_ERR_H__


/*********************************************************************
 * INCLUDES
 */
#include "esp_log.h"

/*********************************************************************
 * CPPMIX
 */
#ifdef __cplusplus
extern "C" {
#endif

/*********************************************************************
 * MACROS
 */
// 错误码定义
#define SYS_OK        (0)
#define SYS_ERR       (-1)

// 错误检查宏定义
#ifndef ERROR_CHECK_CLOSE
#define SYS_ERROR_CHECK(x)  ESP_ERROR_CHECK(x)
#else
#define SYS_ERROR_CHECK(x)  {const uint32_t LOCAL_ERR_CODE = (x); if (LOCAL_ERR_CODE != 0) {ESP_LOGE("ERRCHECK", "error code:0x%x",x);}}
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

#endif /* __SYS_ERR_H__ */
