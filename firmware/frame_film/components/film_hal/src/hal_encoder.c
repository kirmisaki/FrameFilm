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
 * FileName : /film_hal/src/hal_encoder.c
 * Author: Kiritro  Version: v0.1  Date: 2025/4/7
 * Description: Function introduction
 * ChangeLog: Change Notes
 *
***********************************************************/

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

/*********************************************************************
* TYPEDEFS
*/
typedef struct
{
    button_handle_t btn[ENCODER_BUTTON_NUM];
    encoder_press_type_t status;
    encoder_press_type_t statustmp;
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
}

static void button_press_long_cb(void *arg, void *data)
{
    m_encoder.status = ENCODER_PRESS_LONG;
    sys_logi(ENCODER_TAG, "ENCODER LONG PUSH");
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