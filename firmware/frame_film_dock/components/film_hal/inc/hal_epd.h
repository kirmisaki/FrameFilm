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

#define EPD_WIDTH                      760
#define EPD_HEIGHT                     568
#define EPD_PANEL_ID                   0x06    // 3.64" 760×568

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

// 屏幕插入检测开关（1=启用，0=关闭，关闭后一律视为已插入）
#define EPD_DETECT_ENABLE             1

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
 * @brief 检测屏幕是否已插入
 *
 * 返回内部缓存的屏幕插入状态，由 hal_epd_detect_insert() 周期更新。
 *
 * @return true 屏幕已插入；false 屏幕未插入或检测失败
 */
bool hal_epd_is_inserted(void);

/**
 * @brief 执行一次屏幕插入检测并更新内部状态
 *
 * 实际 I2C 探测只在此函数中进行，应由监控任务周期调用，
 * 避免多个线程同时访问 I2C 总线。
 */
void hal_epd_detect_insert(void);

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
 * @brief 局部窗口刷新（单面板）
 *
 * 仅对指定面板的局部窗口更新图像，可用于时钟等小区域动态刷新。
 * 窗口坐标以单面板为参考：整屏 1200 宽由 CS0(左)/CS1(右) 两块各 600 宽的面板拼接，
 * 左半屏局部窗口用 panel=0，右半屏用 panel=1（x_start 相对该面板）。
 * 约束（与官方驱动一致）：x_start 为 4 的倍数，x_start+width <= 600，
 * y_start 为 2 的倍数，y_start+height <= 1600 且为 2 的倍数。
 *
 * @param panel 面板编号：0=CS0(左面板)，1=CS1(右面板)
 * @param x_start 窗口起始 X（像素，相对该面板，需为 4 的倍数）
 * @param y_start 窗口起始 Y（像素，需为 2 的倍数）
 * @param width 窗口宽度（像素，需满足 x_start+width <= 600）
 * @param height 窗口高度（像素，需满足 y_start+height <= 1600）
 * @param data 窗口图像数据（4bpp，字节数 = width*height/2）
 * @param display_enable 非 0 时立即执行刷新（PON/DRF/POF），否则只写入窗口数据
 * @return 0 成功；负数参数错误（-1~-8 与官方驱动一致，-9 data 为空）
 */
int32_t hal_epd_partial_update(uint8_t panel, uint16_t x_start, uint16_t y_start,
                               uint16_t width, uint16_t height,
                               const unsigned char* data, uint8_t display_enable);

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