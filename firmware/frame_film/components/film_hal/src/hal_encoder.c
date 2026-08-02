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
 * Description: 旋转编码器驱动，基于 esp-idf-lib rotary_encoder
 * ChangeLog: Change Notes
 *
 *********************************************************************/

/*********************************************************************
 * INCLUDES
 */
#include "sys_log.h"
#if FRAMEFILM_STD == 1
#include <string.h>
#include "encoder.h"
#include "driver/gpio.h"

#include "hal_api.h"

/*********************************************************************
 * MACROS
 */
#define ENCODER_TAG                       "HAL_ENCODER"

#define ENCODER_PIN_DIFFA                 (6)
#define ENCODER_PIN_DIFFB                 (4)
#define ENCODER_PIN_PUSH                  (5)

#define ENCODER_MAX_CALLBACKS             (5)

#define ENCODER_BTN_DEAD_TIME_US          (50000)     // 50ms 消抖
#define ENCODER_BTN_LONG_PRESS_TIME_US    (2000000)   // 2s 长按
#define ENCODER_POLLING_INTERVAL_US       (2000)      // 2ms 轮询

/*********************************************************************
* TYPEDEFS
*/
typedef struct
{
    rotary_encoder_handle_t handle;
    bool initialized;
    bool button_pressed;
    input_callback_t cbs[INPUT_PRESS_MAX][ENCODER_MAX_CALLBACKS];
    int cb_counts[INPUT_PRESS_MAX];
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
static void encoder_event_cb(const rotary_encoder_event_t *event, void *ctx);
static input_press_type_t map_event_to_press(rotary_encoder_event_type_t type);

/*********************************************************************
 * GLOBAL FUNCTIONS
 */

void hal_input_init(void)
{
    if (m_encoder.initialized)
    {
        return;
    }

    memset(&m_encoder, 0, sizeof(m_encoder));

    rotary_encoder_config_t cfg = ROTARY_ENCODER_DEFAULT_CONFIG();
    cfg.pin_a                  = ENCODER_PIN_DIFFA;
    cfg.pin_b                  = ENCODER_PIN_DIFFB;
    cfg.pin_btn                = ENCODER_PIN_PUSH;
    cfg.btn_pressed_level      = 0;  // 低电平有效
    cfg.enable_internal_pullup = false; // 外部已有上拉
    cfg.btn_dead_time_us       = ENCODER_BTN_DEAD_TIME_US;
    cfg.btn_long_press_time_us = ENCODER_BTN_LONG_PRESS_TIME_US;
    cfg.polling_interval_us    = ENCODER_POLLING_INTERVAL_US;
    cfg.callback               = encoder_event_cb;
    cfg.callback_ctx           = &m_encoder;

    esp_err_t ret = rotary_encoder_create(&cfg, &m_encoder.handle);
    if (ret != ESP_OK)
    {
        sys_loge(ENCODER_TAG, "Failed to create rotary encoder: %s", esp_err_to_name(ret));
        return;
    }

    m_encoder.initialized = true;
    sys_logi(ENCODER_TAG, "encoder initialized (esp-idf-lib)");
}

void hal_input_deinit(void)
{
    if (!m_encoder.initialized)
    {
        return;
    }

    rotary_encoder_delete(m_encoder.handle);
    m_encoder.handle = NULL;
    m_encoder.initialized = false;

    // 将编码器 A/B 引脚设为高阻态
    gpio_config_t io_conf =
    {
        .pin_bit_mask = (1ULL << ENCODER_PIN_DIFFA) | (1ULL << ENCODER_PIN_DIFFB),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    sys_logi(ENCODER_TAG, "encoder deinitialized");
}

static void encoder_event_cb(const rotary_encoder_event_t *event, void *ctx)
{
    encoder_t *enc = (encoder_t *)ctx;

    input_press_type_t type = map_event_to_press(event->type);
    if (type == INPUT_PRESS_NONE)
    {
        return;
    }

    // 按钮按下时，忽略编码器旋转事件（防止误触发方向变化）
    if (type == INPUT_PRESS_PRESSED)
    {
        enc->button_pressed = true;
    }
    else if (type == INPUT_PRESS_SHORT || type == INPUT_PRESS_LONG)
    {
        // 按钮释放后的点击事件
        enc->button_pressed = false;
    }

    if ((type == INPUT_PRESS_UP || type == INPUT_PRESS_DOWN) && enc->button_pressed)
    {
        return;  // 按住按钮时忽略旋转
    }

    if (type == INPUT_PRESS_UP || type == INPUT_PRESS_DOWN)
    {
        if (event->diff > 0)
        {
            type = INPUT_PRESS_UP;
            sys_logi(ENCODER_TAG, "ENCODER DIFF+");
        }
        else
        {
            type = INPUT_PRESS_DOWN;
            sys_logi(ENCODER_TAG, "ENCODER DIFF-");
        }
    }

    if (type == INPUT_PRESS_SHORT)
    {
        sys_logi(ENCODER_TAG, "ENCODER SHORT PUSH");
    }
    else if (type == INPUT_PRESS_LONG)
    {
        sys_logi(ENCODER_TAG, "ENCODER LONG PUSH");
    }

    for (int i = 0; i < enc->cb_counts[type]; i++)
    {
        if (enc->cbs[type][i])
        {
            enc->cbs[type][i]();
        }
    }
}

static input_press_type_t map_event_to_press(rotary_encoder_event_type_t type)
{
    switch (type)
    {
    case RE_ET_CHANGED:
        return INPUT_PRESS_DOWN;  // 方向在回调中判断
    case RE_ET_BTN_CLICKED:
        return INPUT_PRESS_SHORT;
    case RE_ET_BTN_LONG_PRESSED:
        return INPUT_PRESS_LONG;
    case RE_ET_BTN_PRESSED:
        return INPUT_PRESS_PRESSED;
    default:
        return INPUT_PRESS_NONE;
    }
}

int hal_input_register_cb(input_press_type_t type, input_callback_t cb)
{
    if (!cb) return -1;
    if (type < 0 || type >= INPUT_PRESS_MAX) return -4;
    if (m_encoder.cb_counts[type] >= ENCODER_MAX_CALLBACKS) return -2;
    m_encoder.cbs[type][m_encoder.cb_counts[type]++] = cb;
    return 0;
}

int hal_input_unregister_cb(input_press_type_t type, input_callback_t cb)
{
    if (!cb) return -1;
    if (type < 0 || type >= INPUT_PRESS_MAX) return -4;
    for (int i = 0; i < m_encoder.cb_counts[type]; i++)
    {
        if (m_encoder.cbs[type][i] == cb)
        {
            for (int j = i; j < m_encoder.cb_counts[type] - 1; j++)
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
#endif
