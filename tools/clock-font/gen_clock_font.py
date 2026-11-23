#!/usr/bin/env python3
"""生成时钟主读数用的数字字库（LVGL 9 原生位图字库格式，A8）。

为什么需要它：LVGL 内置的 Montserrat 字库最大只到 48px，而时钟主读数要再大一号，
内置档位里没有 56/60 可选。这里用 LVGL 自带的 Montserrat-Medium.ttf 单独生成一份
**只含时钟用得到的字形**（'-' 0-9 ':'）的字库：既拿到更大的字号，Flash 占用又远小于
一档完整字库（完整 48px 字库约 230KB，这份 60px 只有十几 KB）。

输出（字形名固定，换字号只改大小、不动名字，免得改 C 代码）：
    firmware/frame_film/components/film_app/apps/clock/font_clock_hero.h
    firmware/frame_film/components/film_app/apps/clock/font_clock_hero.c

用法（仓库根目录）：
    python tools/clock-font/gen_clock_font.py              # 默认 60px
    python tools/clock-font/gen_clock_font.py --size 56    # 换字号
    python tools/clock-font/gen_clock_font.py --dry-run    # 只打印度量，不写文件

依赖：Pillow（`pip install pillow`）
"""

import argparse
import os
import sys

from PIL import Image, ImageDraw, ImageFont

# 字形集：时钟主读数只会出现这三种字符（"--:--" 占位 + "%02u:%02u" 时间）
GLYPHS = ["-"] + [str(d) for d in range(10)] + [":"]

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))

TTF_CANDIDATES = [
    os.path.join(REPO_ROOT, "assets", "fonts", "Montserrat-Medium.ttf"),
    os.path.join(REPO_ROOT, "tools", "clock-font", "Montserrat-Medium.ttf"),
    os.path.join(REPO_ROOT, "firmware", "frame_film", "managed_components", "lvgl__lvgl",
                 "scripts", "generators", "built_in_font", "Montserrat-Medium.ttf"),
]

OUT_H = os.path.join(REPO_ROOT, "firmware", "frame_film", "components", "film_app",
                     "apps", "clock", "font_clock_hero.h")
OUT_C = os.path.join(REPO_ROOT, "firmware", "frame_film", "components", "film_app",
                     "apps", "clock", "font_clock_hero.c")

# 字面度量按 em 比例给（与内置 montserrat_48 的 line_height=52/base_line=9/
# cap_height=34/x_height=25/underline=-4,2 完全对得上：0.700/0.517/0.09/0.045 em）
EM_CAP = 0.700
EM_X = 0.517
EM_UNDERLINE_POS = 0.090
EM_UNDERLINE_THICK = 0.045


def find_ttf():
    for path in TTF_CANDIDATES:
        if os.path.isfile(path):
            return path
    print("找不到 Montserrat-Medium.ttf，试过：")
    for path in TTF_CANDIDATES:
        print("  " + path)
    return None


def rasterize(ttf, size):
    """渲染字形，返回 (glyphs, line_height, base_line)。

    Pillow 的文本锚点默认是 "la"（左侧 + 升部线），所以 bbox 的 y 以升部线为 0，
    基线在 y = ascent 处 —— 与 LVGL 的 base_line 语义一致。
    LVGL 的 ofs_y 定义是"基线到字形框底部的距离，向上为正"（见
    lv_draw_label.c: box_top = 基线 - box_h - ofs_y），故 ofs_y = ascent - bbox.y1。
    """
    font = ImageFont.truetype(ttf, size)
    ascent, descent = font.getmetrics()

    glyphs = []
    for ch in GLYPHS:
        x0, y0, x1, y1 = font.getbbox(ch)
        w, h = x1 - x0, y1 - y0
        img = Image.new("L", (w, h), 0)
        ImageDraw.Draw(img).text((-x0, -y0), ch, font=font, fill=255)
        glyphs.append({
            "ch": ch,
            "code": ord(ch),
            "w": w,
            "h": h,
            "ofs_x": x0,
            "ofs_y": ascent - y1,
            "adv_w": int(round(font.getlength(ch) * 16)),
            "bitmap": img.tobytes(),
        })

    return glyphs, ascent + descent, descent


def emit_bitmap(glyphs):
    lines = []
    for i, g in enumerate(glyphs):
        lines.append("    /* %s */" % ('空格' if g["ch"] == " " else g["ch"]))
        data = g["bitmap"]
        for off in range(0, len(data), 16):
            row = data[off:off + 16]
            lines.append("    " + " ".join("0x%02X," % b for b in row))
    return "\n".join(lines)


def emit_glyph_dsc(glyphs):
    lines = ["    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0},"
             "   /* id 0 保留（字形未找到时返回 0） */"]
    index = 0
    for g in glyphs:
        lines.append("    {.bitmap_index = %d, .adv_w = %d, .box_w = %d, .box_h = %d, "
                     ".ofs_x = %d, .ofs_y = %d},   /* '%s' */"
                     % (index, g["adv_w"], g["w"], g["h"], g["ofs_x"], g["ofs_y"], g["ch"]))
        index += len(g["bitmap"])
    return "\n".join(lines)


def emit_cmap(glyphs):
    """按连续码位切段：'-'（0x2D）/ 数字（0x30~0x39）/ ':'（0x3A）。

    FORMAT0_TINY 要求每段内码位与字形 id 都连续，故 '-' 与数字之间必须断开。
    """
    spans = []
    for g in glyphs:
        if spans and g["code"] == spans[-1][1] + 1:
            spans[-1][1] = g["code"]
            spans[-1][2] += 1
        else:
            spans.append([g["code"], g["code"], 1])

    # 段的第一码位 → 该段第一个字形在 glyph_dsc 里的下标（+1 跳过 id 0 保留位）
    lines = []
    gid = 1
    for first, _last, count in spans:
        lines.append("    {.range_start = 0x%02X, .range_length = %d, .glyph_id_start = %d, "
                     ".unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0, "
                     ".type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY},"
                     % (first, count, gid))
        gid += count
    return "\n".join(lines), len(spans)


def emit_header(size):
    return """/**
 * 本文件由 tools/clock-font/gen_clock_font.py 生成，请勿手工编辑。
 * 时钟主读数数字字库：Montserrat Medium %dpx，仅含 '-' 0-9 ':'
 */

#ifndef __FONT_CLOCK_HERO_H__
#define __FONT_CLOCK_HERO_H__

#include "lvgl.h"

/* 时钟主读数：'-' 0-9 ':'（LVGL 内置 Montserrat 最大 48px，这里自备更大一档） */
extern const lv_font_t lv_font_clock_hero;

#endif /* __FONT_CLOCK_HERO_H__ */
""" % size


def emit_c(ttf_name, size, glyphs, line_height, base_line, cmaps, cmap_num):
    total = sum(len(g["bitmap"]) for g in glyphs)
    return """/**
 * 本文件由 tools/clock-font/gen_clock_font.py 生成，请勿手工编辑。
 *
 * 源字体：%s（SIL OFL 1.1）  字号：%dpx
 * 字形：%s
 * 位图：%d 字节（A8，每字形 box_w × box_h，行间无填充）
 */

#include "font_clock_hero.h"

/* 所有字形的位图首尾相接；glyph_dsc 里的 bitmap_index 是字节偏移 */
static const uint8_t glyph_bitmap[] = {
%s
};

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
%s
};

static const lv_font_fmt_txt_cmap_t cmaps[] = {
%s
};

static const lv_font_fmt_txt_dsc_t font_dsc = {
    .glyph_bitmap = glyph_bitmap,
    .glyph_dsc = glyph_dsc,
    .cmaps = cmaps,
    .kern_dsc = NULL,               /* 数字之间不需要字偶距 */
    .kern_scale = 0,
    .cmap_num = %d,
    .bpp = 8,                       /* A8：LVGL 直接当灰度覆盖度用 */
    .kern_classes = 0,
    .bitmap_format = LV_FONT_FMT_TXT_PLAIN,
    .stride = 0,                    /* 每行不补齐，stride 即 box_w */
    .are_glyphs_dynamic_loaded = false,
};

const lv_font_t lv_font_clock_hero = {
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,
    .line_height = %d,
    .base_line = %d,
    .cap_height = %d,
    .x_height = %d,
    .subpx = LV_FONT_SUBPX_NONE,
    .kerning = LV_FONT_KERNING_NONE,
    .static_bitmap = 1,             /* 位图是 const，保持不变 */
    .underline_position = %d,
    .underline_thickness = %d,
    .dsc = &font_dsc,
    .fallback = NULL,
    .user_data = NULL,
};
""" % (
        ttf_name, size, " ".join("'%s'" % g["ch"] for g in glyphs), total,
        emit_bitmap(glyphs), emit_glyph_dsc(glyphs), cmaps, cmap_num,
        line_height, base_line,
        int(round(EM_CAP * size)), int(round(EM_X * size)),
        -int(round(EM_UNDERLINE_POS * size)), max(1, int(round(EM_UNDERLINE_THICK * size))),
    )


def main():
    ap = argparse.ArgumentParser(description="生成时钟主读数数字字库")
    ap.add_argument("--size", type=int, default=60, help="字号 px（默认 60）")
    ap.add_argument("--dry-run", action="store_true", help="只打印度量，不写文件")
    args = ap.parse_args()

    ttf = find_ttf()
    if ttf is None:
        return 1

    glyphs, line_height, base_line = rasterize(ttf, args.size)
    cmaps, cmap_num = emit_cmap(glyphs)

    print("源字体: %s" % ttf)
    print("字号: %dpx  line_height=%d  base_line=%d" % (args.size, line_height, base_line))
    print("%-4s %-6s %-8s %-6s %-6s %-6s" % ("字形", "adv_w", "box(w×h)", "ofs_x", "ofs_y", "字节"))
    for g in glyphs:
        print("%-4s %-6d %-8s %-6d %-6d %-6d"
              % (g["ch"], g["adv_w"], "%d×%d" % (g["w"], g["h"]), g["ofs_x"], g["ofs_y"],
                 len(g["bitmap"])))
    print("位图合计: %d 字节" % sum(len(g["bitmap"]) for g in glyphs))

    if args.dry_run:
        return 0

    with open(OUT_H, "w", encoding="utf-8") as fp:
        fp.write(emit_header(args.size))
    with open(OUT_C, "w", encoding="utf-8") as fp:
        fp.write(emit_c(os.path.basename(ttf), args.size, glyphs, line_height, base_line,
                        cmaps, cmap_num))

    print("已写入:\n  %s\n  %s" % (OUT_H, OUT_C))
    return 0


if __name__ == "__main__":
    sys.exit(main())
