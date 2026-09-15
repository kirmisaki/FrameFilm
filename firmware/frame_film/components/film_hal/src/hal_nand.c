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
 * FileName : /film_hal/src/hal_nand.c
 * Author: Kiritro  Version: v0.1  Date: 2026/9/15
 * Description: SPI NAND（W25N01G）存储驱动，复用 SD NAND 走线，对外仍提供 hal_sd_* 接口
 * ChangeLog: Change Notes
 *
 *********************************************************************/

/*********************************************************************
 * INCLUDES
 */
#include "sys_cfg.h"

#if FRAMEFILM_STORAGE_SPINAND == 1

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_vfs_fat.h"

#include "spi_nand_flash.h"
#include "esp_vfs_fat_nand.h"

#include "sys_log.h"
#include "hal_sd.h"

/*********************************************************************
 * MACROS
 */
#define NAND_TAG                    "HAL_NAND"

// W25N01G 复用 SD NAND 的走线，按 SPI 模式接线：
// CLK <-> CLK, DI(MOSI) <-> CMD, DO(MISO) <-> DAT0, CS <-> DAT3
#if FRAMEFILM_MAX == 1
#define PIN_NUM_CLK                 (8)
#define PIN_NUM_MOSI                (3)
#define PIN_NUM_MISO                (5)
#define PIN_NUM_CS                  (15)
#else
#define PIN_NUM_CLK                 (40)
#define PIN_NUM_MOSI                (41)
#define PIN_NUM_MISO                (39)
#define PIN_NUM_CS                  (42)
#endif

// 墨水屏已占用 SPI2，SPI NAND 使用 SPI3
#define NAND_SPI_HOST               SPI3_HOST
#define NAND_SPI_FREQ_HZ            (20 * 1000 * 1000)

#define NAND_ALLOCATION_UNIT_SIZE   (16 * 1024)
#define NAND_MAX_FILES              (5)

/*********************************************************************
* TYPEDEFS
*/


/*********************************************************************
 * CONSTANTS
 */


/*********************************************************************
 * LOCAL VARIABLES
 */
static uint8_t nand_mount_status = 0;
static spi_device_handle_t nand_spi = NULL;
static spi_nand_flash_device_t *nand_dev = NULL;

/*********************************************************************
 * GLOBAL VARIABLES
 */


/*********************************************************************
 * LOCAL FUNCTIONS
 */
static int nand_device_init(void);
static void nand_device_deinit(void);
static int nand_mount(void);
static void nand_unmount(void);

/*********************************************************************
 * GLOBAL FUNCTIONS
 */

void hal_sd_init(void)
{
    if (nand_device_init() != 0)
    {
        return;
    }

    nand_mount();
}

int hal_sd_get_status(void)
{
    return nand_mount_status;
}

void hal_sd_deinit(void)
{
    if (nand_mount_status == SD_MOUNT)
    {
        nand_unmount();
    }

    nand_device_deinit();

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << PIN_NUM_CLK) | (1ULL << PIN_NUM_MOSI) |
                        (1ULL << PIN_NUM_MISO) | (1ULL << PIN_NUM_CS),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    sys_logi(NAND_TAG, "SPI NAND deinitialized");
}

int hal_sd_format(void)
{
    if (nand_mount_status != SD_MOUNT)
    {
        sys_loge(NAND_TAG, "SPI NAND not mounted, cannot format");
        return -1;
    }

    sys_logi(NAND_TAG, "Formatting SPI NAND...");

    // 卸载文件系统后再全片擦除，擦除过程会同时清空驱动的磨损均衡映射
    esp_vfs_fat_nand_unmount(MOUNT_POINT, nand_dev);
    nand_mount_status = SD_UNMOUNT;

    esp_err_t ret = spi_nand_erase_chip(nand_dev);
    if (ret != ESP_OK)
    {
        sys_loge(NAND_TAG, "Erase chip failed: %s", esp_err_to_name(ret));
        nand_mount();
        return -1;
    }

    // 重建句柄，让磨损均衡层基于空片重新初始化
    nand_device_deinit();

    if (nand_device_init() != 0 || nand_mount() != 0)
    {
        sys_loge(NAND_TAG, "Format SPI NAND failed");
        return -1;
    }

    sys_logi(NAND_TAG, "SPI NAND formatted successfully");
    return 0;
}

static int nand_device_init(void)
{
    if (nand_dev != NULL)
    {
        return 0;
    }

    esp_err_t ret;
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = PIN_NUM_MOSI,
        .miso_io_num = PIN_NUM_MISO,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4096 * 2,
    };

    ret = spi_bus_initialize(NAND_SPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK)
    {
        sys_loge(NAND_TAG, "SPI bus init failed: %s", esp_err_to_name(ret));
        return -1;
    }

    // SPI NAND 使用半双工模式
    spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = NAND_SPI_FREQ_HZ,
        .mode = 0,
        .spics_io_num = PIN_NUM_CS,
        .queue_size = 10,
        .flags = SPI_DEVICE_HALFDUPLEX,
    };

    ret = spi_bus_add_device(NAND_SPI_HOST, &dev_cfg, &nand_spi);
    if (ret != ESP_OK)
    {
        sys_loge(NAND_TAG, "Add SPI device failed: %s", esp_err_to_name(ret));
        spi_bus_free(NAND_SPI_HOST);
        return -1;
    }

    spi_nand_flash_config_t nand_cfg = {
        .device_handle = nand_spi,
        .io_mode = SPI_NAND_IO_MODE_SIO,
        .flags = SPI_DEVICE_HALFDUPLEX,
    };

    ret = spi_nand_flash_init_device(&nand_cfg, &nand_dev);
    if (ret != ESP_OK)
    {
        sys_loge(NAND_TAG, "SPI NAND init failed: %s", esp_err_to_name(ret));
        spi_bus_remove_device(nand_spi);
        nand_spi = NULL;
        spi_bus_free(NAND_SPI_HOST);
        return -1;
    }

    uint32_t block_size = 0;
    uint32_t block_num = 0;
    spi_nand_flash_get_block_size(nand_dev, &block_size);
    spi_nand_flash_get_block_num(nand_dev, &block_num);
    sys_logi(NAND_TAG, "SPI NAND W25N01G initialized, %u KB", (unsigned)((block_size * block_num) / 1024));

    return 0;
}

static void nand_device_deinit(void)
{
    if (nand_dev != NULL)
    {
        spi_nand_flash_deinit_device(nand_dev);
        nand_dev = NULL;
    }

    if (nand_spi != NULL)
    {
        spi_bus_remove_device(nand_spi);
        nand_spi = NULL;
    }

    spi_bus_free(NAND_SPI_HOST);
}

static int nand_mount(void)
{
    esp_vfs_fat_mount_config_t mount_config = {
        .format_if_mount_failed = true,
        .max_files = NAND_MAX_FILES,
        .allocation_unit_size = NAND_ALLOCATION_UNIT_SIZE,
    };

    sys_logi(NAND_TAG, "Mounting filesystem at %s", MOUNT_POINT);

    esp_err_t ret = esp_vfs_fat_nand_mount(MOUNT_POINT, nand_dev, &mount_config);
    if (ret != ESP_OK)
    {
        sys_loge(NAND_TAG, "Mount filesystem failed: %s", esp_err_to_name(ret));
        nand_mount_status = SD_UNMOUNT;
        return -1;
    }

    uint64_t bytes_total = 0;
    uint64_t bytes_free = 0;
    if (esp_vfs_fat_info(MOUNT_POINT, &bytes_total, &bytes_free) == ESP_OK)
    {
        sys_logi(NAND_TAG, "FATFS: %u KB total, %u KB free",
                 (unsigned)(bytes_total / 1024), (unsigned)(bytes_free / 1024));
    }

    sys_logi(NAND_TAG, "Filesystem mounted");
    nand_mount_status = SD_MOUNT;

    return 0;
}

static void nand_unmount(void)
{
    esp_vfs_fat_nand_unmount(MOUNT_POINT, nand_dev);
    nand_mount_status = SD_UNMOUNT;

    sys_logi(NAND_TAG, "Filesystem unmounted");
}

#endif /* FRAMEFILM_STORAGE_SPINAND */
