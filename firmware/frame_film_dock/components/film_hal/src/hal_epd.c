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
 * Author: Kiritro  Version: v0.1  Date: 2026/8/25
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

#include "sys_log.h"
#include "hal_epd.h"

/*********************************************************************
 * MACROS
 */
// 4bpp input → 2bpp output conversion
#define EPD_INPUT_LINE      (EPD_WIDTH / 2)   // 380 bytes per line
#define EPD_OUTPUT_LINE     (EPD_WIDTH / 4)   // 190 bytes per line

#define FILM_HEADER_SIZE                  (32)
#define FILM_COLOR_TABLE_SIZE             (16)
#define FILM_OFFSET_FILESIZE              (0x00)
#define FILM_OFFSET_SCREENWIDTH           (0x04)
#define FILM_OFFSET_SCREENHEIGHT          (0x06)
#define FILM_OFFSET_COLORCOUNT            (0x08)
#define FILM_OFFSET_RESERVED              (0x09)
#define FILM_OFFSET_COLORTABLE            (0x10)

// Panel commands
#define PSR                            0x00  // Panel setting
#define PWR                            0x01  // Power setting
#define POF                            0x03  // Power off sequence
#define PON                            0x04  // Power on
#define BTST                           0x06  // Booster soft start
#define DSLP                           0x07  // Deep sleep
#define DTM                            0x10  // Data start transmission (2bpp)
#define POF2                           0x12  // Power off (sleep)
#define PLL                            0x30  // PLL control
#define CDI                            0x50  // VCOM and data interval
#define RES                            0x61  // Resolution
#define RAMADDR                        0x65  // RAM address
#define DRV                            0x70  // Driver/LUT select
#define WAKEUP                         0x8F  // Soft reset / wakeup
#define PWS                            0xE3  // Power saving
#define BOD                            0xE9  // Border

// Pack 4 identical 2-bit values into one byte
#define PACK4(v)  (((v) << 6) | ((v) << 4) | ((v) << 2) | (v))

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
// 4-bit color index → device 4-bit LUT (for film format playback)
static const unsigned char color_lut[256] =
{
    [0x00] = 0x00, // Black
    [0xFF] = 0x01, // White
    [0xFC] = 0x02, // Yellow
    [0xE0] = 0x03, // Red
    [0x03] = 0x05, // Blue
    [0x1C] = 0x06, // Green
};

// 双 pass 刷新颜色映射（color_map = pass1，color_map1 = pass2）
// 索引约定：0=黑 1=白 2=黄 3=红 5=蓝 6=绿
static const uint8_t color_map[16]  = {1, 1, 1, 3, 1, 0, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1};
static const uint8_t color_map1[16] = {0, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};

/*********************************************************************
 * LOCAL VARIABLES
 */
static spi_device_handle_t m_spi_device;

/*********************************************************************
 * LOCAL FUNCTIONS
 */
static void spi_init(void);
static void EPD_W21_WriteCMD(unsigned char command);
static void EPD_W21_WriteDATA(unsigned char datas);
static void EPD_W21_WriteDATA_Bulk(const unsigned char *data, uint32_t len);
static void reset(void);
static void lcd_chkstatus(void);
static void epd_init_seq(void);
static void epd_update(void);
static void epd_wakeup(void);
static void epd_do_pass(const unsigned char *input_data, const uint8_t *cmap);
static void epd_display_solid_pass(unsigned char fill_byte);
static void epd_display_solid(unsigned char color_index);
static void epd_sleep(void);
static esp_err_t film_parse_header(const unsigned char *filmData, FilmHeader *header);

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
        .max_transfer_sz = EPD_OUTPUT_LINE * 4
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

static void reset(void)
{
    EPD_W21_RST_1;
    vTaskDelay(20 / portTICK_PERIOD_MS);
    EPD_W21_RST_0;
    vTaskDelay(50 / portTICK_PERIOD_MS);
    EPD_W21_RST_1;
    vTaskDelay(30 / portTICK_PERIOD_MS);
    lcd_chkstatus();
}

static void lcd_chkstatus(void)
{
    int timeout = 0;
    while (isEPD_W21_BUSY == 0)
    {
        vTaskDelay(1 / portTICK_PERIOD_MS);
        if (++timeout > 90000)
        {
            sys_loge(EPD_TAG, "wait BUSY timeout");
            break;
        }
    }
}

/* 完整初始化命令序列（每次刷新前都会发送一次） */
static void epd_init_seq(void)
{
    EPD_W21_WriteCMD(PSR);     EPD_W21_WriteDATA(0x0F); EPD_W21_WriteDATA(0x29);
    EPD_W21_WriteCMD(0x4D);    EPD_W21_WriteDATA(0x78);
    EPD_W21_WriteCMD(PSR);     EPD_W21_WriteDATA(0x07); EPD_W21_WriteDATA(0x29);
    EPD_W21_WriteCMD(BTST);    EPD_W21_WriteDATA(0x0D); EPD_W21_WriteDATA(0x12);
                               EPD_W21_WriteDATA(0x30); EPD_W21_WriteDATA(0x20);
                               EPD_W21_WriteDATA(0x19); EPD_W21_WriteDATA(0x34);
                               EPD_W21_WriteDATA(0x10);
    EPD_W21_WriteCMD(PWR);     EPD_W21_WriteDATA(0x07); EPD_W21_WriteDATA(0x00);
    EPD_W21_WriteCMD(POF);     EPD_W21_WriteDATA(0x10); EPD_W21_WriteDATA(0x54); EPD_W21_WriteDATA(0x44);
    EPD_W21_WriteCMD(PLL);     EPD_W21_WriteDATA(0x08);
    EPD_W21_WriteCMD(CDI);     EPD_W21_WriteDATA(0x37);
    EPD_W21_WriteCMD(0xF0);    EPD_W21_WriteDATA(0x7D);
    EPD_W21_WriteCMD(RES);     EPD_W21_WriteDATA(0x02); EPD_W21_WriteDATA(0xF8);
                               EPD_W21_WriteDATA(0x02); EPD_W21_WriteDATA(0x38);
    EPD_W21_WriteCMD(RAMADDR); EPD_W21_WriteDATA(0x00); EPD_W21_WriteDATA(0x00);
                               EPD_W21_WriteDATA(0x20); EPD_W21_WriteDATA(0x00);
    EPD_W21_WriteCMD(PWS);     EPD_W21_WriteDATA(0x22);
    EPD_W21_WriteCMD(BOD);     EPD_W21_WriteDATA(0x01);
    EPD_W21_WriteCMD(DRV);     EPD_W21_WriteDATA(0x8E); EPD_W21_WriteDATA(0x02); EPD_W21_WriteDATA(0x01);
    lcd_chkstatus();
}

static void epd_update(void)
{
    lcd_chkstatus();
    EPD_W21_WriteCMD(POF2);
    EPD_W21_WriteDATA(0x00);
}

static void epd_wakeup(void)
{
    lcd_chkstatus();
    EPD_W21_WriteCMD(WAKEUP);
    EPD_W21_WriteDATA(0x06);
    lcd_chkstatus();
}

static void epd_do_pass(const unsigned char *input_data, const uint8_t *cmap)
{
    EPD_W21_WriteCMD(DTM);

    unsigned char out_line[EPD_OUTPUT_LINE];
    for (int row = 0; row < EPD_HEIGHT; row++)
    {
        const unsigned char *in_ptr = input_data + row * EPD_INPUT_LINE;
        for (int col = 0, out_idx = 0; col < EPD_INPUT_LINE; col += 2, out_idx++)
        {
            uint8_t in1 = in_ptr[col];
            uint8_t in2 = in_ptr[col + 1];
            uint8_t p1 = (in1 >> 4) & 0x0F;
            uint8_t p2 = in1 & 0x0F;
            uint8_t p3 = (in2 >> 4) & 0x0F;
            uint8_t p4 = in2 & 0x0F;
            out_line[out_idx] = (cmap[p1] << 6) | (cmap[p2] << 4)
                                | (cmap[p3] << 2) | cmap[p4];
        }
        EPD_W21_WriteDATA_Bulk(out_line, EPD_OUTPUT_LINE);
    }

    EPD_W21_WriteCMD(PON);
    lcd_chkstatus();
}

static void epd_display_solid_pass(unsigned char fill_byte)
{
    EPD_W21_WriteCMD(DTM);

    unsigned char line[EPD_OUTPUT_LINE];
    memset(line, fill_byte, EPD_OUTPUT_LINE);
    for (int row = 0; row < EPD_HEIGHT; row++)
    {
        EPD_W21_WriteDATA_Bulk(line, EPD_OUTPUT_LINE);
    }

    EPD_W21_WriteCMD(PON);
    lcd_chkstatus();
}

static void epd_display_solid(unsigned char color_index)
{
    uint8_t pass1 = PACK4(color_map[color_index]);
    uint8_t pass2 = PACK4(color_map1[color_index]);

    sys_logi(EPD_TAG, "Solid idx=%d p1=0x%02X p2=0x%02X",
             color_index, pass1, pass2);

    epd_init_seq();
    epd_display_solid_pass(pass1);
    epd_update();
    epd_sleep();

    epd_wakeup();
    epd_init_seq();
    epd_display_solid_pass(pass2);
    epd_update();
    epd_sleep();
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

static void epd_sleep(void)
{   
    lcd_chkstatus();
    EPD_W21_WriteCMD(0X02);
    EPD_W21_WriteDATA(0x00);
    lcd_chkstatus();
    EPD_W21_WriteCMD(0X07);
    EPD_W21_WriteDATA(0xA5);
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

    sys_logi(EPD_TAG, "EPD initialized (hardware SPI half-duplex)");
}

void hal_epd_display_init(void)
{
    reset();
    epd_init_seq();

    sys_logi(EPD_TAG, "EPD display initialized");
}

void hal_epd_display_white(void)
{
    epd_display_solid(0x01);
}
void hal_epd_display_black(void)
{
    epd_display_solid(0x00);
}
void hal_epd_display_yellow(void)
{
    epd_display_solid(0x02);
}
void hal_epd_display_red(void)
{
    epd_display_solid(0x03);
}
void hal_epd_display_blue(void)
{
    epd_display_solid(0x05);
}
void hal_epd_display_green(void)
{
    epd_display_solid(0x06);
}

void hal_epd_display_pic(const unsigned char *picData)
{
    if (picData == NULL)
    {
        sys_loge(EPD_TAG, "picData is NULL");
        return;
    }

    epd_init_seq();
    epd_do_pass(picData, color_map);
    epd_update();
	epd_sleep();

    epd_wakeup();
    epd_init_seq();
    epd_do_pass(picData, color_map1);
    epd_update();
    epd_sleep();

    sys_logi(EPD_TAG, "Display pic completed");
}

void hal_epd_display_film(const unsigned char *filmData)
{
    FilmHeader header;
    const unsigned char *pixelData;
    uint32_t i, j;

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

    for (int pass = 0; pass < 2; pass++)
    {
        const uint8_t *cmap = (pass == 0) ? color_map : color_map1;

        if (pass == 1)
        {
            epd_wakeup();
        }
        epd_init_seq();
        EPD_W21_WriteCMD(DTM);

        unsigned char line_4bpp[EPD_INPUT_LINE];
        unsigned char out_line[EPD_OUTPUT_LINE];

        for (i = 0; i < EPD_HEIGHT; i++)
        {
            for (j = 0; j < EPD_INPUT_LINE; j++)
            {
                uint8_t byte = pixelData[i * EPD_INPUT_LINE + j];
                uint8_t cc1 = (byte >> 4) & 0x0F;
                uint8_t cc2 = byte & 0x0F;
                line_4bpp[j] = (color_lut[header.colorTable[cc1]] << 4)
                               |  color_lut[header.colorTable[cc2]];
            }
            for (int k = 0; k < EPD_INPUT_LINE; k += 2)
            {
                uint8_t in1 = line_4bpp[k];
                uint8_t in2 = line_4bpp[k + 1];
                uint8_t p1 = (in1 >> 4) & 0x0F;
                uint8_t p2 = in1 & 0x0F;
                uint8_t p3 = (in2 >> 4) & 0x0F;
                uint8_t p4 = in2 & 0x0F;
                out_line[k / 2] = (cmap[p1] << 6) | (cmap[p2] << 4)
                                  | (cmap[p3] << 2) | cmap[p4];
            }
            EPD_W21_WriteDATA_Bulk(out_line, EPD_OUTPUT_LINE);
        }

        EPD_W21_WriteCMD(PON);
        lcd_chkstatus();

        epd_update();
        epd_sleep();
    }

    sys_logi(EPD_TAG, "Film display completed");
}

void hal_epd_sleep(void)
{
    epd_sleep();

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
