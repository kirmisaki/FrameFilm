#ifndef __HAL_LED_H__
#define __HAL_LED_H__


/*********************************************************************
 * INCLUDES
 */
#include "led_strip.h"


/*********************************************************************
 * CPPMIX
 */
#ifdef __cplusplus
extern "C" {
#endif

/*********************************************************************
 * MACROS
 */


/*********************************************************************
* TYPEDEFS
*/
// 定义LED颜色结构体
typedef struct
{
    uint8_t r;
    uint8_t g;
    uint8_t b;
} led_color_t;

// 定义LED对象结构体
typedef struct
{
    led_color_t current_color;
    uint8_t brightness;
    led_strip_config_t strip_config;
    led_strip_handle_t strip_handle;
} led_object_t;

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
 * @brief 初始化LED硬件
 *
 * 此函数用于初始化LED硬件，使其处于可用状态。
 * 在使用其他LED相关函数之前，必须先调用此函数。
 */
extern void hal_led_init(void);

/**
 * @brief 获取LED的当前亮度
 *
 * 此函数用于获取LED的当前亮度值。
 *
 * @return 当前LED的亮度值，范围通常为0到255。
 */
extern uint32_t hal_led_get_brightness(void);

/**
 * @brief 设置LED的亮度
 *
 * 此函数用于设置LED的亮度。
 *
 * @param brightness 要设置的亮度值，范围通常为0到255。
 */
extern void hal_led_set_brightness(uint32_t brightness);

/**
 * @brief 设置LED的颜色
 *
 * 此函数用于设置LED的颜色。
 *
 * @param color 要设置的颜色值，通常是一个32位的颜色编码。
 */
extern void hal_led_set_color(uint32_t color);

/**
 * @brief 获取LED的当前颜色
 *
 * 此函数用于获取LED的当前颜色值。
 *
 * @return 当前LED的颜色值，通常是一个32位的颜色编码。
 */
extern uint32_t hal_led_get_color(void);

#ifdef __cplusplus
}
#endif

#endif /* __HAL_LED_H__ */
