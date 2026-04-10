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
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "encoder.h"
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

#define ENCODER_EV_QUEUE_LEN              (5)

#define ENCODER_BUTTON_ACTIVE_LEVEL       (0)

/*********************************************************************
* TYPEDEFS
*/
typedef struct
{
    button_handle_t btn;
    QueueHandle_t event_queue;
    rotary_encoder_handle_t re;
    encoder_press_type_t status;
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

static void encoder_event_handler(const rotary_encoder_event_t *event, void *ctx);
static void encoder_task(void *arg);

/*********************************************************************
 * GLOBAL FUNCTIONS
 */


void hal_encoder_init(void)
{
    m_encoder.status = ENCODER_PRESS_NONE;

    button_config_t cfg = {
        .type = BUTTON_TYPE_GPIO,
        .long_press_time = CONFIG_BUTTON_LONG_PRESS_TIME_MS,
        .short_press_time = CONFIG_BUTTON_SHORT_PRESS_TIME_MS,
        .gpio_button_config = {
            .gpio_num = ENCODER_PIN_PUSH,
            .active_level = ENCODER_BUTTON_ACTIVE_LEVEL,
        },
    };
    m_encoder.btn = iot_button_create(&cfg);
    iot_button_register_cb(m_encoder.btn, BUTTON_PRESS_DOWN, button_press_pressed_cb, NULL);
    iot_button_register_cb(m_encoder.btn, BUTTON_PRESS_UP, button_press_pressed_cb, NULL);
    iot_button_register_cb(m_encoder.btn, BUTTON_SINGLE_CLICK, button_press_short_cb, NULL);
    iot_button_register_cb(m_encoder.btn, BUTTON_LONG_PRESS_START, button_press_long_cb, NULL);

    // Create queue for rotary encoder events
    m_encoder.event_queue = xQueueCreate(ENCODER_EV_QUEUE_LEN, sizeof(rotary_encoder_event_t));

    // Create an encoder
    rotary_encoder_config_t config = ROTARY_ENCODER_DEFAULT_CONFIG();
    config.pin_a = ENCODER_PIN_DIFFA;
    config.pin_b = ENCODER_PIN_DIFFB;
    config.pin_btn = ENCODER_PIN_PUSH;
    config.callback = encoder_event_handler;
    config.callback_ctx = m_encoder.event_queue;
    config.acceleration_threshold_ms = 100;
    config.acceleration_cap_ms = 1;
    esp_err_t ret = rotary_encoder_create(&config, &m_encoder.re);

    if (ret != ESP_OK)
    {
        sys_loge(ENCODER_TAG, "Failed to create encoder: %d", ret);
        return;
    }
    xTaskCreate(encoder_task, "encoder_task", 4096, NULL, 5, NULL);
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


static void encoder_event_handler(const rotary_encoder_event_t *event, void *ctx)
{
    QueueHandle_t queue = (QueueHandle_t)ctx;
    xQueueSendToBack(queue, event, 0);
}

static void encoder_task(void *arg)
{
    rotary_encoder_event_t e;

    while (1)
    {
        xQueueReceive(m_encoder.event_queue, &e, portMAX_DELAY);

        switch (e.type)
        {
        case RE_ET_CHANGED:
            if (e.diff > 0)
            {
                m_encoder.status = ENCODER_PRESS_UP;
                sys_logi(ENCODER_TAG, "ENCODER DIFF+");
            }
            else if (e.diff < 0)
            {
                m_encoder.status = ENCODER_PRESS_DOWN;
                sys_logi(ENCODER_TAG, "ENCODER DIFF-");
            }
            break;
        default:
            break;
        }
    }
}

static void button_press_pressed_cb(void *arg, void *data)
{
    if(iot_button_get_event(arg) == BUTTON_PRESS_DOWN)
    {
        // m_encoder.status = ENCODER_PRESS_PRESSED;
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
