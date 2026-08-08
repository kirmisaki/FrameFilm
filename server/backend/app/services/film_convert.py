"""RGB 图片 -> film 文件转换引擎

算法对齐小程序 film-utils.js / Web convert.js：
- 6 色调色板 + HSL/Lab 最近色匹配
- 抖动：none / floyd_steinberg / atkinson / stucki / jarvis /
        gamma_floyd_steinberg（线性空间误差扩散）/ bayer（4×4 有序）/ adaptive（边缘感知误差扩散）
- 文件头：32B（FileSize + ScreenWidth/Height + ColorCount + ColorTable）
- 像素：每字节 2 像素（高 4 位在前）
"""
import colorsys
import struct
from functools import lru_cache

from PIL import Image, ImageEnhance, ImageOps

FILM_HEADER_SIZE = 32

# 6 色调色板（与 film-utils.js rgbPalette 完全一致）
PALETTE = [
    (0, 0, 0),        # 黑
    (255, 255, 255),  # 白
    (255, 255, 0),    # 黄
    (255, 0, 0),      # 红
    (0, 0, 255),      # 蓝
    (41, 204, 20),    # 绿
]
PIXEL_CODES = [0x00, 0x01, 0x02, 0x03, 0x04, 0x05]

# 文件 ColorTable（与固件 hal_epd_360.c color_lut 对齐）
COLOR_TABLE = [0x00, 0xFF, 0xFC, 0xE0, 0x03, 0x1C] + [0] * 10

# 黑白模式
BW_PALETTE = [(0, 0, 0), (255, 255, 255)]
BW_PIXEL_CODES = [0x00, 0x01]
BW_COLOR_TABLE = [0x00, 0xFF] + [0] * 14


def _clamp255(v: float) -> int:
    return max(0, min(255, int(round(v))))


def _rgb_to_hsl(r: int, g: int, b: int):
    h, l, s = colorsys.rgb_to_hls(r / 255.0, g / 255.0, b / 255.0)
    return {"h": h * 360.0, "s": s, "l": l}


def _rgb_to_lab(r: int, g: int, b: int):
    # sRGB -> XYZ -> Lab
    def _f(c: float):
        c = c / 255.0
        c = c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4
        return c

    r_, g_, b_ = _f(r), _f(g), _f(b)
    x = (r_ * 0.4124 + g_ * 0.3576 + b_ * 0.1805) / 0.95047
    y = (r_ * 0.2126 + g_ * 0.7152 + b_ * 0.0722) / 1.0
    z = (r_ * 0.0193 + g_ * 0.1192 + b_ * 0.9505) / 1.08883

    def _g(t: float):
        return t ** (1 / 3) if t > 0.008856 else (7.787 * t) + (16 / 116)

    x, y, z = _g(x), _g(y), _g(z)
    return {"l": (116 * y) - 16, "a": 500 * (x - y), "b": 200 * (y - z)}


_PALETTE_HSL = [_rgb_to_hsl(*c) for c in PALETTE]
_PALETTE_LAB = [_rgb_to_lab(*c) for c in PALETTE]


def _lab_distance(l1, l2):
    dl = l1["l"] - l2["l"]
    da = l1["a"] - l2["a"]
    db = l1["b"] - l2["b"]
    return (dl * dl + da * da + db * db) ** 0.5


@lru_cache(maxsize=65536)
def find_closest_color(r: int, g: int, b: int) -> tuple:
    """与 JS findClosestColor 等价：HSL 匹配 + Lab 中性色校正"""
    if len(PALETTE) != len(_PALETTE_HSL):
        _recompute_palette()
    input_hsl = _rgb_to_hsl(r, g, b)
    if input_hsl["s"] < 0.12:
        return PALETTE[1] if input_hsl["l"] > 0.5 else PALETTE[0]

    min_dist = float("inf")
    closest = PALETTE[0]
    for i in range(2, len(_PALETTE_HSL)):  # 黄/红/蓝/绿
        p = _PALETTE_HSL[i]
        hue_diff = abs(input_hsl["h"] - p["h"])
        if hue_diff > 180:
            hue_diff = 360 - hue_diff
        sat_diff = abs(input_hsl["s"] - p["s"])
        lum_diff = abs(input_hsl["l"] - p["l"])
        dist = hue_diff + sat_diff * 120 + lum_diff * 80
        if dist < min_dist:
            min_dist = dist
            closest = PALETTE[i]

    lab_input = _rgb_to_lab(r, g, b)
    dist_black = _lab_distance(lab_input, _PALETTE_LAB[0])
    dist_white = _lab_distance(lab_input, _PALETTE_LAB[1])
    dist_neutral = min(dist_black, dist_white)
    neutral = PALETTE[0] if dist_black < dist_white else PALETTE[1]
    lab_chosen = _rgb_to_lab(*closest)
    dist_chosen = _lab_distance(lab_input, lab_chosen)
    if dist_neutral < dist_chosen * 0.45:
        return neutral
    return closest


def _recompute_palette():
    global _PALETTE_HSL, _PALETTE_LAB
    _PALETTE_HSL = [_rgb_to_hsl(*c) for c in PALETTE]
    _PALETTE_LAB = [_rgb_to_lab(*c) for c in PALETTE]


def fit_cover(img: Image.Image, width: int, height: int) -> Image.Image:
    return ImageOps.fit(img.convert("RGB"), (width, height), method=Image.LANCZOS)


def adjust_image(img: Image.Image, contrast: int = 100, brightness: int = 0) -> Image.Image:
    """与 JS adjustContrast 等价：(v-128)*factor+128，再叠加亮度"""
    factor = contrast / 100.0
    offset = brightness

    def _map(v):
        return _clamp255((v - 128) * factor + 128 + offset)

    return img.point(_map)


def apply_saturation(img: Image.Image, saturation: int = 100) -> Image.Image:
    """饱和度调整（PIL HSV 增强），100 = 不变"""
    if saturation == 100:
        return img
    return ImageEnhance.Color(img).enhance(saturation / 100.0)


def _diffuse(px, w, h, strength, offsets, divisor, write_self=True):
    """通用误差扩散"""
    for y in range(h):
        for x in range(w):
            i = y * w + x
            r, g, b = px[i]
            closest = find_closest_color(r, g, b)
            if write_self:
                px[i] = closest
            err = ((r - closest[0]) * strength, (g - closest[1]) * strength, (b - closest[2]) * strength)
            for dx, dy, wgt in offsets:
                nx, ny = x + dx, y + dy
                if 0 <= nx < w and 0 <= ny < h:
                    j = ny * w + nx
                    pr, pg, pb = px[j]
                    px[j] = (
                        _clamp255(pr + err[0] * wgt / divisor),
                        _clamp255(pg + err[1] * wgt / divisor),
                        _clamp255(pb + err[2] * wgt / divisor),
                    )
    if not write_self:
        for i in range(w * h):
            px[i] = find_closest_color(*px[i])


# sRGB <-> 线性空间（gamma 感知抖动用，对齐 convert.js LUT 公式）
def _srgb_to_linear(v: int) -> float:
    v = v / 255.0
    return v / 12.92 if v <= 0.04045 else ((v + 0.055) / 1.055) ** 2.4


def _linear_to_srgb(c: float) -> int:
    c = max(0.0, min(1.0, c))
    srgb = c * 12.92 if c <= 0.0031308 else 1.055 * c ** (1 / 2.4) - 0.055
    return max(0, min(255, round(srgb * 255)))


def _gamma_diffuse(px, w, h, strength):
    """Gamma 感知 Floyd-Steinberg：量化用感知化 find_closest_color，
    误差在线性空间计算与累积（对齐 convert.js gammaFloydSteinbergDither）"""
    lin = [(_srgb_to_linear(r), _srgb_to_linear(g), _srgb_to_linear(b)) for (r, g, b) in px]
    for y in range(h):
        for x in range(w):
            i = y * w + x
            r, g, b = lin[i]
            closest = find_closest_color(_linear_to_srgb(r), _linear_to_srgb(g), _linear_to_srgb(b))
            px[i] = closest
            err = (
                (r - _srgb_to_linear(closest[0])) * strength,
                (g - _srgb_to_linear(closest[1])) * strength,
                (b - _srgb_to_linear(closest[2])) * strength,
            )
            for dx, dy, wgt in ((1, 0, 7), (-1, 1, 3), (0, 1, 5), (1, 1, 1)):
                nx, ny = x + dx, y + dy
                if 0 <= nx < w and 0 <= ny < h:
                    j = ny * w + nx
                    lin[j] = (
                        lin[j][0] + err[0] * wgt / 16,
                        lin[j][1] + err[1] * wgt / 16,
                        lin[j][2] + err[2] * wgt / 16,
                    )


# 4×4 Bayer 有序抖动矩阵（对齐 convert.js BAYER_MATRIX）
BAYER_MATRIX = (
    (0, 8, 2, 10),
    (12, 4, 14, 6),
    (3, 11, 1, 9),
    (15, 7, 13, 5),
)


def _bayer_diffuse(px, w, h, strength):
    """Bayer 4×4 有序抖动：按位置加中心化偏置后量化（对齐 convert.js bayerDither）"""
    for y in range(h):
        row = BAYER_MATRIX[y & 3]
        for x in range(w):
            bias = (row[x & 3] - 8) * strength
            i = y * w + x
            r, g, b = px[i]
            px[i] = find_closest_color(
                _clamp255(r + bias), _clamp255(g + bias), _clamp255(b + bias)
            )


def _sobel_edge_map(gray, w, h):
    edges = [0.0] * (w * h)
    for y in range(1, h - 1):
        for x in range(1, w - 1):
            i = y * w + x
            tl, tc, tr = gray[i - w - 1], gray[i - w], gray[i - w + 1]
            ml, mr = gray[i - 1], gray[i + 1]
            bl, bc, br = gray[i + w - 1], gray[i + w], gray[i + w + 1]
            gx = -tl - 2 * ml - bl + tr + 2 * mr + br
            gy = -tl - 2 * tc - tr + bl + 2 * bc + br
            edges[i] = (gx * gx + gy * gy) ** 0.5
    return edges


def _adaptive_diffuse(px, gray, w, h, strength):
    """边缘感知误差扩散：强边缘处减少扩散（保细节），平坦处全量扩散"""
    edges = _sobel_edge_map(gray, w, h)
    # 归一化边缘强度 0..1
    max_edge = max(edges) or 1.0
    frac = 1 / 16
    for y in range(h):
        for x in range(w):
            i = y * w + x
            r, g, b = px[i]
            closest = find_closest_color(r, g, b)
            px[i] = closest
            edge = edges[i] / max_edge
            eff = strength * (1.0 - 0.7 * edge)  # 边缘处降低误差扩散
            err = ((r - closest[0]) * eff, (g - closest[1]) * eff, (b - closest[2]) * eff)
            for dx, dy in ((1, 0), (-1, 1), (0, 1), (1, 1)):
                nx, ny = x + dx, y + dy
                if 0 <= nx < w and 0 <= ny < h:
                    j = ny * w + nx
                    pr, pg, pb = px[j]
                    px[j] = (
                        _clamp255(pr + err[0] * frac),
                        _clamp255(pg + err[1] * frac),
                        _clamp255(pb + err[2] * frac),
                    )


def _process_pixels(img: Image.Image, dither_type: str, strength: float, palette: list):
    """输入 RGB 图，输出每个像素的调色板索引（对应 PIXEL_CODES 下标）"""
    w, h = img.size
    px = list(img.getdata())  # list of (r,g,b)
    strength = strength / 100.0  # 0-100 -> 0-1

    if dither_type == "none":
        for i in range(w * h):
            px[i] = find_closest_color(*px[i])
    elif dither_type == "floyd_steinberg":
        _diffuse(px, w, h, strength, [
            (1, 0, 7), (-1, 1, 3), (0, 1, 5), (1, 1, 1),
        ], 16, write_self=False)
    elif dither_type == "atkinson":
        _diffuse(px, w, h, strength, [
            (1, 0, 1), (2, 0, 1), (-1, 1, 1), (0, 1, 1), (1, 1, 1), (0, 2, 1),
        ], 8, write_self=True)
    elif dither_type == "stucki":
        _diffuse(px, w, h, strength, [
            (1, 0, 8), (2, 0, 4),
            (-2, 1, 2), (-1, 1, 4), (0, 1, 8), (1, 1, 4), (2, 1, 2),
            (-2, 2, 1), (-1, 2, 2), (0, 2, 4), (1, 2, 2), (2, 2, 1),
        ], 42, write_self=False)
    elif dither_type == "jarvis":
        _diffuse(px, w, h, strength, [
            (1, 0, 7), (2, 0, 5),
            (-2, 1, 3), (-1, 1, 5), (0, 1, 7), (1, 1, 5), (2, 1, 3),
            (-2, 2, 1), (-1, 2, 3), (0, 2, 5), (1, 2, 3), (2, 2, 1),
        ], 48, write_self=True)
    elif dither_type == "gamma_floyd_steinberg":
        _gamma_diffuse(px, w, h, strength)
    elif dither_type == "bayer":
        _bayer_diffuse(px, w, h, strength)
    else:  # adaptive
        gray = [round(r * 0.299 + g * 0.587 + b * 0.114) for (r, g, b) in px]
        _adaptive_diffuse(px, gray, w, h, strength)

    # 映射为调色板索引
    idx_map = {c: i for i, c in enumerate(palette)}
    return [idx_map[c] for c in px]


def build_film(indices, width: int, height: int, color_table: list) -> bytes:
    """像素索引 -> film 文件二进制（32B 头 + 4bit 像素体）"""
    pixel_data_size = len(indices) // 2
    buf = bytearray(FILM_HEADER_SIZE)
    struct.pack_into("<I", buf, 0x00, pixel_data_size)
    struct.pack_into("<H", buf, 0x04, width)
    struct.pack_into("<H", buf, 0x06, height)
    buf[0x08] = 6 if len(color_table) >= 6 else 2
    buf[0x10:0x20] = bytes(color_table)

    body = bytearray()
    for i in range(0, len(indices) - 1, 2):
        body.append((indices[i] << 4) | indices[i + 1])
    # 奇数像素时末尾补齐
    if len(indices) % 2:
        body.append(indices[-1] << 4)

    return bytes(buf) + bytes(body)


def convert_image(img: Image.Image, width: int, height: int, render_config: dict | None = None,
                  preview_scale: float = 1.0):
    """完整转换管线（画布直写，行优先布局）

    返回 (film_bytes, preview_png_bytes)
    - render_config: {dither_type, dither_strength, contrast, brightness, saturation, palette}
    - preview_scale: 预览降采样倍率（<1 时在更小画布上抖动转换，输出更小 PNG，仅用于预览展示）
    - 注意：本函数按给定画布尺寸直写像素体，仅适用于 e-ink 预览等展示场景；
      设备实际下载的 film 请使用 convert_for_device（对齐小程序布局）。
    """
    rc = render_config or {}
    dither_type = rc.get("dither_type", "floyd_steinberg")
    dither_strength = int(rc.get("dither_strength", 80))
    contrast = int(rc.get("contrast", 100))
    brightness = int(rc.get("brightness", 0))
    saturation = int(rc.get("saturation", 100))
    palette_mode = rc.get("palette", "6color")

    scale = max(0.25, min(1.0, float(preview_scale)))
    tw = max(1, int(round(width * scale)))
    th = max(1, int(round(height * scale)))

    img = fit_cover(img, tw, th)
    img = adjust_image(img, contrast, brightness)
    img = apply_saturation(img, saturation)

    if palette_mode == "bw":
        indices = _process_pixels(img, dither_type, dither_strength, BW_PALETTE)
        film = build_film(indices, tw, th, BW_COLOR_TABLE)
    else:
        indices = _process_pixels(img, dither_type, dither_strength, PALETTE)
        film = build_film(indices, tw, th, COLOR_TABLE)

    # 预览：把调色板色值渲染回 RGB，模拟墨水屏效果
    palette_rgb = BW_PALETTE if palette_mode == "bw" else PALETTE
    preview = Image.new("RGB", (tw, th))
    preview.putdata([palette_rgb[i] for i in indices])

    import io
    buf = io.BytesIO()
    preview.save(buf, format="PNG")
    return film, buf.getvalue()


def _pack_indices(indices: list, width: int, height: int, layout: str) -> bytes:
    """把行优先排列的像素索引按设备布局打包为 4bit 像素体

    - row-major: pos = (y * width) + x
    - rotated:   pos = (x * height) + (height - 1 - y)   # 对齐小程序列优先翻转
    """
    body = bytearray((width * height + 1) // 2)
    if layout == "row-major":
        for i in range(0, width * height - 1, 2):
            body[i // 2] = (indices[i] << 4) | indices[i + 1]
    else:  # rotated
        for y in range(height):
            for x in range(width):
                src = y * width + x
                pos = x * height + (height - 1 - y)
                if pos % 2 == 0:
                    body[pos // 2] |= indices[src] << 4
                else:
                    body[pos // 2] |= indices[src]
    return bytes(body)


def convert_for_device(canvas_img: Image.Image, device_type: str, render_config: dict | None = None):
    """设备 film 生成：复现小程序 film-utils.js 完整管线

    流程（与小程序一致）：
      1. 竖屏画布 (canvas_w×canvas_h) —— 模板/创作视角
      2. transpose(ROTATE_90) 旋转为横屏数据（等价小程序 translate(0,sh);rotate(-π/2)）
      3. 对比度/亮度 + 抖动在横屏数据上执行
      4. 按设备 pixel_layout 打包像素体（basic=rotated / pro=row-major）
      5. 文件头写入横屏分辨率（basic=600×400 / pro=792×528）

    返回 (film_bytes, preview_png_bytes)；预览为竖屏画布调色板化效果。
    """
    from ..config import SCREENS

    scr = SCREENS[device_type]
    cw, ch = scr["canvas_w"], scr["canvas_h"]
    sw, sh = scr["width"], scr["height"]
    layout = scr["pixel_layout"]

    rc = render_config or {}
    dither_type = rc.get("dither_type", "floyd_steinberg")
    dither_strength = int(rc.get("dither_strength", 80))
    contrast = int(rc.get("contrast", 100))
    brightness = int(rc.get("brightness", 0))
    saturation = int(rc.get("saturation", 100))
    palette_mode = rc.get("palette", "6color")

    # 1-2. 竖屏画布 -> 横屏数据
    canvas = canvas_img.convert("RGB")
    if canvas.size != (cw, ch):
        canvas = fit_cover(canvas, cw, ch)
    landscape = canvas.transpose(Image.ROTATE_90)
    landscape = adjust_image(landscape, contrast, brightness)
    landscape = apply_saturation(landscape, saturation)

    palette = BW_PALETTE if palette_mode == "bw" else PALETTE
    color_table = BW_COLOR_TABLE if palette_mode == "bw" else COLOR_TABLE
    indices = _process_pixels(landscape, dither_type, dither_strength, palette)

    # 3-5. 打包 + 文件头（横屏分辨率）
    body = _pack_indices(indices, sw, sh, layout)
    pixel_data_size = len(body)
    buf = bytearray(FILM_HEADER_SIZE)
    struct.pack_into("<I", buf, 0x00, pixel_data_size)
    struct.pack_into("<H", buf, 0x04, sw)
    struct.pack_into("<H", buf, 0x06, sh)
    buf[0x08] = 6 if len(color_table) >= 6 else 2
    buf[0x10:0x20] = bytes(color_table)
    film = bytes(buf) + bytes(body)

    # 预览：横屏索引旋转回竖屏画布视角（逆映射 portrait(px,py)=landscape(py, sh-1-px)）
    rgb = BW_PALETTE if palette_mode == "bw" else PALETTE
    pixels = [rgb[0]] * (cw * ch)
    for y in range(sh):
        for x in range(sw):
            src = y * sw + x
            ppx = sh - 1 - y
            ppy = x
            pixels[ppy * cw + ppx] = rgb[indices[src]]
    preview = Image.new("RGB", (cw, ch))
    preview.putdata(pixels)

    import io
    out = io.BytesIO()
    preview.save(out, format="PNG")
    return film, out.getvalue()
