"""设备协议 API：心跳（含注册与指令下发）+ film 获取

协议详见 docs/filmhub/design.md §7-§8。
"""
import datetime as dt
import json
import secrets

from fastapi import APIRouter, Depends, HTTPException, Request, Response, status
from sqlalchemy.orm import Session

from ..config import HEARTBEAT_DEFAULT_INTERVAL, HEARTBEAT_MAX_INTERVAL, HEARTBEAT_MIN_INTERVAL
from ..db import get_db
from ..models import Device
from ..schemas.device import HeartbeatCommand, HeartbeatResponse, HeartbeatResponseData
from ..services import scheduler

router = APIRouter(prefix="/api/v1/device", tags=["device-proto"])


def _authenticate(db: Session, device_id: str, token: str) -> tuple[Device, bool]:
    """设备认证。返回 (device, is_new)；新设备自动注册并签发 token。

    - 未找到设备：自动注册（is_claimed=0，待认领）
    - 已注册但 token 不匹配：未认领设备沿用原记录（重复注册），已认领设备拒绝
    """
    device = db.query(Device).filter(Device.device_id == device_id).first()
    if device is None:
        device = Device(
            device_id=device_id,
            name=device_id,
            device_type="basic",
            token=secrets.token_hex(16),
            is_claimed=False,
        )
        db.add(device)
        db.commit()
        db.refresh(device)
        return device, True
    if not device.token:
        device.token = secrets.token_hex(16)
        db.commit()
    if token != device.token:
        if not device.is_claimed:
            # 未认领：沿用原记录（设备可能丢了 NVS token）
            db.refresh(device)
            return device, False
        raise HTTPException(status.HTTP_401_UNAUTHORIZED, "设备 token 无效")
    return device, False


def _film_download_url(request: Request, device: Device) -> str:
    base = str(request.base_url).rstrip("/")
    return f"{base}/api/v1/device/film/latest.film?device_id={device.device_id}&token={device.token}"


@router.get("/heartbeat", response_model=HeartbeatResponse)
def heartbeat(
    request: Request,
    device_id: str = "",
    token: str = "",
    battery: int | None = None,
    voltage_mv: int | None = None,
    play_mode: int | None = None,
    wifi_enable: int | None = None,
    sleep_mode: int | None = None,
    sleep_auto: int | None = None,
    sleep_time: int | None = None,
    ble_enable: int | None = None,
    current_file_id: int | None = None,
    state: str = "",
    db: Session = Depends(get_db),
):
    if not device_id:
        raise HTTPException(status.HTTP_400_BAD_REQUEST, "缺少 device_id")
    device, is_new = _authenticate(db, device_id, token)

    # 更新上报字段（设备已有设置参数）
    if battery is not None and 0 <= battery <= 100:
        device.battery_percent = battery
    if voltage_mv is not None:
        device.voltage_mv = voltage_mv
    if play_mode is not None:
        device.play_mode = play_mode
    if wifi_enable is not None:
        device.wifi_enable = bool(wifi_enable)
    if sleep_mode is not None:
        device.sleep_mode = bool(sleep_mode)
    if sleep_auto is not None:
        device.sleep_auto = bool(sleep_auto)
    if sleep_time is not None:
        device.sleep_time = sleep_time
    if ble_enable is not None:
        device.ble_enable = bool(ble_enable)
    if current_file_id is not None:
        device.current_file_id = current_file_id
    if state:
        device.state = state
    device.last_heartbeat_at = dt.datetime.now()
    device.last_ip = request.client.host if request.client else ""
    db.commit()

    # 组装指令
    commands: list[HeartbeatCommand] = []

    # 1. 待下发配置
    if device.pending_config and device.pending_config != "{}":
        try:
            cfg = json.loads(device.pending_config)
        except json.JSONDecodeError:
            cfg = {}
        if cfg:
            commands.append(HeartbeatCommand(cmd="set_config", params=cfg))
        device.pending_config = "{}"

    # 2. 服务端主动推送：当前条目变化则下发下载指令
    if not is_new:
        need_push, item = scheduler.should_push(db, device)
        if need_push and item is not None:
            commands.append(HeartbeatCommand(
                cmd="download_film", params={"url": _film_download_url(request, device)}
            ))

    # 3. 管理端主动指令队列
    if device.pending_commands and device.pending_commands != "[]":
        try:
            queue = json.loads(device.pending_commands)
        except json.JSONDecodeError:
            queue = []
        for c in queue:
            if isinstance(c, dict) and c.get("cmd"):
                commands.append(HeartbeatCommand(cmd=c["cmd"], params=c.get("params", {})))
        device.pending_commands = "[]"
    db.commit()

    return HeartbeatResponse(data=HeartbeatResponseData(
        server_time=int(dt.datetime.now().timestamp()),
        heartbeat_interval=device.heartbeat_interval or HEARTBEAT_DEFAULT_INTERVAL,
        token=device.token if is_new else None,
        commands=commands,
    ))


@router.get("/film/latest.film")
def get_latest_film(
    request: Request,
    device_id: str = "",
    token: str = "",
    db: Session = Depends(get_db),
):
    """设备 film 获取：按设备类型分辨率渲染轮播当前内容并转换返回"""
    if not device_id:
        raise HTTPException(status.HTTP_400_BAD_REQUEST, "缺少 device_id")
    device, _ = _authenticate(db, device_id, token)

    film, item, preview = scheduler.resolve_film_for_device(db, device)
    if film is None:
        return Response(status_code=status.HTTP_204_NO_CONTENT)

    # 消费记录（pull 模式推进 / push 模式记录下发）
    stream = scheduler.active_stream(db)
    method = "push" if (stream and stream.mode == "server_push") else "pull"
    film_name = f"item{item.id}.film" if item else "latest.film"
    scheduler.record_push(db, device, item, film_name, method)

    return Response(
        content=film,
        media_type="application/octet-stream",
        headers={"Content-Disposition": f'attachment; filename="{film_name}"'},
    )
