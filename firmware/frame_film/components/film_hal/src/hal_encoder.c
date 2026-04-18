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
#include "driver/gpio.h"
#include "esp_timer.h"

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

#define ENCODER_MIN_TICKS                 (20)
#define ENCODER_MAX_TICKS                 (100)

#define ENCODER_BUTTON_ACTIVE_LEVEL       (0)

#define ENCODER_DEBOUNCE_TIME_US          (5000)

#define DIFFA_PIN_SEL                     (1ULL << ENCODER_PIN_DIFFA)
#define DIFFB_PIN_SEL                     (1ULL << ENCODER_PIN_DIFFB)

/*********************************************************************
* TYPEDEFS
*/
typedef struct
{
    button_handle_t btn[ENCODER_BUTTON_NUM];
    encoder_press_type_t status;
    encoder_press_type_t statustmp;
} encoder_t;

typedef struct
{
    int8_t last_state;
    int8_t counter;
    encoder_press_type_t direction;
    bool debouncing;
} rotary_encoder_t;

/*********************************************************************
 * CONSTANTS
 */


/*********************************************************************
 * LOCAL VARIABLES
 */
static encoder_t m_encoder;
static rotary_encoder_t m_rotary;

static esp_timer_handle_t m_debounce_timer = NULL;

/*********************************************************************
 * GLOBAL VARIABLES
 */


/*********************************************************************
 * LOCAL FUNCTIONS
 */
static void button_press_pressed_cb(void *arg, void *data);
static void button_press_short_cb(void *arg, void *data);
static void button_press_long_cb(void *arg, void *data);

static void diff_isr_handler(void *arg);
static void rotary_encoder_debounce(void *arg);
static void rotary_encoder_init(void);
static void rotary_encoder_update(int gpio_num);

/*********************************************************************
 * GLOBAL FUNCTIONS
 */



void hal_encoder_init(void)
{
    m_encoder.status = ENCODER_PRESS_NONE;
    m_encoder.statustmp = ENCODER_PRESS_NONE;

    rotary_encoder_init();

    button_config_t cfg =
    {
        .type = BUTTON_TYPE_GPIO,
        .long_press_time = CONFIG_BUTTON_LONG_PRESS_TIME_MS,
        .short_press_time = CONFIG_BUTTON_SHORT_PRESS_TIME_MS,
        .gpio_button_config = {
            .gpio_num = ENCODER_PIN_PUSH,
            .active_level = ENCODER_BUTTON_ACTIVE_LEVEL,
        },
    };
    m_encoder.btn[ENCODER_BTN_PUSH] = iot_button_create(&cfg);
    iot_button_register_cb(m_encoder.btn[ENCODER_BTN_PUSH], BUTTON_PRESS_DOWN, button_press_pressed_cb, NULL);
    iot_button_register_cb(m_encoder.btn[ENCODER_BTN_PUSH], BUTTON_PRESS_UP, button_press_pressed_cb, NULL);
    iot_button_register_cb(m_encoder.btn[ENCODER_BTN_PUSH], BUTTON_SINGLE_CLICK, button_press_short_cb, NULL);
    iot_button_register_cb(m_encoder.btn[ENCODER_BTN_PUSH], BUTTON_LONG_PRESS_START, button_press_long_cb, NULL);
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

static void rotary_encoder_init(void)
{
    m_rotary.last_state = 0;
    m_rotary.counter = 0;
    m_rotary.direction = ENCODER_PRESS_NONE;
    m_rotary.debouncing = false;

    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_ANYEDGE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = DIFFA_PIN_SEL | DIFFB_PIN_SEL,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
    };
    gpio_config(&io_conf);

    esp_timer_create_args_t timer_args = {
        .callback = &rotary_encoder_debounce,
        .arg = NULL,
        .name = "rotary_debounce"
    };
    esp_timer_create(&timer_args, &m_debounce_timer);

    gpio_install_isr_service(0);
    gpio_isr_handler_add(ENCODER_PIN_DIFFA, diff_isr_handler, (void*)ENCODER_PIN_DIFFA);
    gpio_isr_handler_add(ENCODER_PIN_DIFFB, diff_isr_handler, (void*)ENCODER_PIN_DIFFB);

    m_rotary.last_state = (gpio_get_level(ENCODER_PIN_DIFFA) << 1) | gpio_get_level(ENCODER_PIN_DIFFB);
}

static void diff_isr_handler(void *arg)
{
    if(m_rotary.debouncing)
    {
        return;
    }
    
    esp_timer_start_once(m_debounce_timer, ENCODER_DEBOUNCE_TIME_US);
    m_rotary.debouncing = true;
}

static void rotary_encoder_debounce(void *arg)
{
    rotary_encoder_update(ENCODER_PIN_DIFFA);
    rotary_encoder_update(ENCODER_PIN_DIFFB);
    m_rotary.debouncing = false;
}

static void rotary_encoder_update(int gpio_num)
{
    int8_t current_a = gpio_get_level(ENCODER_PIN_DIFFA);
    int8_t current_b = gpio_get_level(ENCODER_PIN_DIFFB);
    int8_t current_state = (current_a << 1) | current_b;

    int8_t delta = current_state - m_rotary.last_state;

    if(delta == 1 || delta == -3)
    {
        m_rotary.counter++;
        m_rotary.direction = ENCODER_PRESS_UP;
    }
    else if(delta == -1 || delta == 3)
    {
        m_rotary.counter--;
        m_rotary.direction = ENCODER_PRESS_DOWN;
    }

    if(m_rotary.counter >= 2)
    {
        // m_encoder.status = ENCODER_PRESS_UP;
        sys_logi(ENCODER_TAG, "ENCODER DIFF+");
        m_rotary.counter = 0;
    }
    else if(m_rotary.counter <= -2)
    {
        // m_encoder.status = ENCODER_PRESS_DOWN;
        sys_logi(ENCODER_TAG, "ENCODER DIFF-");
        m_rotary.counter = 0;
    }

    m_rotary.last_state = current_state;
}
