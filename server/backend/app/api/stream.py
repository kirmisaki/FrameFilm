"""轮播流管理 API"""
import datetime as dt

from fastapi import APIRouter, Depends, HTTPException
from sqlalchemy.orm import Session

from ..db import get_db
from ..models import Device, Stream, StreamItem, Template, User
from ..schemas.stream import (
    StreamCreate,
    StreamDevicesRequest,
    StreamItemCreate,
    StreamItemOut,
    StreamItemSortRequest,
    StreamItemUpdate,
    StreamOut,
    StreamUpdate,
)
from .deps import get_current_user

router = APIRouter(prefix="/api/v1/admin/streams", tags=["streams"])


def _item_out(i: StreamItem) -> StreamItemOut:
    tname = ""
    if i.template is not None:
        tname = i.template.name
    return StreamItemOut(
        id=i.id, template_id=i.template_id, template_name=tname,
        position=i.position, schedule_type=i.schedule_type,
        duration_sec=i.duration_sec, start_at=i.start_at, enabled=i.enabled,
    )


def _stream_out(s: Stream) -> StreamOut:
    return StreamOut(
        id=s.id, name=s.name, mode=s.mode, enabled=s.enabled,
        created_at=s.created_at, items=[_item_out(i) for i in s.items],
    )


def _get_stream(db: Session, stream_id: int) -> Stream:
    s = db.get(Stream, stream_id)
    if s is None:
        raise HTTPException(404, "轮播流不存在")
    return s


@router.get("", response_model=list[StreamOut])
def list_streams(db: Session = Depends(get_db), _: User = Depends(get_current_user)):
    return [_stream_out(s) for s in db.query(Stream).order_by(Stream.id).all()]


@router.post("", response_model=StreamOut)
def create_stream(body: StreamCreate, db: Session = Depends(get_db),
                  _: User = Depends(get_current_user)):
    s = Stream(name=body.name, mode=body.mode, enabled=body.enabled)
    db.add(s)
    db.commit()
    db.refresh(s)
    return _stream_out(s)


@router.get("/{stream_id}", response_model=StreamOut)
def get_stream(stream_id: int, db: Session = Depends(get_db), _: User = Depends(get_current_user)):
    return _stream_out(_get_stream(db, stream_id))


@router.put("/{stream_id}", response_model=StreamOut)
def update_stream(stream_id: int, body: StreamUpdate, db: Session = Depends(get_db),
                  _: User = Depends(get_current_user)):
    s = _get_stream(db, stream_id)
    data = body.model_dump(exclude_unset=True)
    for k, v in data.items():
        setattr(s, k, v)
    db.commit()
    db.refresh(s)
    return _stream_out(s)


@router.delete("/{stream_id}")
def delete_stream(stream_id: int, db: Session = Depends(get_db), _: User = Depends(get_current_user)):
    s = _get_stream(db, stream_id)
    db.delete(s)
    db.commit()
    return {"msg": "已删除轮播流"}


def _normalize_start_at(v):
    """absolute 条目只存时刻（date 固定为当日 2000-01-01），create/update 行为一致"""
    if v is None:
        return None
    return v.replace(year=2000, month=1, day=1)


@router.post("/{stream_id}/items", response_model=StreamItemOut)
def add_item(stream_id: int, body: StreamItemCreate, db: Session = Depends(get_db),
             _: User = Depends(get_current_user)):
    s = _get_stream(db, stream_id)
    if db.get(Template, body.template_id) is None:
        raise HTTPException(404, "模板不存在")
    pos = max((i.position for i in s.items), default=-1) + 1
    it = StreamItem(
        stream_id=stream_id, template_id=body.template_id, position=pos,
        schedule_type=body.schedule_type, duration_sec=body.duration_sec,
        start_at=_normalize_start_at(body.start_at), enabled=body.enabled,
    )
    db.add(it)
    db.commit()
    db.refresh(it)
    return _item_out(it)


@router.put("/{stream_id}/items/{item_id}", response_model=StreamItemOut)
def update_item(stream_id: int, item_id: int, body: StreamItemUpdate,
                db: Session = Depends(get_db), _: User = Depends(get_current_user)):
    s = _get_stream(db, stream_id)
    it = db.get(StreamItem, item_id)
    if it is None or it.stream_id != s.id:
        raise HTTPException(404, "条目不存在")
    data = body.model_dump(exclude_unset=True)
    if "template_id" in data and db.get(Template, data["template_id"]) is None:
        raise HTTPException(404, "模板不存在")
    if "start_at" in data:
        data["start_at"] = _normalize_start_at(data["start_at"])  # 绝对条目只存时刻
    for k, v in data.items():
        setattr(it, k, v)
    db.commit()
    db.refresh(it)
    return _item_out(it)


@router.delete("/{stream_id}/items/{item_id}")
def delete_item(stream_id: int, item_id: int, db: Session = Depends(get_db),
                _: User = Depends(get_current_user)):
    s = _get_stream(db, stream_id)
    it = db.get(StreamItem, item_id)
    if it is None or it.stream_id != s.id:
        raise HTTPException(404, "条目不存在")
    db.delete(it)
    db.commit()
    return {"msg": "已删除条目"}


@router.post("/{stream_id}/items/sort")
def sort_items(stream_id: int, body: StreamItemSortRequest, db: Session = Depends(get_db),
               _: User = Depends(get_current_user)):
    s = _get_stream(db, stream_id)
    by_id = {i.id: i for i in s.items}
    for pos, iid in enumerate(body.item_ids):
        if iid in by_id:
            by_id[iid].position = pos
    db.commit()
    return {"msg": "排序已更新"}


@router.post("/{stream_id}/devices")
def set_stream_devices(stream_id: int, body: StreamDevicesRequest, db: Session = Depends(get_db),
                       _: User = Depends(get_current_user)):
    """指定播放该轮播流的设备集：勾选集合完整赋值

    - 勾选内的设备绑定到该流（覆盖其旧绑定）
    - 原本绑定该流、本次未勾选的设备解绑，回退全局第一个启用流
    - 其他流的绑定不受影响
    """
    s = _get_stream(db, stream_id)
    ids = set(body.device_ids)
    if ids:
        n = db.query(Device).filter(Device.id.in_(ids)).count()
        if n != len(ids):
            raise HTTPException(404, "存在不存在的设备")
    for d in db.query(Device).filter(Device.play_stream_id == stream_id).all():
        if d.id not in ids:
            d.play_stream_id = None  # 解绑回退
    if ids:
        db.query(Device).filter(Device.id.in_(ids)).update(
            {Device.play_stream_id: s.id}, synchronize_session=False)
    db.commit()
    return {"msg": f"已更新指定设备，共 {len(ids)} 台"}


@router.get("/{stream_id}/timeline")
def timeline(stream_id: int, db: Session = Depends(get_db), _: User = Depends(get_current_user)):
    """当日触发预览：列出各条目当天触发点（循环条目按周期列当日各次，定点列当日一次）"""
    s = _get_stream(db, stream_id)
    now = dt.datetime.now()
    t_sec = now.hour * 3600 + now.minute * 60 + now.second
    names = {t.id: t.name for t in db.query(Template).all()}
    events = []
    for it in sorted(s.items, key=lambda i: i.position):
        if not it.enabled:
            continue
        if it.schedule_type == "absolute" and it.start_at:
            ts = it.start_at.hour * 3600 + it.start_at.minute * 60 + it.start_at.second
            events.append((ts, 0, it))  # 定点：当日全部安排（含已过的，前端置灰标记）
        elif it.schedule_type == "relative":
            d = max(1, it.duration_sec or 30)
            ts = (t_sec // d) * d  # 当前周期起点（最近一次触发点）
            events.append((ts, d, it))  # 循环条目聚合为一条：展示周期即可，不展开当日所有触发点
    events.sort(key=lambda e: (e[0], e[1]))
    return [
        {
            "start_sec": st, "duration_sec": d,
            "start_time": f"{st // 3600:02d}:{st % 3600 // 60:02d}",
            "item_id": it.id, "template_id": it.template_id,
            "template_name": names.get(it.template_id, ""),
            "schedule_type": it.schedule_type,
            "passed": st < t_sec,  # 定点条目已过触发时间
        }
        for st, d, it in events
    ]
