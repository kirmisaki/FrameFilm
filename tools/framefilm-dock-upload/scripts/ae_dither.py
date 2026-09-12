#!/usr/bin/env python3
"""Atkinson 增强（Atkinson Enhanced）六色抖动 —— FrameFilm Dock 上传工具内置实现

与 ForFilm convert.js / 小程序 dither-advanced.js 的 atkinsonEnhancedQuantize 逐像素一致：

1) 选色：常规像素走 CIELAB 加权最近色（2.0·ΔL² + 0.8·Δa² + Δb²）；
2) 蓝/青区域（b* < -10 或 a* < -35）走「修正 LUT + 选色 LUT」两级查表，补偿墨水屏蓝色显色偏差；
3) 残差：按墨水屏校准显色值（AE_RESIDUAL_PALETTE）计算，而非显示色；
4) 扩散：Atkinson 六邻域各 1/8，三行滚动缓冲累加原始残差，消费时一次性 (sum + 4) >> 3。

LUT（786432 B 修正表 + 262144 B 选色表）复用仓库里已生成的
tools/ForFilm/js/atkinson_enhanced_lut.js（与小程序同一份，自动生成，请勿手改）；
取不到时退化为纯 CIELAB 选色，与 JS 端 LUT 解码失败时的行为一致。
"""

import base64
import os
import re

# 六色显示调色板（AE 索引顺序: 0黑 1白 2黄 3红 4绿 5蓝）
AE_DISPLAY_PALETTE = [
    (0, 0, 0),
    (255, 255, 255),
    (255, 255, 0),
    (255, 0, 0),
    (0, 255, 0),
    (0, 0, 255),
]

# 六色残差校准色（墨水屏实际显色值，误差扩散用，与显示色不同）
AE_RESIDUAL_PALETTE = [
    (0, 0, 0),
    (255, 255, 255),
    (255, 235, 0),
    (154, 0, 0),
    (20, 85, 16),
    (0, 36, 154),
]

# AE 索引 -> film 调色板索引（film 顺序: 0黑 1白 2黄 3红 4蓝 5绿）
AE_TO_FILM_INDEX = [0, 1, 2, 3, 5, 4]

# LUT 尺寸与来源（与 atkinson_enhanced_lut.js 头注释一致）
CORRECTION_LUT_SIZE = 786432
SELECTION_LUT_SIZE = 262144
LUT_SOURCES = [
    os.path.join("tools", "ForFilm", "js", "atkinson_enhanced_lut.js"),
    os.path.join("tools", "wechart", "miniprogram", "pkgCreate", "utils",
                 "atkinson_enhanced_lut.js"),
]

# sRGB 通道线性化查表（D65 标准，与 JS aeChannelToLinear 一致）
_SRGB_TO_LINEAR = tuple(
    (c / 255.0) / 12.92 if c <= 10 else (((c / 255.0) + 0.055) / 1.055) ** 2.4
    for c in range(256)
)


def _lab_pivot(t):
    return t ** (1.0 / 3.0) if t > 0.008856 else 7.787 * t + 16.0 / 116.0


def rgb_to_lab(r, g, b):
    """sRGB -> D65 CIELAB，与 JS aeRgbToLab 一致"""
    rl = _SRGB_TO_LINEAR[r] * 100.0
    gl = _SRGB_TO_LINEAR[g] * 100.0
    bl = _SRGB_TO_LINEAR[b] * 100.0
    x = (rl * 0.4124564 + gl * 0.3575761 + bl * 0.1804375) / 95.047
    y = (rl * 0.2126729 + gl * 0.7151522 + bl * 0.0721750) / 100.0
    z = (rl * 0.0193339 + gl * 0.1191920 + bl * 0.9503041) / 108.883
    fx = _lab_pivot(x)
    fy = _lab_pivot(y)
    fz = _lab_pivot(z)
    return (116.0 * fy - 16.0, 500.0 * (fx - fy), 200.0 * (fy - fz))


_AE_PALETTE_LABS = tuple(rgb_to_lab(*c) for c in AE_DISPLAY_PALETTE)


def load_lut(repo_root):
    """解码 AE 的修正表/选色表；找不到或长度不符时返回 (None, None)"""
    if not repo_root:
        return None, None
    for rel in LUT_SOURCES:
        path = os.path.join(repo_root, rel)
        if not os.path.isfile(path):
            continue
        try:
            with open(path, encoding="utf-8") as f:
                text = f.read()
            corr_b64 = re.search(r'AE_CORRECTION_LUT_B64\s*=\s*"([^"]+)"', text).group(1)
            sel_b64 = re.search(r'AE_SELECTION_LUT_B64\s*=\s*"([^"]+)"', text).group(1)
            correction = base64.b64decode(corr_b64)
            selection = base64.b64decode(sel_b64)
        except Exception:  # noqa: BLE001 - 数据缺失时按 JS 端同样退化为纯 CIELAB
            continue
        if len(correction) == CORRECTION_LUT_SIZE and len(selection) == SELECTION_LUT_SIZE:
            return correction, selection
    return None, None


def _select_index(r, g, b, lab, correction, selection):
    """六色选色：蓝/青区域查 LUT，其余走 CIELAB 加权最近色（JS aeSelectInkColor）"""
    if correction is not None and (lab[2] < -10.0 or lab[1] < -35.0):
        cell = (((r >> 2) << 12) | ((g >> 2) << 6) | (b >> 2)) * 3
        r2 = r - ((r - correction[cell]) >> 2)
        g2 = g - ((g - correction[cell + 1]) >> 2)
        b2 = b - ((b - correction[cell + 2]) >> 2)
        index = selection[((r2 >> 2) << 12) | ((g2 >> 2) << 6) | (b2 >> 2)]
        return 0 if index >= 6 else index

    l, a, bb = lab
    best_index = 0
    best_distance = 0x7FFFFFFF
    for i, p in enumerate(_AE_PALETTE_LABS):
        dl = l - p[0]
        da = a - p[1]
        db = bb - p[2]
        distance = int(2.0 * dl * dl + 0.8 * da * da + db * db)
        if distance < best_distance:
            best_distance = distance
            best_index = i
    return best_index


def quantize(data, width, height, correction=None, selection=None):
    """Atkinson 增强量化

    data: RGB 字节流（行优先，每像素 3 字节）
    返回: film 调色板索引列表（0黑 1白 2黄 3红 4蓝 5绿），可直接交给 build_film
    """
    stride = (width + 3) * 3
    current = [0] * stride
    nxt = [0] * stride
    second = [0] * stride
    out = [0] * (width * height)

    residuals = AE_RESIDUAL_PALETTE
    to_film = AE_TO_FILM_INDEX
    to_lab = rgb_to_lab
    pick = _select_index

    pos = 0
    for y in range(height):
        row = y * width
        for x in range(width):
            e = (x + 1) * 3
            r = data[pos]
            g = data[pos + 1]
            b = data[pos + 2]
            pos += 3

            # 缓冲里累加的是未除以 8 的原始残差，消费时一次性 (sum + 4) >> 3
            r = _clamp_u8(r + ((current[e] + 4) >> 3))
            g = _clamp_u8(g + ((current[e + 1] + 4) >> 3))
            b = _clamp_u8(b + ((current[e + 2] + 4) >> 3))

            index = pick(r, g, b, to_lab(r, g, b), correction, selection)
            out[row + x] = to_film[index]

            # 残差按墨水屏校准色计算
            rr, gg, bb = residuals[index]
            er = r - rr
            eg = g - gg
            eb = b - bb

            # 六邻域: (x+1,y) (x+2,y) (x-1,y+1) (x,y+1) (x+1,y+1) (x,y+2)
            n = (x + 2) * 3
            current[n] += er
            current[n + 1] += eg
            current[n + 2] += eb
            n = (x + 3) * 3
            current[n] += er
            current[n + 1] += eg
            current[n + 2] += eb
            n = x * 3
            nxt[n] += er
            nxt[n + 1] += eg
            nxt[n + 2] += eb
            n = (x + 1) * 3
            nxt[n] += er
            nxt[n + 1] += eg
            nxt[n + 2] += eb
            n = (x + 2) * 3
            nxt[n] += er
            nxt[n + 1] += eg
            nxt[n + 2] += eb
            n = (x + 1) * 3
            second[n] += er
            second[n + 1] += eg
            second[n + 2] += eb

        # 滚动三行缓冲
        current, nxt, second = nxt, second, current
        for i in range(stride):
            second[i] = 0

    return out


def _clamp_u8(value):
    return 0 if value < 0 else (255 if value > 255 else value)
