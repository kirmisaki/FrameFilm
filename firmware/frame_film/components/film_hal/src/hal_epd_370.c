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
 * Description: SE版 3.7寸 720x480 六色 EPD 驱动（JD7601, GDEH037E01），
 *              基于官方参考驱动（MSP430 版 + ESP-IDF 版）移植
 * ChangeLog: Change Notes
 *
 *********************************************************************/

/*********************************************************************
 * INCLUDES
 */
#include "hal_epd.h"
#if EPD_SELECT_E6_3_70_720_480 == 1
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

#include "sys_log.h"

/*********************************************************************
 * MACROS
 */
// JD7601 命令（参考 GDEH037E01 官方驱动）
#define SOFT_START                    0xE9  // 软启动（初始化）
#define POF                           0x02  // Power off
#define PON                           0x04  // Power on
#define DSLP                          0x07  // Deep sleep
#define DTM1                          0x10  // 数据传输 1（写入帧数据）
#define REF                           0x12  // Display refresh

#define EPD_INPUT_LINE                (EPD_WIDTH / 2)  // 每行字节数（4bpp，720/2=360）

// BUSY 等待超时（ms）
#define BUSY_TIMEOUT_INIT_MS          (15000)
#define BUSY_TIMEOUT_POWER_MS         (60000)
#define BUSY_TIMEOUT_REFRESH_MS       (120000)
#define BUSY_TIMEOUT_POLARITY_MS      (3000)

#define FILM_HEADER_SIZE              (32)
#define FILM_COLOR_TABLE_SIZE         (16)
#define FILM_OFFSET_FILESIZE          (0x00)
#define FILM_OFFSET_SCREENWIDTH       (0x04)
#define FILM_OFFSET_SCREENHEIGHT      (0x06)
#define FILM_OFFSET_COLORCOUNT        (0x08)
#define FILM_OFFSET_RESERVED          (0x09)
#define FILM_OFFSET_COLORTABLE        (0x10)

/*********************************************************************
* TYPEDEFS
*/
typedef struct
{
    uint32_t fileSize;
    uint16_t screenWidth;
    uint16_t screenHeight;
    uint8_t  colorCount;
    uint8_t  reserved[7];
    uint8_t  colorTable[FILM_COLOR_TABLE_SIZE];
} __attribute__((packed)) FilmHeader;

/*********************************************************************
 * CONSTANTS
 */
// film 颜色表实际值 → JD7601 面板字节（DTM1 数据 4bpp 直写）
// 参考驱动颜色编码：黑0x00 白0x11 黄0x22 红0x33 蓝0x55 绿0x66
static const unsigned char color_lut[256] =
{
    [0x00] = 0x00, // Black
    [0xFF] = 0x11, // White
    [0xFC] = 0x22, // Yellow
    [0xE0] = 0x33, // Red
    [0x03] = 0x55, // Blue
    [0x1C] = 0x66, // Green
};

/*********************************************************************
 * LOCAL VARIABLES
 */
static spi_device_handle_t m_spi_device;
static bool m_busy_active_low = true;   // JD7601 BUSY 低电平表示忙（极性自适应）
static bool m_ignore_busy = false;      // BUSY 极性检测失败后忽略等待

/*********************************************************************
 * LOCAL FUNCTIONS
 */
static void spi_init(void);
static void EPD_W21_WriteCMD(unsigned char command);
static void EPD_W21_WriteDATA(unsigned char datas);
static void EPD_W21_WriteDATA_Bulk(const unsigned char *data, uint32_t len);
static bool epd_wait_busy(uint32_t timeout_ms);
static void epd_try_fix_busy_polarity(void);
static void reset(void);
static esp_err_t film_parse_header(const unsigned char *filmData, FilmHeader *header);
static void epd_display_refresh_seq(void);
static void epd_display_solid(unsigned char color_byte);

/*********************************************************************
 * HARDWARE SPI (half-duplex, 3-wire mode)
 *********************************************************************/

static void spi_init(void)
{
    esp_err_t ret;

    spi_bus_config_t buscfg =
    {
        .miso_io_num = -1,
        .mosi_io_num = EPD_SDIN_PIN,
        .sclk_io_num = EPD_SCK_PIN,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = EPD_INPUT_LINE * 4
    };

    ret = spi_bus_initialize(EPD_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO);
    assert(ret == ESP_OK);

    spi_device_interface_config_t devcfg =
    {
        .clock_speed_hz = 10000000,
        .mode = 0,
        .spics_io_num = -1,
        .queue_size = 1,
        .flags = SPI_DEVICE_HALFDUPLEX | SPI_DEVICE_3WIRE,
    };

    ret = spi_bus_add_device(EPD_SPI_HOST, &devcfg, &m_spi_device);
    assert(ret == ESP_OK);
}

static void EPD_W21_WriteCMD(unsigned char command)
{
    EPD_W21_CS_0;
    EPD_W21_DC_0;
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length = 8;
    t.tx_buffer = &command;
    spi_device_transmit(m_spi_device, &t);
    EPD_W21_CS_1;
}

static void EPD_W21_WriteDATA(unsigned char datas)
{
    EPD_W21_CS_0;
    EPD_W21_DC_1;
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length = 8;
    t.tx_buffer = &datas;
    spi_device_transmit(m_spi_device, &t);
    EPD_W21_CS_1;
}

static void EPD_W21_WriteDATA_Bulk(const unsigned char *data, uint32_t len)
{
    EPD_W21_CS_0;
    EPD_W21_DC_1;
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length = len * 8;
    t.tx_buffer = data;
    spi_device_transmit(m_spi_device, &t);
    EPD_W21_CS_1;
}

/*********************************************************************
 * HELPER FUNCTIONS
 *********************************************************************/

static bool epd_wait_busy(uint32_t timeout_ms)
{
    if (m_ignore_busy)
    {
        return true;
    }

    TickType_t start = xTaskGetTickCount();
    int busy_level = m_busy_active_low ? 0 : 1;

    while (gpio_get_level(EPD_BUSY_PIN) == busy_level)
    {
        vTaskDelay(pdMS_TO_TICKS(1));
        if (((xTaskGetTickCount() - start) * portTICK_PERIOD_MS) > timeout_ms)
        {
            sys_logw(EPD_TAG, "wait busy timeout (active_low=%d)", m_busy_active_low ? 1 : 0);
            return false;
        }
    }
    return true;
}

static void epd_try_fix_busy_polarity(void)
{
    if (epd_wait_busy(BUSY_TIMEOUT_POLARITY_MS))
    {
        sys_logi(EPD_TAG, "BUSY polarity ok (active_low=%d)", m_busy_active_low ? 1 : 0);
        return;
    }

    m_busy_active_low = !m_busy_active_low;
    if (epd_wait_busy(BUSY_TIMEOUT_POLARITY_MS))
    {
        sys_logi(EPD_TAG, "BUSY polarity switched (active_low=%d)", m_busy_active_low ? 1 : 0);
    }
    else
    {
        sys_logw(EPD_TAG, "BUSY polarity detection failed, ignore BUSY and continue");
        m_ignore_busy = true;
    }
}

static void reset(void)
{
    EPD_W21_RST_0;
    vTaskDelay(20 / portTICK_PERIOD_MS);
    EPD_W21_RST_1;
    vTaskDelay(20 / portTICK_PERIOD_MS);
    epd_wait_busy(BUSY_TIMEOUT_INIT_MS);
    vTaskDelay(10 / portTICK_PERIOD_MS);
}

static esp_err_t film_parse_header(const unsigned char *filmData, FilmHeader *header)
{
    if (filmData == NULL || header == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(&header->fileSize, &filmData[FILM_OFFSET_FILESIZE], sizeof(uint32_t));
    memcpy(&header->screenWidth, &filmData[FILM_OFFSET_SCREENWIDTH], sizeof(uint16_t));
    memcpy(&header->screenHeight, &filmData[FILM_OFFSET_SCREENHEIGHT], sizeof(uint16_t));
    memcpy(&header->colorCount, &filmData[FILM_OFFSET_COLORCOUNT], sizeof(uint8_t));
    memcpy(header->reserved, &filmData[FILM_OFFSET_RESERVED], 7);
    memcpy(header->colorTable, &filmData[FILM_OFFSET_COLORTABLE], FILM_COLOR_TABLE_SIZE);

    if (header->colorCount < 2 || header->colorCount > 6)
    {
        sys_loge(EPD_TAG, "Invalid color count: %d (expected 2-6)", header->colorCount);
        return ESP_ERR_INVALID_SIZE;
    }

    uint32_t expectedSize = (EPD_WIDTH * EPD_HEIGHT) / 2;
    if (header->fileSize != expectedSize)
    {
        sys_loge(EPD_TAG, "File size mismatch: %lu (expected %lu)",
                 (unsigned long)header->fileSize, (unsigned long)expectedSize);
        return ESP_ERR_INVALID_SIZE;
    }

    return ESP_OK;
}

// 写完整帧数据（4bpp，DTM1）
static void epd_display_refresh_seq(void)
{
    EPD_W21_WriteCMD(PON);   // 0x04 Power ON
    epd_wait_busy(BUSY_TIMEOUT_POWER_MS);
    vTaskDelay(10 / portTICK_PERIOD_MS);

    EPD_W21_WriteCMD(REF);   // 0x12 Display Refresh
    EPD_W21_WriteDATA(0x00);
    vTaskDelay(10 / portTICK_PERIOD_MS);
    epd_wait_busy(BUSY_TIMEOUT_REFRESH_MS);

    EPD_W21_WriteCMD(POF);   // 0x02 Power OFF
    EPD_W21_WriteDATA(0x00);
    epd_wait_busy(BUSY_TIMEOUT_POWER_MS);
    vTaskDelay(20 / portTICK_PERIOD_MS);
}

static void epd_display_solid(unsigned char color_byte)
{
    unsigned char line[EPD_INPUT_LINE];
    memset(line, color_byte, EPD_INPUT_LINE);

    EPD_W21_WriteCMD(DTM1);
    epd_wait_busy(BUSY_TIMEOUT_INIT_MS);
    for (int row = 0; row < EPD_HEIGHT; row++)
    {
        EPD_W21_WriteDATA_Bulk(line, EPD_INPUT_LINE);
    }

    epd_display_refresh_seq();
}

/*********************************************************************
 * GLOBAL FUNCTIONS
 *********************************************************************/

void hal_epd_init(void)
{
    gpio_config_t io_conf;

    // BUSY: input
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (1ULL << EPD_BUSY_PIN);
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&io_conf);

    // RST, DC, CS: output
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = (1ULL << EPD_RST_PIN) | (1ULL << EPD_DC_PIN) |
                           (1ULL << EPD_CS_PIN);
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&io_conf);

    // Initial control pin states
    EPD_W21_CS_1;
    EPD_W21_DC_1;

    // Init hardware SPI (half-duplex, SCK + SDIN controlled by peripheral)
    spi_init();

    sys_logi(EPD_TAG, "EPD initialized (JD7601, hardware SPI half-duplex)");
}

void hal_epd_display_init(void)
{
    reset();
    epd_try_fix_busy_polarity();

    // JD7601 初始化序列（参考官方驱动）
    EPD_W21_WriteCMD(SOFT_START);  // 0xE9
    EPD_W21_WriteDATA(0x01);
    epd_wait_busy(BUSY_TIMEOUT_INIT_MS);

    sys_logi(EPD_TAG, "EPD display initialized (JD7601)");
}

void hal_epd_display_white(void)
{
    epd_display_solid(EPD_COLOR_WHITE);
}

void hal_epd_display_black(void)
{
    epd_display_solid(EPD_COLOR_BLACK);
}

void hal_epd_display_yellow(void)
{
    epd_display_solid(EPD_COLOR_YELLOW);
}

void hal_epd_display_red(void)
{
    epd_display_solid(EPD_COLOR_RED);
}

void hal_epd_display_blue(void)
{
    epd_display_solid(EPD_COLOR_BLUE);
}

void hal_epd_display_green(void)
{
    epd_display_solid(EPD_COLOR_GREEN);
}

void hal_epd_display_pic(const unsigned char *picData)
{
    unsigned int i, j;
    unsigned char line[EPD_INPUT_LINE];

    if (picData == NULL)
    {
        sys_loge(EPD_TAG, "picData is NULL");
        return;
    }

    // 输入为每像素 1 字节的颜色表值（0x00/0xFF/0xFC/0xE0/0x03/0x1C），转换为 JD7601 面板字节
    // 注意：与 display_film 一致，180° 旋转写入（行倒序 + 行内像素倒序）
    EPD_W21_WriteCMD(DTM1);
    epd_wait_busy(BUSY_TIMEOUT_INIT_MS);
    for (i = 0; i < EPD_HEIGHT; i++)
    {
        for (j = 0; j < EPD_INPUT_LINE; j++)
        {
            line[j] = (color_lut[picData[(EPD_HEIGHT - 1 - i) * EPD_WIDTH + (EPD_WIDTH - 1 - 2 * j)]] << 4)
                      | color_lut[picData[(EPD_HEIGHT - 1 - i) * EPD_WIDTH + (EPD_WIDTH - 2 - 2 * j)]];
        }
        EPD_W21_WriteDATA_Bulk(line, EPD_INPUT_LINE);
    }

    epd_display_refresh_seq();
    sys_logi(EPD_TAG, "Display pic completed");
}

void hal_epd_display_film(const unsigned char *filmData)
{
    FilmHeader header;
    const unsigned char *pixelData;
    unsigned int i, j;
    unsigned char byte;
    unsigned char cc1, cc2;
    unsigned char line[EPD_INPUT_LINE];

    if (filmData == NULL)
    {
        sys_loge(EPD_TAG, "filmData is NULL");
        return;
    }

    if (film_parse_header(filmData, &header) != ESP_OK)
    {
        sys_loge(EPD_TAG, "Failed to parse film header");
        return;
    }

    sys_logi(EPD_TAG, "Film: %ux%u, %d colors, %lu bytes",
             header.screenWidth, header.screenHeight,
             header.colorCount, (unsigned long)header.fileSize);

    pixelData = filmData + FILM_HEADER_SIZE;

    // 颜色转换：colorTable 索引 -> colorTable 实际值 -> JD7601 面板字节（4bpp 直写）
    // 注意：JD7601 面板需 180° 旋转写入（行倒序 + 行内像素倒序）
    EPD_W21_WriteCMD(DTM1);
    epd_wait_busy(BUSY_TIMEOUT_INIT_MS);
    for (i = 0; i < EPD_HEIGHT; i++)
    {
        for (j = 0; j < EPD_INPUT_LINE; j++)
        {
            byte = pixelData[(EPD_HEIGHT - 1 - i) * EPD_INPUT_LINE + (EPD_INPUT_LINE - 1 - j)];
            cc1 = (byte >> 4) & 0x0F;   // 源像素 2k（偶）→ 屏幕右像素（低半字节）
            cc2 = byte & 0x0F;          // 源像素 2k+1（奇）→ 屏幕左像素（高半字节）
            line[j] = (color_lut[header.colorTable[cc2]] << 4)
                      | color_lut[header.colorTable[cc1]];
        }
        EPD_W21_WriteDATA_Bulk(line, EPD_INPUT_LINE);
    }

    epd_display_refresh_seq();
    sys_logi(EPD_TAG, "Film display completed");
}

int32_t hal_epd_partial_update(uint8_t panel, uint16_t x_start, uint16_t y_start,
                               uint16_t width, uint16_t height,
                               const unsigned char* data, uint8_t display_enable)
{
    // JD7601 官方驱动未提供窗口刷新命令，暂不支持局部刷新
    sys_logw(EPD_TAG, "partial update not supported on JD7601");
    return -1;
}

void hal_epd_sleep(void)
{
    epd_wait_busy(BUSY_TIMEOUT_INIT_MS);
    EPD_W21_WriteCMD(DSLP);  // 0x07
    EPD_W21_WriteDATA(0xA5);

    sys_logi(EPD_TAG, "EPD sleep");
}

void hal_epd_pwroff(void)
{
    hal_epd_sleep();
    vTaskDelay(100 / portTICK_PERIOD_MS);

    sys_logi(EPD_TAG, "EPD power off");
}

void hal_epd_deinit(void)
{
    if (m_spi_device != NULL)
    {
        spi_bus_remove_device(m_spi_device);
        m_spi_device = NULL;
    }
    spi_bus_free(EPD_SPI_HOST);

    gpio_config_t io_conf =
    {
        .pin_bit_mask = (1ULL << EPD_BUSY_PIN) | (1ULL << EPD_RST_PIN) |
        (1ULL << EPD_DC_PIN)   | (1ULL << EPD_CS_PIN) |
        (1ULL << EPD_SCK_PIN)  | (1ULL << EPD_SDIN_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    sys_logi(EPD_TAG, "EPD deinit, SPI and GPIOs released");
}
#endif /* EPD_SELECT_E6_3_70_720_480 */
