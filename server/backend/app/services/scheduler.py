"""轮播流调度

时间轴模型（v1）：
- 绝对条目（schedule_type=absolute）为锚点，在指定时刻生效，显示至下一绝对锚点
- 相对条目（relative）在锚点之间的间隙按 duration_sec 顺延
- 每日 00:00 重置；超出时间轴末尾时保持最后一条
"""
import datetime as dt
import json

from sqlalchemy.orm import Session

from ..config import SCREENS
from ..models import PushRecord, Stream, Template


def active_stream(db: Session) -> Stream | None:
    """当前生效的轮播流：取启用的第一个"""
    return db.query(Stream).filter(Stream.enabled.is_(True)).order_by(Stream.id).first()


def _to_secs(t: dt.time) -> int:
    return t.hour * 3600 + t.minute * 60 + t.second


def build_timeline(stream: Stream, now: dt.datetime):
    """构建当日时间轴：[(start_sec, duration_sec, StreamItem)]"""
    items = [i for i in stream.items if i.enabled]
    items.sort(key=lambda i: i.position)
    if not items:
        return []

    end_of_day = 24 * 3600
    abs_starts = sorted(
        _to_secs(it.start_at.time()) for it in items
        if it.schedule_type == "absolute" and it.start_at
    )

    segments = []
    cursor = 0
    for it in items:
        if it.schedule_type == "absolute" and it.start_at:
            start = _to_secs(it.start_at.time())
            nxt = next((s for s in abs_starts if s > start), None)
            duration = (nxt - start) if nxt is not None else (end_of_day - start)
            segments.append((start, duration, it))
            cursor = start + duration
        else:
            start = cursor
            # 最后一个 absolute 锚点后相对条目已跨日：当日不再排程（不产生不可达段）
            if start >= end_of_day:
                continue
            duration = max(1, it.duration_sec or 30)
            nxt = next((s for s in abs_starts if s > start), None)
            if nxt is not None and start + duration > nxt:
                duration = max(1, nxt - start)
            segments.append((start, duration, it))
            cursor = start + duration
    return segments


def current_item(stream: Stream, now: dt.datetime):
    """返回 (当前 StreamItem, 已显示秒数)；无当前内容返回 (None, None)"""
    segments = build_timeline(stream, now)
    if not segments:
        return None, None
    t = now.hour * 3600 + now.minute * 60 + now.second
    for start, duration, it in segments:
        if start <= t < start + duration:
            return it, t - start
    if t < segments[0][0]:
        return None, None  # 首条绝对锚点尚未到
    return segments[-1][2], max(0, t - segments[-1][0])  # 超出末尾保持最后一条


def resolve_film_for_device(db: Session, device, now: dt.datetime | None = None):
    """计算设备当前应显示的轮播内容，渲染并转换为 film

    返回 (film_bytes, StreamItem|None, bytes|None preview)；无内容返回 (None, None, None)
    """
    from . import film_convert, renderer

    now = now or dt.datetime.now()
    stream = active_stream(db)
    if not stream:
        return None, None, None
    if stream.mode == "device_pull":
        item, elapsed = advance_pull(db, stream, device, now)
    else:
        item, elapsed = current_item(stream, now)
    if not item:
        return None, None, None
    template = db.get(Template, item.template_id)
    if not template:
        return None, None, None

    scr = SCREENS.get(device.device_type, SCREENS["basic"])
    rc = json.loads(template.render_config or "{}")
    img = renderer.render_template(template, scr["canvas_w"], scr["canvas_h"], now, db=db)
    film, preview = film_convert.convert_for_device(img, device.device_type, rc)
    return film, item, preview


def should_push(db: Session, device, now: dt.datetime | None = None) -> tuple[bool, object | None]:
    """服务端主动推送判定：当前条目与上次推送不同则需下发 download_film"""
    now = now or dt.datetime.now()
    stream = active_stream(db)
    if not stream or stream.mode != "server_push":
        return False, None
    item, elapsed = current_item(stream, now)
    if not item:
        return False, None
    last = (
        db.query(PushRecord)
        .filter(PushRecord.device_id == device.id, PushRecord.method == "push")
        .order_by(PushRecord.id.desc())
        .first()
    )
    if last is None or last.stream_item_id != item.id:
        return True, item
    return False, item


def _last_pull_record(db: Session, device_id: int) -> PushRecord | None:
    return (
        db.query(PushRecord)
        .filter(PushRecord.device_id == device_id, PushRecord.method == "pull")
        .order_by(PushRecord.id.desc())
        .first()
    )


def advance_pull(db: Session, stream: Stream, device, now: dt.datetime):
    """设备主动拉取（device_pull）模式的消费推进

    - 绝对条目：到点（start_at <= now < start_at + duration）优先显示
    - 相对条目：每次拉取视为一次消费，按 position 顺序循环推进
    返回 (StreamItem, elapsed_sec)
    """
    segments = build_timeline(stream, now)
    if not segments:
        return None, None
    t = now.hour * 3600 + now.minute * 60 + now.second
    last = _last_pull_record(db, device.id)

    # 1. 绝对条目到点优先
    for s, d, it in segments:
        if it.schedule_type == "absolute" and s <= t < s + d:
            return it, t - s

    # 2. 相对条目顺序循环推进
    if last is None:
        return segments[0][2], 0
    for i, (_, _, it) in enumerate(segments):
        if it.id == last.stream_item_id:
            return segments[(i + 1) % len(segments)][2], 0
    return segments[0][2], 0


def record_push(db: Session, device, item, film_path: str, method: str):
    db.add(PushRecord(device_id=device.id, stream_item_id=item.id if item else None,
                      film_path=film_path, method=method))
    # 控制 push_records 表膨胀：每设备仅保留最近 1000 条
    over = (
        db.query(PushRecord.id)
        .filter(PushRecord.device_id == device.id)
        .order_by(PushRecord.id.desc())
        .offset(1000)
        .all()
    )
    if over:
        db.query(PushRecord).filter(PushRecord.id.in_([r[0] for r in over])).delete(
            synchronize_session=False)
    db.commit()
