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
 * Description: Function introduction
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
#include <string.h>
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

#define PIN_NUM_CLK             (40)
#define PIN_NUM_CMD             (41)
#define PIN_NUM_D0              (39)
#define PIN_NUM_D1              (38)
#define PIN_NUM_D2              (2)
#define PIN_NUM_D3              (42)
#define PIN_NUM_DET             (45)
#define GPIO_INPUT_PIN_SEL      (1ULL << PIN_NUM_DET)

#define SD_USE_SDNAND           1


/*********************************************************************
* TYPEDEFS
*/


/*********************************************************************
 * CONSTANTS
 */


/*********************************************************************
 * LOCAL VARIABLES
 */
#if SD_USE_SDNAND == 0
static QueueHandle_t gpio_evt_queue = NULL;
#endif
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
#if SD_USE_SDNAND == 0
static void sd_det_init(void);
static void gpio_isr_handler(void *arg);
static void sd_check_task(void *arg);
#endif

/*********************************************************************
 * GLOBAL FUNCTIONS
 */



void hal_sd_init(void)
{
#if SD_USE_SDNAND
    sd_mount();
#else
    sd_det_init();
    int io_level = gpio_get_level(PIN_NUM_DET);
    if(!io_level)
    {
        sd_mount();
    }
#endif
}

int hal_sd_get_status(void)
{
    return sd_mount_status;
}

#if SD_USE_SDNAND == 0
static void IRAM_ATTR gpio_isr_handler(void *arg)
{
    uint32_t gpio_num = (uint32_t) arg;
    xQueueSendFromISR(gpio_evt_queue, &gpio_num, NULL);
}

static void sd_check_task(void *arg)
{
    uint32_t io_num;
    for(;;)
    {
        if(xQueueReceive(gpio_evt_queue, &io_num, portMAX_DELAY))
        {
            // sys_logi(TF_TAG, "GPIO[%"PRIu32"] intr, val: %d\n", io_num, gpio_get_level(io_num));

            int io_level = gpio_get_level(io_num);

            if(io_level == 0 && sd_mount_status == SD_UNMOUNT)
            {
                sys_logi(TF_TAG, "MOUNT TF");
                sd_mount();
            }

            if(io_level == 1 && sd_mount_status == SD_MOUNT)
            {
                sys_logi(TF_TAG, "UNMOUNT TF");
                sd_unmount();
            }

            vTaskDelay(100);
        }
    }
}

static void sd_det_init(void)
{
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_POSEDGE;
    io_conf.pin_bit_mask = GPIO_INPUT_PIN_SEL;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_down_en = 0;
    io_conf.pull_up_en = 1;
    gpio_config(&io_conf);

    gpio_evt_queue = xQueueCreate(10, sizeof(uint32_t));
    xTaskCreate(sd_check_task, "sd_check_task", 4096, NULL, 2, NULL);

    gpio_install_isr_service(0);
    gpio_isr_handler_add(PIN_NUM_DET, gpio_isr_handler, (void *) PIN_NUM_DET);
}
#endif

static void sd_mount(void)
{
    esp_err_t ret;
    esp_vfs_fat_sdmmc_mount_config_t mount_config =
    {
#if SD_USE_SDNAND
        .format_if_mount_failed = true,
#else
        .format_if_mount_failed = false,
#endif
        .max_files = 5,
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

    gpio_isr_handler_remove(PIN_NUM_DET);

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

