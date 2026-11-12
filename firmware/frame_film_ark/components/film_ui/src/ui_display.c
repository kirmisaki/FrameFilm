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
 * FileName : /film_ui/src/ui_display.c
 * Author: Kiritro  Version: v0.1  Date: 2026/9/17
 * Description: UI 层显示链路：LVGL I1 缓冲 → 90° 转置 → mono 位图 → EPD
 * ChangeLog: Change Notes
 *
 *********************************************************************/

/*********************************************************************
 * INCLUDES
 */
#include <string.h>

#include "sys_log.h"
#include "hal_epd.h"

#include "ui_conf.h"
#include "ui_display.h"

#if (SYS_UI_ENABLE == 0)

/*********************************************************************
 * UI 层被裁剪：空实现，且不引用任何 lv_* 符号（LVGL 静态库不会被链接）
 *********************************************************************/

int  ui_display_acquire(void)               { return -1; }
void ui_display_release(void)               { }
void ui_display_set_output(int enable)      { (void)enable; }
void ui_display_invalidate_all(void)        { }
int64_t ui_display_last_flush_us(void)      { return 0; }
int ui_display_snapshot_mono(uint8_t *dst, uint32_t len)
{ (void)dst; (void)len; return -1; }
int ui_display_set_color_card(const uint8_t *pixels, int x, int y, int w, int h)
{ (void)pixels; (void)x; (void)y; (void)w; (void)h; return -1; }
void ui_display_clear_color_card(void)      { }

#else /* SYS_UI_ENABLE */

#include "esp_heap_caps.h"
#include "esp_timer.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "lvgl.h"

/*********************************************************************
 * MACROS
 */
#define UI_DISPLAY_TAG      "ui_disp"

/* LVGL 把索引色的**调色板放在缓冲区的开头**（`lv_draw_buf.c` 的 "Skip palette"），
 * 图像数据在其之后：
 *   · flush 回调拿到的 px_map 前 8 字节是调色板，必须先跳过才能拿到像素
 *   · 显存必须多申请这 8 字节（否则 LVGL 会写越界 8 字节）
 * 官方 SDL 驱动的 flush 就是 `px_map += LV_COLOR_INDEXED_PALETTE_SIZE(I1) * 4`。
 *
 * 漏跳这 8 字节的后果：8 字节 = 64 个 I1 像素 → 画面整体横移 64px；
 * 且 8 不是行跨度的整数倍（480/8 = 60 字节）→ 绕回的那 64px 跨了一行，再差 1px 纵向。 */
#define UI_I1_PALETTE_ENTRIES   (2u)   /* I1 = 2^1 个索引 */
#define UI_I1_PALETTE_BYTES     (UI_I1_PALETTE_ENTRIES * sizeof(lv_color32_t))

/* 显存实际字节数 = 调色板 + 图像数据 */
#define UI_I1_ALLOC_BYTES       (UI_I1_PALETTE_BYTES + UI_FB_BYTES)

/*********************************************************************
 * LOCAL VARIABLES
 */
static lv_display_t *m_disp = NULL;
static uint8_t *m_i1_buf = NULL;      /* LVGL I1 渲染缓冲（逻辑竖屏 UI_LOGICAL_W x UI_LOGICAL_H） */
static uint8_t *m_mono_buf = NULL;    /* 送驱动的 mono 位图（物理横向 UI_MONO_W x UI_MONO_H） */
static uint8_t *m_color_buf = NULL;   /* 通行证彩色页临时整屏 ColorFast 8bpp */
static const uint8_t *m_card_pixels = NULL;
static int m_card_x, m_card_y, m_card_w, m_card_h;

/* 保护 m_mono_buf：ui_task 的 flush 写、app_task 的截图读 */
static SemaphoreHandle_t m_fb_lock = NULL;

/* 输出闸门：app_task 侧 pause/page_exit 需要"立即"生效（防止正在进行的 flush
 * 覆盖随后绘制的直绘内容），而 ui_task 侧每帧都要读，故用 volatile 避免被缓存。 */
static volatile uint8_t m_output_enabled = 0;

/* 最近一次真正推屏的时刻（µs）；ui_task 据此判断 UI 空闲多久后给面板断电 */
static volatile int64_t m_last_flush_us = 0;

/*********************************************************************
 * LOCAL FUNCTIONS
 */

/**
 * @brief 读 I1 缓冲某像素的位值（每字节 8 像素，MSB 在前）
 */
static inline int ui_i1_get_bit(const uint8_t *buf, int x, int y)
{
    uint32_t idx = (uint32_t)y * UI_LOGICAL_W + (uint32_t)x;
    return (buf[idx >> 3] >> (7u - (idx & 7u))) & 1u;
}

/**
 * @brief 写 mono 位图某像素（每字节 8 像素，MSB 在前）
 */
static inline void ui_mono_put_pixel(uint8_t *buf, int x, int y, int black)
{
    uint32_t idx = (uint32_t)y * UI_MONO_W + (uint32_t)x;
    uint8_t mask = (uint8_t)(0x80u >> (idx & 7u));

    if(black)
    {
        buf[idx >> 3] |= mask;
    }
    else
    {
        buf[idx >> 3] &= (uint8_t)~mask;
    }
}

/**
 * @brief 分配显示缓冲：优先 PSRAM，失败回退内部 RAM
 *
 * 两块缓冲共 86KB。UI 层与 DIRECT 层**互斥**，故这两块只在 UI 页存在期间持有
 * （进入页面申请、退出页面归还），不会与图片 app 需要的大块连续 PSRAM 抢内存。
 * 优先 PSRAM：内部 RAM 还要留给 WiFi/BLE 与文件服务；UI 层低频刷新，PSRAM 带宽足够。
 */
static uint8_t *ui_display_alloc(size_t size, const char *what)
{
    uint8_t *p = (uint8_t *)heap_caps_malloc(size, MALLOC_CAP_SPIRAM);

    if(p != NULL)
    {
        sys_logi(UI_DISPLAY_TAG, "%s: %u bytes from PSRAM", what, (unsigned)size);
        return p;
    }

    p = (uint8_t *)heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if(p != NULL)
    {
        sys_logw(UI_DISPLAY_TAG, "%s: %u bytes from internal RAM (PSRAM unavailable)",
                 what, (unsigned)size);
    }
    return p;
}

/**
 * @brief I1 缓冲 → mono 位图：90° 转置 + 极性对齐
 *
 * 逻辑坐标 (sx, sy) ∈ 480x720 → 物理坐标 (dx, dy) ∈ 720x480，
 * 即目标像素 (dx, dy) 取源像素 (sx = dy, sy = 719 - dx)。
 *
 * 实现要点（实测：逐像素版 493ms/帧，本版约 20ms）：**按目标行攒满一整行再顺序写**。
 * 逐像素版的内层循环沿源行推进，于是每个像素写进 mono 的位置跨 90 字节
 * （一行 90 字节），345,600 次零散读改写 PSRAM 是这一帧的绝对大头。
 * 换算到"目标行 + 目标字节"后，一行只对应源缓冲里的**同一列**：
 *   · 同一目标行 dy 内，源字节偏移固定为 dy>>3、位固定为 7-(dy&7)——取位是常量掩码；
 *   · 目标字节 k 覆盖 dx = 8k..8k+7，对应源行 sy = (719-8k-r)，r = 0..7，
 *     即 8 个相邻源行、步长 60 字节，可整块读出。
 * 于是每字节 8 次取位拼好后写进**内部 RAM 的行缓冲**，再 memcpy 进 PSRAM（顺序写）。
 *
 * 注意：入参 i1 必须已**跳过调色板**（见 UI_I1_PALETTE_BYTES 与 ui_flush_cb）。
 */
static void ui_i1_to_mono(const uint8_t *i1, uint8_t *mono)
{
    /* 先整体填"白"：若转置存在未覆盖像素，失败姿态是白底而不是花屏 */
    memset(mono, UI_MONO_WHITE_BYTE, UI_MONO_BYTES);

#if UI_ROTATE_90_CW
    {
        const int src_row_bytes = UI_LOGICAL_W / 8;   /* 60 */
        const int dst_row_bytes = UI_MONO_W / 8;      /* 90 */
        uint8_t row[UI_MONO_W / 8];

        for(int dy = 0; dy < UI_MONO_H; dy++)
        {
            /* 源列 dy 的字节偏移：sy = 719 那一行的第 (dy>>3) 字节 */
            const uint8_t *col = i1 + (uint32_t)(UI_MONO_W - 1) * (uint32_t)src_row_bytes
                                    + (uint32_t)(dy >> 3);
            const uint8_t bitmask = (uint8_t)(1u << (7u - ((uint32_t)dy & 7u)));

            for(int k = 0; k < dst_row_bytes; k++)
            {
                /* r = 0 对应 dx = 8k → sy = 719-8k；r 每加 1 源行上一行 */
                const uint8_t *p = col - (uint32_t)(8 * k) * (uint32_t)src_row_bytes;
                uint8_t v = UI_MONO_WHITE_BYTE;

                for(int r = 0; r < 8; r++)
                {
                    uint8_t mask = (uint8_t)(0x80u >> r);
                    int black = (((*p & bitmask) ? 1 : 0) == UI_I1_BIT_BLACK);

                    if(black)
                    {
                        v |= mask;
                    }
                    else
                    {
                        v &= (uint8_t)~mask;
                    }
                    p -= src_row_bytes;
                }
                row[k] = v;
            }

            memcpy(mono + (uint32_t)dy * (uint32_t)dst_row_bytes, row, sizeof(row));
        }
    }
#else
    for(int sy = 0; sy < UI_LOGICAL_H; sy++)
    {
        for(int sx = 0; sx < UI_LOGICAL_W; sx++)
        {
            /* I1 位值 → 是否黑（UI_I1_BIT_BLACK 定义 I1 缓冲中"黑"的位值） */
            int black = (ui_i1_get_bit(i1, sx, sy) == UI_I1_BIT_BLACK);

            /* 逆时针：源左上角 → 目标左下角 */
            ui_mono_put_pixel(mono, sy, UI_MONO_H - 1 - sx, black);
        }
    }
#endif
}

static void ui_i1_to_color(const uint8_t *i1, uint8_t *color)
{
    /* FFUI native six-color codes -> measured ColorFast palette indexes.
       Selected with ForFilm cfClosestIndex() for the six RGB preview colors;
       these are waveform indexes, NOT native codes repeated in two planes.
       The FFUI loader rejects code 4 and codes above 6. */
    static const uint8_t native_to_fast[7] = {0, 21, 63, 47, 0, 32, 52};
    for(int dy = 0; dy < UI_MONO_H; ++dy)
    {
        uint8_t *dst = color + (size_t)dy * UI_MONO_W;
        for(int dx = 0; dx < UI_MONO_W; ++dx)
        {
            uint8_t code;
#if UI_ROTATE_90_CW
            const int sx = dy, sy = UI_LOGICAL_H - 1 - dx;
#else
            const int sx = UI_LOGICAL_W - 1 - dy, sy = dx;
#endif
            const int cx = sx - m_card_x, cy = sy - m_card_y;
            if(cx >= 0 && cy >= 0 && cx < m_card_w && cy < m_card_h)
            {
                const size_t index = (size_t)cy * (size_t)m_card_w + (size_t)cx;
                const uint8_t packed = m_card_pixels[index >> 1];
                code = (index & 1u) ? (packed & 0x0Fu) : (packed >> 4);
            }
            else code = ui_i1_get_bit(i1, sx, sy) == UI_I1_BIT_BLACK ? 0 : 1;
            dst[dx] = native_to_fast[code];
        }
    }
}

/**
 * @brief LVGL flush 回调
 *
 * RENDER_MODE_FULL 下 area 恒为整屏，px_map 即整块 I1 缓冲（含调色板前缀），
 * 故一帧只推一次面板；推送在 flush_ready 之前，天然形成"面板能多快就多快"的节流。
 */
static void ui_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    (void)area;

    if(m_output_enabled && lv_display_flush_is_last(disp))
    {
        int64_t t0 = esp_timer_get_time();

        /* px_map 指向缓冲区开头，前 UI_I1_PALETTE_BYTES 字节是调色板：
           每次 flush 顺手写一次，保证索引色语义确定（索引 0 = 黑、1 = 白，
           与 UI_I1_BIT_BLACK "位 1 = 白" 一致；黑白在任意字节序下都一样，
           故按字节写，不依赖结构体字段顺序） */
        uint8_t *pal = px_map;

        pal[0] = 0x00; pal[1] = 0x00; pal[2] = 0x00; pal[3] = 0xFF;   /* 索引 0：黑 */
        pal[4] = 0xFF; pal[5] = 0xFF; pal[6] = 0xFF; pal[7] = 0xFF;   /* 索引 1：白 */

        if(m_fb_lock != NULL) xSemaphoreTake(m_fb_lock, portMAX_DELAY);
        if(m_card_pixels != NULL)
            ui_i1_to_color(px_map + UI_I1_PALETTE_BYTES, m_color_buf);
        else
            ui_i1_to_mono(px_map + UI_I1_PALETTE_BYTES, m_mono_buf);
        if(m_fb_lock != NULL) xSemaphoreGive(m_fb_lock);
        int64_t t1 = esp_timer_get_time();

        /* 打开黑白快刷会话：UI 连续刷新（进度条逐格 / 连点换页）帧间省掉
           每帧约 200ms 的 PON/POF。会话由 ui_task 在空闲一段时间后关闭
           （见 ui_core.c），页面退出时也会关，面板不会留在带电态。 */
        /* 一次 flush 的多个驱动调用当作一个原子事务：期间不允许直绘层
           （图片/动图刷新）插进来复位面板或改写 RAM。 */
        hal_epd_lock();
        if(m_card_pixels != NULL)
        {
            hal_epd_mono_session_end();
            hal_epd_display_init();
            hal_epd_display_8bpp_mode(m_color_buf, 0); /* ColorFast, same as Film */
            hal_epd_pwroff();
        }
        else
        {
            hal_epd_mono_session_begin();
            hal_epd_display_mono(m_mono_buf);
        }
        hal_epd_unlock();
        int64_t t2 = esp_timer_get_time();

        /* TEMP 打点：定位 UI 一帧 ~1s 花在哪（conv = I1→mono 转置 / epd = 驱动刷屏） */
        sys_logd(UI_DISPLAY_TAG, "flush: period=%dus conv=%dus epd=%dus",
                 (int32_t)(m_last_flush_us ? (t0 - m_last_flush_us) : 0),
                 (int32_t)(t1 - t0), (int32_t)(t2 - t1));

        m_last_flush_us = t2;
    }

    lv_display_flush_ready(disp);
}

/*********************************************************************
 * GLOBAL FUNCTIONS
 */

int ui_display_acquire(void)
{
    if(m_disp != NULL)
    {
        return 0;   // 已申请
    }

    if(m_fb_lock == NULL)
    {
        m_fb_lock = xSemaphoreCreateMutex();
        if(m_fb_lock == NULL)
        {
            sys_loge(UI_DISPLAY_TAG, "fb mutex create failed");
            return -1;
        }
    }

    /* 两块缓冲共 86KB；只在 UI 页存在期间持有（见 ui_display_acquire 说明）。
       I1 缓冲要多申请调色板那 8 字节（见 UI_I1_PALETTE_BYTES） */
    m_i1_buf = ui_display_alloc(UI_I1_ALLOC_BYTES, "i1 framebuffer");
    m_mono_buf = ui_display_alloc(UI_MONO_BYTES, "mono frame");
    if(m_i1_buf == NULL || m_mono_buf == NULL)
    {
        sys_loge(UI_DISPLAY_TAG, "alloc failed (fb=%d+%d mono=%d): psram_free=%u psram_largest=%u internal_free=%u internal_largest=%u",
                 UI_I1_PALETTE_BYTES, UI_FB_BYTES, UI_MONO_BYTES,
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
        ui_display_release();
        return -1;
    }
    memset(m_i1_buf, UI_I1_WHITE_BYTE, UI_I1_ALLOC_BYTES);
    memset(m_mono_buf, UI_MONO_WHITE_BYTE, UI_MONO_BYTES);

    m_disp = lv_display_create(UI_LOGICAL_W, UI_LOGICAL_H);
    if(m_disp == NULL)
    {
        sys_loge(UI_DISPLAY_TAG, "lv_display_create failed");
        ui_display_release();
        return -1;
    }

    lv_display_set_color_format(m_disp, LV_COLOR_FORMAT_I1);
    lv_display_set_flush_cb(m_disp, ui_flush_cb);
    /* 全屏单缓冲：flush_cb 一次拿到整屏，与 EPD 的整帧推送模型吻合。
       入参 size 用整块分配大小（含调色板），避免 LVGL 的边界判断按 43200 去算 */
    lv_display_set_buffers(m_disp, m_i1_buf, NULL, UI_I1_ALLOC_BYTES, LV_DISPLAY_RENDER_MODE_FULL);

    m_output_enabled = 0;
    sys_logi(UI_DISPLAY_TAG, "display acquired: %dx%d I1, fb=%u+%d bytes (palette+data)",
             UI_LOGICAL_W, UI_LOGICAL_H, (unsigned)UI_I1_PALETTE_BYTES, UI_FB_BYTES);
    return 0;
}

void ui_display_release(void)
{
    m_output_enabled = 0;
    ui_display_clear_color_card();

    if(m_disp != NULL)
    {
        lv_display_delete(m_disp);
        m_disp = NULL;
    }
    if(m_i1_buf != NULL)
    {
        heap_caps_free(m_i1_buf);
        m_i1_buf = NULL;
    }
    if(m_mono_buf != NULL)
    {
        heap_caps_free(m_mono_buf);
        m_mono_buf = NULL;
    }
    /* m_fb_lock 常驻：app_task 的截图可能与页面销毁同时发生，删锁会引入竞态 */
}

int ui_display_set_color_card(const uint8_t *pixels, int x, int y, int w, int h)
{
    if(pixels == NULL || w <= 0 || h <= 0 || m_disp == NULL) return -1;
    if(m_color_buf == NULL)
    {
        m_color_buf = ui_display_alloc((size_t)UI_MONO_W * UI_MONO_H, "ColorFast frame");
        if(m_color_buf == NULL) return -1;
    }
    m_card_x = x; m_card_y = y; m_card_w = w; m_card_h = h;
    m_card_pixels = pixels;
    return 0;
}

void ui_display_clear_color_card(void)
{
    m_card_pixels = NULL;
    if(m_color_buf != NULL) { heap_caps_free(m_color_buf); m_color_buf = NULL; }
}

void ui_display_set_output(int enable)
{
    if(enable)
    {
        /* 新一页开始：清掉上一页的推屏时刻，避免 ui_task 拿着旧时间戳提前断电 */
        m_last_flush_us = 0;
    }
    m_output_enabled = enable ? 1 : 0;
}

int64_t ui_display_last_flush_us(void)
{
    return m_last_flush_us;
}

int ui_display_snapshot_mono(uint8_t *dst, uint32_t len)
{
    if(dst == NULL || len < UI_MONO_BYTES || m_mono_buf == NULL || m_disp == NULL)
    {
        return -1;
    }

    if(m_fb_lock != NULL) xSemaphoreTake(m_fb_lock, portMAX_DELAY);
    /* 彩色卡页面走的是 m_color_buf，m_mono_buf 里仍是上一张黑白画面 */
    if(m_card_pixels != NULL)
    {
        if(m_fb_lock != NULL) xSemaphoreGive(m_fb_lock);
        return -2;
    }
    memcpy(dst, m_mono_buf, UI_MONO_BYTES);
    if(m_fb_lock != NULL) xSemaphoreGive(m_fb_lock);
    return 0;
}

void ui_display_invalidate_all(void)
{
    lv_obj_t *scr = lv_screen_active();

    if(scr != NULL)
    {
        (void)lv_obj_invalidate(scr);
    }
}

#endif /* SYS_UI_ENABLE */
