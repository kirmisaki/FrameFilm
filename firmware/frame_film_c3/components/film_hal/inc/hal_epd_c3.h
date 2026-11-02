// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef __HAL_EPD_H__
#define __HAL_EPD_H__
#include "esp_system.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#define EPD_TAG "HAL_EPD_C3"
#define EPD_SELECT_E4_3_98_768_552 1
#define EPD_WIDTH 768
#define EPD_HEIGHT 552
#define EPD_PANEL_ID 0x08
// Existing C3 development-board wiring; NOT a verified PCB9 pin profile.
#define EPD_SDIN_PIN CONFIG_FRAMEFILM_C3_EPD_MOSI
#define EPD_SCK_PIN CONFIG_FRAMEFILM_C3_EPD_SCK
#define EPD_CS_PIN CONFIG_FRAMEFILM_C3_EPD_CS
#define EPD_DC_PIN CONFIG_FRAMEFILM_C3_EPD_DC
#define EPD_RST_PIN CONFIG_FRAMEFILM_C3_EPD_RST
#define EPD_BUSY_PIN CONFIG_FRAMEFILM_C3_EPD_BUSY
#define EPD_SPI_HOST SPI2_HOST
#define isEPD_W21_BUSY gpio_get_level(EPD_BUSY_PIN)
#define EPD_W21_RST_0 gpio_set_level(EPD_RST_PIN,0)
#define EPD_W21_RST_1 gpio_set_level(EPD_RST_PIN,1)
#define EPD_W21_DC_0 gpio_set_level(EPD_DC_PIN,0)
#define EPD_W21_DC_1 gpio_set_level(EPD_DC_PIN,1)
#define EPD_W21_CS_0 gpio_set_level(EPD_CS_PIN,0)
#define EPD_W21_CS_1 gpio_set_level(EPD_CS_PIN,1)
void hal_epd_init(void);
void hal_epd_deinit(void);
void hal_epd_display_init(void);
void hal_epd_display_white(void);
void hal_epd_display_black(void);
void hal_epd_display_yellow(void);
void hal_epd_display_red(void);
void hal_epd_display_blue(void);
void hal_epd_display_green(void);
void hal_epd_display_pic(const unsigned char *data);
void hal_epd_display_film(const unsigned char *data);
int32_t hal_epd_partial_update(uint8_t panel,uint16_t x,uint16_t y,uint16_t w,uint16_t h,const uint8_t *data,uint8_t color);
void hal_epd_sleep(void);
void hal_epd_pwroff(void);
#endif
