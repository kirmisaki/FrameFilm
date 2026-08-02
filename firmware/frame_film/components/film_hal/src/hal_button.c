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
 * Description: 三按键驱动，基于 espressif__button 库
 * ChangeLog: Change Notes
 *
 *********************************************************************/

/*********************************************************************
 * INCLUDES
 */
#include "sys_log.h"
#if FRAMEFILM_PRO == 1
#include <string.h>
#include "iot_button.h"
#include "driver/gpio.h"

#include "hal_api.h"

/*********************************************************************
 * MACROS
 */
#define BUTTON_TAG                        "HAL_BUTTON"

#define BUTTON_PIN_UP                     (4)    // 上/右按键
#define BUTTON_PIN_DOWN                   (6)    // 下/左按键
#define BUTTON_PIN_CONFIRM                (5)    // 确认按键

#define BUTTON_MAX_CALLBACKS              (5)

#define BUTTON_SHORT_PRESS_TIME_MS        (50)   // 50ms 短按
#define BUTTON_LONG_PRESS_TIME_MS         (2000) // 2s 长按

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

typedef struct
{
    button_t buttons[3];  // 0: UP, 1: DOWN, 2: CONFIRM
    bool initialized;
} button_mgr_t;

/*********************************************************************
 * CONSTANTS
 */


/*********************************************************************
 * LOCAL VARIABLES
 */
static button_mgr_t m_button_mgr;

/*********************************************************************
 * GLOBAL VARIABLES
 */


/*********************************************************************
 * LOCAL FUNCTIONS
 */
static void button_up_cb(void *button_handle, void *usr_data);
static void button_down_cb(void *button_handle, void *usr_data);
static void button_confirm_cb(void *button_handle, void *usr_data);
static void trigger_callbacks(input_press_type_t type);

/*********************************************************************
 * GLOBAL FUNCTIONS
 */

void hal_input_init(void)
{
    if (m_button_mgr.initialized)
    {
        return;
    }

    memset(&m_button_mgr, 0, sizeof(m_button_mgr));

    // 初始化上/右按键
    button_config_t up_cfg =
    {
        .type = BUTTON_TYPE_GPIO,
        .long_press_time = BUTTON_LONG_PRESS_TIME_MS,
        .short_press_time = BUTTON_SHORT_PRESS_TIME_MS,
        .gpio_button_config = {
            .gpio_num = BUTTON_PIN_UP,
            .active_level = 0,
        },
    };
    m_button_mgr.buttons[0].handle = iot_button_create(&up_cfg);
    if (m_button_mgr.buttons[0].handle)
    {
        iot_button_register_cb(m_button_mgr.buttons[0].handle, BUTTON_SINGLE_CLICK, button_up_cb, NULL);
        iot_button_register_cb(m_button_mgr.buttons[0].handle, BUTTON_LONG_PRESS_START, button_up_cb, NULL);
        m_button_mgr.buttons[0].initialized = true;
        sys_logi(BUTTON_TAG, "UP button initialized");
    }
    else
    {
        sys_loge(BUTTON_TAG, "Failed to create UP button");
    }

    // 初始化下/左按键
    button_config_t down_cfg =
    {
        .type = BUTTON_TYPE_GPIO,
        .long_press_time = BUTTON_LONG_PRESS_TIME_MS,
        .short_press_time = BUTTON_SHORT_PRESS_TIME_MS,
        .gpio_button_config = {
            .gpio_num = BUTTON_PIN_DOWN,
            .active_level = 0,
        },
    };
    m_button_mgr.buttons[1].handle = iot_button_create(&down_cfg);
    if (m_button_mgr.buttons[1].handle)
    {
        iot_button_register_cb(m_button_mgr.buttons[1].handle, BUTTON_SINGLE_CLICK, button_down_cb, NULL);
        iot_button_register_cb(m_button_mgr.buttons[1].handle, BUTTON_LONG_PRESS_START, button_down_cb, NULL);
        m_button_mgr.buttons[1].initialized = true;
        sys_logi(BUTTON_TAG, "DOWN button initialized");
    }
    else
    {
        sys_loge(BUTTON_TAG, "Failed to create DOWN button");
    }

    // 初始化确认按键
    button_config_t confirm_cfg =
    {
        .type = BUTTON_TYPE_GPIO,
        .long_press_time = BUTTON_LONG_PRESS_TIME_MS,
        .short_press_time = BUTTON_SHORT_PRESS_TIME_MS,
        .gpio_button_config = {
            .gpio_num = BUTTON_PIN_CONFIRM,
            .active_level = 0,
        },
    };
    m_button_mgr.buttons[2].handle = iot_button_create(&confirm_cfg);
    if (m_button_mgr.buttons[2].handle)
    {
        iot_button_register_cb(m_button_mgr.buttons[2].handle, BUTTON_SINGLE_CLICK, button_confirm_cb, NULL);
        iot_button_register_cb(m_button_mgr.buttons[2].handle, BUTTON_LONG_PRESS_START, button_confirm_cb, NULL);
        m_button_mgr.buttons[2].initialized = true;
        sys_logi(BUTTON_TAG, "CONFIRM button initialized");
    }
    else
    {
        sys_loge(BUTTON_TAG, "Failed to create CONFIRM button");
    }

    m_button_mgr.initialized = true;
    sys_logi(BUTTON_TAG, "button initialized (espressif__button)");
}

void hal_input_deinit(void)
{
    if (!m_button_mgr.initialized)
    {
        return;
    }

    for (int i = 0; i < 3; i++)
    {
        if (m_button_mgr.buttons[i].handle)
        {
            iot_button_delete(m_button_mgr.buttons[i].handle);
            m_button_mgr.buttons[i].handle = NULL;
            m_button_mgr.buttons[i].initialized = false;
        }
    }

    // 将按键引脚设为高阻态
    gpio_config_t io_conf =
    {
        .pin_bit_mask = (1ULL << BUTTON_PIN_UP) | (1ULL << BUTTON_PIN_DOWN) | (1ULL << BUTTON_PIN_CONFIRM),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    m_button_mgr.initialized = false;
    sys_logi(BUTTON_TAG, "button deinitialized");
}

static void button_up_cb(void *button_handle, void *usr_data)
{
    button_event_t event = iot_button_get_event(button_handle);
    if (event == BUTTON_SINGLE_CLICK)
    {
        sys_logi(BUTTON_TAG, "UP SHORT PRESS");
        trigger_callbacks(INPUT_PRESS_UP);
    }
    else if (event == BUTTON_LONG_PRESS_START)
    {
        sys_logi(BUTTON_TAG, "UP LONG PRESS");
        trigger_callbacks(INPUT_PRESS_UP);
    }
}

static void button_down_cb(void *button_handle, void *usr_data)
{
    button_event_t event = iot_button_get_event(button_handle);
    if (event == BUTTON_SINGLE_CLICK)
    {
        sys_logi(BUTTON_TAG, "DOWN SHORT PRESS");
        trigger_callbacks(INPUT_PRESS_DOWN);
    }
    else if (event == BUTTON_LONG_PRESS_START)
    {
        sys_logi(BUTTON_TAG, "DOWN LONG PRESS");
        trigger_callbacks(INPUT_PRESS_DOWN);
    }
}

static void button_confirm_cb(void *button_handle, void *usr_data)
{
    button_event_t event = iot_button_get_event(button_handle);
    if (event == BUTTON_SINGLE_CLICK)
    {
        sys_logi(BUTTON_TAG, "CONFIRM SHORT PRESS");
        trigger_callbacks(INPUT_PRESS_SHORT);
    }
    else if (event == BUTTON_LONG_PRESS_START)
    {
        sys_logi(BUTTON_TAG, "CONFIRM LONG PRESS");
        trigger_callbacks(INPUT_PRESS_LONG);
    }
}

static void trigger_callbacks(input_press_type_t type)
{
    if (type < 0 || type >= INPUT_PRESS_MAX)
    {
        return;
    }

    for (int i = 0; i < m_button_mgr.buttons[0].cb_counts[type]; i++)
    {
        if (m_button_mgr.buttons[0].cbs[type][i])
        {
            m_button_mgr.buttons[0].cbs[type][i]();
        }
    }
}

int hal_input_register_cb(input_press_type_t type, input_callback_t cb)
{
    if (!cb) return -1;
    if (type < 0 || type >= INPUT_PRESS_MAX) return -4;
    if (m_button_mgr.buttons[0].cb_counts[type] >= BUTTON_MAX_CALLBACKS) return -2;
    m_button_mgr.buttons[0].cbs[type][m_button_mgr.buttons[0].cb_counts[type]++] = cb;
    return 0;
}

int hal_input_unregister_cb(input_press_type_t type, input_callback_t cb)
{
    if (!cb) return -1;
    if (type < 0 || type >= INPUT_PRESS_MAX) return -4;
    for (int i = 0; i < m_button_mgr.buttons[0].cb_counts[type]; i++)
    {
        if (m_button_mgr.buttons[0].cbs[type][i] == cb)
        {
            for (int j = i; j < m_button_mgr.buttons[0].cb_counts[type] - 1; j++)
            {
                m_button_mgr.buttons[0].cbs[type][j] = m_button_mgr.buttons[0].cbs[type][j + 1];
            }
            m_button_mgr.buttons[0].cbs[type][m_button_mgr.buttons[0].cb_counts[type] - 1] = NULL;
            m_button_mgr.buttons[0].cb_counts[type]--;
            return 0;
        }
    }
    return -3;
}
#endif
