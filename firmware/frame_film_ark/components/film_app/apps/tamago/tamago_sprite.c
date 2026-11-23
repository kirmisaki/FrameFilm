/*********************************************************************
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * FileName : /film_app/apps/tamago/tamago_sprite.c
 * Description: 拓麻歌子 1-bit 精灵绘制（40×40 逻辑格 → 160×160 L8）
 *
 *********************************************************************/

/*********************************************************************
 * INCLUDES
 */
#include <math.h>
#include <string.h>

#include "tamago_sprite.h"

/*********************************************************************
 * MACROS
 */
#define GRID_W      (40)
#define GRID_H      (40)
#define SCALE       (TAMAGO_SPRITE_W / GRID_W)      /* 4 */

#define INK         (0x00u)
#define PAPER       (0xFFu)

/*********************************************************************
 * LOCAL VARIABLES
 */
static uint8_t m_grid[GRID_W * GRID_H];

/*********************************************************************
 * LOCAL FUNCTIONS
 */
static void g_rect(int x, int y, int w, int h, uint8_t v);
static void g_ellipse(int cx, int cy, int rx, int ry, uint8_t v);
static void g_tri(int x0, int y0, int x1, int y1, int x2, int y2, uint8_t v);
static void g_line(int x0, int y0, int x1, int y1, uint8_t v);
static void draw_egg(uint32_t frame);
static void draw_tombstone(void);
static void draw_amiya(int stage, uint8_t face, uint32_t frame);
static void draw_poop(int x, int y);
static void draw_badge_skull(int x, int y);
static void draw_zzz(uint32_t frame);
static void draw_sparkles(uint32_t frame);

/*********************************************************************
 * LOCAL FUNCTIONS
 */

static void g_rect(int x, int y, int w, int h, uint8_t v)
{
    int i;
    int j;

    for(j = y; j < y + h; j++)
    {
        if(j < 0 || j >= GRID_H)
        {
            continue;
        }
        for(i = x; i < x + w; i++)
        {
            if(i < 0 || i >= GRID_W)
            {
                continue;
            }
            m_grid[j * GRID_W + i] = v;
        }
    }
}

static void g_ellipse(int cx, int cy, int rx, int ry, uint8_t v)
{
    int y;

    if(rx <= 0 || ry <= 0)
    {
        return;
    }
    for(y = cy - ry; y <= cy + ry; y++)
    {
        float dy = (float)(y - cy) / (float)ry;
        float t = 1.0f - dy * dy;
        int hw;

        if(t <= 0.0f)
        {
            continue;
        }
        hw = (int)((float)rx * sqrtf(t) + 0.5f);
        g_rect(cx - hw, y, hw * 2 + 1, 1, v);
    }
}

static void g_tri(int x0, int y0, int x1, int y1, int x2, int y2, uint8_t v)
{
    int minx = x0 < x1 ? (x0 < x2 ? x0 : x2) : (x1 < x2 ? x1 : x2);
    int maxx = x0 > x1 ? (x0 > x2 ? x0 : x2) : (x1 > x2 ? x1 : x2);
    int miny = y0 < y1 ? (y0 < y2 ? y0 : y2) : (y1 < y2 ? y1 : y2);
    int maxy = y0 > y1 ? (y0 > y2 ? y0 : y2) : (y1 > y2 ? y1 : y2);
    int x;
    int y;

    for(y = miny; y <= maxy; y++)
    {
        for(x = minx; x <= maxx; x++)
        {
            float px = (float)x + 0.5f;
            float py = (float)y + 0.5f;
            float d1 = (px - x1) * (y0 - y1) - (x0 - x1) * (py - y1);
            float d2 = (px - x2) * (y1 - y2) - (x1 - x2) * (py - y2);
            float d3 = (px - x0) * (y2 - y0) - (x2 - x0) * (py - y0);
            int neg = (d1 < 0.0f) || (d2 < 0.0f) || (d3 < 0.0f);
            int pos = (d1 > 0.0f) || (d2 > 0.0f) || (d3 > 0.0f);

            if(!(neg && pos))
            {
                if(x >= 0 && x < GRID_W && y >= 0 && y < GRID_H)
                {
                    m_grid[y * GRID_W + x] = v;
                }
            }
        }
    }
}

static void g_line(int x0, int y0, int x1, int y1, uint8_t v)
{
    int dx = (x1 > x0) ? (x1 - x0) : (x0 - x1);
    int dy = (y1 > y0) ? (y1 - y0) : (y0 - y1);
    int sx = (x1 > x0) ? 1 : -1;
    int sy = (y1 > y0) ? 1 : -1;
    int err = dx - dy;

    for(;;)
    {
        if(x0 >= 0 && x0 < GRID_W && y0 >= 0 && y0 < GRID_H)
        {
            m_grid[y0 * GRID_W + x0] = v;
        }
        if(x0 == x1 && y0 == y1)
        {
            break;
        }
        if(2 * err > -dy)
        {
            err -= dy;
            x0 += sx;
        }
        if(2 * err < dx)
        {
            err += dx;
            y0 += sy;
        }
    }
}

/**
 * @brief 蛋（阶段 0）：轻微左右晃
 */
static void draw_egg(uint32_t frame)
{
    int wob = ((frame / 2) % 2) ? 1 : -1;

    g_ellipse(20 + wob, 21, 11, 13, INK);
    g_ellipse(20 + wob, 21, 9, 12, PAPER);
    g_rect(16 + wob, 14, 2, 2, INK);
    g_rect(24 + wob, 22, 2, 2, INK);
    g_rect(20 + wob, 11, 2, 2, INK);
    g_rect(12, 35, 16, 1, INK);
}

/**
 * @brief 墓碑（死亡）
 */
static void draw_tombstone(void)
{
    g_ellipse(20, 19, 8, 6, INK);
    g_rect(12, 19, 17, 15, INK);
    g_ellipse(20, 19, 7, 5, PAPER);
    g_rect(13, 20, 15, 13, PAPER);
    g_rect(19, 22, 3, 9, INK);
    g_rect(15, 25, 11, 3, INK);
    g_rect(9, 34, 23, 2, INK);
}

/**
 * @brief 阿米娅：兔耳 / 棕发（1-bit 下即黑帽檐）/ 蓝眼 / 深色外套
 *
 * 全部按逻辑格作画：头 16-22 格宽，身体 8-11 格宽，整体约 33 格高。
 */
static void draw_amiya(int stage, uint8_t face, uint32_t frame)
{
    int s = stage;
    int cx = 20;
    int cy = 19;
    int hr;
    int top;
    int bob;
    int sway;
    int ex1;
    int ex2;
    int ey;
    int bw;
    int btop;
    int bbot;

    if(s < 1) s = 1;
    if(s > 4) s = 4;

    hr = 8 + s / 2;                       /* 8..10 */
    bob = ((frame / 2) % 2) ? 0 : 1;      /* 呼吸：上下 1 格 */
    sway = (int)(frame % 8) < 4 ? 0 : (((frame % 8) < 6) ? 1 : -1);

    if(face == TAMAGO_FACE_SICK)
    {
        cx += ((frame % 2) ? 1 : -1);     /* 生病：左右抖动 */
        bob = 0;
    }
    if(face == TAMAGO_FACE_HAPPY)
    {
        bob = (int)(frame % 2);           /* 高兴：蹦一下 */
    }

    top = cy - hr + bob;

    /* ---- 兔耳（先画，后面用头发盖住根部） ---- */
    g_tri(cx - 6, top + 3, cx - 3, top + 3, cx - 9 + sway, top - 11, INK);
    g_tri(cx + 6, top + 3, cx + 3, top + 3, cx + 9 - sway, top - 11, INK);
    /* 耳内白线 */
    g_tri(cx - 5, top + 2, cx - 4, top + 2, cx - 8 + sway, top - 9, PAPER);
    g_tri(cx + 5, top + 2, cx + 4, top + 2, cx + 8 - sway, top - 9, PAPER);
    /* 耳尖 1 格 */
    g_rect(cx - 10 + sway, top - 12, 2, 2, INK);
    g_rect(cx + 8 - sway, top - 12, 2, 2, INK);

    /* ---- 头发（黑）盖住头顶 ---- */
    g_ellipse(cx, cy - 1 + bob, hr + 1, hr, INK);

    /* ---- 脸（白） ---- */
    g_ellipse(cx, cy + 2 + bob, hr - 1, hr - 2, PAPER);

    /* ---- 刘海：中间两片尖角 ---- */
    g_tri(cx - hr, cy - 2 + bob, cx - 1, cy - 2 + bob, cx - 4, cy + 3 + bob, INK);
    g_tri(cx + hr, cy - 2 + bob, cx + 1, cy - 2 + bob, cx + 4, cy + 3 + bob, INK);

    /* ---- 眼睛 ---- */
    ex1 = cx - 4;
    ex2 = cx + 4;
    ey = cy + 3 + bob;

    if(face == TAMAGO_FACE_SLEEP)
    {
        g_rect(ex1 - 1, ey + 1, 3, 1, INK);
        g_rect(ex2 - 1, ey + 1, 3, 1, INK);
    }
    else if(face == TAMAGO_FACE_HAPPY)
    {
        g_line(ex1 - 1, ey + 1, ex1, ey - 1, INK);
        g_line(ex1, ey - 1, ex1 + 1, ey + 1, INK);
        g_line(ex2 - 1, ey + 1, ex2, ey - 1, INK);
        g_line(ex2, ey - 1, ex2 + 1, ey + 1, INK);
    }
    else if(face == TAMAGO_FACE_SICK)
    {
        g_line(ex1 - 1, ey - 1, ex1 + 1, ey + 1, INK);
        g_line(ex1 + 1, ey - 1, ex1 - 1, ey + 1, INK);
        g_line(ex2 - 1, ey - 1, ex2 + 1, ey + 1, INK);
        g_line(ex2 + 1, ey - 1, ex2 - 1, ey + 1, INK);
    }
    else if(face == TAMAGO_FACE_EVOLVE)
    {
        g_rect(ex1 - 1, ey - 1, 3, 3, INK);
        g_rect(ex1, ey, 1, 1, PAPER);
        g_rect(ex2 - 1, ey - 1, 3, 3, INK);
        g_rect(ex2, ey, 1, 1, PAPER);
    }
    else
    {
        /* 大眼 + 高光（阿米娅的蓝眼在 1-bit 下就是黑眼珠 + 白点） */
        g_rect(ex1 - 1, ey - 1, 3, 4, INK);
        g_rect(ex2 - 1, ey - 1, 3, 4, INK);
        g_rect(ex1, ey - 1, 1, 1, PAPER);
        g_rect(ex2, ey - 1, 1, 1, PAPER);
    }

    /* ---- 嘴 ---- */
    if(face == TAMAGO_FACE_EAT)
    {
        g_rect(cx - 1, ey + 3, 3, 2, INK);
    }
    else if(face == TAMAGO_FACE_HAPPY || face == TAMAGO_FACE_EVOLVE)
    {
        g_line(cx - 2, ey + 4, cx, ey + 5, INK);
        g_line(cx, ey + 5, cx + 2, ey + 4, INK);
    }
    else
    {
        g_rect(cx - 1, ey + 4, 3, 1, INK);
    }

    /* ---- 身体（深色外套） ---- */
    bw = 4 + s;
    btop = cy + hr - 1 + bob;
    bbot = btop + 6 + s;

    {
        int y;

        for(y = btop; y <= bbot; y++)
        {
            int hw = bw + (y - btop) / 3;
            g_rect(cx - hw, y, hw * 2, 1, INK);
        }
    }
    /* 白色 V 领 */
    g_tri(cx - 3, btop, cx + 3, btop, cx, btop + 4, PAPER);

    /* 围巾（青年/成年） */
    if(s >= 3)
    {
        g_rect(cx - bw, btop, bw * 2, 1, INK);
        g_rect(cx - 1, btop + 1, 2, 1, PAPER);
    }

    /* 脚 */
    g_rect(cx - bw + 1, bbot + 1, 3, 2, INK);
    g_rect(cx + bw - 4, bbot + 1, 3, 2, INK);

    /* 吃东西时手里的小饭团 */
    if(face == TAMAGO_FACE_EAT)
    {
        g_ellipse(cx + bw + 3, btop + 4, 3, 3, INK);
        g_ellipse(cx + bw + 3, btop + 4, 2, 2, PAPER);
        g_rect(cx + bw + 2, btop + 5, 3, 2, INK);
    }
}

/**
 * @brief 便便（右下角）
 */
static void draw_poop(int x, int y)
{
    g_ellipse(x, y + 2, 3, 2, INK);
    g_ellipse(x, y - 1, 2, 2, INK);
    g_rect(x - 1, y - 4, 2, 2, INK);
}

/**
 * @brief 生病角标（骷髅）
 */
static void draw_badge_skull(int x, int y)
{
    g_ellipse(x, y, 3, 3, INK);
    g_ellipse(x, y, 2, 2, PAPER);
    g_rect(x - 1, y - 1, 1, 1, INK);
    g_rect(x + 1, y - 1, 1, 1, INK);
    g_rect(x - 1, y + 2, 3, 1, INK);
}

/**
 * @brief 睡觉的 Zzz
 */
static void draw_zzz(uint32_t frame)
{
    int up = (int)(frame % 4);

    g_rect(29, 9 - up / 2, 3, 1, INK);
    g_rect(31, 10 - up / 2, 1, 1, INK);
    g_rect(29, 11 - up / 2, 3, 1, INK);
    g_rect(32, 5 - up / 2, 2, 1, INK);
    g_rect(33, 6 - up / 2, 1, 1, INK);
    g_rect(32, 7 - up / 2, 2, 1, INK);
}

/**
 * @brief 进化闪光
 */
static void draw_sparkles(uint32_t frame)
{
    int i;
    static const uint8_t pos[4][2] = {{8, 9}, {33, 12}, {10, 27}, {31, 29}};

    for(i = 0; i < 4; i++)
    {
        if(((frame / 2 + i) % 3) == 0)
        {
            continue;
        }
        g_rect(pos[i][0] - 1, pos[i][1], 3, 1, INK);
        g_rect(pos[i][0], pos[i][1] - 1, 1, 3, INK);
    }
}

/*********************************************************************
 * GLOBAL FUNCTIONS
 */

void tamago_sprite_render(uint8_t *buf, uint8_t face, uint8_t stage,
                          uint32_t frame, uint8_t poop)
{
    int x;
    int y;

    if(buf == NULL)
    {
        return;
    }

    memset(m_grid, PAPER, sizeof(m_grid));

    if(face == TAMAGO_FACE_DEAD)
    {
        draw_tombstone();
    }
    else if(stage == 0)
    {
        draw_egg(frame);
    }
    else
    {
        draw_amiya((int)stage, face, frame);
    }

    /* 状态叠加 */
    if(face == TAMAGO_FACE_SICK)
    {
        draw_badge_skull(33, 8);
    }
    if(face == TAMAGO_FACE_SLEEP)
    {
        draw_zzz(frame);
    }
    if(face == TAMAGO_FACE_EVOLVE)
    {
        draw_sparkles(frame);
    }
    if(poop > 0)
    {
        draw_poop(30, 33);
        if(poop > 1)
        {
            draw_poop(24, 35);
        }
    }

    /* 逻辑格 → 160×160（每格 4×4 像素） */
    for(y = 0; y < GRID_H; y++)
    {
        for(x = 0; x < GRID_W; x++)
        {
            uint8_t v = m_grid[y * GRID_W + x];
            uint8_t *p = &buf[(y * SCALE) * TAMAGO_SPRITE_W + x * SCALE];
            int i;

            for(i = 0; i < SCALE; i++)
            {
                memset(p + i * TAMAGO_SPRITE_W, v, SCALE);
            }
        }
    }
}
