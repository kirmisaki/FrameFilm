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
 * FileName : /film_hal/src/hal_pwr.c
 * Author: Kiritro  Version: v0.1  Date: 2025/4/4
 * Description: Power management HAL layer implementation
 * ChangeLog: Change Notes
 *
***********************************************************/

/*********************************************************************
 * INCLUDES
 */
#include "esp_sleep.h"
#include "driver/gpio.h"
#include "esp_pm.h"

#include "sys_log.h"
#include "hal_pwr.h"

/*********************************************************************
 * MACROS
 */
#define PWR_TAG                 "HAL_PWR"

#define WAKEUP_GPIO_NUM         (5)
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
    
    sys_logi(PWR_TAG, "Enabling GPIO wakeup on GPIO %d", WAKEUP_GPIO_NUM);
    
    esp_err_t ret = esp_sleep_enable_gpio_wakeup();
    if (ret != ESP_OK)
    {
        sys_loge(PWR_TAG, "Failed to enable GPIO wakeup: %s", esp_err_to_name(ret));
        return;
    }
    
    gpio_wakeup_enable(WAKEUP_GPIO_NUM, WAKEUP_GPIO_LEVEL ? GPIO_INTR_HIGH_LEVEL : GPIO_INTR_LOW_LEVEL);
    
    sys_logi(PWR_TAG, "Configured wakeup source: GPIO %d, level %d", WAKEUP_GPIO_NUM, WAKEUP_GPIO_LEVEL);
    
    esp_deep_sleep_start();
}

bool hal_pwr_check_wakeup(void)
{
    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
    
    if (wakeup_reason == ESP_SLEEP_WAKEUP_GPIO)
    {
        sys_logi(PWR_TAG, "Wakeup from deep sleep by GPIO");
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
