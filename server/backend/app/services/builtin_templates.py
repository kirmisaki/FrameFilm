"""内置模板定义

模板以 400×600（竖版）为参考坐标系，渲染时按目标分辨率缩放。
设计态 = schemes（配色方案）+ layers（布局结构）+ params（应用态参数定义）
应用态 = definition.data.params（参数当前值，用户可在应用弹窗中配置并随模板保存）
日历模板完整移植自小程序 tpl-calendar.js（农历/节气/节日/今日高亮/进度条）。
"""
import datetime as dt

# ==================== 配色方案（移植自小程序） ====================
# 日历 5 套（tpl-calendar.js SCHEMES）
_CALENDAR_SCHEMES = [
    {"name": "米杏", "text": "#3a3631", "sub": "#4a443c", "sub2": "#5a5448", "accent": "#e8553d", "card": "#ffffff"},
    {"name": "雾蓝", "text": "#2c3a4d", "sub": "#33455c", "sub2": "#47586e", "accent": "#2f6fd8", "card": "#ffffff"},
    {"name": "薄荷", "text": "#2c4438", "sub": "#33503f", "sub2": "#46604f", "accent": "#1fa86c", "card": "#ffffff"},
    {"name": "奶油", "text": "#453a38", "sub": "#4a3c3a", "sub2": "#5e4a46", "accent": "#e8557a", "card": "#ffffff"},
    {"name": "杏黄", "text": "#453b28", "sub": "#4a402c", "sub2": "#5c503a", "accent": "#d94a2a", "card": "#ffffff"},
]

# 文字类模板通用浅色纸张风（前 4 套）
_PAPER_SCHEMES = _CALENDAR_SCHEMES[:4]

# 倒计时 4 套（tpl-countdown.js SCHEMES）
_COUNTDOWN_SCHEMES = [
    {"name": "蜜桃", "accent": "#ff5f7f", "text": "#3a2a2e", "sub": "#3a2a2e", "num": "#3a2a2e"},
    {"name": "湖蓝", "accent": "#3d7bff", "text": "#23364e", "sub": "#23364e", "num": "#23364e"},
    {"name": "抹茶", "accent": "#1fb573", "text": "#2a4636", "sub": "#2a4636", "num": "#2a4636"},
    {"name": "赤金", "accent": "#c44a1f", "text": "#463a2c", "sub": "#463a2c", "num": "#463a2c"},
]

_DEFAULT_RC = {"dither_type": "adaptive", "dither_strength": 80, "contrast": 100, "brightness": 0, "saturation": 100}

_THIS_MONTH = dt.date.today().strftime("%Y-%m")


def _params(**values) -> dict:
    return {"data": {"params": values}}


# ==================== 日历（完整移植小程序） ====================
_CALENDAR = {
    "kind": "calendar",
    "schemes": _CALENDAR_SCHEMES,
    "params": [
        {"key": "month_mode", "label": "月份", "type": "radio",
         "options": [{"label": "跟随当前时间", "value": "current"},
                     {"label": "固定月份", "value": "fixed"}],
         "default": "current"},
        {"key": "fixed_month", "label": "固定月份", "type": "month", "default": _THIS_MONTH},
        {"key": "scheme", "label": "配色方案", "type": "scheme", "default": 0},
    ],
    "background": {"color": "#ffffff"},
    "layers": [
        # 顶部标题区（与小程序一致：同一中线 y=33）：大月份(左) + 英文月·年(中) / 今日农历(右)
        {"type": "text", "x": 24, "y": 33, "w": 0, "h": 0, "size": 40, "weight": "bold",
         "baseline": "middle", "color": {"scheme": "text"},
         "value": {"source": "calendar", "key": "month_full"}},
        {"type": "text", "x": 88, "y": 33, "w": 0, "h": 0, "size": 15,
         "baseline": "middle", "color": {"scheme": "sub"},
         "value": {"source": "calendar", "key": "month_en_year"}},
        {"type": "text", "x": 376, "y": 33, "w": 0, "h": 0, "size": 13, "weight": "bold",
         "align": "right", "baseline": "middle", "color": {"scheme": "accent"},
         "value": {"source": "calendar", "key": "today_lunar_full"}},
        # 标题分隔线（小程序 H*0.13 = 78）
        {"type": "line", "x": 24, "y": 78, "w": 352, "h": 0, "width": 2, "color": {"scheme": "accent"}},
        # 月历网格（小程序：星期行 y=99，网格 120-480，cellH=60，进度条 y=510）
        {"type": "calendar_grid", "x": 24, "y": 120, "w": 352, "h": 360,
         "rows": 6, "weekday_header": True, "weekday_header_cy": 99,
         "show_progress": True, "progress_gap": 30,
         "value": {"source": "calendar", "key": "cells"}},
        # 底部本月进度文字（小程序 H*0.9 = 540）
        {"type": "text", "x": 200, "y": 540, "w": 0, "h": 0, "size": 12,
         "align": "center", "baseline": "middle", "color": {"scheme": "sub"},
         "value": {"source": "calendar", "key": "progress_text"}},
    ],
    **_params(month_mode="current", fixed_month=_THIS_MONTH, scheme=0),
}

# ==================== 相册 ====================
_ALBUM = {
    "kind": "album",
    "params": [
        {"key": "album_id", "label": "相册", "type": "album", "default": None},
        {"key": "rotate_sec", "label": "自动轮换间隔（秒）", "type": "number",
         "default": 300, "min": 0, "max": 86400},
        {"key": "photo_index", "label": "固定照片序号（0 起）", "type": "number",
         "default": 0, "min": 0, "max": 999},
    ],
    "background": {"color": "#ffffff"},
    "layers": [
        {"type": "image", "x": 0, "y": 0, "w": 400, "h": 600,
         "source": {"album_id": None, "rotate_sec": 300, "photo_index": 0}},
    ],
    **_params(album_id=None, rotate_sec=300, photo_index=0),
}

# ==================== 倒计时（移植小程序样式） ====================
_COUNTDOWN = {
    "kind": "countdown",
    "schemes": _COUNTDOWN_SCHEMES,
    "params": [
        {"key": "title", "label": "纪念日名称", "type": "text", "default": "在一起", "maxlength": 8},
        {"key": "target", "label": "目标日期", "type": "date", "default": "2026-12-31"},
        {"key": "scheme", "label": "配色方案", "type": "scheme", "default": 0},
    ],
    "background": {"color": "#ffffff"},
    "layers": [
        # 顶部：品牌 + 今日
        {"type": "text", "x": 24, "y": 24, "w": 240, "h": 24, "size": 13,
         "baseline": "middle", "color": {"scheme": "sub"}, "value": "FRAME FILM · 幸福倒数"},
        {"type": "text", "x": 376, "y": 24, "w": 0, "h": 24, "size": 13,
         "align": "right", "baseline": "middle", "color": {"scheme": "sub"},
         "value": {"source": "countdown", "key": "today_str"}},
        # 白色撕页卡片（圆角卡 + 顶部日期条）
        {"type": "rect", "x": 48, "y": 96, "w": 304, "h": 236, "radius": 10,
         "fill": "#ffffff", "color": {"scheme": "sub"}, "width": 2},
        {"type": "rect", "x": 56, "y": 104, "w": 288, "h": 40, "radius": 8,
         "fill": {"scheme": "accent"}},
        {"type": "text", "x": 200, "y": 104, "w": 0, "h": 40, "size": 13, "weight": "bold",
         "align": "center", "baseline": "middle", "color": "#ffffff",
         "value": {"source": "countdown", "key": "date_label"}},
        # 大数字 + DAYS
        {"type": "text", "x": 200, "y": 198, "w": 0, "h": 110, "size": 96, "weight": "bold",
         "align": "center", "baseline": "middle", "color": {"scheme": "num"},
         "value": {"source": "countdown", "key": "days"}},
        {"type": "text", "x": 200, "y": 288, "w": 0, "h": 24, "size": 14, "weight": "bold",
         "align": "center", "baseline": "middle", "color": {"scheme": "accent"}, "value": "DAYS"},
        # 名称 / 状态 / 提示
        {"type": "text", "x": 200, "y": 350, "w": 0, "h": 40, "size": 30, "weight": "bold",
         "align": "center", "baseline": "middle", "color": {"scheme": "text"},
         "value": {"source": "countdown", "key": "title"}},
        {"type": "text", "x": 200, "y": 394, "w": 0, "h": 24, "size": 13, "weight": "bold",
         "align": "center", "baseline": "middle", "color": {"scheme": "accent"},
         "value": {"source": "countdown", "key": "mode_label"}},
        {"type": "text", "x": 200, "y": 440, "w": 0, "h": 24, "size": 11,
         "align": "center", "baseline": "middle", "color": {"scheme": "sub"},
         "value": {"source": "countdown", "key": "hint"}},
        # 底部装饰线
        {"type": "line", "x": 170, "y": 480, "w": 60, "h": 0, "width": 2, "color": {"scheme": "accent"}},
    ],
    **_params(title="在一起", target="2026-12-31", scheme=0),
}

# ==================== 备忘录 ====================
_MEMO = {
    "kind": "memo",
    "schemes": _PAPER_SCHEMES,
    "params": [
        {"key": "title", "label": "标题", "type": "text", "default": "备忘"},
        {"key": "items", "label": "备忘条目", "type": "list",
         "default": ["1. 记得喝水", "2. 买菜：牛奶、鸡蛋", "3. 晚上 8 点会议"]},
        {"key": "scheme", "label": "配色方案", "type": "scheme", "default": 0},
    ],
    "background": {"color": "#ffffff"},
    "layers": [
        {"type": "text", "x": 24, "y": 20, "w": 352, "h": 40, "size": 32, "weight": "bold",
         "color": {"scheme": "text"}, "value": {"source": "memo", "key": "title"}},
        {"type": "line", "x": 24, "y": 72, "w": 352, "h": 0, "width": 2, "color": {"scheme": "accent"}},
        {"type": "text", "x": 24, "y": 92, "w": 352, "h": 400, "size": 24,
         "color": {"scheme": "text"}, "value": {"source": "memo", "key": "text_block"}},
    ],
    **_params(title="备忘", items=["1. 记得喝水", "2. 买菜：牛奶、鸡蛋", "3. 晚上 8 点会议"], scheme=0),
}

# ==================== 今日运势 ====================
_FORTUNE = {
    "kind": "fortune",
    "schemes": _PAPER_SCHEMES,
    "params": [{"key": "scheme", "label": "配色方案", "type": "scheme", "default": 0}],
    "background": {"color": "#ffffff"},
    "layers": [
        {"type": "text", "x": 24, "y": 24, "w": 300, "h": 44, "size": 36, "weight": "bold",
         "color": {"scheme": "text"}, "value": "今日运势"},
        {"type": "text", "x": 220, "y": 36, "w": 156, "h": 28, "size": 18,
         "align": "right", "baseline": "middle", "color": {"scheme": "sub"},
         "value": {"source": "calendar", "key": "title"}},
        {"type": "text", "x": 24, "y": 110, "w": 352, "h": 60, "size": 40, "weight": "bold",
         "color": {"scheme": "text"}, "value": {"source": "fortune", "key": "fortune"}},
        {"type": "line", "x": 24, "y": 200, "w": 352, "h": 0, "width": 2, "color": {"scheme": "sub2"}},
        {"type": "text", "x": 24, "y": 230, "w": 200, "h": 36, "size": 28,
         "color": {"scheme": "text"}, "value": "幸运颜色"},
        {"type": "text", "x": 240, "y": 230, "w": 136, "h": 36, "size": 28,
         "align": "right", "baseline": "middle", "color": {"scheme": "accent"},
         "value": {"source": "fortune", "key": "color"}},
    ],
    **_params(scheme=0),
}

# ==================== 每日一言 ====================
_QUOTE = {
    "kind": "quote",
    "schemes": _PAPER_SCHEMES,
    "params": [
        {"key": "text", "label": "固定文案（留空则每日自动换一句）", "type": "textarea", "default": ""},
        {"key": "scheme", "label": "配色方案", "type": "scheme", "default": 0},
    ],
    "background": {"color": "#ffffff"},
    "layers": [
        {"type": "circle", "x": 40, "y": 40, "w": 24, "h": 24, "fill": {"scheme": "accent"}},
        {"type": "text", "x": 24, "y": 100, "w": 352, "h": 260, "size": 40, "weight": "bold",
         "color": {"scheme": "text"}, "value": {"source": "quote", "key": "text"}},
        {"type": "text", "x": 376, "y": 400, "w": 0, "h": 36, "size": 22,
         "align": "right", "baseline": "middle", "color": {"scheme": "sub2"},
         "value": {"source": "quote", "key": "date"}},
    ],
    **_params(text="", scheme=0),
}

# ==================== 冰箱食材 ====================
_FRIDGE = {
    "kind": "fridge",
    "schemes": _PAPER_SCHEMES,
    "params": [
        {"key": "items", "label": "食材条目（名称 | 日期 YYYY-MM-DD，日期可省略）", "type": "list",
         "default": ["牛奶|2026-08-10", "鸡蛋|2026-08-12", "面包"]},
        {"key": "scheme", "label": "配色方案", "type": "scheme", "default": 0},
    ],
    "background": {"color": "#ffffff"},
    "layers": [
        {"type": "text", "x": 24, "y": 20, "w": 352, "h": 40, "size": 32, "weight": "bold",
         "color": {"scheme": "text"}, "value": "冰箱食材"},
        {"type": "line", "x": 24, "y": 72, "w": 352, "h": 0, "width": 2, "color": {"scheme": "accent"}},
        {"type": "text", "x": 24, "y": 92, "w": 352, "h": 400, "size": 24,
         "color": {"scheme": "text"}, "value": {"source": "fridge", "key": "text_block"}},
    ],
    **_params(items=["牛奶|2026-08-10", "鸡蛋|2026-08-12", "面包"], scheme=0),
}

# ==================== 天气 ====================
_WEATHER = {
    "kind": "weather",
    "schemes": _PAPER_SCHEMES,
    "params": [
        {"key": "city", "label": "城市（预留）", "type": "text", "default": ""},
        {"key": "scheme", "label": "配色方案", "type": "scheme", "default": 0},
    ],
    "background": {"color": "#ffffff"},
    "layers": [
        {"type": "text", "x": 24, "y": 24, "w": 300, "h": 44, "size": 36, "weight": "bold",
         "color": {"scheme": "text"}, "value": "今日天气"},
        {"type": "text", "x": 24, "y": 110, "w": 352, "h": 120, "size": 80, "weight": "bold",
         "baseline": "middle", "color": {"scheme": "text"}, "value": {"source": "weather", "key": "temp"}},
        {"type": "text", "x": 24, "y": 250, "w": 352, "h": 60, "size": 32,
         "baseline": "middle", "color": {"scheme": "sub"}, "value": {"source": "weather", "key": "text"}},
    ],
    **_params(city="", scheme=0),
}


BUILTIN_TEMPLATES = [
    {"name": "日历", "kind": "calendar", "definition": _CALENDAR, "render_config": _DEFAULT_RC},
    {"name": "相册", "kind": "album", "definition": _ALBUM, "render_config": _DEFAULT_RC},
    {"name": "倒计时", "kind": "countdown", "definition": _COUNTDOWN, "render_config": _DEFAULT_RC},
    {"name": "备忘录", "kind": "memo", "definition": _MEMO, "render_config": _DEFAULT_RC},
    {"name": "今日运势", "kind": "fortune", "definition": _FORTUNE, "render_config": _DEFAULT_RC},
    {"name": "每日一言", "kind": "quote", "definition": _QUOTE, "render_config": _DEFAULT_RC},
    {"name": "冰箱食材", "kind": "fridge", "definition": _FRIDGE, "render_config": _DEFAULT_RC},
    {"name": "天气", "kind": "weather", "definition": _WEATHER, "render_config": _DEFAULT_RC},
]


def builtin_definitions() -> list[dict]:
    return BUILTIN_TEMPLATES
