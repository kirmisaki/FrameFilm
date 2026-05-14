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
 * FileName : /film_hal/src/hal_encoder.c
 * Author: Kiritro  Version: v0.1  Date: 2026/4/7
 * Description: Function introduction
 * ChangeLog: Change Notes
 *
 *********************************************************************/

/*********************************************************************
 * INCLUDES
 */
#include "iot_button.h"

#include "sys_log.h"
#include "hal_api.h"
#include "hal_encoder.h"

/*********************************************************************
 * MACROS
 */
#define ENCODER_TAG                       "HAL_ENCODER"

#define ENCODER_PIN_DIFFA                 (46)
#define ENCODER_PIN_DIFFB                 (10)
#define ENCODER_PIN_PUSH                  (9)

#define ENCODER_BTN_PUSH                  (0)
#define ENCODER_BTN_UP                    (1)
#define ENCODER_BTN_DOWN                  (2)
#define ENCODER_BUTTON_NUM                (3)

#define ENCODER_MIN_TICKS                 (10)
#define ENCODER_MAX_TICKS                 (50)

#define ENCODER_BUTTON_ACTIVE_LEVEL       (0)
#define ENCODER_MAX_CALLBACKS             (5)

/*********************************************************************
* TYPEDEFS
*/
typedef struct
{
    button_handle_t btn[ENCODER_BUTTON_NUM];
    encoder_press_type_t status;
    encoder_press_type_t statustmp;
    encoder_callback_t cbs[ENCODER_PRESS_MAX][ENCODER_MAX_CALLBACKS];
    int cb_counts[ENCODER_PRESS_MAX];
} encoder_t;

/*********************************************************************
 * CONSTANTS
 */


/*********************************************************************
 * LOCAL VARIABLES
 */
static encoder_t m_encoder;


/*********************************************************************
 * GLOBAL VARIABLES
 */


/*********************************************************************
 * LOCAL FUNCTIONS
 */
static void button_press_pressed_cb(void *arg, void *data);
static void button_press_short_cb(void *arg, void *data);
static void button_press_long_cb(void *arg, void *data);
static void button_press_up_cb(void *arg, void *data);

/*********************************************************************
 * GLOBAL FUNCTIONS
 */



void hal_encoder_init(void)
{
    m_encoder.status = ENCODER_PRESS_NONE;
    m_encoder.statustmp = ENCODER_PRESS_NONE;

    for(int i = 0; i < ENCODER_PRESS_MAX; i++)
    {
        m_encoder.cb_counts[i] = 0;
        for(int j = 0; j < ENCODER_MAX_CALLBACKS; j++)
        {
            m_encoder.cbs[i][j] = NULL;
        }
    }

    button_config_t cfg =
    {
        .type = BUTTON_TYPE_GPIO,
        .long_press_time = CONFIG_BUTTON_LONG_PRESS_TIME_MS,
        .short_press_time = CONFIG_BUTTON_SHORT_PRESS_TIME_MS,
        .gpio_button_config = {
            .gpio_num = ENCODER_PIN_PUSH,
            .active_level = ENCODER_BUTTON_ACTIVE_LEVEL,
            .disable_pull = false,
        },
    };
    m_encoder.btn[ENCODER_BTN_PUSH] = iot_button_create(&cfg);
    cfg.gpio_button_config.gpio_num = ENCODER_PIN_DIFFA;
    m_encoder.btn[ENCODER_BTN_UP] = iot_button_create(&cfg);
    cfg.gpio_button_config.gpio_num = ENCODER_PIN_DIFFB;
    m_encoder.btn[ENCODER_BTN_DOWN] = iot_button_create(&cfg);
    iot_button_register_cb(m_encoder.btn[ENCODER_BTN_PUSH], BUTTON_PRESS_DOWN, button_press_pressed_cb, NULL);
    iot_button_register_cb(m_encoder.btn[ENCODER_BTN_PUSH], BUTTON_PRESS_UP, button_press_pressed_cb, NULL);
    iot_button_register_cb(m_encoder.btn[ENCODER_BTN_PUSH], BUTTON_SINGLE_CLICK, button_press_short_cb, NULL);
    iot_button_register_cb(m_encoder.btn[ENCODER_BTN_PUSH], BUTTON_LONG_PRESS_START, button_press_long_cb, NULL);
    iot_button_register_cb(m_encoder.btn[ENCODER_BTN_UP], BUTTON_PRESS_DOWN, button_press_up_cb, NULL);
    iot_button_register_cb(m_encoder.btn[ENCODER_BTN_UP], BUTTON_PRESS_UP, button_press_up_cb, NULL);
}

encoder_press_type_t hal_encoder_get_press(void)
{
    encoder_press_type_t type = m_encoder.status;
    if(m_encoder.status != ENCODER_PRESS_PRESSED)
    {
        m_encoder.status = ENCODER_PRESS_NONE;
    }
    return type;
}


static void button_press_pressed_cb(void *arg, void *data)
{
    if(iot_button_get_event(arg) == BUTTON_PRESS_DOWN)
    {
        m_encoder.status = ENCODER_PRESS_PRESSED;
    }
    else if(iot_button_get_event(arg) == BUTTON_PRESS_UP)
    {
        m_encoder.status = ENCODER_PRESS_NONE;
    }
}

static void button_press_short_cb(void *arg, void *data)
{
    m_encoder.status = ENCODER_PRESS_SHORT;
    sys_logi(ENCODER_TAG, "ENCODER SHORT PUSH");
    for(int i = 0; i < m_encoder.cb_counts[ENCODER_PRESS_SHORT]; i++)
    {
        if(m_encoder.cbs[ENCODER_PRESS_SHORT][i])
        {
            m_encoder.cbs[ENCODER_PRESS_SHORT][i]();
        }
    }
}

static void button_press_long_cb(void *arg, void *data)
{
    m_encoder.status = ENCODER_PRESS_LONG;
    sys_logi(ENCODER_TAG, "ENCODER LONG PUSH");
    for(int i = 0; i < m_encoder.cb_counts[ENCODER_PRESS_LONG]; i++)
    {
        if(m_encoder.cbs[ENCODER_PRESS_LONG][i])
        {
            m_encoder.cbs[ENCODER_PRESS_LONG][i]();
        }
    }
}

static void button_press_up_cb(void *arg, void *data)
{
    if(!iot_button_get_key_level(m_encoder.btn[ENCODER_BTN_PUSH]))
    {
        if(iot_button_get_event(arg) == BUTTON_PRESS_DOWN)
        {
            if(iot_button_get_key_level(m_encoder.btn[ENCODER_BTN_DOWN]))
            {
                m_encoder.statustmp = ENCODER_PRESS_DOWN;
            }
            else
            {
                m_encoder.statustmp = ENCODER_PRESS_UP;
            }
        }
        else if(iot_button_get_event(arg) == BUTTON_PRESS_UP)
        {
            uint32_t ticks = iot_button_get_ticks_time(m_encoder.btn[ENCODER_BTN_UP]);
            if(ticks > ENCODER_MIN_TICKS && ticks < ENCODER_MAX_TICKS)
            {
                // m_encoder.status = m_encoder.statustmp;
                if(m_encoder.status == ENCODER_PRESS_UP)
                {
                    sys_logi(ENCODER_TAG, "ENCODER DIFF+");
                }
                else if(m_encoder.status == ENCODER_PRESS_DOWN)
                {
                    sys_logi(ENCODER_TAG, "ENCODER DIFF-");
                }
            }
            else
            {
                m_encoder.statustmp = ENCODER_PRESS_NONE;
            }
        }
    }
}

int hal_encoder_register_cb(encoder_press_type_t type, encoder_callback_t cb)
{
    if(!cb)
    {
        return -1;
    }
    if(type < 0 || type >= ENCODER_PRESS_MAX)
    {
        return -4;
    }
    if(m_encoder.cb_counts[type] >= ENCODER_MAX_CALLBACKS)
    {
        return -2;
    }
    m_encoder.cbs[type][m_encoder.cb_counts[type]++] = cb;
    return 0;
}

int hal_encoder_unregister_cb(encoder_press_type_t type, encoder_callback_t cb)
{
    if(!cb)
    {
        return -1;
    }
    if(type < 0 || type >= ENCODER_PRESS_MAX)
    {
        return -4;
    }
    for(int i = 0; i < m_encoder.cb_counts[type]; i++)
    {
        if(m_encoder.cbs[type][i] == cb)
        {
            for(int j = i; j < m_encoder.cb_counts[type] - 1; j++)
            {
                m_encoder.cbs[type][j] = m_encoder.cbs[type][j + 1];
            }
            m_encoder.cbs[type][m_encoder.cb_counts[type] - 1] = NULL;
            m_encoder.cb_counts[type]--;
            return 0;
        }
    }
    return -3;
}