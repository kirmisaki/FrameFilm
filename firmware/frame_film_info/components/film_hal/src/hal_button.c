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
 * FileName : /film_hal/src/hal_button.c
 * Author: Kiritro  Version: v0.1  Date: 2026/7/11
 * Description: 单按键驱动（FrameFilmCommon KEY = GPIO4，低有效），
 *              基于 espressif__button 库；单击/双击/长按三种事件。
 * ChangeLog: Change Notes
 *
 *********************************************************************/

/*********************************************************************
 * INCLUDES
 */
#include "sys_log.h"
#include <string.h>
#include <stdint.h>
#include "iot_button.h"
#include "driver/gpio.h"

#include "hal_api.h"

/*********************************************************************
 * MACROS
 */
#define BUTTON_TAG                        "HAL_BUTTON"

#define BUTTON_PIN                        (4)    // FrameFilmCommon U7: KEY，按下接地
#define BUTTON_ACTIVE_LEVEL               (0)    // 低电平有效

#define BUTTON_MAX_CALLBACKS              (5)

#define BUTTON_LONG_PRESS_TIME_MS         (2000) // 2s 长按
/* 本工程只有一个键，必须同时支持单击和双击。
 * 按钮库把 short_press_time 同时当作"单击结算窗口"和"双击配对窗口"，
 * 必须明显大于人手的双击间隔（现取 350ms），否则双击会被判成两次单击。
 * 代价：单击事件晚一个窗口下发。 */
#define BUTTON_DOUBLE_CLICK_WINDOW_MS     (350)

/*********************************************************************
* TYPEDEFS
*/
typedef struct
{
    button_handle_t handle;
    bool initialized;
    input_callback_t cbs[INPUT_PRESS_MAX][BUTTON_MAX_CALLBACKS];
    int cb_counts[INPUT_PRESS_MAX];
} button_t;

/*********************************************************************
 * CONSTANTS
 */


/*********************************************************************
 * LOCAL VARIABLES
 */
static button_t m_button;

/*********************************************************************
 * GLOBAL VARIABLES
 */


/*********************************************************************
 * LOCAL FUNCTIONS
 */
static void button_event_cb(void *button_handle, void *usr_data);
static void trigger_callbacks(input_press_type_t type);

/*********************************************************************
 * GLOBAL FUNCTIONS
 */

void hal_input_init(void)
{
    if (m_button.initialized)
    {
        return;
    }

    memset(&m_button, 0, sizeof(m_button));

    button_config_t cfg =
    {
        .type = BUTTON_TYPE_GPIO,
        .long_press_time = BUTTON_LONG_PRESS_TIME_MS,
        .short_press_time = BUTTON_DOUBLE_CLICK_WINDOW_MS,
        .gpio_button_config = {
            .gpio_num = BUTTON_PIN,
            .active_level = BUTTON_ACTIVE_LEVEL,
        },
    };

    m_button.handle = iot_button_create(&cfg);
    if (m_button.handle)
    {
        iot_button_register_cb(m_button.handle, BUTTON_SINGLE_CLICK, button_event_cb, NULL);
        iot_button_register_cb(m_button.handle, BUTTON_LONG_PRESS_START, button_event_cb, NULL);
        /* 库内部已做消歧：双击只发 DOUBLE_CLICK、不发 SINGLE_CLICK */
        iot_button_register_cb(m_button.handle, BUTTON_DOUBLE_CLICK, button_event_cb, NULL);
        m_button.initialized = true;
        sys_logi(BUTTON_TAG, "KEY button initialized (GPIO%d)", BUTTON_PIN);
    }
    else
    {
        sys_loge(BUTTON_TAG, "Failed to create KEY button");
    }
}

void hal_input_deinit(void)
{
    if (!m_button.initialized)
    {
        return;
    }

    if (m_button.handle)
    {
        iot_button_delete(m_button.handle);
        m_button.handle = NULL;
    }

    // 将按键引脚设为高阻态
    gpio_config_t io_conf =
    {
        .pin_bit_mask = (1ULL << BUTTON_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    m_button.initialized = false;
    sys_logi(BUTTON_TAG, "button deinitialized");
}

static void button_event_cb(void *button_handle, void *usr_data)
{
    button_event_t event = iot_button_get_event(button_handle);

    if (event == BUTTON_SINGLE_CLICK)
    {
        sys_logi(BUTTON_TAG, "KEY SHORT PRESS");
        trigger_callbacks(INPUT_PRESS_SHORT);
    }
    else if (event == BUTTON_LONG_PRESS_START)
    {
        sys_logi(BUTTON_TAG, "KEY LONG PRESS");
        trigger_callbacks(INPUT_PRESS_LONG);
    }
    else if (event == BUTTON_DOUBLE_CLICK)
    {
        sys_logi(BUTTON_TAG, "KEY DOUBLE PRESS");
        trigger_callbacks(INPUT_PRESS_DOUBLE);
    }
}

static void trigger_callbacks(input_press_type_t type)
{
    if (type < 0 || type >= INPUT_PRESS_MAX)
    {
        return;
    }

    for (int i = 0; i < m_button.cb_counts[type]; i++)
    {
        if (m_button.cbs[type][i])
        {
            m_button.cbs[type][i]();
        }
    }
}

int hal_input_register_cb(input_press_type_t type, input_callback_t cb)
{
    if (!cb) return -1;
    if (type < 0 || type >= INPUT_PRESS_MAX) return -4;
    if (m_button.cb_counts[type] >= BUTTON_MAX_CALLBACKS) return -2;
    m_button.cbs[type][m_button.cb_counts[type]++] = cb;
    return 0;
}

int hal_input_unregister_cb(input_press_type_t type, input_callback_t cb)
{
    if (!cb) return -1;
    if (type < 0 || type >= INPUT_PRESS_MAX) return -4;
    for (int i = 0; i < m_button.cb_counts[type]; i++)
    {
        if (m_button.cbs[type][i] == cb)
        {
            for (int j = i; j < m_button.cb_counts[type] - 1; j++)
            {
                m_button.cbs[type][j] = m_button.cbs[type][j + 1];
            }
            m_button.cbs[type][m_button.cb_counts[type] - 1] = NULL;
            m_button.cb_counts[type]--;
            return 0;
        }
    }
    return -3;
}
