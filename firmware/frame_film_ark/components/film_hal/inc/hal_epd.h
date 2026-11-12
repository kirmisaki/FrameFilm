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

/* 单机型固件：屏幕固定 E6 3.70" 720×480（面板 ID 0x02），驱动见 hal_epd_370.c。
   原先的 EPD_SELECT_E6_* 选择宏与其余屏的分支已删除（需要选屏请用 firmware/frame_film/）。 */
#define EPD_WIDTH                      720
#define EPD_HEIGHT                     480
#define EPD_PANEL_ID                   0x02    // 3.70" 720×480

//IO settings
//SCK--GPIO12(SCLK)
//SDIN---GPIO11(MOSI)
// 3.70" 屏：SPI2 四线（带 DC）
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

/**
 * @brief 电子纸能力位（hal_epd_get_capabilities 返回）
 */
typedef enum
{
    EPD_CAP_4BPP     = 1 << 0,  // 所有屏都有（v1 4bpp）
    EPD_CAP_8BPP     = 1 << 1,  // 3.7 屏 8bpp 索引色（ColorQual/ColorFast）
    EPD_CAP_MONOFAST = 1 << 2,  // 3.7 屏 MonoFast 差分局刷
    EPD_CAP_PARTIAL  = 1 << 3,  // 局部窗口刷新
} epd_cap_t;

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
 * @brief 独占面板（递归锁）
 *
 * 驱动内部的 SPI / GPIO / BUSY 状态不是线程安全的，两个任务同时驱动会
 * 互相打断刷新、卡在 BUSY 等待里。以下公开绘制入口已各自在锁内执行；
 * 需要把"多个调用"当成一个原子事务时（如 init + 刷图 + 断电），
 * 由调用方用 hal_epd_lock()/hal_epd_unlock() 把整段包住。
 *
 * 锁可重入：被包住的段里再调公开入口不会自锁。等待无超时。
 */
void hal_epd_lock(void);
void hal_epd_unlock(void);

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

/* 原生面板色码 4bpp：每字节高 nibble 为左像素；仅 Ark UI 彩色合成使用。 */
void hal_epd_display_native_4bpp(const unsigned char *pixels);

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
 * @brief 查询电子纸能力位
 *
 * 返回 EPD_CAP_* 组合，供 app 层决定渲染路径：
 * - EPD_CAP_4BPP     所有屏支持（v1 4bpp）
 * - EPD_CAP_8BPP     3.7 屏 8bpp 索引色
 * - EPD_CAP_MONOFAST 3.7 屏 MonoFast 差分局刷
 * - EPD_CAP_PARTIAL  局部窗口刷新（本机型未使用）
 *
 * @return 能力位组合
 */
uint32_t hal_epd_get_capabilities(void);

/**
 * @brief 显示 8bpp 索引色图片（带刷新模式，仅 E6 3.70" 720x480）
 *
 * 输入为 720x480 个 8bpp 颜色索引（取值范围 0-63），驱动按 spectra
 * 算法拆成高/低两个 3bit 平面，配合波形表多相位刷新，用时间
 * 抖动在 6 色硬件上混出中间色。
 *
 * @param index8Data 720*480 字节的 8bpp 颜色索引缓冲
 * @param mode 刷新模式：0=ColorFast（2 相），1=ColorQual（3 相）
 */
void hal_epd_display_8bpp_mode(const unsigned char *index8Data, uint8_t mode);

#if defined(CONFIG_ARK_EPD_ABC_PREVIEW) || defined(CONFIG_ARK_EPD_ABC_DIAG)
/* Experimental Format 0x7F ABC9 body: LE16 A[8:6]/B[5:3]/C[2:0].
 * Validate the complete frame before initializing or refreshing the panel. */
esp_err_t hal_epd_display_abc9(const uint8_t *pixels, uint32_t size);
#endif

/**
 * @brief 黑白快刷（MonoFast 局刷，仅 E6 3.70" 720x480）
 *
 * 输入 1bpp 位图（720*480/8 = 43200 字节，每字节 8 像素、MSB 在前，
 * 1 为黑、0 为白）。驱动对每个像素编码 (上一帧, 当前帧) 的 2bit 跳变，
 * 配合 mono_fast 波形只驱动变化的像素，实现快速、无闪烁的差分刷新。
 * 首次调用会自动初始化 spectra，之后跨调用保持上一帧状态。
 *
 * 默认每次调用都会自己做一趟 PON/POF（画完即断电）。需要连续逐帧刷新
 * （动图播放、进度条这类"一个会话内多帧"的场景）时，用
 * hal_epd_mono_session_begin()/end() 把这一趟省掉，见下。
 *
 * 注：位图约定 `1` 为黑、`0` 为白；面板 mono 跳变码的实际码位极性与之相反，
 * 由驱动内部统一映射（见 `hal_epd_370.c` 的 `MONO_CODE_BIT_WHITE`），调用方无需自行反色。
 *
 * @param mono_bitmap 1bpp 位图缓冲（720*480/8 字节）
 */
void hal_epd_display_mono(const unsigned char *mono_bitmap);

/**
 * @brief 打开黑白快刷会话：会话内帧与帧之间保持面板上电
 *
 * 省掉每帧约 100ms 的 PON/POF（实测一帧 197ms 与 410ms 的差别）。
 * **只适合连续逐帧刷新的场景**（动图播放、开机进度条这种一个会话内多帧）；
 * 一次性绘制不要用，否则画完面板会一直带电。
 *
 * 必须与 hal_epd_mono_session_end() 成对使用。
 */
void hal_epd_mono_session_begin(void);

/**
 * @brief 结束黑白快刷会话（并断电）
 *
 * 把会话期间省下的 POF 补上；已在断电态时是空操作，可重复调用。
 */
void hal_epd_mono_session_end(void);

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
