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
 * FileName : /film_service/src/service_init.c
 * Author: Kiritro  Version: v0.1  Date: 2026/4/16
 * Description: Function introduction
 * ChangeLog: Change Notes
 *
 *********************************************************************/

/*********************************************************************
 * INCLUDES
 */
#include "service_init.h"
#include "service_ble.h"
#include "service_param.h"
#include "service_monitor.h"
#include "service_file.h"
#include "service_film.h"


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



void service_init(void)
{
    // 初始化服务参数
    service_param_init();
    // 初始化ble服务
    service_ble_init();
    // 初始化监控服务
    service_monitor_init();
    // 初始化文件服务
    service_file_init();
    // 初始化照片服务
    service_film_init();
}

