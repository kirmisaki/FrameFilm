// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef __HAL_LED_H__
#define __HAL_LED_H__
#define LED_COLOR_RED 0xff0000
#define LED_COLOR_GREEN 0x00ff00
#define LED_COLOR_BLUE 0x0000ff
#define LED_COLOR_WHITE 0xffffff
#define LED_COLOR_BLACK 0
void hal_led_init(void);
void hal_led_deinit(void);
void hal_led_set_color(uint32_t color);
void hal_led_set_brightness(uint32_t brightness);
uint32_t hal_led_get_brightness(void);
uint32_t hal_led_get_color(void);
#endif
