#!/usr/bin/env python3
"""生成 unscii 的"加高版"字库：8 宽 × 16 高（纵向整数 2×）。

**为什么不是 12 号**：unscii 的字形是按 **8×8 像素网格**设计的 —— LVGL 内置的
`lv_font_unscii_8.c` 就是 `unscii-8.ttf --size 8 --bpp 1` 生成的。点阵字只有
**整数倍**放大才不糊：12 不是 8 的整数倍，笔画落不到像素格上，实测把 TTF 直接渲染
到 12px、把点阵按 1.5× 重采样、用 FreeType 单色光栅渲染，三种做法字形都会碎。
（这也是 LVGL 只提供 unscii_8 / unscii_16 两档的原因：16 正是 8 的整数 2 倍。）

**为什么只加高不加宽**：adv_w 仍是 128（每字 8px），于是所有既有文案的行宽、
居中、折行统统不用改，只是字高翻倍。代价是字形被纵向拉长（偏瘦长）。

度量与 lv_font_unscii_8 一致地按 2× 推：line_height 9→18、base_line 0→0、
每字步进 8px 不变；字形框由"原始 8×8 点阵复制一遍行"后重新求紧包围盒得到。

用法（仓库根目录）：
    python tools/ui-fonts/gen_unscii_8x16.py --preview   # 先打样例字形看效果
    python tools/ui-fonts/gen_unscii_8x16.py             # 生成 C 文件

依赖：Pillow（`pip install pillow`）
"""

import argparse
import os
import sys

from PIL import Image, ImageDraw, ImageFont

# 字符集：与 lv_font_unscii_8 的 cmap 一致（0x20~0x7F）
FIRST, LAST = 0x20, 0x7F

CELL_W, CELL_H = 8, 8           # unscii-8 的设计网格
SCALE_Y = 2                     # 只纵向放大（整数倍才干净）
ADV_W = 128                     # 8.4 定点：128 / 16 = 8px，与 unscii_8 相同
LINE_H = 9 * SCALE_Y            # unscii_8 的 line_height = 9
BASE_LINE = 0                   # unscii_8 的 base_line = 0（基线在行底）

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))

TTF_CANDIDATES = [
    os.path.join(REPO_ROOT, "assets", "fonts", "unscii-8.ttf"),
    os.path.join(REPO_ROOT, "tools", "ui-fonts", "unscii-8.ttf"),
    os.path.join(REPO_ROOT, "firmware", "frame_film", "managed_components", "lvgl__lvgl",
                 "scripts", "generators", "built_in_font", "unscii-8.ttf"),
]

OUT_INC = os.path.join(REPO_ROOT, "firmware", "frame_film", "components", "film_ui", "inc")
OUT_SRC = os.path.join(REPO_ROOT, "firmware", "frame_film", "components", "film_ui", "src")


def find_ttf():
    for path in TTF_CANDIDATES:
        if os.path.isfile(path):
            return path
    print("找不到 unscii-8.ttf，试过：")
    for path in TTF_CANDIDATES:
        print("  " + path)
    return None


def rasterize(ttf):
    """把 0x20~0x7F 逐个渲染成 8×16 点阵（先在原生 8px 渲染，再纵向复制行）

    原生 8px 渲染是像素精确的（字形就是按这个网格画的）；纵向 2× 直接复制行，
    不引入任何插值 —— 这是点阵字唯一不糊的放大方式。

    返回 [(code, rows)]，rows 为 16 行、每行 8 个 0/1（左→右）。
    """
    font = ImageFont.truetype(ttf, CELL_H)
    out = []

    for code in range(FIRST, LAST + 1):
        img = Image.new("L", (CELL_W, CELL_H), 0)
        ImageDraw.Draw(img).text((0, 0), chr(code), font=font, fill=255)
        rows8 = [[1 if img.getpixel((x, y)) > 127 else 0 for x in range(CELL_W)]
                 for y in range(CELL_H)]
        rows = []
        for r in rows8:
            for _ in range(SCALE_Y):     # 纵向整数放大 = 复制行
                rows.append(list(r))
        out.append((code, rows))

    return out


def glyph_metrics(rows):
    """由 16 行点阵求紧包围盒 -> (box_w, box_h, ofs_x, ofs_y)

    ofs_y 的语义与 LVGL 一致：基线到字形框底部的距离，向上为正
    （见 lv_draw_label.c: box_top = 基线 - box_h - ofs_y）。
    本字库基线在 16 行格子的底部，故 ofs_y = (CELL_H*SCALE_Y - 1) - 最下行。
    """
    xs = [x for r in rows for x in range(CELL_W) if r[x]]
    ys = [y for y, r in enumerate(rows) if any(r)]
    if not xs:
        return 0, 0, 0, 0
    left, right = min(xs), max(xs)
    top, bottom = min(ys), max(ys)
    return (right - left + 1), (bottom - top + 1), left, (CELL_H * SCALE_Y - 1 - bottom)


def emit_bitmap(glyphs):
    lines = []
    for code, rows in glyphs:
        name = chr(code) if 0x21 <= code <= 0x7E else "(0x%02X)" % code
        lines.append("    /* '%s' */" % name)
        for r in rows:
            byte = 0
            for x, v in enumerate(r):
                if v:
                    byte |= 0x80 >> x
            lines.append("    0x%02X," % byte)
    return "\n".join(lines)


def emit_glyph_dsc(glyphs):
    lines = ["    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0},"
             "   /* id 0 保留（字形未找到时返回 0） */"]
    index = 0
    for code, rows in glyphs:
        w, h, ox, oy = glyph_metrics(rows)
        name = chr(code) if 0x21 <= code <= 0x7E else "(0x%02X)" % code
        lines.append("    {.bitmap_index = %d, .adv_w = %d, .box_w = %d, .box_h = %d, "
                     ".ofs_x = %d, .ofs_y = %d},   /* '%s' */"
                     % (index, ADV_W, w, h, ox, oy, name))
        index += CELL_H * SCALE_Y            # 每字形固定 16 字节（stride = 1 字节/行）
    return "\n".join(lines)


def emit_header():
    return """/**
 * 本文件由 tools/ui-fonts/gen_unscii_8x16.py 生成，请勿手工编辑。
 * unscii 加高版：8 宽 × 16 高（纵向整数 2×），字宽与 unscii_8 相同
 */

#ifndef __FONT_UNSCII_8X16_H__
#define __FONT_UNSCII_8X16_H__

#include "lvgl.h"

/* unscii 的"加高版"：字宽仍 8px（adv_w 与 unscii_8 相同），字高 16px。
   点阵字只能整数倍放大，12px 做不出干净字形（见生成脚本注释）。 */
extern const lv_font_t lv_font_unscii_8x16;

#endif /* __FONT_UNSCII_8X16_H__ */
"""


def emit_c(glyphs):
    return """/**
 * 本文件由 tools/ui-fonts/gen_unscii_8x16.py 生成，请勿手工编辑。
 *
 * 源字体：unscii-8.ttf（公共领域）  设计网格：8×8，纵向 ×2 → 8×16
 * 字形：0x20 ~ 0x7F（与 lv_font_unscii_8 相同）
 * 位图：1bpp，每行 1 字节（MSB 在左，1 = 墨），每字形固定 16 字节
 */

#include "font_unscii_8x16.h"

static const uint8_t glyph_bitmap[] = {
%s
};

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
%s
};

static const lv_font_fmt_txt_cmap_t cmaps[] = {
    {.range_start = 0x%02X, .range_length = %d, .glyph_id_start = 1,
     .unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0,
     .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY},
};

static const lv_font_fmt_txt_dsc_t font_dsc = {
    .glyph_bitmap = glyph_bitmap,
    .glyph_dsc = glyph_dsc,
    .cmaps = cmaps,
    .kern_dsc = NULL,               /* 等宽字，不需要字偶距 */
    .kern_scale = 0,
    .cmap_num = 1,
    .bpp = 1,
    .kern_classes = 0,
    .bitmap_format = LV_FONT_FMT_TXT_PLAIN,
    .stride = 1,                    /* 每行补齐到 1 字节 */
    .are_glyphs_dynamic_loaded = false,
};

const lv_font_t lv_font_unscii_8x16 = {
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,
    .line_height = %d,
    .base_line = %d,
    .subpx = LV_FONT_SUBPX_NONE,
    .kerning = LV_FONT_KERNING_NONE,
    .static_bitmap = 1,
    .dsc = &font_dsc,
    .fallback = NULL,
    .user_data = NULL,
};
""" % (emit_bitmap(glyphs), emit_glyph_dsc(glyphs), FIRST, LAST - FIRST + 1,
       LINE_H, BASE_LINE)


def preview(glyphs, text="SELECT APPLICATION"):
    table = dict(glyphs)
    rows = [[] for _ in range(CELL_H * SCALE_Y)]
    for ch in text:
        rows16 = table.get(ord(ch))
        if rows16 is None:
            continue
        for y, r in enumerate(rows16):
            rows[y].extend(r)
    print("预览 '%s'（8 宽 × 16 高）:" % text)
    for r in rows:
        print("   " + "".join("#" if v else "." for v in r))


def main():
    ap = argparse.ArgumentParser(description="生成 unscii 加高版字库（8 宽 × 16 高）")
    ap.add_argument("--preview", action="store_true", help="只打印样例字形，不写文件")
    args = ap.parse_args()

    ttf = find_ttf()
    if ttf is None:
        return 1

    glyphs = rasterize(ttf)
    empty = [chr(c) for c, r in glyphs if c != 0x20 and not any(any(x) for x in r)]
    if empty:
        print("这些字形渲染出来是空的，说明渲染尺寸/对齐不对：%s" % "".join(empty))
        return 1

    print("源字体: %s" % ttf)
    print("字形数: %d（0x%02X~0x%02X）  line_height=%d  base_line=%d  adv_w=%d(8px)"
          % (len(glyphs), FIRST, LAST, LINE_H, BASE_LINE, ADV_W // 16))
    print("位图: %d 字节" % (len(glyphs) * CELL_H * SCALE_Y))

    preview(glyphs)

    if args.preview:
        return 0

    with open(os.path.join(OUT_INC, "font_unscii_8x16.h"), "w", encoding="utf-8") as fp:
        fp.write(emit_header())
    with open(os.path.join(OUT_SRC, "font_unscii_8x16.c"), "w", encoding="utf-8") as fp:
        fp.write(emit_c(glyphs))

    print("已写入:\n  components/film_ui/inc/font_unscii_8x16.h\n"
          "  components/film_ui/src/font_unscii_8x16.c")
    return 0


if __name__ == "__main__":
    sys.exit(main())
