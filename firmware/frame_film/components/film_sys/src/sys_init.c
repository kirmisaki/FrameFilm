/*********************************************************************
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Copyright (c) 2026 kiritro
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 *
 * FileName : /film_sys/src/sys_init.c
 * Author: Kiritro  Version: v0.1  Date: 2026/4/16
 * Description: Function introduction
 * ChangeLog: Change Notes
 *
 *********************************************************************/

/*********************************************************************
 * INCLUDES
 */
#include "sys_log.h"
#include "sys_init.h"
#include "sys_event.h"

#include "hal_init.h"
#include "service_init.h"
#include "app_init.h"

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



void film_sys_init(void)
{
    // 全局事件总线（须先于各层发布/订阅）
    sys_event_init();
    // 抽象层初始化
    film_hal_init();
    // 服务层初始化
    film_service_init();
    // 应用层初始化（调度器 + 默认进入图片 app）
    film_app_init();
}
