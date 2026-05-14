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
 * FileName : /film_sys/src/sys_com.c
 * Author: Kiritro  Version: v0.1  Date: 2026/4/30
 * Description: Common functions
 * ChangeLog: Change Notes
 *
 *********************************************************************/

/*********************************************************************
 * INCLUDES
 */
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "sys_com.h"
#include "sys_log.h"

/*********************************************************************
 * MACROS
 */


/*********************************************************************
* TYPEDEFS
*/
#define COMMON_TAG                    "common"

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


void sys_reboot(void)
{
    sys_logw(COMMON_TAG, "sys restart!");
    esp_restart();
    vTaskDelay(100);
    esp_restart();
}
