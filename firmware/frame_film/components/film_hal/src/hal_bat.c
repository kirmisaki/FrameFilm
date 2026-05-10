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
 * FileName : /film_hal/src/hal_bat.c
 * Author: Kiritro  Version: v0.1  Date: 2025/4/3
 * Description: Function introduction
 * ChangeLog: Change Notes
 *
***********************************************************/

/*********************************************************************
 * INCLUDES
 */
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

#include "sys_log.h"
#include "hal_bat.h"

/*********************************************************************
 * MACROS
 */
#define BAT_TAG             "HAL_BAT"

#define BAT_VOL_MAX         (4200)    //最大电压
#define BAT_VOL_MIN         (3300)    //最小电压
#define VOLTAGE_LEVEL_COUNT (11)      //电压等级数量


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
static const int voltage_levels[VOLTAGE_LEVEL_COUNT] = {3300, 3680, 3733, 3770, 3790, 3840, 3890, 3970, 4070, 4150, 4200};
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
    if (m_bat.do_calibration_chan0)
    {
        ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, ADC_CHANNEL_0, &m_bat.adc_raw));
        sys_logd(BAT_TAG, "ADC%d Channel[%d] Raw Data: %d", ADC_UNIT_1 + 1, ADC_CHANNEL_0, m_bat.adc_raw);
        ESP_ERROR_CHECK(adc_cali_raw_to_voltage(adc_cali_chan0_handle, m_bat.adc_raw, &m_bat.voltage));
        sys_logd(BAT_TAG, "ADC%d Channel[%d] Cali Voltage: %d mV", ADC_UNIT_1 + 1, ADC_CHANNEL_0, m_bat.voltage);
    }
    else
    {
        m_bat.voltage = 0;
    }
    m_bat.voltage = m_bat.voltage * 3; // 20k-10k 分压
    m_bat.level = hal_bat_voltage_to_level(m_bat.voltage);
    sys_logi(BAT_TAG, "bat vlotage: %d mV bat level: %d", m_bat.voltage, m_bat.level);
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

