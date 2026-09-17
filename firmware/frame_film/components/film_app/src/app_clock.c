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
 * FileName : /film_app/src/app_clock.c
 * Author: Kiritro  Version: v0.1  Date: 2026/9/9
 * Description: 时钟 app：设备端生成时间，MonoFast 差分局刷（WiFi/蓝牙校时 + 本地 RTC 兜底）
 * ChangeLog: Change Notes
 *
 *********************************************************************/

/*********************************************************************
 * INCLUDES
 */
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "esp_heap_caps.h"

#include "sys_log.h"
#include "sys_event.h"
#include "hal_epd.h"
#include "app_clock.h"
#include "app_render.h"
#include "hal_input.h"

/*********************************************************************
 * MACROS
 */
#define APP_CLOCK_TAG       "app_clock"

// 1bpp mono 帧缓冲字节数（行优先，MSB 在左）
#define CLOCK_MONO_BYTES    ((EPD_WIDTH * EPD_HEIGHT) / 8)

// 5x7 字体绘制尺度（由屏宽自适应，保证内容完整且居中）
#define CLOCK_SCALE_MIN     (8)
#define CLOCK_SCALE_MAX     (22)

// 时钟刷新周期（毫秒）：秒级刷新即可，避免 EPD 无谓刷新
#define CLOCK_TICK_MS       (1000)

/*********************************************************************
* TYPEDEFS
*/

/*********************************************************************
 * CONSTANTS
 */

// 5x7 点阵数字（低 5 位有效，bit4=最左列）
static const uint8_t FONT_DIGIT[10][7] = {
    {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E},   // 0
    {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E},   // 1
    {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F},   // 2
    {0x0E,0x11,0x01,0x06,0x01,0x11,0x0E},   // 3
    {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02},   // 4
    {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E},   // 5
    {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E},   // 6
    {0x1F,0x01,0x02,0x04,0x08,0x08,0x08},   // 7
    {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E},   // 8
    {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C},   // 9
};

// 冒号（上下两圆点）
static const uint8_t FONT_COLON[7] = {
    0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00,
};

// 空白（未定义字符）
static const uint8_t FONT_BLANK[7] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

/*********************************************************************
 * LOCAL VARIABLES
 */
static uint8_t *m_mono_buf = NULL;      // SPIRAM mono 帧缓冲（懒加载，常驻）
static char m_last_text[16] = {0};      // 上一次显示内容，用于去重（避免每 tick 无谓刷屏）

// 关注的全局事件：数据落地（WiFi/蓝牙校时）后重新生成时间显示
static const uint16_t m_clock_events[] = {
    SYS_EVT_FILE_SAVED,
    0,
};

/*********************************************************************
 * LOCAL FUNCTIONS
 */
static void app_clock_on_enter(void);
static void app_clock_on_exit(void);
static void app_clock_on_event(const app_event_t *e);
static void app_clock_on_tick(void);
static const uint8_t *clock_font_char(char c);
static void clock_set_pixel(uint8_t *bin, int x, int y);
static void clock_draw_char(uint8_t *bin, int x, int y, int scale, const uint8_t *glyph);
static void clock_draw_string(uint8_t *bin, const char *s, int scale);
static int clock_render_now(void);

/*********************************************************************
 * GLOBAL VARIABLES
 */
const app_entry_t g_app_clock_entry = {
    .id = APP_ID_CLOCK,
    .name = "clock",
    .data_dir = NULL,        // 时钟为设备端生成，不占用文件目录
    .keys = APP_KEY_NONE,    // 不占用按键
    .tick_ms = CLOCK_TICK_MS,
    .events = m_clock_events,
    .on_enter = app_clock_on_enter,
    .on_exit = app_clock_on_exit,
    .on_event = app_clock_on_event,
    .on_tick = app_clock_on_tick,
};

/*********************************************************************
 * LOCAL FUNCTIONS
 */

static void app_clock_on_enter(void)
{
    sys_logi(APP_CLOCK_TAG, "enter clock app");
    clock_render_now();
}

static void app_clock_on_exit(void)
{
    sys_logi(APP_CLOCK_TAG, "exit clock app");
}

static void app_clock_on_tick(void)
{
    // 每 CLOCK_TICK_MS 触发一次，仅在内容变化（秒数改变）时重绘，避免 EPD 无谓刷新
    clock_render_now();
}

static void app_clock_on_event(const app_event_t *e)
{
    if(e == NULL || e->type != APP_EVT_SYS)
    {
        return;
    }

    // 数据落地（WiFi/蓝牙校时等）：重新生成时间显示
    if((sys_event_id_t)e->cmd == SYS_EVT_FILE_SAVED)
    {
        clock_render_now();
    }
}

static const uint8_t *clock_font_char(char c)
{
    if(c >= '0' && c <= '9')
    {
        return FONT_DIGIT[c - '0'];
    }
    if(c == ':')
    {
        return FONT_COLON;
    }
    return FONT_BLANK;
}

static void clock_set_pixel(uint8_t *bin, int x, int y)
{
    if(x < 0 || y < 0 || x >= EPD_WIDTH || y >= EPD_HEIGHT)
    {
        return;
    }
    uint32_t idx = (uint32_t)y * EPD_WIDTH + x;
    bin[idx >> 3] |= (0x80 >> (idx & 7));   // MSB 在左
}

static void clock_draw_char(uint8_t *bin, int x, int y, int scale, const uint8_t *glyph)
{
    for(int gy = 0; gy < 7; gy++)
    {
        uint8_t bits = glyph ? glyph[gy] : 0x00;
        for(int gx = 0; gx < 5; gx++)
        {
            if(bits & (0x10 >> gx))
            {
                for(int dy = 0; dy < scale; dy++)
                {
                    for(int dx = 0; dx < scale; dx++)
                    {
                        clock_set_pixel(bin, x + gx * scale + dx, y + gy * scale + dy);
                    }
                }
            }
        }
    }
}

static void clock_draw_string(uint8_t *bin, const char *s, int scale)
{
    int n = (int)strlen(s);
    if(n <= 0)
    {
        return;
    }

    int slot_digit = 5 * scale;
    int slot_colon = 3 * scale;
    int gap = scale;

    int total = 0;
    for(int i = 0; i < n; i++)
    {
        total += (s[i] == ':') ? slot_colon : slot_digit;
    }
    total += (n - 1) * gap;

    int x = (EPD_WIDTH - total) / 2;
    int y = (EPD_HEIGHT - (7 * scale)) / 2;

    for(int i = 0; i < n; i++)
    {
        char c = s[i];
        int slot_w = (c == ':') ? slot_colon : slot_digit;
        clock_draw_char(bin, x, y, scale, clock_font_char(c));
        x += slot_w + gap;
    }
}

static int clock_render_now(void)
{
    if(!app_render_has_monofast())
    {
        sys_logw(APP_CLOCK_TAG, "monofast not supported, clock display off");
        return 0;
    }

    time_t now = time(NULL);
    struct tm tmv;
    localtime_r(&now, &tmv);

    char text[16];
    snprintf(text, sizeof(text), "%02d:%02d:%02d", tmv.tm_hour, tmv.tm_min, tmv.tm_sec);

    if(strcmp(text, m_last_text) == 0)
    {
        return 0;   // 内容未变，跳过
    }
    strncpy(m_last_text, text, sizeof(m_last_text) - 1);

    if(m_mono_buf == NULL)
    {
        m_mono_buf = (uint8_t*)heap_caps_malloc(CLOCK_MONO_BYTES, MALLOC_CAP_SPIRAM);
        if(m_mono_buf == NULL)
        {
            sys_loge(APP_CLOCK_TAG, "mono buffer alloc failed: %d bytes", CLOCK_MONO_BYTES);
            return 0;
        }
    }

    memset(m_mono_buf, 0, CLOCK_MONO_BYTES);   // 白底

    int scale = EPD_WIDTH / 46;
    if(scale < CLOCK_SCALE_MIN)
    {
        scale = CLOCK_SCALE_MIN;
    }
    if(scale > CLOCK_SCALE_MAX)
    {
        scale = CLOCK_SCALE_MAX;
    }

    clock_draw_string(m_mono_buf, text, scale);
    app_render_display_mono(m_mono_buf);

    sys_logi(APP_CLOCK_TAG, "clock render: %s", text);
    return 1;
}
