/***********************************************************
 *
 * MIT License
 *
 * Copyright (c) 2025 kiritro
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 *
 * FileName : /film_hal/inc/hal_pwr.h
 * Author: Kiritro  Version: v0.1  Date: 2025/4/4
 * Description: Power management HAL layer interface
 * ChangeLog: Change Notes
 *
***********************************************************/

#ifndef __HAL_PWR_H__
#define __HAL_PWR_H__


/*********************************************************************
 * INCLUDES
 */
#include <stdbool.h>


/*********************************************************************
 * CPPMIX
 */
#ifdef __cplusplus
extern "C" {
#endif

/*********************************************************************
 * MACROS
 */


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
/**
 * @brief 初始化电源管理模块
 * 
 * 该函数用于初始化电源管理模块，配置唤醒源和相关参数。
 * 通常在系统启动时调用此函数。
 */
extern void hal_pwr_init(void);

/**
 * @brief 进入深度睡眠模式
 * 
 * 该函数使设备进入深度睡眠模式以节省电量。
 * 在深度睡眠模式下，大部分外设将被关闭，仅保留RTC和唤醒源。
 * 设备可通过配置的唤醒源（如按键）唤醒。
 */
extern void hal_pwr_enter_sleep(void);

/**
 * @brief 检查唤醒原因
 * 
 * 该函数用于检查设备是否从深度睡眠中唤醒，并返回唤醒原因。
 * 
 * @return bool true-从深度睡眠唤醒，false-正常启动
 */
extern bool hal_pwr_check_wakeup(void);


#ifdef __cplusplus
}
#endif

#endif /* __HAL_PWR_H__ */
