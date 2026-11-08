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
 * FileName : /film_hal/src/hal_pwr.c
 * Author: Kiritro  Version: v0.1  Date: 2026/4/4
 * Description: Power management HAL layer implementation
 * ChangeLog: Change Notes
 *
 *********************************************************************/

/*********************************************************************
 * INCLUDES
 */
#include "esp_sleep.h"
#include "driver/gpio.h"
#include "esp_pm.h"

#include "sys_log.h"
#include "sys_cfg.h"
#include "hal_pwr.h"
#if (SYS_NFC_ENABLE == 1)
#include "hal_nfc.h"
#endif

/*********************************************************************
 * MACROS
 */
#define PWR_TAG                 "HAL_PWR"

#define WAKEUP_GPIO_LEVEL       (0)     // 低电平唤醒（按键按下为低）
#define WAKEUP_GPIO_NUM         (5)

#define PERI_PWR_PIN            GPIO_NUM_21     //外设供电管脚
#define PERI_PWR_ON             gpio_set_level(PERI_PWR_PIN, 1)
#define PERI_PWR_OFF            gpio_set_level(PERI_PWR_PIN, 0)


/*********************************************************************
* TYPEDEFS
*/


/*********************************************************************
 * CONSTANTS
 */


/*********************************************************************
 * LOCAL VARIABLES
 */
static uint64_t m_timer_wakeup_us = 0;


/*********************************************************************
 * GLOBAL VARIABLES
 */


/*********************************************************************
 * LOCAL FUNCTIONS
 */


/*********************************************************************
 * GLOBAL FUNCTIONS
 */

void hal_pwr_init(void)
{
    sys_logi(PWR_TAG, "pwr init");

    // 初始化外设供电管脚
    gpio_config_t io_conf = {0};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = (1ULL << PERI_PWR_PIN);
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&io_conf);
    PERI_PWR_ON;

    // esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
    // switch (wakeup_reason)
    // {
    //     case ESP_SLEEP_WAKEUP_EXT0:
    //         sys_logi(PWR_TAG, "Wakeup caused by external signal using RTC_IO");
    //         break;
    //     case ESP_SLEEP_WAKEUP_EXT1:
    //         sys_logi(PWR_TAG, "Wakeup caused by external signal using RTC_CNTL");
    //         break;
    //     case ESP_SLEEP_WAKEUP_TIMER:
    //         sys_logi(PWR_TAG, "Wakeup caused by timer");
    //         break;
    //     case ESP_SLEEP_WAKEUP_TOUCHPAD:
    //         sys_logi(PWR_TAG, "Wakeup caused by touchpad");
    //         break;
    //     case ESP_SLEEP_WAKEUP_ULP:
    //         sys_logi(PWR_TAG, "Wakeup caused by ULP program");
    //         break;
    //     case ESP_SLEEP_WAKEUP_GPIO:
    //         sys_logi(PWR_TAG, "Wakeup caused by GPIO");
    //         break;
    //     case ESP_SLEEP_WAKEUP_UART:
    //         sys_logi(PWR_TAG, "Wakeup caused by UART");
    //         break;
    //     default:
    //         sys_logi(PWR_TAG, "Wakeup was not caused by deep sleep: %d", wakeup_reason);
    //         break;
    // }
}

bool hal_pwr_wakeup_prepare(void)
{
    esp_err_t ret = esp_sleep_enable_ext0_wakeup(WAKEUP_GPIO_NUM, WAKEUP_GPIO_LEVEL);
    if (ret != ESP_OK)
    {
        sys_loge(PWR_TAG, "Failed to enable GPIO wakeup: %s", esp_err_to_name(ret));
        return false;
    }

    if (m_timer_wakeup_us > 0)
    {
        ret = esp_sleep_enable_timer_wakeup(m_timer_wakeup_us);
        if (ret != ESP_OK)
        {
            /* 定时唤醒配不上不该拦着入睡：只是少了一次自动唤醒 */
            sys_loge(PWR_TAG, "Failed to enable timer wakeup: %s", esp_err_to_name(ret));
        }
        else
        {
            sys_logi(PWR_TAG, "Configured timer wakeup: %llu us", m_timer_wakeup_us);
        }
    }

    sys_logi(PWR_TAG, "Configured wakeup source: GPIO %d, level %d", WAKEUP_GPIO_NUM, WAKEUP_GPIO_LEVEL);
    return true;
}

void hal_pwr_enter_sleep(void)
{
    sys_logi(PWR_TAG, "Entering deep sleep");
    
    // 关闭外设供电管脚
    PERI_PWR_OFF;
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << PERI_PWR_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    
    // GPIO5 是 ext0 唤醒输入；隔离并 hold 它会使按键无法唤醒。

    esp_deep_sleep_start();
}

void hal_pwr_set_timer_wakeup(uint32_t minutes)
{
    if (minutes > 0)
    {
        m_timer_wakeup_us = (uint64_t)minutes * 60 * 1000000;
        sys_logi(PWR_TAG, "Timer wakeup set: %d min (%llu us)", minutes, m_timer_wakeup_us);
    }
    else
    {
        m_timer_wakeup_us = 0;
        sys_logi(PWR_TAG, "Timer wakeup disabled");
    }
}

uint32_t hal_pwr_get_timer_wakeup(void)
{
    return (uint32_t)(m_timer_wakeup_us / (60ULL * 1000000ULL));
}

bool hal_pwr_check_wakeup(void)
{
    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
    
    if (wakeup_reason == ESP_SLEEP_WAKEUP_GPIO || wakeup_reason == ESP_SLEEP_WAKEUP_TIMER)
    {
        sys_logi(PWR_TAG, "Wakeup from deep sleep by %s", wakeup_reason == ESP_SLEEP_WAKEUP_TIMER ? "Timer" : "GPIO");
        return true;
    }
    else if (wakeup_reason == ESP_SLEEP_WAKEUP_UNDEFINED)
    {
        sys_logi(PWR_TAG, "Normal boot (not from deep sleep)");
        return false;
    }
    
    sys_logi(PWR_TAG, "Wakeup from other source: %d", wakeup_reason);
    return false;
}

bool hal_pwr_wake_condition_met(void)
{
    /* 查的就是 hal_pwr_enter_sleep() 里 esp_sleep_enable_ext0_wakeup() 配的那一路。
       用 WAKEUP_GPIO_LEVEL 而不是某颗键的有效电平：这是"唤醒源"的语义，
       不是"按键"的语义，两者恰好重合但不该混为一谈。 */
    if(gpio_get_level(WAKEUP_GPIO_NUM) == WAKEUP_GPIO_LEVEL)
    {
        return true;
    }
#if (SYS_NFC_ENABLE == 1)
    /* NFC 场还在时也不要入睡：EXT1 是高电平触发，IRQ 高会立刻醒回来。 */
    if(hal_nfc_irq_active())
    {
        return true;
    }
#endif
    return false;
}

hal_pwr_wake_cause_t hal_pwr_get_wakeup_cause(void)
{
    esp_sleep_wakeup_cause_t reason = esp_sleep_get_wakeup_cause();
    switch(reason)
    {
    case ESP_SLEEP_WAKEUP_EXT0:  return HAL_PWR_WAKE_KEY;
    case ESP_SLEEP_WAKEUP_TIMER: return HAL_PWR_WAKE_TIMER;
    case ESP_SLEEP_WAKEUP_EXT1:
#if (SYS_NFC_ENABLE == 1)
        if(esp_sleep_get_ext1_wakeup_status() & (1ULL << HAL_NFC_PIN_IRQ))
        {
            return HAL_PWR_WAKE_NFC;
        }
#endif
        return HAL_PWR_WAKE_OTHER;
    default: return HAL_PWR_WAKE_NONE;
    }
}
