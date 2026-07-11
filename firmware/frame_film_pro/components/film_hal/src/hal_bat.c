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
 * FileName : /film_hal/src/hal_bat.c
 * Author: Kiritro  Version: v0.1  Date: 2026/4/3
 * Description: Function introduction
 * ChangeLog: Change Notes
 *
 *********************************************************************/

/*********************************************************************
 * INCLUDES
 */
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "sys_log.h"
#include "hal_bat.h"

/*********************************************************************
 * MACROS
 */
#define BAT_TAG             "HAL_BAT"

#define BAT_VOL_MAX         (4140)    //最大电压
#define BAT_VOL_MIN         (3300)    //最小电压
#define VOLTAGE_LEVEL_COUNT (11)      //电压等级数量

#define BAT_ADC_EN_PIN      (GPIO_NUM_8)  // ADC采样使能引脚
#define BAT_SAMPLE_COUNT    (10)          // 采样次数
#define BAT_SAMPLE_DELAY_MS (2)           // 采样间隔(ms)
#define BAT_STABIL_DELAY_MS (100)         // 电容稳定时间(ms)


/*********************************************************************
* TYPEDEFS
*/
typedef struct
{
    bool do_calibration_chan0; //是否进行ADC0校准
    int adc_raw;
    int voltage;
    int level;
} bat_t;

/*********************************************************************
 * CONSTANTS
 */
static const int voltage_levels[VOLTAGE_LEVEL_COUNT] = {3300, 3680, 3733, 3770, 3790, 3840, 3890, 3920, 3970, 4070, 4140};
static const int battery_levels[VOLTAGE_LEVEL_COUNT] = {0, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100};

/*********************************************************************
 * LOCAL VARIABLES
 */
static adc_oneshot_unit_handle_t adc_handle;
static adc_cali_handle_t adc_cali_chan0_handle;
static bat_t m_bat = {false, 0, 0, 0};


/*********************************************************************
 * GLOBAL VARIABLES
 */


/*********************************************************************
 * LOCAL FUNCTIONS
 */
static bool hal_adc_cali_chan0_handle(adc_unit_t unit, adc_channel_t channel, adc_atten_t atten, adc_cali_handle_t *out_handle);
static int hal_bat_voltage_to_level(int voltage);


/*********************************************************************
 * GLOBAL FUNCTIONS
 */



void hal_bat_init(void)
{
    //-------------ADC使能引脚配置---------------//
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BAT_ADC_EN_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,  // 使能下拉，确保默认低电平
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    gpio_set_level(BAT_ADC_EN_PIN, 0);  // 初始状态关闭采样

    //-------------ADC1 Init---------------//
    adc_oneshot_unit_init_cfg_t init_config =
    {
        .unit_id = ADC_UNIT_1,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &adc_handle));

    //-------------ADC1 Config---------------//
    adc_oneshot_chan_cfg_t config =
    {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, ADC_CHANNEL_0, &config));
    m_bat.do_calibration_chan0 = hal_adc_cali_chan0_handle(ADC_UNIT_1, ADC_CHANNEL_0, ADC_ATTEN_DB_12, &adc_cali_chan0_handle);

    sys_logi(BAT_TAG, "bat init");

    // 读取一次电池电压
    hal_bat_get_level();
}

int hal_bat_get_level(void)
{
    // 使能ADC采样电路
    gpio_set_level(BAT_ADC_EN_PIN, 1);
    // 等待电容稳定
    vTaskDelay(pdMS_TO_TICKS(BAT_STABIL_DELAY_MS));

    if (m_bat.do_calibration_chan0)
    {
        int raw_samples[BAT_SAMPLE_COUNT];
        int voltage_samples[BAT_SAMPLE_COUNT];
        int min_idx = 0, max_idx = 0;
        int32_t sum = 0;

        // 多次采样
        for (int i = 0; i < BAT_SAMPLE_COUNT; i++)
        {
            ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, ADC_CHANNEL_0, &raw_samples[i]));
            int voltage = 0;
            ESP_ERROR_CHECK(adc_cali_raw_to_voltage(adc_cali_chan0_handle, raw_samples[i], &voltage));
            voltage_samples[i] = voltage;

            // 记录最大最小值索引
            if (voltage_samples[i] < voltage_samples[min_idx]) min_idx = i;
            if (voltage_samples[i] > voltage_samples[max_idx]) max_idx = i;

            if (i < BAT_SAMPLE_COUNT - 1)
            {
                vTaskDelay(pdMS_TO_TICKS(BAT_SAMPLE_DELAY_MS));
            }
        }

        sys_logd(BAT_TAG, "Raw samples: %d, %d, %d, %d, %d, %d, %d, %d, %d, %d",
                 raw_samples[0], raw_samples[1], raw_samples[2], raw_samples[3], raw_samples[4],
                 raw_samples[5], raw_samples[6], raw_samples[7], raw_samples[8], raw_samples[9]);

        // 去掉最大最小值后求平均
        for (int i = 0; i < BAT_SAMPLE_COUNT; i++)
        {
            if (i != min_idx && i != max_idx)
            {
                sum += voltage_samples[i];
            }
        }
        m_bat.voltage = sum / (BAT_SAMPLE_COUNT - 2);

        sys_logd(BAT_TAG, "ADC%d Channel[%d] Avg Voltage: %d mV (removed min:%d, max:%d)",
                 ADC_UNIT_1 + 1, ADC_CHANNEL_0, m_bat.voltage, voltage_samples[min_idx], voltage_samples[max_idx]);
    }
    else
    {
        m_bat.voltage = 0;
    }

    // 关闭ADC采样电路，防止分压电路漏电
    gpio_set_level(BAT_ADC_EN_PIN, 0);

    m_bat.voltage = m_bat.voltage * 3.06; // 20k-10k 分压（修正系数）
    m_bat.level = hal_bat_voltage_to_level(m_bat.voltage);
    sys_logi(BAT_TAG, "bat voltage: %d mV bat level: %d", m_bat.voltage, m_bat.level);
    return m_bat.level;
}

int hal_bat_get_percent(void)
{
    return m_bat.level;
}

static int hal_bat_voltage_to_level(int voltage)
{
    if (voltage >= BAT_VOL_MAX)
    {
        return 100;
    }
    else if (voltage <= BAT_VOL_MIN)
    {
        return 0;
    }
    else
    {
        for (int i = 0; i < VOLTAGE_LEVEL_COUNT - 1; i++)
        {
            if (voltage >= voltage_levels[i] && voltage < voltage_levels[i + 1])
            {
                return battery_levels[i] + (voltage - voltage_levels[i]) * (battery_levels[i + 1] - battery_levels[i]) / (voltage_levels[i + 1] - voltage_levels[i]);
            }
        }
    }
    return 0;
}

static bool hal_adc_cali_chan0_handle(adc_unit_t unit, adc_channel_t channel, adc_atten_t atten, adc_cali_handle_t *out_handle)
{
    adc_cali_handle_t handle = NULL;
    esp_err_t ret = ESP_FAIL;
    bool calibrated = false;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    if (!calibrated)
    {
        sys_logi(BAT_TAG, "calibration scheme version is %s", "Curve Fitting");
        adc_cali_curve_fitting_config_t cali_config =
        {
            .unit_id = unit,
            .chan = channel,
            .atten = atten,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        ret = adc_cali_create_scheme_curve_fitting(&cali_config, &handle);
        if (ret == ESP_OK)
        {
            calibrated = true;
        }
    }
#endif

#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    if (!calibrated)
    {
        sys_logi(BAT_TAG, "calibration scheme version is %s", "Line Fitting");
        adc_cali_line_fitting_config_t cali_config =
        {
            .unit_id = unit,
            .atten = atten,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        ret = adc_cali_create_scheme_line_fitting(&cali_config, &handle);
        if (ret == ESP_OK)
        {
            calibrated = true;
        }
    }
#endif

    *out_handle = handle;
    if (ret == ESP_OK)
    {
        sys_logi(BAT_TAG, "Calibration Success");
    }
    else if (ret == ESP_ERR_NOT_SUPPORTED || !calibrated)
    {
        sys_logw(BAT_TAG, "eFuse not burnt, skip software calibration");
    }
    else
    {
        sys_loge(BAT_TAG, "Invalid arg or no memory");
    }

    return calibrated;
}

void hal_bat_deinit(void)
{
    // 关闭ADC采样使能引脚
    gpio_set_level(BAT_ADC_EN_PIN, 0);

    if (m_bat.do_calibration_chan0)
    {
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
        adc_cali_delete_scheme_curve_fitting(adc_cali_chan0_handle);
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
        adc_cali_delete_scheme_line_fitting(adc_cali_chan0_handle);
#endif
        adc_cali_chan0_handle = NULL;
        m_bat.do_calibration_chan0 = false;
    }

    if (adc_handle != NULL)
    {
        adc_oneshot_del_unit(adc_handle);
        adc_handle = NULL;
    }

    // 配置ADC引脚为高阻态，减少休眠漏电
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << GPIO_NUM_1) | (1ULL << BAT_ADC_EN_PIN),  // ADC_CHANNEL_0 = GPIO1 
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    sys_logi(BAT_TAG, "bat deinitialized, ADC pin set to high-impedance");
}

