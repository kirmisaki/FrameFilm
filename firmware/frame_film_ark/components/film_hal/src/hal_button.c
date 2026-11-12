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
#include <string.h>
#include "iot_button.h"
#include "driver/gpio.h"
#include "sdkconfig.h"

#include "hal_api.h"

/*********************************************************************
 * MACROS
 */
#define BUTTON_TAG                        "HAL_BUTTON"

#define BUTTON_PIN_UP                     (6)    // 上/右按键
#define BUTTON_PIN_DOWN                   (4)    // 下/左按键
#define BUTTON_PIN_CONFIRM                (5)    // 确认按键
#define BUTTON_PIN_BACK                   (18)   // n16r8 返回键
#define BUTTON_ACTIVE_LEVEL               (0)    // 按键激活电平为低电平

#if CONFIG_ARK_INPUT_BACK_KEY
#define BUTTON_NUM                        (4)
#else
#define BUTTON_NUM                        (3)
#endif

#define BUTTON_MAX_CALLBACKS              (5)

#define BUTTON_SHORT_PRESS_TIME_MS        (50)   // 50ms 短按（上/下键的单击结算窗口）
#define BUTTON_LONG_PRESS_TIME_MS         (1000) // 2s 长按
/* 确认键的双击配对窗口。按钮库把 short_press_time 当窗口用（见 confirm_cfg 的注释），
   必须明显大于人手的双击间隔，否则双击会被判成两次单击。 */
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

typedef struct
{
    button_t buttons[BUTTON_NUM];  // 0: UP, 1: DOWN, 2: CONFIRM, 3: BACK (n16r8)
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
#if CONFIG_ARK_INPUT_BACK_KEY
static void button_back_cb(void *button_handle, void *usr_data);
#endif
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
    /* 上键要认双击（截图），与确认键同理：short_press_time 兼作双击配对窗口，
       取 50ms 时双击必被判成两次单击。代价是单击上键晚一个窗口下发。 */
    button_config_t up_cfg =
    {
        .type = BUTTON_TYPE_GPIO,
        .long_press_time = BUTTON_LONG_PRESS_TIME_MS,
        .short_press_time = BUTTON_DOUBLE_CLICK_WINDOW_MS,
        .gpio_button_config = {
            .gpio_num = BUTTON_PIN_UP,
            .active_level = BUTTON_ACTIVE_LEVEL,
        },
    };
    m_button_mgr.buttons[0].handle = iot_button_create(&up_cfg);
    if (m_button_mgr.buttons[0].handle)
    {
        iot_button_register_cb(m_button_mgr.buttons[0].handle, BUTTON_SINGLE_CLICK, button_up_cb, NULL);
        iot_button_register_cb(m_button_mgr.buttons[0].handle, BUTTON_LONG_PRESS_START, button_up_cb, NULL);
        /* 双击只发 DOUBLE_CLICK、不发 SINGLE_CLICK（库内部已消歧），用于全局截图 */
        iot_button_register_cb(m_button_mgr.buttons[0].handle, BUTTON_DOUBLE_CLICK, button_up_cb, NULL);
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
            .active_level = BUTTON_ACTIVE_LEVEL,
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
        /* 确认键要认双击，所以它的"单击结算窗口"必须放开到人手的双击间隔里。
           这个值在按钮库里有双重身份（见 iot_button.c 的 state 2/3）：
             ① 松手后要等这么久才结算 SINGLE_CLICK；
             ② 窗口内第二次按下才算 DOUBLE_CLICK —— 超窗就各自算一次单击。
           取 50ms 时窗口比手速还短，双击必然被判成两次单击（上机实测：两次单击相隔
           230ms 就没配上）。代价是单击下发晚一个窗口，但确认键本来就承担双击，
           这个延迟躲不掉。上/下键不认双击，保持 BUTTON_SHORT_PRESS_TIME_MS 不受影响。 */
        .short_press_time = BUTTON_DOUBLE_CLICK_WINDOW_MS,
        .gpio_button_config = {
            .gpio_num = BUTTON_PIN_CONFIRM,
            .active_level = BUTTON_ACTIVE_LEVEL,
        },
    };
    m_button_mgr.buttons[2].handle = iot_button_create(&confirm_cfg);
    if (m_button_mgr.buttons[2].handle)
    {
        iot_button_register_cb(m_button_mgr.buttons[2].handle, BUTTON_SINGLE_CLICK, button_confirm_cb, NULL);
        iot_button_register_cb(m_button_mgr.buttons[2].handle, BUTTON_LONG_PRESS_START, button_confirm_cb, NULL);
        /* 双击只挂在确认键上（上/下双击没有语义）。
           按钮库内部已经处理了消歧：双击只发 BUTTON_DOUBLE_CLICK、不发 SINGLE_CLICK，
           而单击本身也是等双击窗口过期后才发的 —— 所以注册它不会让单击"多两次" */
        iot_button_register_cb(m_button_mgr.buttons[2].handle, BUTTON_DOUBLE_CLICK, button_confirm_cb, NULL);
        m_button_mgr.buttons[2].initialized = true;
        sys_logi(BUTTON_TAG, "CONFIRM button initialized");
    }
    else
    {
        sys_loge(BUTTON_TAG, "Failed to create CONFIRM button");
    }

#if CONFIG_ARK_INPUT_BACK_KEY
    // 初始化返回按键（n16r8，仅短按）
    button_config_t back_cfg =
    {
        .type = BUTTON_TYPE_GPIO,
        .long_press_time = BUTTON_LONG_PRESS_TIME_MS,
        .short_press_time = BUTTON_SHORT_PRESS_TIME_MS,
        .gpio_button_config = {
            .gpio_num = BUTTON_PIN_BACK,
            .active_level = BUTTON_ACTIVE_LEVEL,
        },
    };
    m_button_mgr.buttons[3].handle = iot_button_create(&back_cfg);
    if (m_button_mgr.buttons[3].handle)
    {
        iot_button_register_cb(m_button_mgr.buttons[3].handle, BUTTON_SINGLE_CLICK, button_back_cb, NULL);
        m_button_mgr.buttons[3].initialized = true;
        sys_logi(BUTTON_TAG, "BACK button initialized");
    }
    else
    {
        sys_loge(BUTTON_TAG, "Failed to create BACK button");
    }
#endif

    m_button_mgr.initialized = true;
    sys_logi(BUTTON_TAG, "button initialized (espressif__button)");
}

void hal_input_deinit(void)
{
    if (!m_button_mgr.initialized)
    {
        return;
    }

    for (int i = 0; i < BUTTON_NUM; i++)
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
        .pin_bit_mask = (1ULL << BUTTON_PIN_UP) | (1ULL << BUTTON_PIN_DOWN) | (1ULL << BUTTON_PIN_CONFIRM)
#if CONFIG_ARK_INPUT_BACK_KEY
                        | (1ULL << BUTTON_PIN_BACK)
#endif
                        ,
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
    else if (event == BUTTON_DOUBLE_CLICK)
    {
        sys_logi(BUTTON_TAG, "UP DOUBLE PRESS");
        trigger_callbacks(INPUT_PRESS_UP_DOUBLE);
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
    else if (event == BUTTON_DOUBLE_CLICK)
    {
        sys_logi(BUTTON_TAG, "CONFIRM DOUBLE PRESS");
        trigger_callbacks(INPUT_PRESS_DOUBLE);
    }
}

#if CONFIG_ARK_INPUT_BACK_KEY
static void button_back_cb(void *button_handle, void *usr_data)
{
    button_event_t event = iot_button_get_event(button_handle);
    if (event == BUTTON_SINGLE_CLICK)
    {
        sys_logi(BUTTON_TAG, "BACK SHORT PRESS");
        trigger_callbacks(INPUT_PRESS_BACK);
    }
}
#endif

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
