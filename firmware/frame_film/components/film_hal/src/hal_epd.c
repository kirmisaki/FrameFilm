/***********************************************************
 *
 * MIT License
 *
 * Copyright (c) 2025 kiritro
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 *
 * FileName : /film_hal/src/hal_epd.c
 * Author: Kiritro  Version: v0.1  Date: 2025/3/31
 * Description: Function introduction
 * ChangeLog: Change Notes
 *
***********************************************************/

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

/*********************************************************************
* TYPEDEFS
*/

/*********************************************************************
 * CONSTANTS
 */

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
static unsigned char color_get(unsigned char color_index);

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
        .max_transfer_sz = 0
    };

    ret = spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
    assert(ret == ESP_OK);

    spi_device_interface_config_t devcfg =
    {
        .clock_speed_hz = 10000000, // 10MHz
        .mode = 0, // SPI mode 0
        .spics_io_num = -1, // CS pin handled manually
        .queue_size = 7
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

static unsigned char color_get(unsigned char color_index)
{
    unsigned char datas;

    switch(color_index)
    {
    case 0x00: // Black
        datas = 0x00; // 对应原始图像数据 0000
        break;
    case 0xff: // White
        datas = 0x01; // 对应原始图像数据 0001
        break;
    case 0xfc: // Yellow
        datas = 0x02; // 对应原始图像数据 0010
        break;
    case 0xE0: // Red
        datas = 0x03; // 对应原始图像数据 0011
        break;
    case 0x03: // Blue
        datas = 0x05; // 对应原始图像数据 0101
        break;
    case 0x1c: // Green
        datas = 0x06; // 对应原始图像数据 0110
        break;
    default:
        datas = 0x00; // 默认为黑色
        break;
    }
    return datas;
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
    io_conf.pin_bit_mask = (1ULL << EPD_RST_PIN) | (1ULL << EPD_DC_PIN) | (1ULL << EPD_CS_PIN) | (1ULL << EPD_PWR_PIN);
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&io_conf);

    // Initialize SPI bus
    spi_init();

    // 提前打开电源进行电路充电防止电流过大导致重启
    EPD_W21_PWR_ON;

    sys_logi(EPD_TAG, "EPD hardware initialized");
}

void hal_epd_display_init(void)
{
    EPD_W21_PWR_ON;

    reset();
    lcd_chkstatus();
    vTaskDelay(30 / portTICK_PERIOD_MS);

    //20211212
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

    //===================
    //20211212
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
    unsigned long i;

    EPD_W21_WriteCMD(DTM);
    {
        for(i = 0; i < 120000; i++)
        {
            EPD_W21_WriteDATA(EPD_COLOR_WHITE);
        }
    }
    EPD_W21_WriteCMD(PON);
    lcd_chkstatus();

    //20211212
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
    unsigned long i;

    EPD_W21_WriteCMD(DTM);
    {
        for(i = 0; i < 120000; i++)
        {
            EPD_W21_WriteDATA(EPD_COLOR_BLACK);
        }
    }
    EPD_W21_WriteCMD(PON);
    lcd_chkstatus();

    //20211212
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
    unsigned long i;

    EPD_W21_WriteCMD(DTM);
    {
        for(i = 0; i < 120000; i++)
        {
            EPD_W21_WriteDATA(EPD_COLOR_YELLOW);
        }
    }
    EPD_W21_WriteCMD(PON);
    lcd_chkstatus();

    //20211212
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
    unsigned long i;

    EPD_W21_WriteCMD(DTM);
    {
        for(i = 0; i < 120000; i++)
        {
            EPD_W21_WriteDATA(EPD_COLOR_RED);
        }
    }
    EPD_W21_WriteCMD(PON);
    lcd_chkstatus();

    //20211212
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
    unsigned long i;

    EPD_W21_WriteCMD(DTM);
    {
        for(i = 0; i < 120000; i++)
        {
            EPD_W21_WriteDATA(EPD_COLOR_BLUE);
        }
    }
    EPD_W21_WriteCMD(PON);
    lcd_chkstatus();

    //20211212
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
    unsigned long i;

    EPD_W21_WriteCMD(DTM);
    {
        for(i = 0; i < 120000; i++)
        {
            EPD_W21_WriteDATA(EPD_COLOR_GREEN);
        }
    }
    EPD_W21_WriteCMD(PON);
    lcd_chkstatus();

    //20211212
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
    unsigned char data_H, data_L, data;

    EPD_W21_WriteCMD(0x10);
    for(i = 0; i < EPD_HEIGHT; i++)
    {
        k = 0;
        for(j = 0; j < EPD_WIDTH / 2; j++)
        {
            temp1 = picData[i * EPD_WIDTH + k++];
            temp2 = picData[i * EPD_WIDTH + k++];
            data_H = color_get(temp1) << 4;
            data_L = color_get(temp2);
            data = data_H | data_L;

            EPD_W21_WriteDATA(data);
        }
    }

    //Refresh

    EPD_W21_WriteCMD(PON);
    lcd_chkstatus();

    //20211212
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

    EPD_W21_PWR_OFF;

    sys_logi(EPD_TAG, "EPD power off");
}