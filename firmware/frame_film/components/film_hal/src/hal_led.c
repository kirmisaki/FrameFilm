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
 * FileName : /film_hal/src/hal_led.c
 * Author: Kiritro  Version: v0.1  Date: 2025/3/31
 * Description: Function introduction
 * ChangeLog: Change Notes
 *
***********************************************************/

/*********************************************************************
 * INCLUDES
 */
#include "led_strip.h"

#include "hal_led.h"
#include "sys_log.h"


/*********************************************************************
 * MACROS
 */
#define LED_TAG                        "HAL_LED"

#define RGB_LED_WS2812_PIN             (GPIO_NUM_17)
#define RGB_LED_NUMBERS                (2)
#define RGB_LED_RMT_RES_HZ             (10 * 1000 * 1000)

#define LED_STRIP_USE_DMA              (0)
#if LED_STRIP_USE_DMA
#define LED_STRIP_MEMORY_BLOCK_WORDS   (1024)
#else
#define LED_STRIP_MEMORY_BLOCK_WORDS   (0)
#endif

/*********************************************************************
* TYPEDEFS
*/
typedef struct
{
    uint32_t brightness;  // LED亮度值，范围通常为0到255
    uint32_t color;       // LED颜色值，通常是一个32位的颜色编码
} led_t;

/*********************************************************************
 * CONSTANTS
 */


/*********************************************************************
 * LOCAL VARIABLES
 */
static led_strip_handle_t m_rgb;
static led_t m_led =
{
    .brightness = 20,
    .color = LED_COLOR_LIGHT_BLUE
};

/*********************************************************************
 * GLOBAL VARIABLES
 */


/*********************************************************************
 * LOCAL FUNCTIONS
 */
static led_strip_handle_t configure_led(void);

/*********************************************************************
 * GLOBAL FUNCTIONS
 */



/**
 * @brief 初始化LED硬件
 *
 * 此函数用于初始化LED硬件，使其处于可用状态。
 * 在使用其他LED相关函数之前，必须先调用此函数。
 */
void hal_led_init(void)
{
    m_rgb = configure_led();

    hal_led_set_color(m_led.color);
    hal_led_set_brightness(m_led.brightness);
}

/**
 * @brief 获取LED的当前亮度
 *
 * 此函数用于获取LED的当前亮度值。
 *
 * @return 当前LED的亮度值，范围通常为0到255。
 */
uint32_t hal_led_get_brightness(void)
{
    return m_led.brightness;
}

/**
 * @brief 设置LED的亮度
 *
 * 此函数用于设置LED的亮度。
 *
 * @param brightness 要设置的亮度值，范围通常为0到255。
 */
void hal_led_set_brightness(uint32_t brightness)
{
    m_led.brightness = brightness;
    hal_led_set_color(m_led.color);
}

/**
 * @brief 设置LED的颜色
 *
 * 此函数用于设置LED的颜色。
 *
 * @param color 要设置的颜色值，通常是一个32位的颜色编码。
 */
void hal_led_set_color(uint32_t color)
{
    uint32_t r = 0, g = 0, b = 0;

    m_led.color = color;
    if (m_led.brightness > 0 && m_led.brightness <= 100)
    {
        r = (((color >> 16) & 0xff) * m_led.brightness / 100);
        g = (((color >> 8) & 0xff) * m_led.brightness / 100);
        b = ((color & 0xff) * m_led.brightness / 100);
    }

    for(int i = 0; i < RGB_LED_NUMBERS; i++)
    {
        led_strip_set_pixel(m_rgb, i, r, g, b);
    }
    led_strip_refresh(m_rgb);
}

/**
 * @brief 获取LED的当前颜色
 *
 * 此函数用于获取LED的当前颜色值。
 *
 * @return 当前LED的颜色值，通常是一个32位的颜色编码。
 */
uint32_t hal_led_get_color(void)
{
    return m_led.color;
}

led_strip_handle_t configure_led(void)
{
    // LED strip general initialization, according to your led board design
    led_strip_config_t strip_config =
    {
        .strip_gpio_num = RGB_LED_WS2812_PIN, // The GPIO that connected to the LED strip's data line
        .max_leds = RGB_LED_NUMBERS,      // The number of LEDs in the strip,
        .led_model = LED_MODEL_WS2812,        // LED strip model
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB, // The color order of the strip: GRB
        .flags = {
            .invert_out = false, // don't invert the output signal
        }
    };

    // LED strip backend configuration: RMT
    led_strip_rmt_config_t rmt_config =
    {
        .clk_src = RMT_CLK_SRC_DEFAULT,        // different clock source can lead to different power consumption
        .resolution_hz = RGB_LED_RMT_RES_HZ, // RMT counter clock frequency
        .mem_block_symbols = LED_STRIP_MEMORY_BLOCK_WORDS, // the memory block size used by the RMT channel
        .flags = {
            .with_dma = LED_STRIP_USE_DMA,     // Using DMA can improve performance when driving more LEDs
        }
    };

    // LED Strip object handle
    led_strip_handle_t led_strip;
    SYS_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
    sys_logi(LED_TAG, "Created LED strip object with RMT backend");
    return led_strip;
}

