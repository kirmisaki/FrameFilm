"""设备管理 API"""
import datetime as dt
import json
import secrets

from fastapi import APIRouter, Depends, HTTPException, Request, Response
from sqlalchemy.orm import Session

from ..config import HEARTBEAT_DEFAULT_INTERVAL, ONLINE_FACTOR
from ..db import get_db
from ..models import Device, PushRecord, StreamItem, Template, User
from ..schemas.device import (
    CommandRequest,
    DeviceClaim,
    DeviceConfig,
    DeviceOut,
    DeviceUpdate,
)
from ..services.scheduler import active_stream_for_device, advance_pull, resolve_film_for_device
from .deps import get_current_user

router = APIRouter(prefix="/api/v1/admin/devices", tags=["devices"])


def _playing_template(db: Session, stream, d: Device, now: dt.datetime) -> tuple[int, str]:
    """按生效轮播流推断设备当前播放的模板 (template_id, template_name)；无返回 (0, "")"""
    if not stream:
        return 0, ""
    if stream.mode == "device_pull":
        item, _ = advance_pull(db, stream, d, now)
        if not item:
            return 0, ""
        t = db.get(Template, item.template_id)
        return (t.id, t.name) if t else (0, "")
    # 服务端推送：当前内容 = 最近一次推送的条目
    last = (
        db.query(PushRecord)
        .filter(PushRecord.device_id == d.id, PushRecord.method == "push")
        .order_by(PushRecord.id.desc())
        .first()
    )
    if not last or not last.stream_item_id:
        return 0, ""
    si = db.get(StreamItem, last.stream_item_id)
    if not si:
        return 0, ""
    t = db.get(Template, si.template_id)
    if not t:
        return 0, ""
    return t.id, t.name


def _device_out(d: Device, db: Session, stream) -> DeviceOut:
    online = False
    if d.last_heartbeat_at is not None:
        interval = d.heartbeat_interval or HEARTBEAT_DEFAULT_INTERVAL
        online = (dt.datetime.now() - d.last_heartbeat_at).total_seconds() <= ONLINE_FACTOR * interval
    template_id, template_name = (0, "")
    if d.is_claimed:
        template_id, template_name = _playing_template(db, stream, d, dt.datetime.now())
    return DeviceOut(
        id=d.id, device_id=d.device_id, name=d.name, device_type=d.device_type,
        token=d.token, is_claimed=d.is_claimed,
        wifi_enable=d.wifi_enable, play_mode=d.play_mode, sleep_mode=d.sleep_mode,
        sleep_auto=d.sleep_auto, sleep_time=d.sleep_time, ble_enable=d.ble_enable,
        current_file_id=d.current_file_id, heartbeat_interval=d.heartbeat_interval,
        template_id=template_id, template_name=template_name,
        play_stream_id=d.play_stream_id,
        battery_percent=d.battery_percent, voltage_mv=d.voltage_mv, state=d.state,
        last_heartbeat_at=d.last_heartbeat_at, last_ip=d.last_ip,
        created_at=d.created_at, online=online,
    )


def _get_device(db: Session, device_id: int) -> Device:
    d = db.get(Device, device_id)
    if d is None:
        raise HTTPException(404, "设备不存在")
    return d


@router.get("", response_model=list[DeviceOut])
def list_devices(db: Session = Depends(get_db), _: User = Depends(get_current_user)):
    return [_device_out(d, db, active_stream_for_device(db, d))
            for d in db.query(Device).order_by(Device.id).all()]


@router.post("/{device_id}/claim", response_model=DeviceOut)
def claim_device(
    device_id: int,
    body: DeviceClaim,
    db: Session = Depends(get_db),
    _: User = Depends(get_current_user),
):
    d = _get_device(db, device_id)
    d.name = body.name
    d.device_type = body.device_type
    d.is_claimed = True
    db.commit()
    db.refresh(d)
    return _device_out(d, db, active_stream_for_device(db, d))


@router.put("/{device_id}", response_model=DeviceOut)
def update_device(
    device_id: int,
    body: DeviceUpdate,
    db: Session = Depends(get_db),
    _: User = Depends(get_current_user),
):
    d = _get_device(db, device_id)
    data = body.model_dump(exclude_unset=True)
    if data.get("play_stream_id") == 0:  # 传 0 = 清除绑定，存 NULL 回退全局
        data["play_stream_id"] = None
    for k, v in data.items():
        setattr(d, k, v)
    db.commit()
    db.refresh(d)
    return _device_out(d, db, active_stream_for_device(db, d))


@router.delete("/{device_id}")
def delete_device(
    device_id: int,
    db: Session = Depends(get_db),
    _: User = Depends(get_current_user),
):
    d = _get_device(db, device_id)
    db.delete(d)
    db.commit()
    return {"msg": "已删除"}


@router.post("/{device_id}/reset-token", response_model=DeviceOut)
def reset_token(
    device_id: int,
    db: Session = Depends(get_db),
    _: User = Depends(get_current_user),
):
    d = _get_device(db, device_id)
    d.token = secrets.token_hex(16)
    d.is_claimed = False  # 需重新认领
    db.commit()
    db.refresh(d)
    return _device_out(d, db, active_stream_for_device(db, d))


@router.post("/{device_id}/set-config")
def set_config(
    device_id: int,
    body: DeviceConfig,
    db: Session = Depends(get_db),
    _: User = Depends(get_current_user),
):
    """写入待下发配置，设备下次心跳时收到 set_config 指令"""
    d = _get_device(db, device_id)
    cfg = {k: v for k, v in body.model_dump(exclude_none=True).items() if v is not None}
    if not cfg:
        raise HTTPException(400, "没有可下发的配置项")
    try:
        current = json.loads(d.pending_config or "{}")
    except json.JSONDecodeError:
        current = {}
    current.update(cfg)
    d.pending_config = json.dumps(current, ensure_ascii=False)
    db.commit()
    return {"msg": "配置已排队，等待设备下次心跳下发"}


@router.post("/{device_id}/commands")
def send_command(
    device_id: int,
    body: CommandRequest,
    db: Session = Depends(get_db),
    _: User = Depends(get_current_user),
):
    """下发任意指令（download_film / reboot / clear_files / sync_time ...）"""
    d = _get_device(db, device_id)
    try:
        queue = json.loads(d.pending_commands or "[]")
    except json.JSONDecodeError:
        queue = []
    queue.append({"cmd": body.cmd, "params": body.params})
    d.pending_commands = json.dumps(queue, ensure_ascii=False)
    db.commit()
    return {"msg": "指令已排队，等待设备下次心跳执行"}


@router.post("/{device_id}/sync-film")
def sync_film(
    device_id: int,
    request: Request,
    db: Session = Depends(get_db),
    _: User = Depends(get_current_user),
):
    """立即推送最新 film：向设备下发 download_film 指令（URL 使用短期签名，不携带明文 token）"""
    d = _get_device(db, device_id)
    from .device_proto import _film_download_url

    url = _film_download_url(request, d)
    try:
        queue = json.loads(d.pending_commands or "[]")
    except json.JSONDecodeError:
        queue = []
    queue.append({"cmd": "download_film", "params": {"url": url}})
    d.pending_commands = json.dumps(queue, ensure_ascii=False)
    db.commit()
    return {"msg": "已发起最新内容推送"}


@router.get("/{device_id}/preview")
def device_preview(device_id: int, db: Session = Depends(get_db),
                   _: User = Depends(get_current_user)):
    """设备当前内容预览 PNG：与设备下载同一渲染路径（相册按 rotate_sec 轮换等动态内容与设备一致）"""
    d = _get_device(db, device_id)
    _, _, preview = resolve_film_for_device(db, d, for_preview=True)
    if preview is None:
        raise HTTPException(404, "该设备当前没有可播放内容")
    return Response(content=preview, media_type="image/png",
                    headers={"Cache-Control": "no-store"})  # 不缓存：相册按时间轮换，需实时画面
