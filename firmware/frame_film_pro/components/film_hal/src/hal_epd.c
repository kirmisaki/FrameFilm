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
 * FileName : /film_hal/src/hal_epd.c
 * Author: Kiritro  Version: v0.1  Date: 2026/3/31
 * Description: Function introduction
 * ChangeLog: Change Notes
 *
 *********************************************************************/

/*********************************************************************
 * INCLUDES
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

#include "hal_epd.h"
#include "sys_log.h"

/*********************************************************************
 * MACROS
 */
#define EPD_WIDTH                         (600)
#define EPD_HEIGHT                        (400)

#define FILM_HEADER_SIZE                  (32)
#define FILM_COLOR_TABLE_SIZE             (16)
#define FILM_OFFSET_FILESIZE              (0x00)
#define FILM_OFFSET_SCREENWIDTH           (0x04)
#define FILM_OFFSET_SCREENHEIGHT          (0x06)
#define FILM_OFFSET_COLORCOUNT            (0x08)
#define FILM_OFFSET_RESERVED              (0x09)
#define FILM_OFFSET_COLORTABLE            (0x10)

/*********************************************************************
* TYPEDEFS
*/
typedef struct {
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
static const unsigned char color_lut[256] = {
    [0x00] = 0x00, // Black
    [0xFF] = 0x01, // White
    [0xFC] = 0x02, // Yellow
    [0xE0] = 0x03, // Red
    [0x03] = 0x05, // Blue
    [0x1C] = 0x06, // Green
};

/*********************************************************************
 * LOCAL VARIABLES
 */
// SPI bus handle
static spi_device_handle_t m_spi_device;

/*********************************************************************
 * GLOBAL VARIABLES
 */

/*********************************************************************
 * LOCAL FUNCTIONS
 */
static void spi_init(void);
static void reset(void);
static void lcd_chkstatus(void);
static void EPD_W21_WriteCMD(unsigned char command);
static void EPD_W21_WriteDATA(unsigned char datas);
static void EPD_W21_WriteDATA_Bulk(const unsigned char *data, uint32_t len);
static unsigned char color_get(unsigned char color_index);
static esp_err_t film_parse_header(const unsigned char *filmData, FilmHeader *header);

/*********************************************************************
 * GLOBAL FUNCTIONS
 */

static void spi_init(void)
{
    esp_err_t ret;
    spi_bus_config_t buscfg =
    {
        .miso_io_num = -1, // Not used
        .mosi_io_num = EPD_SDIN_PIN, // SDIN (SPI2 MOSI)
        .sclk_io_num = EPD_SCK_PIN, // SCK (SPI2 SCLK)
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = EPD_WIDTH * EPD_HEIGHT / 2
    };

    ret = spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
    assert(ret == ESP_OK);

    spi_device_interface_config_t devcfg =
    {
        .clock_speed_hz = 10000000, // 40MHz - ESP32-S3 max stable speed
        .mode = 0, // SPI mode 0
        .spics_io_num = -1, // CS pin handled manually
        .queue_size = 1
    };

    ret = spi_bus_add_device(SPI2_HOST, &devcfg, &m_spi_device);
    assert(ret == ESP_OK);
}

static void reset(void)
{
    //20220330
    //dual reset
    EPD_W21_RST_0; // Reset
    vTaskDelay(30 / portTICK_PERIOD_MS);
    EPD_W21_RST_1;
    vTaskDelay(30 / portTICK_PERIOD_MS);
    EPD_W21_RST_0; // Reset
    vTaskDelay(30 / portTICK_PERIOD_MS);
    EPD_W21_RST_1;
}

static void lcd_chkstatus(void)
{
    while(!isEPD_W21_BUSY);
}

static void EPD_W21_WriteCMD(unsigned char command)
{
    EPD_W21_CS_0;
    EPD_W21_DC_0;  // D/C#   0:command  1:data
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
    EPD_W21_DC_1;  // D/C#   0:command  1:data
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
    EPD_W21_DC_1;  // D/C#   0:command  1:data
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length = len * 8;
    t.tx_buffer = data;
    spi_device_transmit(m_spi_device, &t);
    EPD_W21_CS_1;
}

static inline unsigned char color_get(unsigned char color_index)
{
    return color_lut[color_index];
}

static esp_err_t film_parse_header(const unsigned char *filmData, FilmHeader *header)
{
    if (filmData == NULL || header == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(&header->fileSize, &filmData[FILM_OFFSET_FILESIZE], sizeof(uint32_t));
    memcpy(&header->screenWidth, &filmData[FILM_OFFSET_SCREENWIDTH], sizeof(uint16_t));
    memcpy(&header->screenHeight, &filmData[FILM_OFFSET_SCREENHEIGHT], sizeof(uint16_t));
    memcpy(&header->colorCount, &filmData[FILM_OFFSET_COLORCOUNT], sizeof(uint8_t));
    memcpy(header->reserved, &filmData[FILM_OFFSET_RESERVED], 7);
    memcpy(header->colorTable, &filmData[FILM_OFFSET_COLORTABLE], FILM_COLOR_TABLE_SIZE);

    if (header->colorCount < 2 || header->colorCount > 6) {
        sys_loge(EPD_TAG, "Invalid color count: %d (expected 2-6)", header->colorCount);
        return ESP_ERR_INVALID_SIZE;
    }

    uint32_t expectedSize = (EPD_WIDTH * EPD_HEIGHT) / 2;
    if (header->fileSize != expectedSize) {
        sys_loge(EPD_TAG, "File size mismatch: %u (expected %u)", header->fileSize, expectedSize);
        return ESP_ERR_INVALID_SIZE;
    }

    return ESP_OK;
}

void hal_epd_init(void)
{
    gpio_config_t io_conf;

    // Configure BUSY pin as input
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (1ULL << EPD_BUSY_PIN);
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&io_conf);

    // Configure RES, DC, CS pins as output
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = (1ULL << EPD_RST_PIN) | (1ULL << EPD_DC_PIN) | (1ULL << EPD_CS_PIN);
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&io_conf);

    // Initialize SPI bus
    spi_init();

    sys_logi(EPD_TAG, "EPD hardware initialized");
}

void hal_epd_display_init(void)
{
    reset();
    lcd_chkstatus();
    vTaskDelay(30 / portTICK_PERIOD_MS);

    EPD_W21_WriteCMD(0xAA);
    EPD_W21_WriteDATA(0x49);
    EPD_W21_WriteDATA(0x55);
    EPD_W21_WriteDATA(0x20);
    EPD_W21_WriteDATA(0x08);
    EPD_W21_WriteDATA(0x09);
    EPD_W21_WriteDATA(0x18);

    EPD_W21_WriteCMD(PWR);
    EPD_W21_WriteDATA(0x3F);

    EPD_W21_WriteCMD(PSR);
    EPD_W21_WriteDATA(0x5F);
    EPD_W21_WriteDATA(0x69);


    EPD_W21_WriteCMD(BTST1);
    EPD_W21_WriteDATA(0x40);
    EPD_W21_WriteDATA(0x1F);
    EPD_W21_WriteDATA(0x1F);
    EPD_W21_WriteDATA(0x2C);

    EPD_W21_WriteCMD(BTST3);
    EPD_W21_WriteDATA(0x6F);
    EPD_W21_WriteDATA(0x1F);
    EPD_W21_WriteDATA(0x1F);
    EPD_W21_WriteDATA(0x22);

    //First setting
    EPD_W21_WriteCMD(BTST2);
    EPD_W21_WriteDATA(0x6F);
    EPD_W21_WriteDATA(0x1F);
    EPD_W21_WriteDATA(0x17);
    EPD_W21_WriteDATA(0x17);
    //===================

    EPD_W21_WriteCMD(POFS);
    EPD_W21_WriteDATA(0x00);
    EPD_W21_WriteDATA(0x54);
    EPD_W21_WriteDATA(0x00);
    EPD_W21_WriteDATA(0x44);

    EPD_W21_WriteCMD(TCON);
    EPD_W21_WriteDATA(0x02);
    EPD_W21_WriteDATA(0x00);
    //Please notice that PLL must be set for version 2 IC
    EPD_W21_WriteCMD(PLL);
    EPD_W21_WriteDATA(0x08);


    EPD_W21_WriteCMD(CDI);
    EPD_W21_WriteDATA(0x3F);

    EPD_W21_WriteCMD(TRES);
    EPD_W21_WriteDATA(0x01);
    EPD_W21_WriteDATA(0x90);
    EPD_W21_WriteDATA(0x02);
    EPD_W21_WriteDATA(0x58);

    EPD_W21_WriteCMD(PWS);
    EPD_W21_WriteDATA(0x2F);

    EPD_W21_WriteCMD(T_VDCS);
    EPD_W21_WriteDATA(0x01);

    sys_logi(EPD_TAG, "EPD display initialized");
}

void hal_epd_display_white(void)
{
    unsigned char line_buffer[EPD_WIDTH / 2];
    memset(line_buffer, EPD_COLOR_WHITE, EPD_WIDTH / 2);

    EPD_W21_WriteCMD(DTM);
    for(unsigned long i = 0; i < EPD_HEIGHT; i++)
    {
        EPD_W21_WriteDATA_Bulk(line_buffer, EPD_WIDTH / 2);
    }
    EPD_W21_WriteCMD(PON);
    lcd_chkstatus();

    //Second setting
    EPD_W21_WriteCMD(BTST2);
    EPD_W21_WriteDATA(0x6F);
    EPD_W21_WriteDATA(0x1F);
    EPD_W21_WriteDATA(0x17);
    EPD_W21_WriteDATA(0x27);


    EPD_W21_WriteCMD(DRF);
    EPD_W21_WriteDATA(0x00);
    lcd_chkstatus();

    EPD_W21_WriteCMD(POF);
    EPD_W21_WriteDATA(0x00);
    lcd_chkstatus();
}

void hal_epd_display_black(void)
{
    unsigned char line_buffer[EPD_WIDTH / 2];
    memset(line_buffer, EPD_COLOR_BLACK, EPD_WIDTH / 2);

    EPD_W21_WriteCMD(DTM);
    for(unsigned long i = 0; i < EPD_HEIGHT; i++)
    {
        EPD_W21_WriteDATA_Bulk(line_buffer, EPD_WIDTH / 2);
    }
    EPD_W21_WriteCMD(PON);
    lcd_chkstatus();

    //Second setting
    EPD_W21_WriteCMD(BTST2);
    EPD_W21_WriteDATA(0x6F);
    EPD_W21_WriteDATA(0x1F);
    EPD_W21_WriteDATA(0x17);
    EPD_W21_WriteDATA(0x27);


    EPD_W21_WriteCMD(DRF);
    EPD_W21_WriteDATA(0x00);
    lcd_chkstatus();

    EPD_W21_WriteCMD(POF);
    EPD_W21_WriteDATA(0x00);
    lcd_chkstatus();
}

void hal_epd_display_yellow(void)
{
    unsigned char line_buffer[EPD_WIDTH / 2];
    memset(line_buffer, EPD_COLOR_YELLOW, EPD_WIDTH / 2);

    EPD_W21_WriteCMD(DTM);
    for(unsigned long i = 0; i < EPD_HEIGHT; i++)
    {
        EPD_W21_WriteDATA_Bulk(line_buffer, EPD_WIDTH / 2);
    }
    EPD_W21_WriteCMD(PON);
    lcd_chkstatus();

    //Second setting
    EPD_W21_WriteCMD(BTST2);
    EPD_W21_WriteDATA(0x6F);
    EPD_W21_WriteDATA(0x1F);
    EPD_W21_WriteDATA(0x17);
    EPD_W21_WriteDATA(0x27);


    EPD_W21_WriteCMD(DRF);
    EPD_W21_WriteDATA(0x00);
    lcd_chkstatus();

    EPD_W21_WriteCMD(POF);
    EPD_W21_WriteDATA(0x00);
    lcd_chkstatus();
}

void hal_epd_display_red(void)
{
    unsigned char line_buffer[EPD_WIDTH / 2];
    memset(line_buffer, EPD_COLOR_RED, EPD_WIDTH / 2);

    EPD_W21_WriteCMD(DTM);
    for(unsigned long i = 0; i < EPD_HEIGHT; i++)
    {
        EPD_W21_WriteDATA_Bulk(line_buffer, EPD_WIDTH / 2);
    }
    EPD_W21_WriteCMD(PON);
    lcd_chkstatus();

    //Second setting
    EPD_W21_WriteCMD(BTST2);
    EPD_W21_WriteDATA(0x6F);
    EPD_W21_WriteDATA(0x1F);
    EPD_W21_WriteDATA(0x17);
    EPD_W21_WriteDATA(0x27);


    EPD_W21_WriteCMD(DRF);
    EPD_W21_WriteDATA(0x00);
    lcd_chkstatus();

    EPD_W21_WriteCMD(POF);
    EPD_W21_WriteDATA(0x00);
    lcd_chkstatus();
}

void hal_epd_display_blue(void)
{
    unsigned char line_buffer[EPD_WIDTH / 2];
    memset(line_buffer, EPD_COLOR_BLUE, EPD_WIDTH / 2);

    EPD_W21_WriteCMD(DTM);
    for(unsigned long i = 0; i < EPD_HEIGHT; i++)
    {
        EPD_W21_WriteDATA_Bulk(line_buffer, EPD_WIDTH / 2);
    }
    EPD_W21_WriteCMD(PON);
    lcd_chkstatus();

    //Second setting
    EPD_W21_WriteCMD(BTST2);
    EPD_W21_WriteDATA(0x6F);
    EPD_W21_WriteDATA(0x1F);
    EPD_W21_WriteDATA(0x17);
    EPD_W21_WriteDATA(0x27);


    EPD_W21_WriteCMD(DRF);
    EPD_W21_WriteDATA(0x00);
    lcd_chkstatus();

    EPD_W21_WriteCMD(POF);
    EPD_W21_WriteDATA(0x00);
    lcd_chkstatus();
}

void hal_epd_display_green(void)
{
    unsigned char line_buffer[EPD_WIDTH / 2];
    memset(line_buffer, EPD_COLOR_GREEN, EPD_WIDTH / 2);

    EPD_W21_WriteCMD(DTM);
    for(unsigned long i = 0; i < EPD_HEIGHT; i++)
    {
        EPD_W21_WriteDATA_Bulk(line_buffer, EPD_WIDTH / 2);
    }
    EPD_W21_WriteCMD(PON);
    lcd_chkstatus();

    //Second setting
    EPD_W21_WriteCMD(BTST2);
    EPD_W21_WriteDATA(0x6F);
    EPD_W21_WriteDATA(0x1F);
    EPD_W21_WriteDATA(0x17);
    EPD_W21_WriteDATA(0x27);


    EPD_W21_WriteCMD(DRF);
    EPD_W21_WriteDATA(0x00);
    lcd_chkstatus();

    EPD_W21_WriteCMD(POF);
    EPD_W21_WriteDATA(0x00);
    lcd_chkstatus();
}

void hal_epd_display_pic(const unsigned char *picData)
{
    unsigned int i, j, k;
    unsigned char temp1, temp2;
    unsigned char line_buffer[EPD_WIDTH / 2];

    EPD_W21_WriteCMD(DTM);
    for(i = 0; i < EPD_HEIGHT; i++)
    {
        k = 0;
        for(j = 0; j < EPD_WIDTH / 2; j++)
        {
            temp1 = picData[i * EPD_WIDTH + k++];
            temp2 = picData[i * EPD_WIDTH + k++];
            line_buffer[j] = (color_get(temp1) << 4) | color_get(temp2);
        }
        EPD_W21_WriteDATA_Bulk(line_buffer, EPD_WIDTH / 2);
    }

    //Refresh
    EPD_W21_WriteCMD(PON);
    lcd_chkstatus();

    //Second setting
    EPD_W21_WriteCMD(BTST2);
    EPD_W21_WriteDATA(0x6F);
    EPD_W21_WriteDATA(0x1F);
    EPD_W21_WriteDATA(0x17);
    EPD_W21_WriteDATA(0x27);


    EPD_W21_WriteCMD(DRF);
    EPD_W21_WriteDATA(0x00);
    lcd_chkstatus();

    EPD_W21_WriteCMD(POF);
    EPD_W21_WriteDATA(0x00);
    lcd_chkstatus();         //waiting for the electronic paper IC to release the idle signal
}

void hal_epd_display_film(const unsigned char *filmData)
{
    FilmHeader header;
    const unsigned char *pixelData;
    unsigned int i, j;
    unsigned char byte;
    unsigned char colorCode1, colorCode2;
    unsigned char color1, color2;
    unsigned char line_buffer[EPD_WIDTH / 2];

    if (filmData == NULL) {
        sys_loge(EPD_TAG, "filmData is NULL");
        return;
    }

    if (film_parse_header(filmData, &header) != ESP_OK) {
        sys_loge(EPD_TAG, "Failed to parse film header");
        return;
    }

    sys_logi(EPD_TAG, "Film info: %ux%u, %d colors, %u bytes",
             header.screenWidth, header.screenHeight,
             header.colorCount, header.fileSize);

    sys_logi(EPD_TAG, "Color table:");
    for (int k = 0; k < 16; k++) {
        sys_logi(EPD_TAG, "  [%d] = 0x%02X", k, header.colorTable[k]);
    }

    pixelData = filmData + FILM_HEADER_SIZE;

    EPD_W21_WriteCMD(DTM);
    for (i = 0; i < EPD_HEIGHT; i++)
    {
        for (j = 0; j < EPD_WIDTH / 2; j++)
        {
            byte = pixelData[i * (EPD_WIDTH / 2) + j];

            colorCode1 = (byte >> 4) & 0x0F;
            colorCode2 = byte & 0x0F;

            color1 = header.colorTable[colorCode1];
            color2 = header.colorTable[colorCode2];

            line_buffer[j] = (color_get(color1) << 4) | color_get(color2);
        }
        EPD_W21_WriteDATA_Bulk(line_buffer, EPD_WIDTH / 2);
    }

    EPD_W21_WriteCMD(PON);
    lcd_chkstatus();

    EPD_W21_WriteCMD(BTST2);
    EPD_W21_WriteDATA(0x6F);
    EPD_W21_WriteDATA(0x1F);
    EPD_W21_WriteDATA(0x17);
    EPD_W21_WriteDATA(0x27);

    EPD_W21_WriteCMD(DRF);
    EPD_W21_WriteDATA(0x00);
    lcd_chkstatus();

    EPD_W21_WriteCMD(POF);
    EPD_W21_WriteDATA(0x00);
    lcd_chkstatus();

    sys_logi(EPD_TAG, "Film display completed");
}

void hal_epd_sleep(void)
{
    EPD_W21_WriteCMD(DSLP);
    EPD_W21_WriteDATA(0xA5);

    sys_logi(EPD_TAG, "EPD entered sleep mode");
}

void hal_epd_pwroff(void)
{
    hal_epd_sleep();
    vTaskDelay(100 / portTICK_PERIOD_MS);

    sys_logi(EPD_TAG, "EPD power off");
}

void hal_epd_deinit(void)
{
    hal_epd_sleep();
    vTaskDelay(100 / portTICK_PERIOD_MS);

    if (m_spi_device != NULL)
    {
        spi_bus_remove_device(m_spi_device);
        m_spi_device = NULL;
    }
    spi_bus_free(SPI2_HOST);

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << EPD_BUSY_PIN) | (1ULL << EPD_RST_PIN) |
                        (1ULL << EPD_DC_PIN) | (1ULL << EPD_CS_PIN) |
                        (1ULL << EPD_SCK_PIN) | (1ULL << EPD_SDIN_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    sys_logi(EPD_TAG, "EPD power off, SPI and GPIO released");
}
