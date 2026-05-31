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
 * FileName : /film_hal/src/hal_led.c
 * Author: Kiritro  Version: v0.1  Date: 2026/3/31
 * Description: Function introduction
 * ChangeLog: Change Notes
 *
 *********************************************************************/

/*********************************************************************
 * INCLUDES
 */
#include "led_strip.h"
#include "driver/gpio.h"

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
    .brightness = 50,
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

void hal_led_deinit(void)
{
    hal_led_set_color(LED_COLOR_BLACK);

    if (m_rgb != NULL)
    {
        led_strip_del(m_rgb);
        m_rgb = NULL;
    }

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << RGB_LED_WS2812_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    sys_logi(LED_TAG, "LED deinitialized");
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

