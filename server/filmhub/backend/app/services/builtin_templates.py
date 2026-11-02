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

# 文字类模板统一方案 key：text/sub/sub2/accent/card（text 深色→黑，accent 纯色强调）

# 备忘录（小程序 tpl-memo.js 同款配色）
_MEMO_SCHEMES = [
    {"name": "朱砂", "accent": "#e8553d", "text": "#3a3631", "sub": "#4a443c", "sub2": "#5a5448", "card": "#ffffff"},
    {"name": "晴蓝", "accent": "#2f6fd8", "text": "#2c3a4d", "sub": "#33455c", "sub2": "#47586e", "card": "#ffffff"},
    {"name": "竹绿", "accent": "#1fa86c", "text": "#2c4438", "sub": "#33503f", "sub2": "#46604f", "card": "#ffffff"},
    {"name": "蜜桃", "accent": "#e8557a", "text": "#453a38", "sub": "#4a3c3a", "sub2": "#5e4a46", "card": "#ffffff"},
    {"name": "赤金", "accent": "#c44a1f", "text": "#453b28", "sub": "#4a402c", "sub2": "#5c503a", "card": "#ffffff"},
]

# 每日一言（小程序 frame/quote 同款 3 套配色：红/蓝/绿，文字全黑）
_QUOTE_SCHEMES = [
    {"name": "朱红", "text": "#000000", "sub": "#000000", "sub2": "#666666", "accent": "#ff0000", "card": "#ffffff"},
    {"name": "靛蓝", "text": "#000000", "sub": "#000000", "sub2": "#666666", "accent": "#0000ff", "card": "#ffffff"},
    {"name": "青绿", "text": "#000000", "sub": "#000000", "sub2": "#666666", "accent": "#29cc14", "card": "#ffffff"},
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

# ==================== 备忘录（小程序 tpl-memo.js 同款：品牌行 + 居中大标题 + 圆角方块勾选卡片 + 底部统计） ====================
_MEMO = {
    "kind": "memo",
    "schemes": _MEMO_SCHEMES,
    "params": [
        {"key": "title", "label": "标题", "type": "text", "default": "今日备忘"},
        {"key": "items", "label": "待办清单", "type": "todo",
         "default": ["[x] 记得喝水", "买菜：牛奶、鸡蛋", "[ ] 晚上 8 点会议"]},
        {"key": "scheme", "label": "配色方案", "type": "scheme", "default": 0},
    ],
    "background": {"color": "#ffffff"},
    "layers": [
        # 顶部品牌行：左 "FRAME FILM · 备忘录"（sub），右 日期 YYYY.MM.DD（sub）
        {"type": "text", "x": 28, "y": 30, "w": 300, "h": 16, "size": 13,
         "baseline": "middle", "color": {"scheme": "sub"}, "value": "FRAME FILM · 备忘录"},
        {"type": "text", "x": 372, "y": 30, "w": 0, "h": 16, "size": 13,
         "align": "right", "baseline": "middle", "color": {"scheme": "sub"},
         "value": {"source": "memo", "key": "date_dot"}},
        # 大标题（居中 bold）+ accent 下划线
        {"type": "text", "x": 200, "y": 90, "w": 0, "h": 40, "size": 30, "weight": "bold",
         "align": "center", "baseline": "middle", "color": {"scheme": "text"},
         "value": {"source": "memo", "key": "title"}},
        {"type": "line", "x": 184, "y": 126, "w": 32, "h": 0, "width": 3, "color": {"scheme": "accent"}},
        # 条目卡片（圆角矩形，sub2 描边；容量 8 行：40 + 8*32）
        {"type": "rect", "x": 28, "y": 156, "w": 344, "h": 296, "radius": 8,
         "fill": "#ffffff", "color": {"scheme": "sub2"}, "width": 2},
        # 勾选清单：圆角方块（未完成 sub2 描边 / 完成 accent 填充 + 白对勾）+ 完成灰字划线 + 行间实线
        {"type": "checklist", "x": 48, "y": 176, "w": 324, "h": 260, "size": 17,
         "box_size": 18, "box_radius": 4, "line_height": 32, "gap": 12,
         "box": {"scheme": "sub2"}, "done_box_color": {"scheme": "accent"},
         "text_color": {"scheme": "text"}, "done_color": {"scheme": "sub2"},
         "sub_color": {"scheme": "sub2"}, "divider": True, "divider_gap": 7,
         "divider_solid": True, "divider_color": {"scheme": "sub2"}, "max_lines": 8,
         "items": {"source": "memo", "key": "items"}},
        # 底部统计 + accent 装饰线
        {"type": "text", "x": 200, "y": 528, "w": 0, "h": 16, "size": 11,
         "align": "center", "baseline": "middle", "color": {"scheme": "sub2"},
         "value": {"source": "memo", "key": "summary"}},
        {"type": "line", "x": 176, "y": 564, "w": 48, "h": 0, "width": 2, "color": {"scheme": "accent"}},
    ],
    **_params(title="今日备忘", items=["[x] 记得喝水", "买菜：牛奶、鸡蛋", "[ ] 晚上 8 点会议"], scheme=0),
}

# ==================== 每日一言（完全还原小程序 frame/quote：引号 + 居中正文 + 作者 + 电量图标 + 日期） ====================
_QUOTE = {
    "kind": "quote",
    "schemes": _QUOTE_SCHEMES,
    "params": [
        {"key": "scheme", "label": "配色方案", "type": "scheme", "default": 0},
    ],
    "background": {"color": "#ffffff"},
    "layers": [
        # 装饰引号 “（左上角 80px serif accent）
        {"type": "text", "x": 5, "y": 50, "w": 0, "h": 0, "size": 80,
         "font": "serif", "baseline": "middle", "color": {"scheme": "accent"}, "value": "“"},
        # 装饰线 (45,100)-(100,100) accent 3px
        {"type": "line", "x": 45, "y": 100, "w": 55, "h": 0, "width": 3, "color": {"scheme": "accent"}},
        # 正文（bold 30px serif 居中，行高 44，按 320 宽自动换行，垂直居中于 100~470）
        {"type": "text", "x": 40, "y": 100, "w": 320, "h": 370, "size": 30, "weight": "bold",
         "align": "center", "baseline": "middle", "font": "serif", "line_height": 44,
         "wrap_width": 320, "color": {"scheme": "text"},
         "value": {"source": "quote", "key": "text"}},
        # 作者（bold 18px serif 居中底部）
        {"type": "text", "x": 200, "y": 470, "w": 0, "h": 0, "size": 18, "weight": "bold",
         "align": "center", "baseline": "middle", "font": "serif", "color": {"scheme": "text"},
         "value": {"source": "quote", "key": "author"}},
        # 底部装饰线 (170,535)-(230,535) accent 4px
        {"type": "line", "x": 170, "y": 535, "w": 60, "h": 0, "width": 4, "color": {"scheme": "accent"}},
        # 日期（居中底部，accent，2026 年 08 月 08 日）
        {"type": "text", "x": 200, "y": 562, "w": 0, "h": 0, "size": 16,
         "align": "center", "baseline": "middle", "color": {"scheme": "accent"},
         "value": {"source": "quote", "key": "date_cn"}},
    ],
    **_params(scheme=0),
}

# ==================== 老黄历（传统手撕黄历风） ====================
_ALMANAC = {
    "kind": "fortune",
    "background": {"color": "#ffffff"},
    "layers": [
        # === 双线外框 ===
        {"type": "rect", "x": 14, "y": 14, "w": 372, "h": 572, "fill": "#ffffff", "color": "#000000", "width": 2},
        {"type": "rect", "x": 20, "y": 20, "w": 360, "h": 560, "fill": None, "color": "#000000", "width": 1},
        # 四角小装饰（L 形短线）
        {"type": "line", "x": 28, "y": 28, "w": 0, "h": 14, "color": "#000000", "width": 2},
        {"type": "line", "x": 28, "y": 28, "w": 14, "h": 0, "color": "#000000", "width": 2},
        {"type": "line", "x": 372, "y": 28, "w": 0, "h": 14, "color": "#000000", "width": 2},
        {"type": "line", "x": 358, "y": 28, "w": 14, "h": 0, "color": "#000000", "width": 2},
        {"type": "line", "x": 28, "y": 572, "w": 0, "h": -14, "color": "#000000", "width": 2},
        {"type": "line", "x": 28, "y": 572, "w": 14, "h": 0, "color": "#000000", "width": 2},
        {"type": "line", "x": 372, "y": 572, "w": 0, "h": -14, "color": "#000000", "width": 2},
        {"type": "line", "x": 358, "y": 572, "w": 14, "h": 0, "color": "#000000", "width": 2},

        # === 顶部标题区 ===
        {"type": "text", "x": 200, "y": 40, "size": 10, "font": "serif", "color": "#c8302a",
         "align": "center", "value": "CHINESE  TRADITIONAL  ALMANAC"},
        {"type": "text", "x": 200, "y": 60, "size": 52, "weight": "bold", "font": "serif",
         "color": "#000000", "align": "center", "value": "老黄历"},
        # 标题下方双线（与标题底部间距 8px）
        {"type": "line", "x": 96, "y": 122, "w": 208, "h": 0, "color": "#000000", "width": 1},
        {"type": "line", "x": 96, "y": 126, "w": 208, "h": 0, "color": "#000000", "width": 1},

        # 年柱（居中）
        {"type": "text", "x": 200, "y": 140, "size": 14, "font": "serif", "color": "#000000",
         "align": "center",
         "value": {"source": "fortune", "key": "ganzhi_year"}},
        {"type": "text", "x": 200, "y": 160, "size": 12, "font": "serif", "color": "#000000",
         "align": "center",
         "value": {"source": "fortune", "key": "year_nayin"}},
        # 右上角月相
        {"type": "moon", "x": 324, "y": 134, "w": 28, "h": 28, "color": "#000000", "bg": "#ffffff",
         "value": {"source": "fortune", "key": "moon_phase"}},
        {"type": "text", "x": 338, "y": 172, "size": 11, "weight": "bold", "font": "serif", "color": "#000000",
         "align": "center", "value": {"source": "fortune", "key": "moon_name"}},

        # === 主体左列：公历年月日 ===
        {"type": "text", "x": 48, "y": 188, "size": 12, "font": "serif", "color": "#000000",
         "value": {"source": "fortune", "key": "year_full"}},
        {"type": "text", "x": 48, "y": 208, "size": 22, "weight": "bold", "font": "serif",
         "color": "#c8302a", "value": {"source": "fortune", "key": "month_en"}},
        {"type": "text", "x": 48, "y": 240, "size": 12, "font": "serif", "color": "#000000",
         "value": {"source": "fortune", "key": "weekday"}},
        # 大日期阿拉伯数字（竖中居中，左对齐）
        {"type": "text", "x": 48, "y": 268, "w": 120, "h": 130, "size": 110, "weight": "bold",
         "font": "serif", "color": "#000000", "align": "left", "baseline": "middle",
         "value": {"source": "fortune", "key": "day_big"}},

        # === 主体右列：农历日期信息 ===
        # 农历月 + 建星（同一行，月左、建星右）
        {"type": "text", "x": 188, "y": 208, "size": 20, "weight": "bold", "font": "serif",
         "color": "#000000", "value": {"source": "fortune", "key": "lunar_month"}},
        {"type": "text", "x": 188, "y": 212, "w": 168, "size": 12, "weight": "bold", "font": "serif",
         "color": "#c8302a", "align": "right",
         "value": {"source": "fortune", "key": "jianshen"}},
        {"type": "text", "x": 188, "y": 240, "size": 44, "weight": "bold", "font": "serif",
         "color": "#c8302a", "value": {"source": "fortune", "key": "lunar_day"}},
        # 节气/节日红字
        {"type": "text", "x": 188, "y": 298, "size": 13, "weight": "bold", "font": "serif",
         "color": "#c8302a", "value": {"source": "fortune", "key": "label"}},
        {"type": "text", "x": 188, "y": 324, "size": 26, "weight": "bold", "font": "serif",
         "color": "#000000", "value": {"source": "fortune", "key": "ganzhi_day"}},
        {"type": "text", "x": 188, "y": 362, "size": 12, "font": "serif", "color": "#000000",
         "value": {"source": "fortune", "key": "day_nayin"}},
        {"type": "text", "x": 188, "y": 382, "size": 11, "font": "serif", "color": "#000000",
         "value": {"source": "fortune", "key": "wuxing"}},
        {"type": "text", "x": 188, "y": 400, "size": 11, "font": "serif", "color": "#000000",
         "value": {"source": "fortune", "key": "chongsha"}},

        # === 中部分隔双线 ===
        {"type": "line", "x": 40, "y": 428, "w": 320, "h": 0, "color": "#000000", "width": 1},
        {"type": "line", "x": 40, "y": 432, "w": 320, "h": 0, "color": "#000000", "width": 1},

        # === 宜忌区（竖线居中分隔） ===
        {"type": "line", "x": 204, "y": 450, "w": 0, "h": 96, "color": "#000000", "width": 1},
        # 宜字（左列垂直居中）
        {"type": "text", "x": 52, "y": 458, "w": 46, "h": 64, "size": 36, "weight": "bold",
         "font": "serif", "color": "#c8302a", "align": "center", "baseline": "middle", "value": "宜"},
        # 宜事项（与"宜"字垂直居中对齐）
        {"type": "text", "x": 108, "y": 450, "w": 88, "h": 80, "size": 14, "font": "serif",
         "color": "#000000", "line_height": 24, "wrap_width": 88, "baseline": "middle",
         "value": {"source": "fortune", "key": "yi"}},
        # 忌字（右列垂直居中）
        {"type": "text", "x": 222, "y": 458, "w": 46, "h": 64, "size": 36, "weight": "bold",
         "font": "serif", "color": "#000000", "align": "center", "baseline": "middle", "value": "忌"},
        # 忌事项（与"忌"字垂直居中对齐）
        {"type": "text", "x": 278, "y": 450, "w": 88, "h": 80, "size": 14, "font": "serif",
         "color": "#000000", "line_height": 24, "wrap_width": 88, "baseline": "middle",
         "value": {"source": "fortune", "key": "ji"}},

        # === 底部 ===
        {"type": "text", "x": 200, "y": 558, "size": 10, "font": "serif", "color": "#000000",
         "align": "center", "value": "传统民俗 · 择吉参考"},
    ],
    "params": [
        {"key": "fixed_date", "label": "日期（留空为今日）", "type": "date", "default": ""},
    ],
    **_params(fixed_date=""),
}

BUILTIN_TEMPLATES = [
    {"name": "日历", "kind": "calendar", "definition": _CALENDAR, "render_config": _DEFAULT_RC},
    {"name": "相册", "kind": "album", "definition": _ALBUM, "render_config": _DEFAULT_RC},
    {"name": "备忘录", "kind": "memo", "definition": _MEMO, "render_config": _DEFAULT_RC},
    {"name": "每日一言", "kind": "quote", "definition": _QUOTE, "render_config": _DEFAULT_RC},
    {"name": "老黄历", "kind": "fortune", "definition": _ALMANAC, "render_config": _DEFAULT_RC},
]


def builtin_definitions() -> list[dict]:
    return BUILTIN_TEMPLATES
