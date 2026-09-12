#ifndef __SERVICE_USB_H__
#define __SERVICE_USB_H__

#ifdef __cplusplus
extern "C"{
#endif

/*********************************************************************
 * INCLUDES
 */
#include <stdint.h>

#include "sys_cfg.h"

/*********************************************************************
 * MACROS
 */
#define SYS_OS_PRI_USB_TASK            (7)
#define SYS_OS_SIZE_USB_TASK           (4096)
#define SYS_OS_NAME_USB_TASK           "usb_task"

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
#if SYS_FUNC_USB_CDC_EN
/**
 * @brief 初始化USB(CDC)服务
 *
 * 创建收发任务与消息队列，把CDC收发接入统一的命令服务，
 * 使USB与BLE共用同一套命令解析与业务处理。
 */
extern void service_usb_init(void);
#endif /* SYS_FUNC_USB_CDC_EN */


#ifdef __cplusplus
}
#endif

#endif /* __SERVICE_USB_H__ */
