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
 * FileName : /film_hal/src/hal_epd_709.c
 * Author: Kiritro  Version: v0.1  Date: 2026/8/17
 * Description: GDEB0709E01 (GDEP133C02) 6-color EPD driver, 1200x1600,
 *              dual panel (CS0/CS1), 4-wire SPI with 8-bit command
 * ChangeLog: Change Notes
 *
 *********************************************************************/

/*********************************************************************
 * INCLUDES
 */
#include "hal_epd.h"
#if EPD_SELECT_E6_7_09_1600_1200 == 1
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

#include "sys_log.h"

/*********************************************************************
 * MACROS
 */
// 4bpp 输入，双面板：每行 EPD_WIDTH/2 字节，每面板每行 EPD_WIDTH/4 字节
#define EPD_ROW_BYTES        (EPD_WIDTH / 2)   // 600
#define EPD_PANEL_ROW_BYTES  (EPD_WIDTH / 4)   // 300
#define EPD_PANEL_TOTAL_BYTES ((EPD_WIDTH * EPD_HEIGHT) / 4) // 480000

#define FILM_HEADER_SIZE                  (32)
#define FILM_COLOR_TABLE_SIZE             (16)
#define FILM_OFFSET_FILESIZE              (0x00)
#define FILM_OFFSET_SCREENWIDTH           (0x04)
#define FILM_OFFSET_SCREENHEIGHT          (0x06)
#define FILM_OFFSET_COLORCOUNT            (0x08)
#define FILM_OFFSET_RESERVED              (0x09)
#define FILM_OFFSET_COLORTABLE            (0x10)

// GDEB0709E01 (GDEP133C02) commands
#define PSR             0x00
#define PWR             0x01
#define POF             0x02
#define PON             0x04
#define BTST_N          0x05
#define BTST_P          0x06
#define DTM             0x10
#define DRF             0x12
#define CDI             0x50
#define TCON            0x60
#define TRES            0x61
#define PTLW            0x83
#define AN_TM           0x74
#define AGID            0x86
#define BUCK_BOOST_VDDN 0xB0
#define TFT_VCOM_POWER  0xB1
#define EN_BUF          0xB6
#define BOOST_VDDP_EN   0xB7
#define CCSET           0xE0
#define PWS             0xE3
#define CMD66           0xF0

// 局部窗口刷新
#define PTLW_ENABLE     0x01
#define PTLW_DISABLE    0x00

// 709 屏 4bit 颜色码
#define EPD7_COLOR_BLACK   0x00
#define EPD7_COLOR_WHITE   0x01
#define EPD7_COLOR_YELLOW  0x02
#define EPD7_COLOR_RED     0x03
#define EPD7_COLOR_BLUE    0x05
#define EPD7_COLOR_GREEN   0x06

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
// init 参数（来自 GDEP133C02 官方驱动）
static const unsigned char PSR_V[2] = {0xDF, 0x69};
static const unsigned char PWR_V[6] = {0x0F, 0x00, 0x28, 0x2C, 0x28, 0x38};
static const unsigned char POF_V[1] = {0x00};
static const unsigned char DRF_V[1] = {0x01};
static const unsigned char CDI_V[1] = {0xF7};
static const unsigned char TCON_V[2] = {0x03, 0x03};
static const unsigned char TRES_V[4] = {0x04, 0xB0, 0x03, 0x20};
static const unsigned char CMD66_V[6] = {0x49, 0x55, 0x13, 0x5D, 0x05, 0x10};
static const unsigned char EN_BUF_V[1] = {0x07};
static const unsigned char CCSET_V[1] = {0x01};
static const unsigned char PWS_V[1] = {0x22};
static const unsigned char AN_TM_V[9] = {0xC0, 0x1E, 0x1E, 0xCE, 0xCE, 0xCE, 0x15, 0x15, 0x55};
static const unsigned char AGID_V[1] = {0x10};
static const unsigned char BTST_P_V[2] = {0xE8, 0x28};
static const unsigned char BOOST_VDDP_EN_V[1] = {0x01};
static const unsigned char BTST_N_V[2] = {0xE8, 0x28};
static const unsigned char BUCK_BOOST_VDDN_V[1] = {0x01};
static const unsigned char TFT_VCOM_POWER_V[1] = {0x02};

// film 颜色编码 -> 709 屏 4bit 设备色
static const unsigned char color_lut[256] =
{
    [0x00] = EPD7_COLOR_BLACK, // Black
    [0xFF] = EPD7_COLOR_WHITE, // White
    [0xFC] = EPD7_COLOR_YELLOW,// Yellow
    [0xE0] = EPD7_COLOR_RED,   // Red
    [0x03] = EPD7_COLOR_BLUE,  // Blue
    [0x1C] = EPD7_COLOR_GREEN, // Green
};

/*********************************************************************
 * LOCAL VARIABLES
 */
static spi_device_handle_t m_spi_device;

/*********************************************************************
 * GLOBAL VARIABLES
 */

/*********************************************************************
 * LOCAL FUNCTIONS
 */
static void spi_init(void);
static void epd_cmd(unsigned char cmd);
static void epd_cmd_data(unsigned char cmd, const unsigned char *data, unsigned int len);
static void epd_data(const unsigned char *data, unsigned int len);
static void cs_select(unsigned char csx, unsigned int level);
static void cs_select_all(unsigned int level);
static void reset(void);
static void check_busy(void);
static void epd_init_sequence(void);
static void epd_display_refresh(void);
static void epd_display_solid_color(unsigned char color);
static void epd_send_image(const unsigned char *picData);
static void convert_film_row(const unsigned char *pixelData, const FilmHeader *header,
                             unsigned int row, unsigned char *out);
static esp_err_t film_parse_header(const unsigned char *filmData, FilmHeader *header);

/*********************************************************************
 * SPI (4-wire full duplex, 8-bit command bits, dual CS)
 *********************************************************************/

static void spi_init(void)
{
    esp_err_t ret;

    spi_bus_config_t buscfg =
    {
        .data0_io_num = EPD_SDIN_PIN,  // MOSI
        .data1_io_num = EPD_SDIO_PIN,  // MISO
        .sclk_io_num = EPD_SCK_PIN,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 32768,
    };

    ret = spi_bus_initialize(EPD_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO);
    assert(ret == ESP_OK);

    spi_device_interface_config_t devcfg =
    {
        .command_bits = 8,
        .clock_speed_hz = SPI_MASTER_FREQ_10M,
        .duty_cycle_pos = 128,
        .mode = 0,
        .spics_io_num = -1,  // CS 手动控制（双 CS）
        .queue_size = 7,
        .cs_ena_posttrans = 3,
    };

    ret = spi_bus_add_device(EPD_SPI_HOST, &devcfg, &m_spi_device);
    assert(ret == ESP_OK);
}

static void epd_cmd(unsigned char cmd)
{
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.cmd = cmd;
    t.length = 0;
    t.tx_buffer = NULL;
    spi_device_transmit(m_spi_device, &t);
}

static void epd_cmd_data(unsigned char cmd, const unsigned char *data, unsigned int len)
{
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.cmd = cmd;
    t.length = len * 8;
    t.tx_buffer = data;
    spi_device_transmit(m_spi_device, &t);
}

static void epd_data(const unsigned char *data, unsigned int len)
{
    // 纯数据发送：覆盖 command_bits=0，不发送命令头
    while (len > 0)
    {
        unsigned int chunk = (len > 32768) ? 32768 : len;
        spi_transaction_ext_t t;
        memset(&t, 0, sizeof(t));
        t.base.flags = SPI_TRANS_VARIABLE_CMD;
        t.command_bits = 0;
        t.base.length = chunk * 8;
        t.base.tx_buffer = data;
        spi_device_transmit(m_spi_device, (spi_transaction_t *)&t);
        data += chunk;
        len -= chunk;
    }
}

static void cs_select(unsigned char csx, unsigned int level)
{
    if (csx == 0)
    {
        gpio_set_level(EPD_CS0_PIN, level);
    }
    else
    {
        gpio_set_level(EPD_CS1_PIN, level);
    }
}

static void cs_select_all(unsigned int level)
{
    gpio_set_level(EPD_CS0_PIN, level);
    gpio_set_level(EPD_CS1_PIN, level);
}

static void reset(void)
{
    EPD_W21_RST_0;
    vTaskDelay(20 / portTICK_PERIOD_MS);
    EPD_W21_RST_1;
    vTaskDelay(20 / portTICK_PERIOD_MS);
    check_busy();
}

static void check_busy(void)
{
    while (isEPD_W21_BUSY == 0);  // BUSY 低电平表示忙
}

static void epd_init_sequence(void)
{
    cs_select(0, 0);
    epd_cmd_data(AN_TM, AN_TM_V, sizeof(AN_TM_V));
    cs_select_all(1);

    cs_select_all(0);
    epd_cmd_data(CMD66, CMD66_V, sizeof(CMD66_V));
    cs_select_all(1);

    cs_select_all(0);
    epd_cmd_data(PSR, PSR_V, sizeof(PSR_V));
    cs_select_all(1);

    cs_select_all(0);
    epd_cmd_data(CDI, CDI_V, sizeof(CDI_V));
    cs_select_all(1);

    cs_select_all(0);
    epd_cmd_data(TCON, TCON_V, sizeof(TCON_V));
    cs_select_all(1);

    cs_select_all(0);
    epd_cmd_data(AGID, AGID_V, sizeof(AGID_V));
    cs_select_all(1);

    cs_select_all(0);
    epd_cmd_data(PWS, PWS_V, sizeof(PWS_V));
    cs_select_all(1);

    cs_select_all(0);
    epd_cmd_data(CCSET, CCSET_V, sizeof(CCSET_V));
    cs_select_all(1);

    cs_select_all(0);
    epd_cmd_data(TRES, TRES_V, sizeof(TRES_V));
    cs_select_all(1);

    cs_select(0, 0);
    epd_cmd_data(PWR, PWR_V, sizeof(PWR_V));
    cs_select_all(1);

    cs_select(0, 0);
    epd_cmd_data(EN_BUF, EN_BUF_V, sizeof(EN_BUF_V));
    cs_select_all(1);

    cs_select(0, 0);
    epd_cmd_data(BTST_P, BTST_P_V, sizeof(BTST_P_V));
    cs_select_all(1);

    cs_select(0, 0);
    epd_cmd_data(BOOST_VDDP_EN, BOOST_VDDP_EN_V, sizeof(BOOST_VDDP_EN_V));
    cs_select_all(1);

    cs_select(0, 0);
    epd_cmd_data(BTST_N, BTST_N_V, sizeof(BTST_N_V));
    cs_select_all(1);

    cs_select(0, 0);
    epd_cmd_data(BUCK_BOOST_VDDN, BUCK_BOOST_VDDN_V, sizeof(BUCK_BOOST_VDDN_V));
    cs_select_all(1);

    cs_select(0, 0);
    epd_cmd_data(TFT_VCOM_POWER, TFT_VCOM_POWER_V, sizeof(TFT_VCOM_POWER_V));
    cs_select_all(1);
}

static void epd_display_refresh(void)
{
    cs_select_all(0);
    epd_cmd(PON);
    check_busy();
    cs_select_all(1);

    cs_select_all(0);
    vTaskDelay(30 / portTICK_PERIOD_MS);
    epd_cmd_data(DRF, DRF_V, sizeof(DRF_V));
    check_busy();
    cs_select_all(1);

    cs_select_all(0);
    epd_cmd_data(POF, POF_V, sizeof(POF_V));
    check_busy();
    cs_select_all(1);
}

static void epd_display_solid_color(unsigned char color)
{
    // 分块发送缓冲：块大小只影响 SPI 事务次数，480000 字节数据按 512 字节分块
    unsigned char buf[512];
    unsigned long remaining = EPD_PANEL_TOTAL_BYTES;

    memset(buf, color, sizeof(buf));

    cs_select_all(0);
    epd_cmd(DTM);
    while (remaining > 0)
    {
        unsigned long chunk = (remaining > sizeof(buf)) ? sizeof(buf) : remaining;
        epd_data(buf, chunk);
        remaining -= chunk;
    }
    cs_select_all(1);

    epd_display_refresh();
}

static void epd_send_image(const unsigned char *picData)
{
    unsigned int row;

    // CS0 面板：每行前 300 字节
    cs_select(0, 0);
    epd_cmd(DTM);
    for (row = 0; row < EPD_HEIGHT; row++)
    {
        epd_data(picData + row * EPD_ROW_BYTES, EPD_PANEL_ROW_BYTES);
    }
    cs_select_all(1);

    // CS1 面板：每行后 300 字节
    cs_select(1, 0);
    epd_cmd(DTM);
    for (row = 0; row < EPD_HEIGHT; row++)
    {
        epd_data(picData + row * EPD_ROW_BYTES + EPD_PANEL_ROW_BYTES, EPD_PANEL_ROW_BYTES);
    }
    cs_select_all(1);

    epd_display_refresh();
}

static void convert_film_row(const unsigned char *pixelData, const FilmHeader *header,
                             unsigned int row, unsigned char *out)
{
    const unsigned char *src = pixelData + row * EPD_ROW_BYTES;

    for (unsigned int j = 0; j < EPD_ROW_BYTES; j++)
    {
        unsigned char byte = src[j];
        unsigned char c1 = color_lut[header->colorTable[(byte >> 4) & 0x0F]];
        unsigned char c2 = color_lut[header->colorTable[byte & 0x0F]];
        out[j] = (c1 << 4) | c2;
    }
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

    // RST, CS0, CS1: output
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = (1ULL << EPD_RST_PIN) | (1ULL << EPD_CS0_PIN) | (1ULL << EPD_CS1_PIN);
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&io_conf);

    // LOAD_SW: 面板电源负载开关，输出并拉高
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = (1ULL << EPD_LOAD_SW_PIN);
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&io_conf);
    gpio_set_level(EPD_LOAD_SW_PIN, 1);

    // 初始 CS 高电平（不选中）
    cs_select_all(1);

    spi_init();

    sys_logi(EPD_TAG, "EPD 709 initialized (1200x1600 dual panel)");

    // debug
    // hal_epd_display_init();
    // // 初始化完成后全屏刷红，验证显示链路
    // hal_epd_display_red();
}

void hal_epd_display_init(void)
{
    reset();
    epd_init_sequence();

    sys_logi(EPD_TAG, "EPD 709 display initialized");
}

void hal_epd_display_white(void)
{
    epd_display_solid_color(EPD7_COLOR_WHITE);
}
void hal_epd_display_black(void)
{
    epd_display_solid_color(EPD7_COLOR_BLACK);
}
void hal_epd_display_yellow(void)
{
    epd_display_solid_color(EPD7_COLOR_YELLOW);
}
void hal_epd_display_red(void)
{
    epd_display_solid_color(EPD7_COLOR_RED);
}
void hal_epd_display_blue(void)
{
    epd_display_solid_color(EPD7_COLOR_BLUE);
}
void hal_epd_display_green(void)
{
    epd_display_solid_color(EPD7_COLOR_GREEN);
}

void hal_epd_display_pic(const unsigned char *picData)
{
    if (picData == NULL)
    {
        sys_loge(EPD_TAG, "picData is NULL");
        return;
    }

    epd_send_image(picData);

    sys_logi(EPD_TAG, "EPD 709 display pic completed");
}

int32_t hal_epd_partial_update(uint8_t panel, uint16_t x_start, uint16_t y_start,
                               uint16_t width, uint16_t height,
                               const unsigned char *data, uint8_t display_enable)
{
    uint32_t hrst, hred, vrst, vred;
    unsigned char window_data[9];
    uint32_t data_len;

    // 参数校验（错误码与官方 GDEP133C02 驱动一致）
    hrst = x_start * 2;
    hred = (x_start + width) * 2 - 1;
    vrst = y_start / 2;
    vred = (y_start + height) / 2 - 1;

    if (data == NULL)
    {
        sys_loge(EPD_TAG, "partial update: data is NULL");
        return -9;
    }
    if (hrst % 8 != 0)          // HRST[10:0] = 8n
    {
        return -1;
    }
    if ((hred - 7) % 8 != 0)    // HRED[10:0] = 8m+3
    {
        return -2;
    }
    if ((x_start > 584) || (width > 600))
    {
        return -3;
    }
    if ((hred - hrst + 1 < 32) || (hred + 1 > 1200))
    {
        return -4;
    }
    if ((y_start + height) % 2 != 0)
    {
        return -5;
    }
    if ((y_start > 1596) || (height > 1600))
    {
        return -6;
    }
    if (((int32_t)(vred - vrst) + 1 <= 0) || (vred + 1 > 800))
    {
        return -7;
    }
    if (panel > 1)
    {
        return -8;
    }

    memset(window_data, 0, sizeof(window_data));
    window_data[0] = (unsigned char)(hrst >> 8);
    window_data[1] = (unsigned char)(hrst);
    window_data[2] = (unsigned char)(hred >> 8);
    window_data[3] = (unsigned char)(hred);
    window_data[4] = (unsigned char)(vrst >> 8);
    window_data[5] = (unsigned char)(vrst);
    window_data[6] = (unsigned char)(vred >> 8);
    window_data[7] = (unsigned char)(vred);
    window_data[8] = PTLW_ENABLE;

    // 局部窗口更新前必须写 CMD66
    cs_select(panel, 0);
    epd_cmd_data(CMD66, CMD66_V, sizeof(CMD66_V));
    cs_select_all(1);

    // 设置窗口
    cs_select(panel, 0);
    epd_cmd_data(PTLW, window_data, sizeof(window_data));
    cs_select_all(1);

    // 发送窗口图像数据（4bpp，字节数 = 宽*高/2）
    data_len = (uint32_t)width * height / 2;
    cs_select(panel, 0);
    epd_cmd(DTM);
    epd_data(data, data_len);
    cs_select_all(1);

    sys_logi(EPD_TAG, "EPD 709 partial update: panel=%d x=%u y=%u w=%u h=%u len=%lu",
             panel, x_start, y_start, width, height, (unsigned long)data_len);

    if (display_enable)
    {
        epd_display_refresh();
        vTaskDelay(300 / portTICK_PERIOD_MS);

        // 关闭局部窗口
        memset(window_data, 0, sizeof(window_data));
        window_data[8] = PTLW_DISABLE;

        cs_select_all(0);
        epd_cmd_data(PTLW, window_data, sizeof(window_data));
        cs_select_all(1);

        sys_logi(EPD_TAG, "EPD 709 partial update display completed");
    }

    return 0;
}

void hal_epd_display_film(const unsigned char *filmData)
{
    FilmHeader header;
    const unsigned char *pixelData;
    unsigned char line[EPD_ROW_BYTES];
    unsigned int row;

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

    // CS0 面板：每行前半
    cs_select(0, 0);
    epd_cmd(DTM);
    for (row = 0; row < EPD_HEIGHT; row++)
    {
        convert_film_row(pixelData, &header, row, line);
        epd_data(line, EPD_PANEL_ROW_BYTES);
    }
    cs_select_all(1);

    // CS1 面板：每行后半
    cs_select(1, 0);
    epd_cmd(DTM);
    for (row = 0; row < EPD_HEIGHT; row++)
    {
        convert_film_row(pixelData, &header, row, line);
        epd_data(line + EPD_PANEL_ROW_BYTES, EPD_PANEL_ROW_BYTES);
    }
    cs_select_all(1);

    epd_display_refresh();

    sys_logi(EPD_TAG, "EPD 709 film display completed");
}

void hal_epd_sleep(void)
{
    check_busy();
    cs_select_all(0);
    epd_cmd_data(POF, POF_V, sizeof(POF_V));
    check_busy();
    cs_select_all(1);

    sys_logi(EPD_TAG, "EPD 709 sleep");
}

void hal_epd_pwroff(void)
{
    hal_epd_sleep();
    vTaskDelay(100 / portTICK_PERIOD_MS);

    sys_logi(EPD_TAG, "EPD 709 power off");
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
                        (1ULL << EPD_CS0_PIN) | (1ULL << EPD_CS1_PIN) |
                        (1ULL << EPD_SCK_PIN) | (1ULL << EPD_SDIN_PIN) |
                        (1ULL << EPD_SDIO_PIN) | (1ULL << EPD_LOAD_SW_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    sys_logi(EPD_TAG, "EPD 709 deinit, SPI and GPIOs released");
}
#endif
