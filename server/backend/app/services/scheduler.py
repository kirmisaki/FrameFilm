"""轮播流调度

事件触发模型（v2）：
- 循环条目（relative）：自 00:00 起每 duration_sec 秒触发一次内容更新
- 定点条目（absolute）：每天 start_at 触发一次内容更新
- 冲突仲裁：同一时刻多条目到期 → 定点 > 循环；循环间周期长优先；同级取最新触发点
- 限流：两次推送间隔不小于 MIN_PUSH_INTERVAL（30s），不足则顺延至下个心跳周期
"""
import datetime as dt
import json

from sqlalchemy.orm import Session

from ..config import SCREENS
from ..models import PushRecord, Stream, Template

# 两次推送的最小间隔（秒）：到期事件与上次推送不足该间隔时顺延
MIN_PUSH_INTERVAL = 30


def active_stream(db: Session) -> Stream | None:
    """全局生效的轮播流：取启用的第一个（未绑定设备的回退项）"""
    return db.query(Stream).filter(Stream.enabled.is_(True)).order_by(Stream.id).first()


def active_stream_for_device(db: Session, device) -> Stream | None:
    """设备生效的轮播流：设备绑定的流（且启用）优先，未绑定/失效则回退全局第一个启用流"""
    if device and device.play_stream_id:
        s = db.get(Stream, device.play_stream_id)
        if s and s.enabled:
            return s
    return active_stream(db)


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


def _next_push_item(db: Session, stream: Stream, device, now: dt.datetime,
                    check_interval: bool = True):
    """事件触发模型：返回 (应推送条目, 触发时刻)；无到期事件返回 (None, None)

    - 循环（relative）：自 00:00 起每 duration_sec 秒一个触发点
    - 定点（absolute）：每天 start_at 一个触发点
    - 仲裁：定点 > 循环；循环间周期长优先；同周期多条目到期选"最久未推"的公平轮转
    - 限流：距上次推送不足 MIN_PUSH_INTERVAL 则顺延（本次不推，等下个心跳）
      check_interval=False 时跳过限流（仅用于预览展示，不触发推送）
    """
    last = (
        db.query(PushRecord)
        .filter(PushRecord.device_id == device.id, PushRecord.method == "push")
        .order_by(PushRecord.id.desc())
        .first()
    )
    if check_interval and last and last.pushed_at and (now - last.pushed_at).total_seconds() < MIN_PUSH_INTERVAL:
        return None, None
    today0 = now.replace(hour=0, minute=0, second=0, microsecond=0)
    t_sec = _to_secs(now.time())
    events = []  # (priority, cycle_key, trigger_sec, last_key, item)
    for it in stream.items:
        if not it.enabled:
            continue
        # 该条目最近一次推送（用于"该条目该周期是否已推"与最久未推轮转）
        item_last = (
            db.query(PushRecord)
            .filter(PushRecord.device_id == device.id, PushRecord.method == "push",
                    PushRecord.stream_item_id == it.id)
            .order_by(PushRecord.id.desc())
            .first()
        )
        last_key = item_last.pushed_at.timestamp() if item_last and item_last.pushed_at else 0.0
        if it.schedule_type == "absolute" and it.start_at:
            ts = _to_secs(it.start_at.time())
            if ts > t_sec:
                continue  # 定点未到
            if item_last and item_last.pushed_at >= today0 + dt.timedelta(seconds=ts):
                continue  # 该条目今天定点时刻后已推过
            events.append((0, 0, ts, last_key, it))  # 定点优先级最高
        elif it.schedule_type == "relative":
            d = max(1, it.duration_sec or 30)
            ts = (t_sec // d) * d
            if item_last and item_last.pushed_at >= today0 + dt.timedelta(seconds=ts):
                continue  # 该条目该循环周期已推过
            events.append((1, -d, ts, last_key, it))  # 循环：周期长优先，同周期最久未推优先
    if not events:
        return None, None
    events.sort(key=lambda e: (e[0], e[1], -e[2], e[3]))
    _, _, ts, _, item = events[0]
    return item, today0 + dt.timedelta(seconds=ts)


def resolve_film_for_device(db: Session, device, now: dt.datetime | None = None,
                            for_preview: bool = False):
    """计算设备当前应显示的轮播内容，渲染并转换为 film

    返回 (film_bytes, StreamItem|None, bytes|None preview)；无内容返回 (None, None, None)
    for_preview=True 时跳过推送限流（预览仅展示，不触发推送）
    """
    from . import film_convert, renderer

    now = now or dt.datetime.now()
    stream = active_stream_for_device(db, device)
    if not stream:
        return None, None, None
    if stream.mode == "device_pull":
        item, elapsed = advance_pull(db, stream, device, now)
    else:
        item, _ = _next_push_item(db, stream, device, now, check_interval=not for_preview)
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
    """服务端主动推送判定：有到期触发事件则需下发 download_film"""
    now = now or dt.datetime.now()
    stream = active_stream_for_device(db, device)
    if not stream or stream.mode != "server_push":
        return False, None
    item, _ = _next_push_item(db, stream, device, now)
    return item is not None, item


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
