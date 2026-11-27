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
 * FileName : /film_hal/src/hal_sd.c
 * Author: Kiritro  Version: v0.1  Date: 2026/4/3
 * Description: TF 卡（4-bit SDMMC）驱动。
 *              FrameFilmCommon 主板没有卡检测脚，插拔状态只能靠挂载结果判断。
 * ChangeLog: Change Notes
 *
 *********************************************************************/

/*********************************************************************
 * INCLUDES
 */
#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"

#include "sys_log.h"
#include "hal_sd.h"

/*********************************************************************
 * MACROS
 */
#define TF_TAG                  "HAL_TF"

/* FrameFilmCommon CARD1：4-bit SDMMC，无检测脚 */
#define PIN_NUM_CLK             (40)
#define PIN_NUM_CMD             (41)
#define PIN_NUM_D0              (39)
#define PIN_NUM_D1              (38)
#define PIN_NUM_D2              (2)
#define PIN_NUM_D3              (42)

/*********************************************************************
* TYPEDEFS
*/


/*********************************************************************
 * CONSTANTS
 */


/*********************************************************************
 * LOCAL VARIABLES
 */
static uint8_t sd_mount_status = 0;
static sdmmc_card_t *card;

/*********************************************************************
 * GLOBAL VARIABLES
 */


/*********************************************************************
 * LOCAL FUNCTIONS
 */
static void sd_mount(void);
static void sd_unmount(void);

/*********************************************************************
 * GLOBAL FUNCTIONS
 */


void hal_sd_init(void)
{
    /* 无卡检测脚：直接尝试挂载（挂载失败时按配置格式化） */
    sd_mount();
}

int hal_sd_get_status(void)
{
    return sd_mount_status;
}

static void sd_mount(void)
{
    esp_err_t ret;
    esp_vfs_fat_sdmmc_mount_config_t mount_config =
    {
        /* 信息屏把 SD 当缓存/素材，挂载失败只降级为无缓存；
         * 不要自动格式化，格式化只能由用户显式触发（BLE 0x2B / App）。 */
        .format_if_mount_failed = false,
        .max_files = 8,
        .allocation_unit_size = 16 * 1024
    };
    const char mount_point[] = MOUNT_POINT;
    sys_logi(TF_TAG, "Initializing SD card");
    sys_logi(TF_TAG, "Using SDMMC peripheral");

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width  = 4;
    slot_config.clk    = PIN_NUM_CLK;
    slot_config.cmd    = PIN_NUM_CMD;
    slot_config.d0     = PIN_NUM_D0;
    slot_config.d1     = PIN_NUM_D1;
    slot_config.d2     = PIN_NUM_D2;
    slot_config.d3     = PIN_NUM_D3;
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    sys_logi(TF_TAG, "Mounting filesystem");
    ret = esp_vfs_fat_sdmmc_mount(mount_point, &host, &slot_config, &mount_config, &card);

    if (ret != ESP_OK)
    {
        if (ret == ESP_FAIL)
        {
            sys_loge(TF_TAG, "Failed to mount filesystem. "
                     "If you want the card to be formatted, set the EXAMPLE_FORMAT_IF_MOUNT_FAILED menuconfig option.");
        }
        else
        {
            sys_loge(TF_TAG, "Failed to initialize the card (%s). "
                     "Make sure SD card lines have pull-up resistors in place.", esp_err_to_name(ret));
        }
        sd_mount_status = SD_UNMOUNT;
        return;
    }
    sys_logi(TF_TAG, "Filesystem mounted");
    sdmmc_card_print_info(stdout, card);
    sd_mount_status = SD_MOUNT;
}

static void sd_unmount(void)
{
    const char mount_point[] = MOUNT_POINT;
    esp_vfs_fat_sdcard_unmount(mount_point, card);
    sys_logi(TF_TAG, "Card unmounted");

    sd_mount_status = SD_UNMOUNT;
}

void hal_sd_deinit(void)
{
    if (sd_mount_status == SD_MOUNT)
    {
        sd_unmount();
    }

    sdmmc_host_deinit();

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << PIN_NUM_CLK) | (1ULL << PIN_NUM_CMD) |
                        (1ULL << PIN_NUM_D0) | (1ULL << PIN_NUM_D1) |
                        (1ULL << PIN_NUM_D2) | (1ULL << PIN_NUM_D3),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    sys_logi(TF_TAG, "SD deinitialized");
}

int hal_sd_format(void)
{
    if (sd_mount_status != SD_MOUNT)
    {
        sys_loge(TF_TAG, "SD card not mounted, cannot format");
        return -1;
    }

    sys_logi(TF_TAG, "Formatting SD card...");
    const char mount_point[] = MOUNT_POINT;
    esp_err_t ret = esp_vfs_fat_sdcard_format(mount_point, card);
    if (ret != ESP_OK)
    {
        sys_loge(TF_TAG, "Failed to format SD card: %s", esp_err_to_name(ret));
        return -1;
    }

    sys_logi(TF_TAG, "SD card formatted successfully");
    return 0;
}
