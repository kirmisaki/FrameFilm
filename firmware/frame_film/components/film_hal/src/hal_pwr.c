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
#include "driver/rtc_io.h"
#include "esp_pm.h"

#include "sys_log.h"
#include "hal_pwr.h"

/*********************************************************************
 * MACROS
 */
#define PWR_TAG                 "HAL_PWR"

#define WAKEUP_GPIO_NUM         (9)
#define WAKEUP_GPIO_LEVEL       (0)


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

    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
    
    switch (wakeup_reason)
    {
        case ESP_SLEEP_WAKEUP_EXT0:
            sys_logi(PWR_TAG, "Wakeup caused by external signal using RTC_IO");
            break;
        case ESP_SLEEP_WAKEUP_EXT1:
            sys_logi(PWR_TAG, "Wakeup caused by external signal using RTC_CNTL");
            break;
        case ESP_SLEEP_WAKEUP_TIMER:
            sys_logi(PWR_TAG, "Wakeup caused by timer");
            break;
        case ESP_SLEEP_WAKEUP_TOUCHPAD:
            sys_logi(PWR_TAG, "Wakeup caused by touchpad");
            break;
        case ESP_SLEEP_WAKEUP_ULP:
            sys_logi(PWR_TAG, "Wakeup caused by ULP program");
            break;
        case ESP_SLEEP_WAKEUP_GPIO:
            sys_logi(PWR_TAG, "Wakeup caused by GPIO");
            break;
        case ESP_SLEEP_WAKEUP_UART:
            sys_logi(PWR_TAG, "Wakeup caused by UART");
            break;
        default:
            sys_logi(PWR_TAG, "Wakeup was not caused by deep sleep: %d", wakeup_reason);
            break;
    }
}

void hal_pwr_enter_sleep(void)
{
    sys_logi(PWR_TAG, "Entering deep sleep");
    
    esp_err_t ret = esp_sleep_enable_ext0_wakeup(WAKEUP_GPIO_NUM, 0);
    if (ret != ESP_OK)
    {
        sys_loge(PWR_TAG, "Failed to enable GPIO wakeup: %s", esp_err_to_name(ret));
        return;
    }
    
    if (m_timer_wakeup_us > 0)
    {
        ret = esp_sleep_enable_timer_wakeup(m_timer_wakeup_us);
        if (ret != ESP_OK)
        {
            sys_loge(PWR_TAG, "Failed to enable timer wakeup: %s", esp_err_to_name(ret));
        }
        else
        {
            sys_logi(PWR_TAG, "Configured timer wakeup: %llu us", m_timer_wakeup_us);
        }
    }

    sys_logi(PWR_TAG, "Configured wakeup source: GPIO %d, level %d", WAKEUP_GPIO_NUM, WAKEUP_GPIO_LEVEL);
    
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
