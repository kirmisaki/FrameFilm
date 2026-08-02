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
 * FileName : /film_hal/src/hal_init.c
 * Author: Kiritro  Version: v0.1  Date: 2026/4/1
 * Description: Function introduction
 * ChangeLog: Change Notes
 *
 *********************************************************************/

/*********************************************************************
 * INCLUDES
 */
#include "hal_sd.h"
#include "hal_bat.h"
#include "hal_led.h"
#include "hal_pwr.h"
#include "hal_epd.h"
#include "hal_input.h"
#include "hal_init.h"

#include "sys_log.h"

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


void film_hal_init(void)
{
    // 初始化电源
    hal_pwr_init();
    // 初始化电池
    hal_bat_init();
    // 初始化RGB LED
    hal_led_init();
    // 初始化SD卡
    hal_sd_init();
    // 初始化输入设备
    hal_input_init();
    // 初始化EPD
    hal_epd_init();
}

