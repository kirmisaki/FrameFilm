#!/usr/bin/env python3
"""FrameFilm UI 资源生成器

1-bit 图标 + 开机徽章 -> 产出两种形态：

  1) SD 卡可替换资源（自有 FFUI 容器，1bpp）
       assets/app/<app>/icon.bin      80x80   ← 取自 tools/ui-assets/png/ui_icon_<app>.png
       assets/app/pass/icon.bin       80x80   ← 取自 tools/ui-assets/png/pass.png（等比居中）
       assets/app/_ui/badge.bin      304x272  ← 取自 tools/ui-assets/png/icon1.png
  2) 固件内置默认图（C 数组，1bpp 原始位图）
       components/film_ui/inc/ui_defaults.h
       components/film_ui/src/ui_defaults.c

固件加载顺序：SD 上有合法资源就用 SD 的，否则回退内置默认图。

**全部用外部源图**：图标与徽章都放在 `tools/ui-assets/png/`，脚本按目标尺寸量化成
1bit，两种形态一起更新 —— 换图标只要替换那张 PNG 再重跑本脚本，不必改代码。

**为什么不用 LVGL 官方 .bin / LVGLImage.py**
  官方脚本的 I1 路径依赖外部工具 pngquant，且 I1 图像需要调色板；本工程零外部依赖优先。
  因此用一个 16 字节头的自有容器，固件侧把 1bpp 展开成 L8 后交给 LVGL（L8 无调色板语义，
  黑白由亮度直接表达，规避了 I1 调色板方向的不确定性）。

FFUI 容器（小端）：
  0x00  magic  'F''F''U''I'
  0x04  version = 1
  0x05  format  = 0x01（1bpp：1=黑 0=白，每字节 8 像素、MSB 在左）
  0x06  width   uint16
  0x08  height  uint16
  0x0A  reserve 6 字节
  0x10  载荷 w*h/8 字节

用法（仓库根目录）：
    python tools/ui-assets/gen_ui_assets.py
"""

import struct
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
OUT_PNG = ROOT / "tools/ui-assets/png"
OUT_SD = ROOT / "assets/app"
OUT_C_INC = ROOT / "firmware/frame_film_ark/components/film_ui/inc"
OUT_C_SRC = ROOT / "firmware/frame_film_ark/components/film_ui/src"

MAGIC = b"FFUI"
HDR_LEN = 16
FMT_1BPP = 0x01

ICON = 80

# 徽章（开机页主视觉）：横向定 BADGE_W，纵向按源图宽高比拉伸。
# 宽度必须是 8 的倍数 —— 1bpp 位图按整字节分行，容器里没有 stride 字段，
# 固件按 w/8 推算行跨度，所以 300 这类非 8 倍宽取最近的 304。
BADGE_W = 304
BADGE_H = 272

# 二值化门限（0~255）：>= 该值算白，否则算黑。
# 这里**不用抖动**：源图是细密线稿，缩小时抗锯齿造出的中间灰若交给
# Floyd-Steinberg，细线会被打散成断续的点阵（实测徽章顶部三角的曲线纹理与
# "INDUSTRIES" 小字都会碎掉）；硬阈值则保住连续的线宽，这才是线稿要的"细腻"。
# 偏小 → 笔画细、易断；偏大 → 大块区域糊成实心、丢内部结构。128 是中间值。
# 若换成人像/风景这类有连续影调的源图，把 load_1bit 里的量化改成
# `grey.convert("1", dither=Image.Dither.FLOYDSTEINBERG)` 再调这个门限。
SRC_THRESHOLD = 128



# ---------------------------------------------------------------- 源图量化
def load_1bit(src_name, w, h, fit):
    """源图 -> 1bit 位图（本工程约定：1=黑 0=白）

    源图放 tools/ui-assets/png/，两种画法都支持：
      - 图标：RGB 全黑 + 背景全透明，形状由 **alpha** 表达
      - 徽章：黑白线稿，形状由 **亮度** 表达
    统一做法是先按 alpha 合成到白底（本工程约定"未覆盖"为白），再硬阈值切在
    SRC_THRESHOLD 上 —— 对硬边矢量/线稿这就是标准覆盖率二值化。

    fit 决定源图尺寸与目标不符时的处理：
      - "stretch"：要求宽高比一致（差 >1% 直接报错），按 BOX（面积平均）缩放到目标。
        适合"本来就是按这个框设计的"图（徽章、方形图标）—— 比例不一致时宁可不做，
        也不要悄悄拉变形（目标高度同时写在固件常量里）。
      - "contain"：等比缩放到能塞进框内、居中留白。适合来源比例与槽位不同的图
        （如 107x80 的通行证图放 80x80 槽位），只留白不拉变形。

    BOX（面积平均）而非 LANCZOS：源图是硬边线稿，BOX 得到的正是"该像素被墨覆盖的
    比例"，切 0.5 即覆盖率二值化，细线连续；LANCZOS 的振铃会在细线两侧造出过冲，
    二值化后线条反而断续。
    """
    src = OUT_PNG / src_name
    if not src.exists():
        raise SystemExit("缺少源图：%s" % src)

    im = Image.open(src)
    if im.mode != "RGBA":
        im = im.convert("RGBA")

    flat = Image.new("RGB", im.size, (255, 255, 255))
    flat.paste(im, mask=im.getchannel("A"))

    grey = flat.convert("L")

    if fit == "contain":
        scale = min(w / grey.width, h / grey.height)
        nw = max(1, int(round(grey.width * scale)))
        nh = max(1, int(round(grey.height * scale)))
        canvas = Image.new("L", (w, h), 255)
        canvas.paste(grey.resize((nw, nh), Image.BOX), ((w - nw) // 2, (h - nh) // 2))
        grey = canvas
    else:
        want = w / h
        have = im.width / im.height
        if abs(want - have) > 0.01:
            raise SystemExit(
                "源图 %s 宽高比 %.4f 与目标 %dx%d（%.4f）不符；"
                "调整目标尺寸（同时改固件侧常量）、换源图，或把 fit 改成 contain"
                % (src.name, have, w, h, want))
        if grey.size != (w, h):
            grey = grey.resize((w, h), Image.BOX)

    lut = [255 if v >= SRC_THRESHOLD else 0 for v in range(256)]
    return grey.point(lut, "1")


# ---------------------------------------------------------------- 资源清单
ASSETS = [
    # (C 符号名, SD 文件名, 源图, (宽, 高), SD 子目录, 缩放方式)
    ("ui_def_icon_image",     "icon",  "ui_def_icon_image.png",     (ICON, ICON),        "image",     "stretch"),
    ("ui_def_icon_pass",      "icon",  "ui_def_icon_pass.png",      (ICON, ICON),        "pass",      "contain"),
    ("ui_def_icon_wallet",    "icon",  "ui_def_icon_wallet.png",    (ICON, ICON),        "wallet",    "contain"),
    ("ui_def_icon_template",  "icon",  "ui_def_icon_template.png",  (ICON, ICON),        "template",  "stretch"),
    ("ui_def_icon_clock",     "icon",  "ui_def_icon_clock.png",     (ICON, ICON),        "clock",     "stretch"),
    ("ui_def_icon_animation", "icon",  "ui_def_icon_animation.png", (ICON, ICON),        "animation", "stretch"),
    ("ui_def_icon_settings",  "icon",  "ui_def_icon_settings.png",  (ICON, ICON),        "settings",  "stretch"),
    ("ui_def_badge",          "badge", "ui_def_badge.png",          (BADGE_W, BADGE_H),  "_ui",       "stretch"),
]



# ---------------------------------------------------------------- 编码
def pack_1bpp(img):
    """PIL mode '1' -> 1bpp 位图（1=黑，MSB 在左）

    PIL 的 '1' 里 1=白，而本工程约定位值 1=黑，故整体取反。
    """
    w, h = img.size
    assert w % 8 == 0, "宽度必须是 8 的倍数（行须整字节），当前 %d" % w
    raw = img.tobytes()          # 每行 (w+7)//8 字节，MSB 在左
    stride = w // 8
    assert len(raw) == stride * h, "packed size %d != %d" % (len(raw), stride * h)
    return bytes(b ^ 0xFF for b in raw)


def write_container(path, w, h, payload):
    path.parent.mkdir(parents=True, exist_ok=True)
    hdr = MAGIC + bytes([1, FMT_1BPP]) + struct.pack("<HH", w, h) + bytes(6)
    assert len(hdr) == HDR_LEN
    path.write_bytes(hdr + payload)


def read_container(path):
    """回读校验（顺带给固件侧逻辑做一次同构验证）"""
    raw = path.read_bytes()
    assert raw[:4] == MAGIC, "magic 不符"
    ver, fmt = raw[4], raw[5]
    w, h = struct.unpack("<HH", raw[6:10])
    assert ver == 1 and fmt == FMT_1BPP
    body = raw[HDR_LEN:]
    assert len(body) == w * h // 8, "载荷 %d != %d" % (len(body), w * h // 8)
    return w, h, body


def preview(payload, w, h, cols=40, rows=20):
    """把位图降采样成 ASCII，便于在终端核对图形（# = 黑）"""
    out = []
    for r in range(rows):
        line = []
        for c in range(cols):
            x = int((c + 0.5) * w / cols)
            y = int((r + 0.5) * h / rows)
            idx = y * w + x
            bit = (payload[idx >> 3] >> (7 - (idx & 7))) & 1
            line.append("#" if bit else ".")
        out.append("".join(line))
    return "\n".join(out)


def main():
    for p in (OUT_PNG, OUT_C_INC, OUT_C_SRC):
        p.mkdir(parents=True, exist_ok=True)

    c_arrays = []
    decls = []

    for sym, fname, src_name, (w, h), sd_dir, fit in ASSETS:
        img = load_1bit(src_name, w, h, fit)
        assert img.size == (w, h), "%s 尺寸 %s" % (sym, img.size)

        png = OUT_PNG / ("%s.png" % sym)
        img.save(png)

        payload = pack_1bpp(img)
        nbytes = len(payload)

        sd_path = OUT_SD / sd_dir / ("%s.bin" % fname)
        write_container(sd_path, w, h, payload)
        rw, rh, back = read_container(sd_path)
        assert (rw, rh) == (w, h) and back == payload, "回读不一致"

        # 内置默认图：同一份 1bpp 原始位图
        c_arrays.append((sym, nbytes, payload))
        decls.append((sym, w, h, nbytes))

        print("%-24s %dx%d  载荷%5d B  SD %s" % (
            sym, w, h, nbytes,
            str(sd_path.relative_to(ROOT)).replace("\\", "/")))

        if (w, h) == (ICON, ICON):
            print(preview(payload, w, h))
        elif (w, h) == (BADGE_W, BADGE_H):
            # 徽章是开机页主视觉，终端里也过一眼（改源图后尤其要看）
            print(preview(payload, w, h, cols=64, rows=48))

    # ---- ui_defaults.h ----
    hdr = [
        "#ifndef __UI_DEFAULTS_H__",
        "#define __UI_DEFAULTS_H__",
        "",
        "/* 由 tools/ui-assets/gen_ui_assets.py 生成，请勿手改。",
        " * 内置默认图：1bpp 原始位图（1=黑 0=白，MSB 在左），",
        " * SD 卡上没有可用资源时由 ui_assets 回退使用。 */",
        "",
        "#include <stdint.h>",
        "",
    ]
    for sym, w, h, nbytes in decls:
        hdr += ["/* %dx%d, %d B */" % (w, h, nbytes),
                "extern const uint8_t %s[%d];" % (sym, nbytes), ""]
    hdr += ["#endif /* __UI_DEFAULTS_H__ */", ""]
    (OUT_C_INC / "ui_defaults.h").write_text("\n".join(hdr), encoding="utf-8")

    # ---- ui_defaults.c ----
    src = ['/* 由 tools/ui-assets/gen_ui_assets.py 生成，请勿手改。 */',
           '#include "ui_defaults.h"', ""]
    for sym, nbytes, payload in c_arrays:
        src.append("/* %d B */" % nbytes)
        src.append("const uint8_t %s[%d] = {" % (sym, nbytes))
        for i in range(0, nbytes, 16):
            chunk = ", ".join("0x%02X" % b for b in payload[i:i + 16])
            src.append("    " + chunk + ",")
        src.append("};")
        src.append("")
    (OUT_C_SRC / "ui_defaults.c").write_text("\n".join(src), encoding="utf-8")

    print("\n完成：")
    print("  SD 可替换资源 -> assets/app/**")
    print("  内置默认图   -> components/film_ui/{inc/ui_defaults.h,src/ui_defaults.c}")


if __name__ == "__main__":
    main()
