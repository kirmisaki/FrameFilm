"""模板数据源

为模板渲染提供各类动态数据。日历含农历/节气/节日（移植自小程序 tpl-calendar.js），
支持应用态参数（跟随当前时间 / 固定月份 / 配色方案）。天气等外部数据源未配置时返回占位。
老黄历含干支纪年纪日 + 生肖 + 传统宜忌（基于日地支选择）。
"""
import calendar
import datetime as dt

import httpx

QUOTES = [
    "生活不止眼前的苟且，还有诗和远方。",
    "每一次努力，都是幸运的伏笔。",
    "山高路远，看世界，也找自己。",
    "保持热爱，奔赴山海。",
    "慢慢来，比较快。",
    "心里有光，慢食三餐。",
    "今天也要元气满满！",
    "心之所向，素履以往。",
    "把日子过成诗。",
    "知足常乐，随遇而安。",
    "热爱可抵岁月漫长。",
    "万物皆可期。",
]

MONTH_EN = ["JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"]
MONTH_EN_FULL = ["January", "February", "March", "April", "May", "June",
                 "July", "August", "September", "October", "November", "December"]
WEEKDAYS = ["一", "二", "三", "四", "五", "六", "日"]
_WEEKDAYS_FULL = ["星期一", "星期二", "星期三", "星期四", "星期五", "星期六", "星期日"]

_TIANGAN = ["甲", "乙", "丙", "丁", "戊", "己", "庚", "辛", "壬", "癸"]
_DIZHI   = ["子", "丑", "寅", "卯", "辰", "巳", "午", "未", "申", "酉", "戌", "亥"]
_SHENGXIAO = ["鼠", "牛", "虎", "兔", "龙", "蛇", "马", "羊", "猴", "鸡", "狗", "猪"]
_WUXING_TG = {"甲":"木","乙":"木","丙":"火","丁":"火","戊":"土","己":"土","庚":"金","辛":"金","壬":"水","癸":"水"}
_WUXING_DZ = {"子":"水","丑":"土","寅":"木","卯":"木","辰":"土","巳":"火","午":"火","未":"土","申":"金","酉":"金","戌":"土","亥":"水"}
# 建除十二神（按月建地支起"建"）
_JIANCHU = ["建","除","满","平","定","执","破","危","成","收","开","闭"]
# 六十甲子纳音（索引 = tg*6 + dz//2，每对干支共用一个纳音）
_NAYIN = [
    "海中金","海中金","炉中火","炉中火","大林木","大林木",
    "路旁土","路旁土","剑锋金","剑锋金","山头火","山头火",
    "涧下水","涧下水","城头土","城头土","白蜡金","白蜡金",
    "杨柳木","杨柳木","泉中水","泉中水","屋上土","屋上土",
    "霹雳火","霹雳火","松柏木","松柏木","长流水","长流水",
    "沙中金","沙中金","山下火","山下火","平地木","平地木",
    "壁上土","壁上土","金箔金","金箔金","覆灯火","覆灯火",
    "天河水","天河水","大驿土","大驿土","钗钏金","钗钏金",
    "桑柘木","桑柘木","大溪水","大溪水","沙中土","沙中土",
    "天上火","天上火","石榴木","石榴木","大海水","大海水",
]

# ==================== 农历（1900-2100，移植自小程序 tpl-calendar.js） ====================
_LUNAR_INFO = [
    0x04bd8, 0x04ae0, 0x0a570, 0x054d5, 0x0d260, 0x0d950, 0x16554, 0x056a0, 0x09ad0, 0x055d2,
    0x04ae0, 0x0a5b6, 0x0a4d0, 0x0d250, 0x1d255, 0x0b540, 0x0d6a0, 0x0ada2, 0x095b0, 0x14977,
    0x04970, 0x0a4b0, 0x0b4b5, 0x06a50, 0x06d40, 0x1ab54, 0x02b60, 0x09570, 0x052f2, 0x04970,
    0x06566, 0x0d4a0, 0x0ea50, 0x06e95, 0x05ad0, 0x02b60, 0x186e3, 0x092e0, 0x1c8d7, 0x0c950,
    0x0d4a0, 0x1d8a6, 0x0b550, 0x056a0, 0x1a5b4, 0x025d0, 0x092d0, 0x0d2b2, 0x0a950, 0x0b557,
    0x06ca0, 0x0b550, 0x15355, 0x04da0, 0x0a5b0, 0x14573, 0x052b0, 0x0a9a8, 0x0e950, 0x06aa0,
    0x0aea6, 0x0ab50, 0x04b60, 0x0aae4, 0x0a570, 0x05260, 0x0f263, 0x0d950, 0x05b57, 0x056a0,
    0x096d0, 0x04dd5, 0x04ad0, 0x0a4d0, 0x0d4d4, 0x0d250, 0x0d558, 0x0b540, 0x0b5a0, 0x195a6,
    0x095b0, 0x049b0, 0x0a974, 0x0a4b0, 0x0b27a, 0x06a50, 0x06d40, 0x0af46, 0x0ab60, 0x09570,
    0x04af5, 0x04970, 0x064b0, 0x074a3, 0x0ea50, 0x06b58, 0x055c0, 0x0ab60, 0x096d5, 0x092e0,
    0x0c960, 0x0d954, 0x0d4a0, 0x0da50, 0x07552, 0x056a0, 0x0abb7, 0x025d0, 0x092d0, 0x0cab5,
    0x0a950, 0x0b4a0, 0x0baa4, 0x0ad50, 0x055d9, 0x04ba0, 0x0a5b0, 0x15176, 0x052b0, 0x0a930,
    0x07954, 0x06aa0, 0x0ad50, 0x05b52, 0x04b60, 0x0a6e6, 0x0a4e0, 0x0d260, 0x0ea65, 0x0d530,
    0x05aa0, 0x076a3, 0x096d0, 0x04afb, 0x04ad0, 0x0a4d0, 0x1d0b6, 0x0d250, 0x0d520, 0x0dd45,
    0x0b5a0, 0x056d0, 0x055b2, 0x049b0, 0x0a577, 0x0a4b0, 0x0aa50, 0x1b255, 0x06d20, 0x0ada0,
    0x14b63, 0x09370, 0x049f8, 0x04970, 0x064b0, 0x168a6, 0x0ea50, 0x06b20, 0x1a6c4, 0x0aae0,
    0x092e0, 0x0d2e3, 0x0c960, 0x0d557, 0x0d4a0, 0x0da50, 0x05d55, 0x056a0, 0x0a6d0, 0x055d4,
    0x052d0, 0x0a9b8, 0x0a950, 0x0b4a0, 0x0b6a6, 0x0ad50, 0x055a0, 0x0aba4, 0x0a5b0, 0x052b0,
    0x0b273, 0x06930, 0x07337, 0x06aa0, 0x0ad50, 0x14b55, 0x04b60, 0x0a570, 0x054e4, 0x0d160,
    0x0e968, 0x0d520, 0x0daa0, 0x16aa6, 0x056d0, 0x04ae0, 0x0a9d4, 0x0a2d0, 0x0d150, 0x0f252,
    0x0d520,
]

_MON_NAMES = ["正", "二", "三", "四", "五", "六", "七", "八", "九", "十", "冬", "腊"]
_DAY_NAMES = ["初一", "初二", "初三", "初四", "初五", "初六", "初七", "初八", "初九", "初十",
              "十一", "十二", "十三", "十四", "十五", "十六", "十七", "十八", "十九", "二十",
              "廿一", "廿二", "廿三", "廿四", "廿五", "廿六", "廿七", "廿八", "廿九", "三十"]

# 节气（公历固定日期近似）
_SOLAR_TERMS = {
    1: {5: "小寒", 20: "大寒"}, 2: {4: "立春", 19: "雨水"}, 3: {6: "惊蛰", 21: "春分"},
    4: {5: "清明", 20: "谷雨"}, 5: {6: "立夏", 21: "小满"}, 6: {6: "芒种", 21: "夏至"},
    7: {7: "小暑", 23: "大暑"}, 8: {7: "立秋", 23: "处暑"}, 9: {8: "白露", 23: "秋分"},
    10: {8: "寒露", 23: "霜降"}, 11: {7: "立冬", 22: "小雪"}, 12: {7: "大雪", 22: "冬至"},
}

# 公历节日
_FESTIVALS = {
    "1-1": "元旦", "2-14": "情人节", "3-8": "妇女节", "4-1": "愚人节",
    "5-1": "劳动节", "5-4": "青年节", "6-1": "儿童节", "7-1": "建党节",
    "8-1": "建军节", "9-10": "教师节", "10-1": "国庆节", "12-24": "平安夜", "12-25": "圣诞节",
}

# 农历节日
_LUNAR_FESTIVALS = {
    (1, 1): "春节", (1, 15): "元宵节", (5, 5): "端午节",
    (7, 7): "七夕", (8, 15): "中秋节", (9, 9): "重阳节", (12, 30): "除夕",
}


def _leap_month(y): return _LUNAR_INFO[y - 1900] & 0xF


def _leap_days(y): return (_leap_month(y) and 30) if (_LUNAR_INFO[y - 1900] & 0x10000) else (_leap_month(y) and 29) or 0


def _month_days(y, m): return 30 if (_LUNAR_INFO[y - 1900] & (0x10000 >> m)) else 29


def _lunar_year_days(y):
    # 12 个月的大月位掩码依次为 0x8000..0x10（对应小程序 i >>= 1）
    s = 348
    info = _LUNAR_INFO[y - 1900]
    for i in range(12):
        if info & (0x8000 >> i):
            s += 1
    return s + _leap_days(y)


def _solar2lunar(y, m, d) -> dict:
    """阳历 -> 农历 {year, month, day, is_leap}"""
    offset = (dt.date(y, m, d) - dt.date(1900, 1, 31)).days
    i, temp = 1900, 0
    while i < 2101 and temp + _lunar_year_days(i) <= offset:
        temp += _lunar_year_days(i)
        i += 1
    ly = i
    leap = _leap_month(ly)
    is_leap = False
    i = 1
    while i < 13 and temp + _month_days(ly, i) <= offset:
        temp += _month_days(ly, i)
        if leap == i:
            if temp + _leap_days(ly) <= offset:
                temp += _leap_days(ly)
            else:
                is_leap = True
                break
        i += 1
    return {"year": ly, "month": i, "day": offset - temp + 1, "is_leap": is_leap}


def _note_for(month: int, day: int) -> str:
    """农历备注：节日/节气/初一「X月」/日名"""
    fest = _LUNAR_FESTIVALS.get((month, day))
    if fest:
        return fest
    if day == 1:
        return f"{_MON_NAMES[month - 1]}月"
    return _DAY_NAMES[day - 1]


def _day_note(y: int, m: int, d: int, lunar: dict) -> str:
    fest = _FESTIVALS.get(f"{m}-{d}")
    if fest:
        return fest
    term = _SOLAR_TERMS.get(m, {}).get(d)
    if term:
        return term
    return _note_for(lunar["month"], lunar["day"])


# ==================== 日历数据（应用态参数化） ====================
def calendar_data(now: dt.datetime | None = None, params: dict | None = None) -> dict:
    """当月/固定月份日历：农历 · 节气 · 节日 · 今日高亮 · 进度

    params:
      month_mode: "current"（跟随当前时间，默认）| "fixed"（固定月份）
      fixed_month: "YYYY-MM"（month_mode=fixed 时生效）
    """
    now = now or dt.datetime.now()
    params = params or {}
    if params.get("month_mode") == "fixed" and params.get("fixed_month"):
        try:
            y, m = (int(x) for x in str(params["fixed_month"]).split("-")[:2])
        except ValueError:
            y, m = now.year, now.month
    else:
        y, m = now.year, now.month

    is_current_month = (y == now.year and m == now.month)
    today = now.day if is_current_month else None
    days_in_month = calendar.monthrange(y, m)[1]
    first_idx = dt.date(y, m, 1).weekday()  # Python weekday() 已是 周一=0（小程序 getDay() 周日=0 才需转换）

    cells = []
    today_lunar = ""
    for i in range(42):
        d = i - first_idx + 1
        if d < 1 or d > days_in_month:
            cells.append(None)
            continue
        lunar = _solar2lunar(y, m, d)
        is_today = (d == today)
        if is_today:
            today_lunar = ("闰" if lunar["is_leap"] else "") + _MON_NAMES[lunar["month"] - 1] + "月" + _DAY_NAMES[lunar["day"] - 1]
        cells.append({
            "day": d,
            "note": _day_note(y, m, d, lunar),
            "is_today": is_today,
            "is_weekend": i % 7 in (5, 6),
        })

    progress = round(today / days_in_month * 100) if today else 0
    weekday = WEEKDAYS[now.weekday()]
    return {
        "year": y, "month": m, "today": today,
        "month_str": str(m),
        "month_full": f"{m}月",
        "month_en_year": f"{MONTH_EN[m - 1]} · {y}",
        "today_lunar": today_lunar,
        "today_lunar_full": f"今日 {today_lunar}" if today_lunar else "",
        "weekday": weekday, "weekday_tip": f"星期{weekday}",
        "title": f"{y}年{m}月",
        "weekdays": "一  二  三  四  五  六  日",
        "cells": cells,
        "cell_columns": 7,
        "progress": progress,
        "remain_days": (days_in_month - today) if today else 0,
        "progress_text": f"本月已过 {progress}% · 距月底还有 {(days_in_month - today) if today else 0} 天",
    }


def _day_index(now: dt.datetime | None = None) -> int:
    now = now or dt.datetime.now()
    return now.toordinal()


def memo_data(definition_data: dict | None, params: dict | None = None) -> dict:
    """备忘录：应用态 params.items 或 data.memo.items 列表。

    条目支持纯文本、"[x] 已完成" / "[ ] 未完成" 前缀，或 dict {text, done}；
    输出 items 统一为 [{text, done}]，供 checklist 渲染微信式勾选清单。
    """
    data = definition_data or {}
    params = params or {}
    if "items" in params:
        items = params.get("items") or []
    else:
        items = data.get("memo", {}).get("items", [])
    parsed = []
    for it in items:
        if isinstance(it, dict):
            parsed.append({"text": str(it.get("text", "")), "done": bool(it.get("done"))})
        else:
            s = str(it)
            if s.startswith("[x]") or s.startswith("[X]"):
                parsed.append({"text": s[3:].lstrip(), "done": True})
            elif s.startswith("[ ]"):
                parsed.append({"text": s[3:].lstrip(), "done": False})
            else:
                parsed.append({"text": s, "done": False})
    now = dt.datetime.now()
    title = params.get("title", data.get("memo", {}).get("title", "备忘"))
    done_n = sum(1 for p in parsed if p["done"])
    wd = WEEKDAYS[now.weekday()]
    return {
        "items": parsed,
        "done_count": done_n,
        "total_count": len(parsed),
        "progress": f"已完成 {done_n}/{len(parsed)}",
        "summary": f"共 {len(parsed)} 条 · 完成 {done_n} 条",
        "text_block": "\n".join(("[x] " if p["done"] else "") + p["text"] for p in parsed),
        "title": title,
        "date": f"{now.year}年{now.month}月{now.day}日",
        "date_dot": f"{now.year}.{now.month:02d}.{now.day:02d}",
        "weekday": wd,
        "date_cn": f"{now.year}年{now.month}月{now.day}日 {wd}",
    }


def _parse_expiry(item) -> tuple:
    """item 为 dict 或 "名称" / "名称|YYYY-MM-DD" 字符串，返回 (name, expiry)"""
    if isinstance(item, dict):
        return item.get("name", "?"), item.get("expiry")
    s = str(item).strip()
    if "|" in s:
        name, _, exp = s.partition("|")
        return name.strip() or "?", exp.strip() or None
    return s or "?", None


def fridge_data(definition_data: dict | None, params: dict | None = None) -> dict:
    """冰箱食材：应用态 params.items 或 data.fridge.items，含保质期提醒（urgent 临期/过期）"""
    data = definition_data or {}
    params = params or {}
    if "items" in params:
        items = params.get("items") or []
    else:
        items = data.get("fridge", {}).get("items", [])
    today = dt.date.today()
    enriched = []
    lines = []
    for it in items:
        name, exp = _parse_expiry(it)
        days = None
        if exp:
            try:
                days = (dt.date.fromisoformat(exp) - today).days
            except ValueError:
                days = None
        item_out = {"name": name, "expiry": exp, "expiry_days": days,
                    "text": name, "sub": "", "urgent": False}
        if days is None:
            item_out["sub"] = "—"
            lines.append(f"· {name}")
        elif days < 0:
            item_out["sub"] = "已过期!"
            item_out["urgent"] = True
            lines.append(f"· {name}  已过期!")
        elif days <= 2:
            item_out["sub"] = f"剩 {days} 天"
            item_out["urgent"] = True
            lines.append(f"· {name}  剩 {days} 天")
        else:
            item_out["sub"] = exp[5:] if exp and len(exp) >= 5 else exp or ""
            lines.append(f"· {name}  {exp}")
        enriched.append(item_out)
    return {"items": enriched, "text_block": "\n".join(lines)}


def countdown_data(definition_data: dict | None, now: dt.datetime | None = None,
                   params: dict | None = None) -> dict:
    """倒计时：应用态 params.title/target 或 data.countdown 目标日期"""
    now = now or dt.datetime.now()
    data = definition_data or {}
    params = params or {}
    cd = data.get("countdown", {}) or {}
    title = params.get("title", cd.get("title", "倒计时"))
    target = params.get("target", cd.get("target"))
    if not target:
        return {"title": title, "days": 0, "passed": False}
    try:
        td = dt.date.fromisoformat(target)
    except ValueError:
        return {"title": title, "days": 0, "passed": False}
    delta = (td - now.date()).days
    pretty = f"{td.year}.{td.month:02d}.{td.day:02d}"
    today_str = f"{now.year}.{now.month:02d}.{now.day:02d}"
    passed = delta < 0
    days = abs(delta)
    if passed:
        hint = "那些日子都值得被记住"
    elif days > 0:
        hint = f"再坚持 {days} 天就到大日子"
    else:
        hint = "就是今天！大日子来啦"
    return {
        "title": title,
        "date": target,
        "date_pretty": pretty,
        "date_label": f"始于 {pretty}" if passed else f"距离 {pretty}",
        "days": days,
        "passed": passed,
        "unit": "已过" if passed else "天",
        "mode_label": "已纪念" if passed else "倒计时中",
        "hint": hint,
        "today_str": today_str,
    }


# 一言 API（与小程序 pages/frame/quote 同源；拉取后缓存 10 分钟，避免每次渲染阻塞外部请求）
_HITOKOTO_API = "https://international.v1.hitokoto.cn/?c=d&c=h&c=k&c=i&encode=json"
_QUOTE_CACHE_TTL = 10 * 60

_quote_cache: dict = {"text": "", "author": "", "ts": 0.0}


def _fetch_quote() -> tuple:
    """拉取一条新一言：{text, author}；带 TTL 缓存，异常时回退内置语录"""
    import time

    now = time.monotonic()
    if _quote_cache["text"] and now - _quote_cache["ts"] < _QUOTE_CACHE_TTL:
        return _quote_cache["text"], _quote_cache["author"]
    text, author = "", ""
    try:
        resp = httpx.get(_HITOKOTO_API, timeout=2)
        data = resp.json()
        text = (data.get("hitokoto") or "").strip()
        author = (data.get("from_who") or data.get("from") or "").strip()
    except Exception:
        pass
    if not text:
        text = QUOTES[_day_index() % len(QUOTES)]
    _quote_cache.update(text=text, author=author, ts=now)
    return text, author


def quote_data(now: dt.datetime | None = None, params: dict | None = None) -> dict:
    now = now or dt.datetime.now()
    text, author = _fetch_quote()
    return {
        "text": text,
        "author": f"—— {author}" if author else "",
        "date": f"{now.year}.{now.month:02d}.{now.day:02d}",
        "date_cn": f"{now.year} 年 {now.month:02d} 月 {now.day:02d} 日",
    }


# ==================== 干支 / 生肖 / 纳音 / 老黄历 ====================
def _ganzhi_index(tg: int, dz: int) -> int:
    """由天干(0-9)地支(0-11)索引算六十甲子序号(0-59)；满足 n%10=tg, n%12=dz"""
    for n in range(60):
        if n % 10 == tg and n % 12 == dz:
            return n
    return 0


def _nayin(tg: int, dz: int) -> str:
    return _NAYIN[_ganzhi_index(tg, dz)]


def _ganzhi_year(year: int) -> dict:
    """干支纪年 + 生肖 + 纳音"""
    tg = (year - 4) % 10
    dz = (year - 4) % 12
    return {
        "gan": _TIANGAN[tg], "zhi": _DIZHI[dz], "shengxiao": _SHENGXIAO[dz],
        "full": f"{_TIANGAN[tg]}{_DIZHI[dz]}年",
        "nayin": _nayin(tg, dz),
        "wuxing": f"{_WUXING_TG[_TIANGAN[tg]]}{_WUXING_DZ[_DIZHI[dz]]}",
    }


def _ganzhi_day(y: int, m: int, d: int) -> dict:
    """干支纪日（1900-01-01 = 甲戌）+ 纳音 + 五行 + 冲煞"""
    base = dt.date(1900, 1, 1)
    delta = (dt.date(y, m, d) - base).days
    idx = (10 + delta) % 60  # 甲戌 = 10 (0 = 甲子)
    tg = idx % 10
    dz = idx % 12
    chong_dz = (dz + 6) % 12  # 六冲
    return {
        "gan": _TIANGAN[tg], "zhi": _DIZHI[dz],
        "full": f"{_TIANGAN[tg]}{_DIZHI[dz]}日",
        "nayin": _nayin(tg, dz),
        "wuxing": f"{_WUXING_TG[_TIANGAN[tg]]}{_WUXING_DZ[_DIZHI[dz]]}",
        "chong": f"冲{_SHENGXIAO[chong_dz]}({_TIANGAN[(tg+6)%10]}{_DIZHI[chong_dz]})",
        "sha": ["煞东","煞西","煞南","煞北"][dz % 4],
        "idx": idx,
    }


def _moon_phase(lunar_day: int) -> dict:
    """月相（按农历日）：0新月/1娥眉/2上弦/3盈凸/4满月/5亏凸/6下弦/7残月"""
    d = lunar_day
    if d <= 2 or d >= 29:
        return {"phase": 0, "name": "朔月"}
    if d <= 6:
        return {"phase": 1, "name": "娥眉月"}
    if d <= 9:
        return {"phase": 2, "name": "上弦月"}
    if d <= 13:
        return {"phase": 3, "name": "盈凸月"}
    if d <= 16:
        return {"phase": 4, "name": "满月"}
    if d <= 20:
        return {"phase": 5, "name": "亏凸月"}
    if d <= 23:
        return {"phase": 6, "name": "下弦月"}
    return {"phase": 7, "name": "残月"}


def _chinese_num(n: int) -> str:
    """1-31 转中文数字（日期用）"""
    CN = ["〇","一","二","三","四","五","六","七","八","九"]
    if n < 10:
        return CN[n] if n != 0 else "〇"
    if n == 10:
        return "初十"
    if n < 20:
        return "十" + CN[n - 10]
    if n == 20:
        return "二十"
    if n < 30:
        return "廿" + CN[n - 20]
    if n == 30:
        return "三十"
    return "卅一"


# 传统宜忌（按日地支 0-11，每日池，随机选 4-5 项）
_YI_POOL = [
    ["祭祀","祈福","求嗣","开光","塑绘","斋醮","订盟","纳采","嫁娶"],          # 子
    ["出行","纳财","开市","交易","立券","裁衣","造仓","开渠","安碓硙"],      # 丑
    ["开市","交易","立券","纳财","纳畜","造仓","造车器","祭祀","祈福"],      # 寅
    ["入宅","安床","开光","祭祀","祈福","求嗣","斋醮","纳采","订盟"],        # 卯
    ["嫁娶","纳采","订盟","祭祀","祈福","求嗣","开光","出行","解除"],        # 辰
    ["修造","动土","上梁","竖柱","起基","安门","安床","造仓","开厕"],        # 巳
    ["祈福","求嗣","开光","出行","解除","伐木","拆卸","修造","动土"],        # 午
    ["移徙","出行","安床","开光","祈福","求嗣","斋醮","纳采","订盟"],        # 未
    ["祭祀","祈福","求嗣","开光","入学","沐浴","剃头","整手足甲","纳畜"],    # 申
    ["安葬","立碑","破土","启钻","移柩","修坟","祭祀","祈福","斋醮"],        # 酉
    ["开市","纳财","交易","立券","纳畜","造仓","造车器","祭祀","开光"],      # 戌
    ["嫁娶","入宅","出行","移徙","安床","开光","祈福","求嗣","斋醮"],        # 亥
]
_JI_POOL = [
    ["动土","破土","开仓","出货财","安葬","行丧","伐木","作梁"],
    ["安葬","行丧","伐木","作梁","开仓","嫁娶","移徙","入宅"],
    ["入宅","移徙","嫁娶","动土","破土","安葬","行丧","赴任"],
    ["开渠","穿井","掘井","行丧","安葬","动土","破土","伐木"],
    ["词讼","行丧","安葬","伐木","作梁","开仓","出货财","赴任"],
    ["出行","嫁娶","移徙","入宅","开市","安葬","行丧","词讼"],
    ["破土","安葬","行丧","嫁娶","移徙","入宅","开仓","出货财"],
    ["嫁娶","开市","破土","安葬","行丧","动土","开仓","赴任"],
    ["动土","远行","出行","嫁娶","移徙","入宅","开市","交易"],
    ["嫁娶","开市","入宅","移徙","动土","破土","开仓","出货财"],
    ["词讼","出行","移徙","入宅","嫁娶","破土","安葬","行丧"],
    ["安葬","伐木","作梁","动土","破土","开仓","出货财","行丧"],
]


def _pick_yi_ji(seed: int, pool: list, n: int) -> str:
    """按 seed 确定性从 pool 里挑 n 项（同一天结果一致），用空格连接"""
    items = list(pool)
    # Fisher-Yates 前 n 步洗牌
    s = (seed * 9301 + 49297) % 233280
    picked = []
    for i in range(min(n, len(items))):
        s = (s * 9301 + 49297) % 233280
        j = i + s % (len(items) - i)
        items[i], items[j] = items[j], items[i]
        picked.append(items[i])
    return "  ".join(picked)


def fortune_data(now: dt.datetime | None = None, params: dict | None = None) -> dict:
    """老黄历：公历/农历 + 干支纪年纪日 + 生肖/纳音/五行/冲煞/建星/月相/宜忌
    params:
      date_mode: "current"（跟随当前时间，默认）| "fixed"（固定日期）
      fixed_date: "YYYY-MM-DD"（date_mode=fixed 时生效）
    """
    now = now or dt.datetime.now()
    params = params or {}
    fd = str(params.get("fixed_date") or "").strip()
    if fd:
        try:
            y, m, d = (int(x) for x in fd.split("-")[:3])
            target = dt.datetime(y, m, d, 12, 0, 0)
        except (ValueError, TypeError):
            y, m, d, target = now.year, now.month, now.day, now
    else:
        y, m, d, target = now.year, now.month, now.day, now

    lunar = _solar2lunar(y, m, d)
    gyear = _ganzhi_year(lunar["year"])
    gday = _ganzhi_day(y, m, d)

    dz_idx = _DIZHI.index(gday["zhi"])
    seed = gday["idx"] + y * 60
    yi = _pick_yi_ji(seed, _YI_POOL[dz_idx], 4)
    ji = _pick_yi_ji(seed + 7, _JI_POOL[dz_idx], 4)

    # 节气/节日标签（有则显示，无则空）
    term = _SOLAR_TERMS.get(m, {}).get(d)
    festival = _FESTIVALS.get(f"{m}-{d}") or _LUNAR_FESTIVALS.get((lunar["month"], lunar["day"]))
    label = term or festival or ""
    # 当日建星（每日都有）
    lun_note = _note_for(lunar["month"], lunar["day"])

    # 农历月份对应的建星（正月建寅，子月起建=建除...）
    month_branch_month1 = 2  # 正月=寅(索引2)
    month_dz = (lunar["month"] - 1 + month_branch_month1) % 12
    jian_idx = (dz_idx - month_dz) % 12
    jianshen = _JIANCHU[jian_idx]

    moon = _moon_phase(lunar["day"])

    lunar_str = ("闰" if lunar["is_leap"] else "") + _MON_NAMES[lunar["month"] - 1] + "月" + _DAY_NAMES[lunar["day"] - 1]

    return {
        "solar": f"{y}年{m}月{d}日",
        "solar_dot": f"{y}.{m:02d}.{d:02d}",
        "lunar": lunar_str,
        "lunar_month": ("闰" if lunar["is_leap"] else "") + _MON_NAMES[lunar["month"] - 1] + "月",
        "lunar_day": _DAY_NAMES[lunar["day"] - 1],
        "day_big": f"{d:02d}",
        "day_cn": _chinese_num(d),
        "month_en": MONTH_EN_FULL[m - 1].upper(),
        "month_num": m,
        "year_full": str(y),
        "label": label,
        "ganzhi_year": gyear["full"],
        "ganzhi_day": gday["full"],
        "shengxiao": gyear["shengxiao"],
        "year_nayin": gyear["nayin"],
        "day_nayin": gday["nayin"],
        "wuxing": gday["wuxing"],
        "chongsha": f"{gday['chong']} {gday['sha']}",
        "jianshen": jianshen + "日",
        "moon_phase": moon["phase"],
        "moon_name": moon["name"],
        "yi": yi,
        "ji": ji,
        "date": f"{y}.{m:02d}.{d:02d}",
        "weekday": f"星期{WEEKDAYS[target.weekday()]}",
        "weekday_full": _WEEKDAYS_FULL[target.weekday()],
    }


def weather_data(now: dt.datetime | None = None) -> dict:
    """天气：v1 占位，外部 API 后续接入"""
    now = now or dt.datetime.now()
    return {"configured": False, "text": "天气未配置", "temp": "--", "icon": "?",
            "date": f"{now.year}.{now.month:02d}.{now.day:02d}"}


def resolve(kind: str, definition_data: dict | None, now: dt.datetime | None = None) -> dict:
    """按模板 kind 解析数据源；definition.data.params 为应用态参数。

    返回 {kind: {...}} 包装结构，渲染器按 source 名取对应数据（text 图层用
    data.get("calendar") 等；calendar_grid/table 用解包后的当前数据）。
    """
    data = definition_data or {}
    params = data.get("params") or {}
    if kind == "calendar":
        result = calendar_data(now, params)
    elif kind == "memo":
        result = memo_data(data, params)
    elif kind == "fridge":
        result = fridge_data(data, params)
    elif kind == "countdown":
        result = countdown_data(data, now, params)
    elif kind == "quote":
        result = quote_data(now, params)
    elif kind == "fortune":
        result = fortune_data(now, params)
    elif kind == "weather":
        result = weather_data()
    else:
        result = {}
    return {kind: result}
