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

/* 本工程固定一块屏：GDEM075T41WT 7.5" 800×480 黑白 1bpp（U8 单面板 4 线 SPI）。
 * 参考驱动见 E:\GDEM075T41WT_Arduino（Display_EPD_W21.cpp）。
 * 面板数据 0x00=白、0xFF=黑，MSB 在前。 */
#define EPD_WIDTH                      800
#define EPD_HEIGHT                     480
#define EPD_PANEL_ID                   0x09    // 7.50" 800×480 黑白 1bpp

//IO settings（FrameFilmCommon 主板 U8 10Pin FPC）
#define EPD_SCK_PIN  GPIO_NUM_48  //SCK
#define EPD_SDIN_PIN GPIO_NUM_47  //SDIN
#define EPD_BUSY_PIN GPIO_NUM_11  //BUSY（低电平=忙）
#define EPD_RST_PIN  GPIO_NUM_12  //RES
#define EPD_DC_PIN   GPIO_NUM_13  //DC
#define EPD_CS_PIN   GPIO_NUM_14  //CS

/* 单面板方案不使用（悬空，保持高阻）：
 *   EPD_BAK = GPIO10、EPD_CS2 = GPIO21 */

#define EPD_SPI_HOST SPI2_HOST

#define isEPD_W21_BUSY gpio_get_level(EPD_BUSY_PIN)
#define EPD_W21_RST_0  gpio_set_level(EPD_RST_PIN, 0)
#define EPD_W21_RST_1  gpio_set_level(EPD_RST_PIN, 1)
#define EPD_W21_DC_0   gpio_set_level(EPD_DC_PIN,  0)
#define EPD_W21_DC_1   gpio_set_level(EPD_DC_PIN,  1)
#define EPD_W21_CS_0   gpio_set_level(EPD_CS_PIN,  0)
#define EPD_W21_CS_1   gpio_set_level(EPD_CS_PIN,  1)

/* 面板数据编码（GDEM075T41WT，1bpp）：0x00=白，0xFF=黑，MSB 在前。
 * 注意：.film v1 的 ColorTable 存的是 RGB332 风格颜色值
 * （0x00 黑 / 0xFF 白 / 0xFC 黄 / 0xE0 红 / 0x03 蓝 / 0x1C 绿），
 * 由驱动按亮度阈值转成 1bpp，不要和面板字节混用。 */
#define EPD_MONO_WHITE    0x00
#define EPD_MONO_BLACK    0xFF

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
 * @brief 初始化电子纸显示（整屏/全刷波形）
 *
 * 此函数用于初始化电子纸显示参数，准备显示内容。
 * 每次整屏刷新前调用；刷新完成后旧帧会留在控制器内作为差分基准。
 */
void hal_epd_display_init(void);

/**
 * @brief 初始化电子纸显示（差分/局部刷新波形）
 *
 * 切换到 UC8179 的 OTP 差分波形（通过强制温度选择），用于无闪白局部刷新。
 * 复位不会清空控制器 RAM，但要求屏上已有整屏刷新作为基准。
 * 通常无需直接调用：整屏刷新后调用 hal_epd_display_film_partial() /
 * hal_epd_partial_update() 会自动切换。
 */
void hal_epd_display_init_partial(void);

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
 * @brief 显示 .film 文件数据（整屏刷新）
 *
 * 此函数用于在电子纸上显示 .film 格式的文件数据。
 * 根据 film.md 规范解析文件头和像素数据，支持 4bit 颜色编码与 MonoFast 1bpp。
 *
 * @param filmData .film 文件数据指针（包含 32 字节文件头）
 */
void hal_epd_display_film(const unsigned char* filmData);

/**
 * @brief 显示 .film 文件数据（整屏差分/局部刷新）
 *
 * 使用 UC8179 差分波形，只驱动与屏上旧帧不同的像素，无整屏闪白。
 * 需要屏上已有基准帧（先做过一次整屏刷新）；没有基准时自动回退为整屏刷新。
 *
 * @param filmData .film 文件数据指针（包含 32 字节文件头）
 */
void hal_epd_display_film_partial(const unsigned char* filmData);

/**
 * @brief 局部窗口刷新
 *
 * 在指定矩形窗口内更新 1bpp 位图，其余区域保持不变。
 * 要求屏上已有整屏刷新作为差分基准，否则返回失败。
 *
 * @param panel 面板编号（本工程恒为 0）
 * @param x_start 窗口起始 X（像素）
 * @param y_start 窗口起始 Y（像素）
 * @param width 窗口宽度（像素）
 * @param height 窗口高度（像素）
 * @param data 窗口图像数据（1bpp，每行 (width+7)/8 字节，MSB 在前，1=黑）
 * @param display_enable 非 0 时立即刷新
 * @return 0 成功；负数失败
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