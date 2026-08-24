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
 * FileName : /film_hal/src/hal_epd_370.c
 * Author: kiritro  Version: v0.1  Date: 2026/8/24
 * Description: SE版 3.7寸 720x480 六色 EPD 驱动（占位，待实现）
 * ChangeLog: Change Notes
 *
 *********************************************************************/

/*********************************************************************
 * INCLUDES
 */
#include "hal_epd.h"
#if EPD_SELECT_E6_3_70_720_480 == 1
#include "sys_log.h"

/*********************************************************************
 * MACROS
 */
#define EPD_TAG_370                       "HAL_EPD_370"

// TODO: 以下为 3.7寸 720x480 屏的寄存器/时序参数，待根据数据手册补全

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

void hal_epd_init(void)
{
    sys_logw(EPD_TAG_370, "SE 3.7in 720x480 EPD driver not implemented yet");
}

void hal_epd_deinit(void)
{
}

void hal_epd_display_init(void)
{
}

void hal_epd_display_white(void)
{
}

void hal_epd_display_black(void)
{
}

void hal_epd_display_yellow(void)
{
}

void hal_epd_display_red(void)
{
}

void hal_epd_display_blue(void)
{
}

void hal_epd_display_green(void)
{
}

void hal_epd_display_pic(const unsigned char* picData)
{
}

void hal_epd_display_film(const unsigned char* filmData)
{
}

int32_t hal_epd_partial_update(uint8_t panel, uint16_t x_start, uint16_t y_start,
                               uint16_t width, uint16_t height,
                               const unsigned char* data, uint8_t display_enable)
{
    return -1;  // 未实现
}

void hal_epd_sleep(void)
{
}

void hal_epd_pwroff(void)
{
}

#endif /* EPD_SELECT_E6_3_70_720_480 */
