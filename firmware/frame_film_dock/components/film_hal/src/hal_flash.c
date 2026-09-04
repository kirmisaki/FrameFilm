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
 * FileName : /film_hal/src/hal_flash.c
 * Author: Kiritro  Version: v0.1  Date: 2026/9/4
 * Description: 板载Flash(SPIFFS)存储挂载
 * ChangeLog: Change Notes
 *
 *********************************************************************/

/*********************************************************************
 * INCLUDES
 */
#include <stdio.h>
#include <string.h>

#include "esp_spiffs.h"

#include "sys_log.h"
#include "hal_flash.h"

/*********************************************************************
 * MACROS
 */
#define FLASH_TAG           "HAL_FLASH"

#define FLASH_PART_LABEL    "storage"
#define FLASH_MAX_FILES     (8)

/*********************************************************************
* TYPEDEFS
*/


/*********************************************************************
 * CONSTANTS
 */


/*********************************************************************
 * LOCAL VARIABLES
 */
static uint8_t flash_mount_status = 0;

/*********************************************************************
 * GLOBAL VARIABLES
 */


/*********************************************************************
 * LOCAL FUNCTIONS
 */


/*********************************************************************
 * GLOBAL FUNCTIONS
 */

void hal_flash_init(void)
{
    esp_vfs_spiffs_conf_t conf =
    {
        .base_path = FLASH_MOUNT_POINT,
        .partition_label = FLASH_PART_LABEL,
        .max_files = FLASH_MAX_FILES,
        .format_if_mount_failed = true,
    };

    sys_logi(FLASH_TAG, "Mounting SPIFFS partition: %s", FLASH_PART_LABEL);
    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if(ret != ESP_OK)
    {
        sys_loge(FLASH_TAG, "Failed to mount SPIFFS (%s)", esp_err_to_name(ret));
        flash_mount_status = FLASH_UNMOUNT;
        return;
    }

    size_t total = 0, used = 0;
    if(esp_spiffs_info(FLASH_PART_LABEL, &total, &used) == ESP_OK)
    {
        sys_logi(FLASH_TAG, "SPIFFS mounted, total: %d KB, used: %d KB",
                 (int)(total / 1024), (int)(used / 1024));
    }
    flash_mount_status = FLASH_MOUNT;
}

int hal_flash_get_status(void)
{
    return flash_mount_status;
}
