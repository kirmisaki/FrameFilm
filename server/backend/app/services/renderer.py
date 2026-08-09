"""模板渲染引擎

将模板 definition JSON 渲染为指定分辨率的 RGB 图，再交 film_convert 转换。
图层类型：text / image / rect / line / circle / table / calendar_grid
数据绑定：layer.value 为字符串时直接用；为 {"source": kind} 时从数据源解析。
配色方案：definition.schemes 定义多套方案，layer 颜色可写 {"scheme": "key"} 按
应用态参数 params.scheme 索引引用；text 图层支持 baseline: middle 垂直居中。
"""
import datetime as dt
import json
import os

from PIL import Image, ImageDraw, ImageFont

from ..config import ORIGINALS_DIR, SCREENS
from . import data_sources

REF_WIDTH = 400  # 模板以基础版 400 宽（竖版）为参考坐标系，按比例缩放

_FONT_CANDIDATES = {
    "regular": [
        r"C:\Windows\Fonts\msyh.ttc",
        r"C:\Windows\Fonts\simhei.ttf",
        r"C:\Windows\Fonts\simsun.ttc",
        "/System/Library/Fonts/PingFang.ttc",
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
    ],
    "bold": [
        r"C:\Windows\Fonts\msyhbd.ttc",
        r"C:\Windows\Fonts\simhei.ttf",
        "/System/Library/Fonts/PingFang.ttc",
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Bold.ttc",
    ],
    "serif": [
        r"C:\Windows\Fonts\simsun.ttc",
        r"C:\Windows\Fonts\msyh.ttc",
        r"C:\Windows\Fonts\simhei.ttf",
        "/System/Library/Fonts/STSongti-SC.ttc",
        "/usr/share/fonts/opentype/noto/NotoSerifCJK-Regular.ttc",
    ],
    "serif_bold": [
        r"C:\Windows\Fonts\simsun.ttc",
        r"C:\Windows\Fonts\msyhbd.ttc",
        "/System/Library/Fonts/STSongti-SC-Bold.ttf",
        "/usr/share/fonts/opentype/noto/NotoSerifCJK-Bold.ttc",
    ],
}

_FONT_CACHE: dict[str, ImageFont.FreeTypeFont] = {}


def _font(size: int, bold: bool = False, serif: bool = False) -> ImageFont.FreeTypeFont:
    style = ("serif_bold" if serif and bold else "serif" if serif else "bold" if bold else "regular")
    key = f"{style}-{size}"
    if key in _FONT_CACHE:
        return _FONT_CACHE[key]
    font = None
    for path in _FONT_CANDIDATES[style]:
        if os.path.exists(path):
            try:
                font = ImageFont.truetype(path, size)
                break
            except OSError:
                continue
    if font is None:
        font = ImageFont.load_default(size=size)
    _FONT_CACHE[key] = font
    return font


def _hex_to_rgb(color) -> tuple:
    if isinstance(color, (list, tuple)):
        return tuple(int(c) for c in color[:3])
    s = str(color).lstrip("#")
    if len(s) == 6:
        return (int(s[0:2], 16), int(s[2:4], 16), int(s[4:6], 16))
    if len(s) == 3:
        return (int(s[0] * 2, 16), int(s[1] * 2, 16), int(s[2] * 2, 16))
    return (0, 0, 0)


def _resolve_color(value, scheme: dict | None) -> tuple:
    """解析颜色：直接色值 / {"scheme": "key"} 引用当前方案"""
    if isinstance(value, dict):
        key = value.get("scheme")
        if scheme and key and key in scheme:
            return _hex_to_rgb(scheme[key])
        return _hex_to_rgb(value.get("default", "#000000"))
    return _hex_to_rgb(value)


def _current_scheme(definition: dict, params: dict) -> dict | None:
    schemes = definition.get("schemes")
    if not isinstance(schemes, list) or not schemes:
        return None
    idx = params.get("scheme", 0)
    try:
        idx = int(idx)
    except (TypeError, ValueError):
        idx = 0
    if not 0 <= idx < len(schemes):
        idx = 0
    return schemes[idx]


def _resolve_text(value, data: dict) -> str:
    """解析文字：字符串直接使用；dict 从 data 取值；{source, key, default}"""
    if isinstance(value, str):
        return value
    if isinstance(value, dict):
        source = value.get("source")
        key = value.get("key")
        if source and key:
            d = data.get(source, {})
            if isinstance(d, dict) and key in d:
                return str(d[key])
            return str(value.get("default", ""))
        if "text" in value:
            return str(value["text"])
    return ""


def _resolve_items(value, data: dict) -> list:
    """解析条目数组：直接数组 / {source, key} 取数组或按 \\n 拆分字符串"""
    if isinstance(value, list):
        return value
    if isinstance(value, dict):
        source, key = value.get("source"), value.get("key")
        if source and key:
            d = data.get(source, {})
            if isinstance(d, dict) and key in d:
                v = d[key]
                if isinstance(v, list):
                    return v
                if isinstance(v, str):
                    return [ln for ln in v.split("\n") if ln.strip()]
    return []


def _load_album_image(source: dict, width: int, height: int, now: dt.datetime) -> Image.Image | None:
    """从相册取照片（支持按时轮流换），按照片显示布局（layout）适配到目标画布"""
    import json

    from sqlalchemy.orm import Session

    from ..db import SessionLocal
    from ..models import Album, Photo

    album_id = source.get("album_id")
    if not album_id:
        return None
    session: Session = SessionLocal()
    layout_raw = "{}"
    try:
        album = session.get(Album, album_id)
        if not album:
            return None
        photos = session.query(Photo).filter(Photo.album_id == album_id).order_by(Photo.sort).all()
        if not photos:
            return None
        rotate_sec = int(source.get("rotate_sec", 0))
        if rotate_sec > 0:
            idx = int(now.timestamp() / rotate_sec) % len(photos)
        else:
            idx = int(source.get("photo_index", 0)) % len(photos)
        photo = photos[idx]
        if not photo.original_path or not os.path.exists(photo.original_path):
            return None
        layout_raw = photo.layout or "{}"
        img = Image.open(photo.original_path).convert("RGB")
    finally:
        session.close()
    try:
        layout = json.loads(layout_raw)
    except (TypeError, ValueError):
        layout = {}
    s = max(0.5, min(4.0, float(layout.get("scale", 1.0) or 1.0)))
    ox = int(layout.get("x", 0) or 0)
    oy = int(layout.get("y", 0) or 0)
    rotate = int(layout.get("rotate", 0) or 0) % 360
    # 先旋转（原分辨率，顺时针语义与前端预览一致；PIL rotate 逆时针，故取负），再 contain 适配 + 用户缩放/平移
    if rotate:
        img = img.rotate(-rotate, expand=True)
    iw, ih = img.size
    ratio = min(width / iw, height / ih)
    w = max(1, round(iw * ratio * s))
    h = max(1, round(ih * ratio * s))
    pasted = img.resize((w, h), Image.LANCZOS)
    bg = Image.new("RGB", (width, height), (255, 255, 255))
    bg.paste(pasted, ((width - w) // 2 + ox, (height - h) // 2 + oy))
    return bg


def _render_calendar_grid(draw: ImageDraw.ImageDraw, x: int, y: int, w: int, h: int,
                          cells: list, data: dict, scheme: dict, layer: dict,
                          scale: float):
    """月历网格（移植自小程序 tpl-calendar.js）：星期表头 + 圆角卡片 + 今日高亮 + 进度条"""
    s_text = _hex_to_rgb(scheme.get("text", "#000000")) if scheme else (0, 0, 0)
    s_sub = _hex_to_rgb(scheme.get("sub", "#555555")) if scheme else (85, 85, 85)
    s_sub2 = _hex_to_rgb(scheme.get("sub2", "#888888")) if scheme else (136, 136, 136)
    s_accent = _hex_to_rgb(scheme.get("accent", "#e8553d")) if scheme else (232, 85, 61)
    s_card = _hex_to_rgb(scheme.get("card", "#ffffff")) if scheme else (255, 255, 255)

    cols = int(layer.get("cols", 7)) or 7
    rows = int(layer.get("rows", 6)) or 6

    # 星期表头（小程序 headerY = H*0.165；支持 weekday_header_cy 指定独立中心 Y）
    if layer.get("weekday_header"):
        wd_font = _font(max(8, int(round(400 * 0.034 * scale))))
        header_cy = layer.get("weekday_header_cy")
        if header_cy is not None:
            header_cy = int(round(header_cy * scale))
        else:
            header_h = max(8, int(round(layer.get("weekday_header_h", 26) * scale)))
            header_cy = y + header_h // 2
            y += header_h
            h -= header_h
        for i, wd in enumerate(data_sources.WEEKDAYS):
            cx = x + i * (w // cols) + (w // cols) // 2
            draw.text((cx, header_cy), wd, font=wd_font, fill=s_sub, anchor="mm")

    cw, ch = w // cols, h // rows
    gap = max(1, int(round(400 * 0.008 * scale))) or 1
    radius = max(1, int(round(400 * 0.012 * scale))) or 1

    day_font = _font(max(8, int(round(cw * 0.36))), bold=True)
    note_font = _font(max(7, int(round(cw * 0.18))))

    for i, cell in enumerate(cells):
        if not cell or not isinstance(cell, dict):
            continue
        row, col = divmod(i, cols)
        cx = x + col * cw + cw // 2
        cy = y + row * ch + ch // 2
        card_w, card_h = cw - gap * 2, ch - gap * 2

        # 卡片底 + 描边
        draw.rounded_rectangle(
            [cx - card_w // 2, cy - card_h // 2, cx + card_w // 2, cy + card_h // 2],
            radius=radius, fill=s_card, outline=s_sub2, width=1)

        # 今日：强调色圆形高亮
        if cell.get("is_today"):
            r = max(6, int(round(cw * 0.32)))
            draw.ellipse([cx - r, cy - int(ch * 0.12) - r, cx + r, cy - int(ch * 0.12) + r], fill=s_accent)

        # 日期数字 + 农历标注
        day_color = (255, 255, 255) if cell.get("is_today") else s_text
        draw.text((cx, cy - int(ch * 0.1)), str(cell.get("day", "")), font=day_font,
                  fill=day_color, anchor="mm")
        note = cell.get("note", "")
        if note:
            note_color = (255, 255, 255) if cell.get("is_today") else s_sub2
            draw.text((cx, cy + int(ch * 0.3)), note, font=note_font, fill=note_color, anchor="mm")

    # 底部本月进度条
    if layer.get("show_progress") and data.get("progress") is not None:
        bar_gap = max(6, int(round(layer.get("progress_gap", 30) * scale)))
        bar_y = y + h + bar_gap
        bar_h = max(3, int(round(600 * 0.012 * scale)))
        draw.rounded_rectangle([x, bar_y, x + w, bar_y + bar_h], radius=bar_h // 2,
                               outline=s_accent, width=1)
        p = max(0, min(100, int(data.get("progress", 0))))
        if p > 0:
            draw.rounded_rectangle([x, bar_y, x + int(w * p / 100), bar_y + bar_h],
                                   radius=bar_h // 2, fill=s_accent)


def render_definition(definition: dict, width: int, height: int,
                      now: dt.datetime | None = None,
                      force_photo_index: int | None = None,
                      params: dict | None = None) -> Image.Image:
    """渲染模板定义 -> RGB 图；params 非空时覆盖应用态参数（用于实时预览不落库）"""
    now = now or dt.datetime.now()
    scale = width / REF_WIDTH

    kind = definition.get("kind", "custom")
    data_src = definition.get("data") or {}
    if params is not None:
        data_src = {**data_src, "params": params}
    data = data_sources.resolve(kind, data_src, now)
    # 解包当前 kind 的数据（resolve 返回 {kind: {...}}；text 图层走 data.get(kind)）
    cur = data.get(kind, data)

    # 当前配色方案
    scheme = _current_scheme(definition, data_src.get("params") or {})

    # 背景
    bg = definition.get("background", {}) or {}
    if isinstance(bg.get("color"), dict):
        bg_color = _resolve_color(bg["color"], scheme)
    elif bg.get("color"):
        bg_color = _hex_to_rgb(bg["color"])
    else:
        bg_color = (255, 255, 255)
    canvas = Image.new("RGB", (width, height), bg_color)
    draw = ImageDraw.Draw(canvas)

    def _s(v):
        return int(round(v * scale))

    for layer in definition.get("layers", []):
        ltype = layer.get("type")
        x, y = _s(layer.get("x", 0)), _s(layer.get("y", 0))
        w, h = _s(layer.get("w", 0)), _s(layer.get("h", 0))
        color = _resolve_color(layer.get("color", "#000000"), scheme)

        if ltype == "rect":
            if "fill" in layer:
                fill = None if layer.get("fill") is None else _resolve_color(layer["fill"], scheme)
            else:
                fill = _resolve_color(layer.get("color", "#000000"), scheme)
            radius = layer.get("radius")
            if radius:
                draw.rounded_rectangle([x, y, x + w, y + h], radius=max(1, _s(radius)),
                                       fill=fill, outline=color, width=_s(layer.get("width", 1)) or 1)
            else:
                draw.rectangle([x, y, x + w, y + h], fill=fill, outline=color, width=_s(layer.get("width", 1)) or 1)
        elif ltype == "line":
            lw = max(1, _s(layer.get("width", 2)))
            if layer.get("dash"):
                dash = layer["dash"]
                dlen = max(1, _s(dash[0]))
                dgap = max(1, _s(dash[1])) if len(dash) > 1 else dlen
                length = (w * w + h * h) ** 0.5 or 1
                dx, dy = w / length, h / length
                pos = 0.0
                while pos < length:
                    end = min(pos + dlen, length)
                    draw.line([x + dx * pos, y + dy * pos, x + dx * end, y + dy * end],
                              fill=color, width=lw)
                    pos = end + dgap
            else:
                draw.line([x, y, x + w, y + h], fill=color, width=lw)
        elif ltype == "circle":
            fill = _resolve_color(layer.get("fill", "#ffffff"), scheme) if layer.get("fill") else None
            draw.ellipse([x, y, x + w, y + h], fill=fill, outline=color, width=max(1, _s(layer.get("width", 2))))
        elif ltype == "moon":
            # 月相：cx, cy, r 圆心半径；phase 0-7（0 新月/1 娥眉/2 上弦/3 盈凸/4 满月/5 亏凸/6 下弦/7 残月）
            # 描边圆 + 按相位用 bg 色覆盖实现
            phase_val = layer.get("value")
            if isinstance(phase_val, dict):
                phase_val = data.get(phase_val.get("source", "fortune"), {}).get(phase_val.get("key", "moon_phase"), 0)
            try:
                phase = int(phase_val) % 8
            except (TypeError, ValueError):
                phase = 0
            r = max(2, (w if w < h else h) // 2)
            cx, cy = x + w // 2, y + h // 2
            moon_bg = _resolve_color(layer.get("bg", "#ffffff"), scheme)
            moon_ink = color
            moon_box = [cx - r, cy - r, cx + r, cy + r]
            if phase == 4:
                # 满月：白色圆 + 黑色描边
                draw.ellipse(moon_box, fill=moon_bg, outline=moon_ink, width=max(1, _s(layer.get("width", 1))))
            elif phase == 0:
                # 新月：实心黑圆
                draw.ellipse(moon_box, fill=moon_ink)
            else:
                # 先铺白色底 + 黑色描边
                draw.ellipse(moon_box, fill=moon_bg, outline=moon_ink, width=max(1, _s(layer.get("width", 1))))
                # 再用黑色画暗部
                rx = r
                if phase in (2, 6):
                    if phase == 2:  # 上弦（右半圆亮=左半暗）
                        draw.rectangle([cx - rx, cy - r, cx, cy + r], fill=moon_ink)
                    else:  # 下弦（左半圆亮=右半暗）
                        draw.rectangle([cx, cy - r, cx + rx, cy + r], fill=moon_ink)
                elif phase in (1, 3, 5, 7):
                    # 暗部为一个偏移椭圆；sign: 右亮(1,3)暗偏左(负)，左亮(5,7)暗偏右(正)
                    sign = -1 if phase in (1, 3) else 1
                    # 娥眉：留 0.35r 宽亮弧；凸月：留 0.65r 宽亮部（示意性，非真实天文比例）
                    ew = r * (0.65 if phase in (3, 5) else 0.82)
                    ox = sign * (r - ew)
                    draw.ellipse([cx - ew + ox, cy - r, cx + ew + ox, cy + r], fill=moon_ink)

        elif ltype == "text":
            text = _resolve_text(layer.get("value"), data)
            if text:
                size = max(8, _s(layer.get("size", 24)))
                bold = layer.get("weight") == "bold" or layer.get("bold")
                font = _font(size, bold, layer.get("font") == "serif")
                align = layer.get("align", "left")
                middle = layer.get("baseline") == "middle"
                rotate = int(layer.get("rotate", 0) or 0) % 360
                anchors = {
                    "left": "lm" if middle else "la",
                    "center": "mm" if middle else "ma",
                    "right": "rm" if middle else "ra",
                }

                def _draw_line(px, py, line, anchor):
                    if rotate in (90, 270):
                        # 竖排：横向绘制后整体旋转（顺时针语义与 CSS 一致）
                        tmp = Image.new("RGBA", (size * 2 + 8, size * len(line) + 8), (0, 0, 0, 0))
                        ImageDraw.Draw(tmp).text((tmp.width // 2, tmp.height // 2), line,
                                                 font=font, fill=color + (255,), anchor="mm")
                        tmp = tmp.rotate(-rotate, expand=True)
                        canvas.paste(tmp, (px - tmp.width // 2, py - tmp.height // 2), tmp)
                    else:
                        draw.text((px, py), line, font=font, fill=color, anchor=anchor)

                line_height = layer.get("line_height")
                vertical = layer.get("vertical")
                wrap_width = layer.get("wrap_width")
                if vertical:
                    lines = [ch for ch in str(text) if ch.strip()]
                    lh_auto = size
                elif line_height or "\n" in str(text) or wrap_width:
                    raw = str(text)
                    if wrap_width:
                        # 按字体实测宽度自动换行（wrap_width 为参考坐标系宽度）
                        ww = max(8, _s(wrap_width))
                        lines = []
                        cur = ""
                        for ch in raw:
                            if cur and font.getlength(cur + ch) > ww:
                                lines.append(cur)
                                cur = ch
                            else:
                                cur += ch
                        if cur:
                            lines.append(cur)
                    else:
                        lines = raw.split("\n")
                    lh_auto = size * 1.5
                else:
                    lines = None
                if lines:
                    lh = max(size, _s(float(line_height) if line_height else lh_auto))
                    h_anchor = {"left": "l", "center": "m", "right": "r"}[align]
                    px = x + w // 2 if align == "center" else (x + w if align == "right" else x)
                    if middle:
                        start_y = y + h // 2 - (len(lines) * lh) // 2 + lh // 2
                        for i, ln in enumerate(lines):
                            _draw_line(px, start_y + i * lh, ln, h_anchor + "m")
                    else:
                        for i, ln in enumerate(lines):
                            _draw_line(px, y + i * lh, ln, h_anchor + "a")
                else:
                    ax, ay = x, y
                    if middle:
                        ay = y + h // 2
                    if align == "center":
                        ax = x + w // 2
                    elif align == "right":
                        ax = x + w
                    _draw_line(ax, ay, text, anchors.get(align, "la"))
        elif ltype == "checklist":
            # 通用清单：每行 checkbox（方块/圆圈）+ 左文本 + 右对齐 sub（子信息）；
            # urgent 项强调色；done 项（dict.done 或 "[x] 前缀"）实心填充 + 白对勾 + 灰色划线
            items = _resolve_items(layer.get("items", layer.get("value")), data)
            if items:
                size = max(9, _s(layer.get("size", 22)))
                font = _font(size)
                sub_font = _font(max(8, _s(layer.get("sub_size", max(9, size - 5)))))
                lh = max(size, _s(layer.get("line_height", size * 1.9)))
                bs = max(8, int(_s(layer.get("box_size")))) if layer.get("box_size") is not None \
                    else max(8, int(size * 0.95))
                gap = _s(layer.get("gap", 12))
                box = layer.get("box")
                box_style = layer.get("box_style", "square")
                box_color = _resolve_color(box if box else "#000000", scheme) if box else None
                done_box_color = _resolve_color(layer.get("done_box_color", box if box else "#000000"), scheme) \
                    if (layer.get("done_box_color") or box) else None
                urgent_color = _resolve_color(layer.get("urgent_color", "#d63a2a"), scheme)
                text_color = _resolve_color(layer.get("text_color", layer.get("color", "#000000")), scheme)
                sub_color = _resolve_color(layer.get("sub_color", "#888888"), scheme)
                done_color = _resolve_color(layer.get("done_color", sub_color), scheme)
                divider = layer.get("divider")
                divider_gap = _s(layer.get("divider_gap", 8))
                divider_color = _resolve_color(layer.get("divider_color", layer.get("sub_color", "#888888")), scheme)
                max_lines = int(layer.get("max_lines", 30))
                trunc_pad = _s(layer.get("truncate_pad", 4))
                done_font = _font(max(8, int(bs * 0.78)), True)
                for i, it in enumerate(items[:max_lines]):
                    cy = y + i * lh
                    yc = cy + max(0, (lh - bs) // 2)  # 行内容垂直居中
                    if isinstance(it, dict):
                        txt = str(it.get("text", ""))
                        sub = it.get("sub")
                        urgent = bool(it.get("urgent"))
                        done = bool(it.get("done"))
                    else:
                        s = str(it)
                        if s.startswith("[x]") or s.startswith("[X]"):
                            txt = s[3:].lstrip()
                            sub, urgent, done = None, False, True
                        elif s.startswith("[ ]"):
                            txt = s[3:].lstrip()
                            sub, urgent, done = None, False, False
                        else:
                            txt = s
                            sub, urgent, done = None, False, False
                    tcolor = urgent_color if urgent else (done_color if done else text_color)
                    tx = x
                    if box:
                        rad = max(1, _s(layer.get("box_radius", 2)))
                        if box_style == "circle":
                            r = max(4, bs // 2)
                            cxx, cyy = x + r, yc + bs // 2
                            if done:
                                draw.ellipse([cxx - r, cyy - r, cxx + r, cyy + r], fill=done_box_color)
                                draw.text((cxx, cyy + 1), "✓", font=done_font, fill="#ffffff", anchor="mm")
                            else:
                                draw.ellipse([cxx - r, cyy - r, cxx + r, cyy + r],
                                             outline=box_color, width=max(1, _s(2)))
                        else:
                            if done and done_box_color:
                                draw.rounded_rectangle([x, yc, x + bs, yc + bs], radius=rad, fill=done_box_color)
                                # 白色对勾（线条手绘，同小程序）
                                draw.line([x + bs * 0.22, yc + bs // 2, x + bs * 0.44, yc + bs // 2 + bs * 0.2],
                                          fill="#ffffff", width=2, joint="curve")
                                draw.line([x + bs * 0.44, yc + bs // 2 + bs * 0.2, x + bs * 0.8, yc + bs // 2 - bs * 0.24],
                                          fill="#ffffff", width=2, joint="curve")
                            else:
                                draw.rounded_rectangle([x, yc, x + bs, yc + bs], radius=rad,
                                                       outline=box_color, width=max(1, _s(2)))
                            if urgent and not (done and done_box_color):
                                draw.rounded_rectangle([x + _s(2), yc + _s(2), x + bs - _s(2), yc + bs - _s(2)],
                                                       radius=rad, fill=urgent_color)
                        tx = x + bs + gap
                    # 文本超宽截断（加 …）
                    max_text_w = max(10, (x + w) - tx - trunc_pad)
                    if draw.textlength(txt, font=font) > max_text_w:
                        while txt and draw.textlength(txt + "…", font=font) > max_text_w:
                            txt = txt[:-1]
                        txt += "…"
                    draw.text((tx, yc + bs // 2), txt, font=font, fill=tcolor, anchor="lm")
                    if done and txt:
                        tw = draw.textlength(txt, font=font)
                        draw.line([tx, yc + bs // 2, tx + tw, yc + bs // 2], fill=tcolor, width=1)
                    if sub:
                        draw.text((x + w, yc + bs // 2), str(sub), font=sub_font,
                                  fill=urgent_color if urgent else sub_color, anchor="rm")
                    if divider:
                        dy = yc + bs + divider_gap
                        if layer.get("divider_solid"):
                            draw.line([x, dy, x + w, dy], fill=divider_color, width=1)
                        else:
                            dl = _s(layer.get("divider_len", 5))
                            dpos = x
                            while dpos < x + w:
                                draw.line([dpos, dy, min(dpos + dl, x + w), dy], fill=divider_color, width=1)
                                dpos += dl + _s(4)
        elif ltype == "image":
            src = layer.get("source") or {}
            if isinstance(src, dict):
                # 应用态参数可覆盖相册绑定/轮换间隔/固定序号
                p = data_src.get("params") or {}
                for k in ("album_id", "rotate_sec", "photo_index"):
                    if k in p and p[k] is not None:
                        src = {**src, k: p[k]}
                album_id = src.get("album_id") or cur.get("album", {}).get("album_id")
                if album_id:
                    img = _load_album_image({**src, "album_id": album_id}, w or width, h or height, now)
                    if img is not None:
                        canvas.paste(img, (x, y))
        elif ltype == "table":
            # 通用表格：cur.cells 一维数组 + cell_columns
            cells = cur.get("cells", [])
            cols = int(cur.get("cell_columns", 7)) or 7
            if cells:
                rows = (len(cells) + cols - 1) // cols
                cw, ch = w // cols, h // rows
                size = max(8, _s(layer.get("font_size", 20)))
                font = _font(size)
                today = cur.get("today")
                for i, val in enumerate(cells):
                    cx, cy = x + (i % cols) * cw, y + (i // cols) * ch
                    if val and val == today:
                        draw.rectangle([cx + 2, cy + 2, cx + cw - 2, cy + ch - 2], fill=_resolve_color(layer.get("today_fill", "#000000"), scheme))
                        tcolor = (255, 255, 255)
                    else:
                        tcolor = color
                    if val:
                        draw.text((cx + cw // 2, cy + ch // 2), str(val), font=font, fill=tcolor, anchor="mm")
                    draw.rectangle([cx, cy, cx + cw, cy + ch], outline=(200, 200, 200), width=1)
        elif ltype == "calendar_grid":
            cells = cur.get("cells", [])
            if cells:
                _render_calendar_grid(draw, x, y, w, h, cells, cur, scheme, layer, scale)
    return canvas


def render_template(template, width: int, height: int, now: dt.datetime | None = None,
                    force_photo_index: int | None = None,
                    params: dict | None = None) -> Image.Image:
    """渲染 Template ORM 对象（definition 为 JSON 字符串）"""
    definition = json.loads(template.definition or "{}")
    return render_definition(definition, width, height, now, force_photo_index, params)
