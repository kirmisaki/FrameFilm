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
 * FileName : /film_hal/src/hal_epd_750.c
 * Author: kiritro  Version: v0.1  Date: 2026/11/27
 * Description: 7.5 寸 800x480 黑白 1bpp EPD 驱动（GDEM075T41WT / UC8179），
 *              基于厂商 Arduino 参考驱动（Display_EPD_W21.cpp）移植，
 *              支持整屏全刷与 OTP 差分波形的整屏/窗口局部刷新。
 * ChangeLog: Change Notes
 *
 *********************************************************************/

/*********************************************************************
 * INCLUDES
 */
#include "hal_epd.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

#include "sys_log.h"

/*********************************************************************
 * MACROS
 */
/* GDEM075T41WT 命令（参考 Display_EPD_W21.cpp） */
#define EPD_CMD_PSR         0x00  // Panel setting
#define EPD_CMD_PWR         0x01  // Power setting
#define EPD_CMD_POF         0x02  // Power off
#define EPD_CMD_PON         0x04  // Power on
#define EPD_CMD_BTST        0x06  // Booster soft start
#define EPD_CMD_DSLP        0x07  // Deep sleep
#define EPD_CMD_DTM_OLD     0x10  // 旧帧数据（差分刷新参考）
#define EPD_CMD_DRF         0x12  // Display refresh
#define EPD_CMD_DTM_NEW     0x13  // 新帧数据
#define EPD_CMD_CDI         0x50  // VCOM and data interval
#define EPD_CMD_TCON        0x60  // TCON setting
#define EPD_CMD_TRES        0x61  // Resolution setting
#define EPD_CMD_VDCS        0x15  // VCOM data interval
#define EPD_CMD_PTL         0x90  // Partial window bounds
#define EPD_CMD_PTIN        0x91  // Enter partial (windowed) mode
#define EPD_CMD_PTOUT       0x92  // Exit partial mode
#define EPD_CMD_CASCADE     0xE0  // Cascade / 温度源选择
#define EPD_CMD_FORCED_TEMP 0xE5  // 强制温度（选择 OTP 差分波形 bank）

/* 每行字节数：800 像素 / 8 = 100 */
#define EPD_LINE_BYTES      (EPD_WIDTH / 8)
/* 整帧字节数：800×480/8 = 48000 */
#define EPD_FRAME_BYTES     (EPD_LINE_BYTES * EPD_HEIGHT)
/* v1 4bpp 每行字节数：800/2 = 400 */
#define EPD_V1_LINE_BYTES   (EPD_WIDTH / 2)

/* CDI(0x50) 参数：DDX=00 与本驱动 1=黑 的像素编码匹配；bit3 N2OCP 让刷新完成后
 * 控制器把新帧 RAM 复制到旧帧 RAM，后续差分/局部刷新无需主机维护旧帧。
 * 全刷：白边框 + N2OCP = 0x18；局部：边框高阻 + N2OCP = 0xA8。 */
#define EPD_CDI_FULL        0x18
#define EPD_CDI_PARTIAL     0xA8
#define EPD_CDI_INTERVAL    0x07

/* BUSY 等待超时（ms） */
#define BUSY_TIMEOUT_INIT_MS      (5000)
#define BUSY_TIMEOUT_POWER_MS     (5000)
#define BUSY_TIMEOUT_REFRESH_MS   (30000)

/* .film 文件头 */
#define FILM_HEADER_SIZE          (32)
#define FILM_COLOR_TABLE_SIZE     (16)
#define FILM_OFFSET_FILESIZE      (0x00)
#define FILM_OFFSET_SCREENWIDTH   (0x04)
#define FILM_OFFSET_SCREENHEIGHT  (0x06)
#define FILM_OFFSET_COLORCOUNT    (0x08)
#define FILM_OFFSET_FORMAT        (0x09)
#define FILM_OFFSET_FRAMECOUNT    (0x0A)
#define FILM_OFFSET_RESERVED      (0x0C)
#define FILM_OFFSET_COLORTABLE    (0x10)

#define FILM_FORMAT_V1_4BPP       (0x00)
#define FILM_FORMAT_MONO_FAST     (0x01)

/*********************************************************************
* TYPEDEFS
*/
typedef struct
{
    uint32_t fileSize;
    uint16_t screenWidth;
    uint16_t screenHeight;
    uint8_t  colorCount;
    uint8_t  format;
    uint16_t frameCount;
    uint8_t  reserved[4];
    uint8_t  colorTable[FILM_COLOR_TABLE_SIZE];
} __attribute__((packed)) FilmHeader;

/*********************************************************************
 * CONSTANTS
 */
/* .film v1 ColorTable 实际颜色值 → 1bpp 单色（1=黑，0=白）
 * ColorTable 存的是 RGB332 风格值：
 *   0x00 黑 / 0xFF 白 / 0xFC 黄 / 0xE0 红 / 0x03 蓝 / 0x1C 绿
 * 按亮度做二值化：白、黄、绿 → 白；黑、红、蓝 → 黑；未知 → 白。 */
static const uint8_t mono_lut[256] =
{
    [0x00] = 1, // Black
    [0xFF] = 0, // White
    [0xFC] = 0, // Yellow -> White
    [0xE0] = 1, // Red    -> Black
    [0x03] = 1, // Blue   -> Black
    [0x1C] = 0, // Green  -> White
};

/*********************************************************************
 * LOCAL VARIABLES
 */
static spi_device_handle_t m_spi_device = NULL;

/* 当前加载的波形 bank；UC8179 在初始化时选择波形，切换需要复位后重初始化 */
typedef enum
{
    EPD_WAVE_FULL = 0,   // OTP 全刷波形
    EPD_WAVE_PARTIAL,    // OTP 差分波形（强制温度选择）
} epd_wave_t;

static epd_wave_t m_wave = EPD_WAVE_FULL;
/* 屏上是否已有一帧可作为差分基准（整屏刷新后置位；深睡/反初始化后清零） */
static bool m_baseline_valid = false;

/*********************************************************************
 * LOCAL FUNCTIONS
 */
static void spi_init(void);
static void EPD_W21_WriteCMD(unsigned char command);
static void EPD_W21_WriteDATA(unsigned char datas);
static void EPD_W21_WriteDATA_Bulk(const unsigned char *data, uint32_t len);
static bool epd_wait_busy(uint32_t timeout_ms);
static void reset(void);
static esp_err_t film_parse_header(const unsigned char *filmData, FilmHeader *header);
static void epd_write_old_white(void);
static void epd_write_new_film(const FilmHeader *header, const unsigned char *pixelData);
static void epd_update_seq(void);
static void epd_write_solid(uint8_t fill);
static void epd_set_cdi(uint8_t param0);
static bool epd_window_begin(uint16_t x, uint16_t y, uint16_t w, uint16_t h);
static void epd_ensure_partial(void);
static void epd_refresh_partial(void);
static void epd_convert_v1_row(const uint8_t *src, uint8_t *dst, const uint8_t *colorTable);
static void epd_convert_pic_row(const uint8_t *src, uint8_t *dst);

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
        .max_transfer_sz = EPD_V1_LINE_BYTES
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
    TickType_t start = xTaskGetTickCount();

    /* 本屏 BUSY 低电平表示忙 */
    while (gpio_get_level(EPD_BUSY_PIN) == 0)
    {
        vTaskDelay(pdMS_TO_TICKS(1));
        if (((xTaskGetTickCount() - start) * portTICK_PERIOD_MS) > timeout_ms)
        {
            sys_logw(EPD_TAG, "wait busy timeout (%lu ms)", (unsigned long)timeout_ms);
            return false;
        }
    }
    return true;
}

static void reset(void)
{
    EPD_W21_RST_0;
    vTaskDelay(pdMS_TO_TICKS(10));
    EPD_W21_RST_1;
    vTaskDelay(pdMS_TO_TICKS(10));
}

static esp_err_t film_parse_header(const unsigned char *filmData, FilmHeader *header)
{
    if (filmData == NULL || header == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(&header->fileSize,     &filmData[FILM_OFFSET_FILESIZE],     sizeof(uint32_t));
    memcpy(&header->screenWidth,  &filmData[FILM_OFFSET_SCREENWIDTH],  sizeof(uint16_t));
    memcpy(&header->screenHeight, &filmData[FILM_OFFSET_SCREENHEIGHT], sizeof(uint16_t));
    memcpy(&header->colorCount,   &filmData[FILM_OFFSET_COLORCOUNT],   sizeof(uint8_t));
    memcpy(&header->format,       &filmData[FILM_OFFSET_FORMAT],       sizeof(uint8_t));
    memcpy(&header->frameCount,   &filmData[FILM_OFFSET_FRAMECOUNT],   sizeof(uint16_t));
    memcpy(header->reserved,      &filmData[FILM_OFFSET_RESERVED],     sizeof(header->reserved));
    memcpy(header->colorTable,    &filmData[FILM_OFFSET_COLORTABLE],   FILM_COLOR_TABLE_SIZE);

    switch (header->format)
    {
    case FILM_FORMAT_V1_4BPP:
        if (header->fileSize != (uint32_t)(EPD_WIDTH * EPD_HEIGHT / 2))
        {
            sys_loge(EPD_TAG, "v1 file size mismatch: %lu (expected %lu)",
                     (unsigned long)header->fileSize,
                     (unsigned long)(EPD_WIDTH * EPD_HEIGHT / 2));
            return ESP_ERR_INVALID_SIZE;
        }
        break;

    case FILM_FORMAT_MONO_FAST:
        if (header->fileSize != (uint32_t)EPD_FRAME_BYTES)
        {
            sys_loge(EPD_TAG, "mono file size mismatch: %lu (expected %lu)",
                     (unsigned long)header->fileSize,
                     (unsigned long)EPD_FRAME_BYTES);
            return ESP_ERR_INVALID_SIZE;
        }
        break;

    default:
        sys_loge(EPD_TAG, "unsupported film format 0x%02X", header->format);
        return ESP_ERR_NOT_SUPPORTED;
    }

    return ESP_OK;
}

/* 写旧帧数据（全白），作为整屏刷新的差分参考 */
static void epd_write_old_white(void)
{
    unsigned char line[EPD_LINE_BYTES];
    int row;

    memset(line, EPD_MONO_WHITE, EPD_LINE_BYTES);

    EPD_W21_WriteCMD(EPD_CMD_DTM_OLD);
    for (row = 0; row < EPD_HEIGHT; row++)
    {
        EPD_W21_WriteDATA_Bulk(line, EPD_LINE_BYTES);
    }
}

/* 只写新帧数据（.film v1 4bpp 或 MonoFast 1bpp），不写旧帧 */
static void epd_write_new_film(const FilmHeader *header, const unsigned char *pixelData)
{
    unsigned char line[EPD_LINE_BYTES];
    int row;

    EPD_W21_WriteCMD(EPD_CMD_DTM_NEW);

    if (header->format == FILM_FORMAT_V1_4BPP)
    {
        for (row = 0; row < EPD_HEIGHT; row++)
        {
            epd_convert_v1_row(&pixelData[row * EPD_V1_LINE_BYTES], line, header->colorTable);
            EPD_W21_WriteDATA_Bulk(line, EPD_LINE_BYTES);
        }
    }
    else /* FILM_FORMAT_MONO_FAST：1bpp，1=黑，MSB 在前，可直接下发 */
    {
        for (row = 0; row < EPD_HEIGHT; row++)
        {
            EPD_W21_WriteDATA_Bulk(&pixelData[row * EPD_LINE_BYTES], EPD_LINE_BYTES);
        }
    }
}

static void epd_set_cdi(uint8_t param0)
{
    EPD_W21_WriteCMD(EPD_CMD_CDI);
    EPD_W21_WriteDATA(param0);
    EPD_W21_WriteDATA(EPD_CDI_INTERVAL);
}

/* 进入局部窗口模式并设置窗口（0x91 + 0x90）。
 * PTL 的 x/y 以像素计，x 方向向上取整到 8 的倍数，结束坐标为闭区间。 */
static bool epd_window_begin(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    uint32_t stride = (w + 7) / 8;
    uint32_t x_end = (uint32_t)x + stride * 8 - 1;
    uint32_t y_end = (uint32_t)y + h - 1;

    if (w == 0 || h == 0 || x_end >= EPD_WIDTH || y_end >= EPD_HEIGHT)
    {
        return false;
    }

    EPD_W21_WriteCMD(EPD_CMD_PTIN);
    EPD_W21_WriteCMD(EPD_CMD_PTL);
    EPD_W21_WriteDATA((x >> 8) & 0xFF);
    EPD_W21_WriteDATA(x & 0xFF);
    EPD_W21_WriteDATA((x_end >> 8) & 0xFF);
    EPD_W21_WriteDATA(x_end & 0xFF);
    EPD_W21_WriteDATA((y >> 8) & 0xFF);
    EPD_W21_WriteDATA(y & 0xFF);
    EPD_W21_WriteDATA((y_end >> 8) & 0xFF);
    EPD_W21_WriteDATA(y_end & 0xFF);
    EPD_W21_WriteDATA(0x01);

    return true;
}

static void epd_update_seq(void)
{
    EPD_W21_WriteCMD(EPD_CMD_DRF);
    /* 官方驱动要求至少 200us，这里取 1ms */
    vTaskDelay(pdMS_TO_TICKS(1));
    epd_wait_busy(BUSY_TIMEOUT_REFRESH_MS);
}

/* 差分/局部刷新的刷新动作：边框保持高阻，避免边缘闪烁 */
static void epd_refresh_partial(void)
{
    epd_set_cdi(EPD_CDI_PARTIAL);
    EPD_W21_WriteCMD(EPD_CMD_DRF);
    /* 让控制器先拉低 BUSY，再开始轮询 */
    vTaskDelay(pdMS_TO_TICKS(10));
    epd_wait_busy(BUSY_TIMEOUT_REFRESH_MS);
}

static void epd_write_solid(uint8_t fill)
{
    unsigned char line[EPD_LINE_BYTES];
    int row;

    memset(line, fill, EPD_LINE_BYTES);

    epd_write_old_white();
    EPD_W21_WriteCMD(EPD_CMD_DTM_NEW);
    for (row = 0; row < EPD_HEIGHT; row++)
    {
        EPD_W21_WriteDATA_Bulk(line, EPD_LINE_BYTES);
    }
    epd_update_seq();
}

static void epd_convert_v1_row(const uint8_t *src, uint8_t *dst, const uint8_t *colorTable)
{
    uint32_t x;

    for (x = 0; x < EPD_WIDTH; x += 8)
    {
        uint8_t out = 0;
        int k;

        for (k = 0; k < 4; k++)
        {
            uint8_t b = src[k];
            out |= (uint8_t)(mono_lut[colorTable[b >> 4]] << (7 - 2 * k));
            out |= (uint8_t)(mono_lut[colorTable[b & 0x0F]] << (6 - 2 * k));
        }
        *dst++ = out;
        src += 4;
    }
}

static void epd_convert_pic_row(const uint8_t *src, uint8_t *dst)
{
    uint32_t x;

    for (x = 0; x < EPD_WIDTH; x += 8)
    {
        uint8_t out = 0;
        int k;

        for (k = 0; k < 8; k++)
        {
            out |= (uint8_t)(mono_lut[src[k]] << (7 - k));
        }
        *dst++ = out;
        src += 8;
    }
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

    sys_logi(EPD_TAG, "EPD initialized (GDEM075T41WT 800x480 mono, hardware SPI half-duplex)");
}

void hal_epd_display_init(void)
{
    reset();

    // 电源设置 VGH=20V, VGL=-20V, VDH=15V, VDL=-15V
    EPD_W21_WriteCMD(EPD_CMD_PWR);
    EPD_W21_WriteDATA(0x07);
    EPD_W21_WriteDATA(0x07);
    EPD_W21_WriteDATA(0x3F);
    EPD_W21_WriteDATA(0x3F);

    // Booster soft start
    EPD_W21_WriteCMD(EPD_CMD_BTST);
    EPD_W21_WriteDATA(0x17);
    EPD_W21_WriteDATA(0x17);
    EPD_W21_WriteDATA(0x28);
    EPD_W21_WriteDATA(0x17);

    // Power on
    EPD_W21_WriteCMD(EPD_CMD_PON);
    vTaskDelay(pdMS_TO_TICKS(100));
    epd_wait_busy(BUSY_TIMEOUT_INIT_MS);

    // Panel setting（KW 模式）
    EPD_W21_WriteCMD(EPD_CMD_PSR);
    EPD_W21_WriteDATA(0x1F);

    // Resolution 800×480
    EPD_W21_WriteCMD(EPD_CMD_TRES);
    EPD_W21_WriteDATA(0x03);    // source 800
    EPD_W21_WriteDATA(0x20);
    EPD_W21_WriteDATA(0x01);    // gate 480
    EPD_W21_WriteDATA(0xE0);

    EPD_W21_WriteCMD(EPD_CMD_VDCS);
    EPD_W21_WriteDATA(0x00);

    // VCOM and data interval：白边框 + N2OCP（本次刷新后把新帧复制为旧帧）
    epd_set_cdi(EPD_CDI_FULL);

    // TCON
    EPD_W21_WriteCMD(EPD_CMD_TCON);
    EPD_W21_WriteDATA(0x22);

    m_wave = EPD_WAVE_FULL;
    m_baseline_valid = false;

    sys_logi(EPD_TAG, "EPD display initialized (GDEM075T41WT, full waveform)");
}

/* 切到差分/局部刷新波形：UC8179 的 OTP 差分 bank 由强制温度选择。
 * 复位后 RAM 仍保留，调用前应已有整屏刷新作为差分基准。 */
void hal_epd_display_init_partial(void)
{
    reset();

    // Panel setting（KW 模式，LUT 来自 OTP）
    EPD_W21_WriteCMD(EPD_CMD_PSR);
    EPD_W21_WriteDATA(0x1F);

    // Resolution 800×480
    EPD_W21_WriteCMD(EPD_CMD_TRES);
    EPD_W21_WriteDATA(0x03);
    EPD_W21_WriteDATA(0x20);
    EPD_W21_WriteDATA(0x01);
    EPD_W21_WriteDATA(0xE0);

    // Power on
    EPD_W21_WriteCMD(EPD_CMD_PON);
    vTaskDelay(pdMS_TO_TICKS(100));
    epd_wait_busy(BUSY_TIMEOUT_INIT_MS);

    // 温度源改为强制温度寄存器，并选择差分波形 bank
    EPD_W21_WriteCMD(EPD_CMD_CASCADE);
    EPD_W21_WriteDATA(0x02);
    EPD_W21_WriteCMD(EPD_CMD_FORCED_TEMP);
    EPD_W21_WriteDATA(0x6E);

    m_wave = EPD_WAVE_PARTIAL;

    sys_logi(EPD_TAG, "EPD display initialized (GDEM075T41WT, partial/differential waveform)");
}

static void epd_ensure_partial(void)
{
    if (m_wave != EPD_WAVE_PARTIAL)
    {
        hal_epd_display_init_partial();
    }
}

void hal_epd_display_white(void)
{
    epd_write_solid(EPD_MONO_WHITE);
}

void hal_epd_display_black(void)
{
    epd_write_solid(EPD_MONO_BLACK);
}

/* 单色屏没有彩色：黄色/绿色按亮色处理（白），红/蓝按暗色处理（黑） */
void hal_epd_display_yellow(void)
{
    epd_write_solid(EPD_MONO_WHITE);
}

void hal_epd_display_red(void)
{
    epd_write_solid(EPD_MONO_BLACK);
}

void hal_epd_display_blue(void)
{
    epd_write_solid(EPD_MONO_BLACK);
}

void hal_epd_display_green(void)
{
    epd_write_solid(EPD_MONO_WHITE);
}

void hal_epd_display_pic(const unsigned char *picData)
{
    unsigned char line[EPD_LINE_BYTES];
    int row;

    if (picData == NULL)
    {
        sys_loge(EPD_TAG, "picData is NULL");
        return;
    }

    /* 输入为每像素 1 字节的颜色表值（0x00/0xFF/0xFC/0xE0/0x03/0x1C） */
    epd_write_old_white();
    EPD_W21_WriteCMD(EPD_CMD_DTM_NEW);
    for (row = 0; row < EPD_HEIGHT; row++)
    {
        epd_convert_pic_row(&picData[row * EPD_WIDTH], line);
        EPD_W21_WriteDATA_Bulk(line, EPD_LINE_BYTES);
    }

    epd_update_seq();
    m_baseline_valid = true;
    sys_logi(EPD_TAG, "Display pic completed");
}

void hal_epd_display_film(const unsigned char *filmData)
{
    FilmHeader header;
    const unsigned char *pixelData;

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

    sys_logi(EPD_TAG, "Film: %ux%u, fmt=0x%02X, %d colors, %lu bytes, frames=%u",
             header.screenWidth, header.screenHeight, header.format,
             header.colorCount, (unsigned long)header.fileSize, header.frameCount);

    pixelData = filmData + FILM_HEADER_SIZE;

    epd_write_old_white();
    epd_write_new_film(&header, pixelData);
    epd_update_seq();
    m_baseline_valid = true;

    sys_logi(EPD_TAG, "Film display completed (full refresh)");
}

/* 整屏差分刷新：使用局部波形，只驱动变化像素，无整屏闪白。
 * 依赖屏上已有基准帧；没有基准时自动回退为整屏刷新。 */
void hal_epd_display_film_partial(const unsigned char *filmData)
{
    FilmHeader header;
    const unsigned char *pixelData;

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

    if (!m_baseline_valid)
    {
        sys_logw(EPD_TAG, "no baseline on glass, fallback to full refresh");
        hal_epd_display_init();
        hal_epd_display_film(filmData);
        return;
    }

    epd_ensure_partial();
    epd_set_cdi(EPD_CDI_PARTIAL);

    /* 与参考驱动一致：差分数据也放进一个覆盖整屏的窗口，再退出窗口刷新 */
    pixelData = filmData + FILM_HEADER_SIZE;
    if (!epd_window_begin(0, 0, EPD_WIDTH, EPD_HEIGHT))
    {
        sys_loge(EPD_TAG, "failed to set full-panel partial window");
        return;
    }
    epd_write_new_film(&header, pixelData);
    EPD_W21_WriteCMD(EPD_CMD_PTOUT);
    epd_refresh_partial();

    sys_logi(EPD_TAG, "Film display completed (differential/partial refresh)");
}

int32_t hal_epd_partial_update(uint8_t panel, uint16_t x_start, uint16_t y_start,
                               uint16_t width, uint16_t height,
                               const unsigned char* data, uint8_t display_enable)
{
    if (panel != 0)
    {
        sys_loge(EPD_TAG, "invalid panel %u", panel);
        return -1;
    }
    if (data == NULL || width == 0 || height == 0)
    {
        sys_loge(EPD_TAG, "invalid partial update args");
        return -1;
    }

    /* 水平按字节对齐（PTL 的 x 以像素计，宽度向上取整到 8） */
    uint32_t stride = (width + 7) / 8;

    if (!m_baseline_valid)
    {
        sys_logw(EPD_TAG, "partial update requires a full refresh baseline first");
        return -1;
    }

    epd_ensure_partial();
    epd_set_cdi(EPD_CDI_PARTIAL);

    /* 进入局部窗口模式：0x91 → 0x90(窗口) → 0x13(数据) → 0x92 */
    if (!epd_window_begin(x_start, y_start, width, height))
    {
        sys_loge(EPD_TAG, "partial window out of range: x=%u y=%u w=%u h=%u",
                 x_start, y_start, width, height);
        return -1;
    }

    EPD_W21_WriteCMD(EPD_CMD_DTM_NEW);
    EPD_W21_WriteDATA_Bulk(data, stride * height);
    EPD_W21_WriteCMD(EPD_CMD_PTOUT);

    if (display_enable)
    {
        epd_refresh_partial();
    }

    return 0;
}

void hal_epd_sleep(void)
{
    epd_wait_busy(BUSY_TIMEOUT_INIT_MS);

    // VCOM and data interval（WB 模式）
    EPD_W21_WriteCMD(EPD_CMD_CDI);
    EPD_W21_WriteDATA(0xF7);

    // Power off
    EPD_W21_WriteCMD(EPD_CMD_POF);
    epd_wait_busy(BUSY_TIMEOUT_POWER_MS);

    // Deep sleep
    EPD_W21_WriteCMD(EPD_CMD_DSLP);
    EPD_W21_WriteDATA(0xA5);

    /* 深睡后旧帧基准不再可信，下次局部刷新前必须先整屏刷新 */
    m_baseline_valid = false;
    m_wave = EPD_WAVE_FULL;

    sys_logi(EPD_TAG, "EPD sleep");
}

void hal_epd_pwroff(void)
{
    hal_epd_sleep();
    vTaskDelay(pdMS_TO_TICKS(100));

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

    m_baseline_valid = false;
    m_wave = EPD_WAVE_FULL;

    sys_logi(EPD_TAG, "EPD deinit, SPI and GPIOs released");
}
