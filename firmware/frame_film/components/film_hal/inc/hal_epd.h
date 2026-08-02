#ifndef __HAL_EPD_H__
#define __HAL_EPD_H__

/*********************************************************************
 * INCLUDES
 */
#include "esp_system.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "sys_log.h"

/*********************************************************************
 * CPPMIX
 */
#ifdef __cplusplus
extern "C" {
#endif

/*********************************************************************
 * MACROS
 */
#define EPD_TAG                        "HAL_EPD"

#if FRAMEFILM_STD == 1
#define EPD_SELECT_E6_3_68_792_528  0
#define EPD_SELECT_E6_3_70_720_480  0
#define EPD_SELECT_E6_3_60_600_400  1
#define EPD_SELECT_E6_1_54_240_240  0
#endif
#if FRAMEFILM_PRO == 1
#define EPD_SELECT_E6_3_68_792_528  1
#define EPD_SELECT_E6_3_70_720_480  0
#define EPD_SELECT_E6_3_60_600_400  0
#define EPD_SELECT_E6_1_54_240_240  0
#endif

#if EPD_SELECT_E6_3_68_792_528 == 1
#define EPD_WIDTH                      792
#define EPD_HEIGHT                     528
#elif EPD_SELECT_E6_3_70_720_480 == 1
#define EPD_WIDTH                      720
#define EPD_HEIGHT                     480
#elif EPD_SELECT_E6_3_60_600_400 == 1
#define EPD_WIDTH                      600
#define EPD_HEIGHT                     400
#elif EPD_SELECT_E6_1_54_240_240 == 1
#define EPD_WIDTH                      240
#define EPD_HEIGHT                     240
#endif

//IO settings
//SCK--GPIO12(SCLK)
//SDIN---GPIO11(MOSI)
#define EPD_SCK_PIN  GPIO_NUM_48  //SCK
#define EPD_SDIN_PIN GPIO_NUM_47  //SDIN
#define EPD_BUSY_PIN GPIO_NUM_11  //BUSY
#define EPD_RST_PIN  GPIO_NUM_12  //RES
#define EPD_DC_PIN   GPIO_NUM_13  //DC
#define EPD_CS_PIN   GPIO_NUM_14  //CS

#define EPD_SPI_HOST SPI2_HOST

#define isEPD_W21_BUSY gpio_get_level(EPD_BUSY_PIN)
#define EPD_W21_RST_0  gpio_set_level(EPD_RST_PIN, 0)
#define EPD_W21_RST_1  gpio_set_level(EPD_RST_PIN, 1)
#define EPD_W21_DC_0   gpio_set_level(EPD_DC_PIN,  0)
#define EPD_W21_DC_1   gpio_set_level(EPD_DC_PIN,  1)
#define EPD_W21_CS_0   gpio_set_level(EPD_CS_PIN,  0)
#define EPD_W21_CS_1   gpio_set_level(EPD_CS_PIN,  1)

#define EPD_COLOR_BLACK   0x00  
#define EPD_COLOR_WHITE   0x11  
#define EPD_COLOR_GREEN   0x66  
#define EPD_COLOR_BLUE    0x55  
#define EPD_COLOR_RED     0x33  
#define EPD_COLOR_YELLOW  0x22  

/*********************************************************************
* TYPEDEFS
*/

/*********************************************************************
 * CONSTANTS
 */

/*********************************************************************
 * LOCAL VARIABLES
 */

/*********************************************************************
 * GLOBAL VARIABLES
 */

/*********************************************************************
 * LOCAL FUNCTIONS
 */

/*********************************************************************
 * GLOBAL FUNCTIONS
 */

/**
 * @brief 初始化电子纸硬件
 *
 * 此函数用于初始化电子纸硬件，包括GPIO和SPI配置，使其处于可用状态。
 * 在使用其他电子纸相关函数之前，必须先调用此函数。
 */
void hal_epd_init(void);

/**
 * @brief 释放电子纸硬件资源
 *
 * 此函数用于释放电子纸硬件资源，包括GPIO和SPI配置，使电子纸无法再使用。
 */
void hal_epd_deinit(void);

/**
 * @brief 初始化电子纸显示
 *
 * 此函数用于初始化电子纸显示参数，准备显示内容。
 */
void hal_epd_display_init(void);

/**
 * @brief 显示白色屏幕
 *
 * 此函数用于将电子纸显示为全白色。
 */
void hal_epd_display_white(void);

/**
 * @brief 显示黑色屏幕
 *
 * 此函数用于将电子纸显示为全黑色。
 */
void hal_epd_display_black(void);

/**
 * @brief 显示黄色屏幕
 *
 * 此函数用于将电子纸显示为全黄色。
 */
void hal_epd_display_yellow(void);

/**
 * @brief 显示红色屏幕
 *
 * 此函数用于将电子纸显示为全红色。
 */
void hal_epd_display_red(void);

/**
 * @brief 显示蓝色屏幕
 *
 * 此函数用于将电子纸显示为全蓝色。
 */
void hal_epd_display_blue(void);

/**
 * @brief 显示绿色屏幕
 *
 * 此函数用于将电子纸显示为全绿色。
 */
void hal_epd_display_green(void);

/**
 * @brief 显示图片数据
 *
 * 此函数用于在电子纸上显示图片数据。
 *
 * @param picData 图片数据指针
 */
void hal_epd_display_pic(const unsigned char* picData);

/**
 * @brief 显示 .film 文件数据
 *
 * 此函数用于在电子纸上显示 .film 格式的文件数据。
 * 根据 film.md 规范解析文件头和像素数据，支持 4bit 颜色编码。
 *
 * @param filmData .film 文件数据指针（包含 32 字节文件头）
 */
void hal_epd_display_film(const unsigned char* filmData);

/**
 * @brief 电子纸进入睡眠模式
 *
 * 此函数用于将电子纸进入睡眠模式，以节省电量。
 */
void hal_epd_sleep(void);

/**
 * @brief 电子纸关闭电源
 *
 * 此函数用于将电子纸电源关闭，以节省电量。
 */
void hal_epd_pwroff(void);

#ifdef __cplusplus
}
#endif

#endif /* __HAL_EPD_H__ */