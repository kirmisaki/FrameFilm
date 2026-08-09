"""设备协议 API：心跳（含注册与指令下发）+ film 获取

协议详见 docs/filmhub/design.md §7-§8。
"""
import datetime as dt
import hashlib
import hmac
import json
import secrets

from fastapi import APIRouter, Depends, HTTPException, Request, Response, status
from sqlalchemy.orm import Session

from ..config import JWT_SECRET
from ..db import get_db
from ..models import Device
from ..schemas.device import HeartbeatCommand, HeartbeatResponse, HeartbeatResponseData
from ..services import scheduler

router = APIRouter(prefix="/api/v1/device", tags=["device-proto"])

# 签名 URL 有效期（秒）：设备从心跳收到 download_film 指令到实际下载的窗口
_SIGNED_URL_TTL = 600


def _sign_film_url(device_id: str, exp: int) -> str:
    """对 (device_id, exp) 计算 HMAC 签名，避免把设备 token 明文拼进 URL"""
    msg = f"{device_id}:{exp}".encode("utf-8")
    return hmac.new(JWT_SECRET.encode("utf-8"), msg, hashlib.sha256).hexdigest()


def _verify_film_url(device_id: str, sig: str, exp: str) -> bool:
    try:
        exp_i = int(exp)
    except (TypeError, ValueError):
        return False
    if exp_i < int(dt.datetime.now().timestamp()):
        return False
    expected = _sign_film_url(device_id, exp_i)
    return hmac.compare_digest(expected, sig or "")


def _authenticate(db: Session, device_id: str, token: str, strict: bool = False) -> tuple[Device, bool, bool]:
    """设备认证。返回 (device, is_new, token_changed)。

    - 未找到设备：自动注册（is_claimed=0，待认领），返回新 token
    - token 为空：补发新 token
    - token 不匹配：
      - 非 strict（心跳）：未认领设备放行并补发 token（兼容 NVS 丢失场景）；已认领设备拒绝
      - strict（film 下载等资源访问）：一律拒绝
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
        return device, True, True
    token_changed = False
    if not device.token:
        device.token = secrets.token_hex(16)
        token_changed = True
    elif token != device.token:
        if strict or device.is_claimed:
            raise HTTPException(status.HTTP_401_UNAUTHORIZED, "设备 token 无效")
        # 未认领设备：兼容重复注册（设备可能丢了 NVS token），补发新 token
        device.token = secrets.token_hex(16)
        token_changed = True
    if token_changed:
        db.commit()
        db.refresh(device)
    return device, False, token_changed


def _film_download_url(request: Request, device: Device) -> str:
    base = str(request.base_url).rstrip("/")
    exp = int(dt.datetime.now().timestamp()) + _SIGNED_URL_TTL
    sig = _sign_film_url(device.device_id, exp)
    return f"{base}/api/v1/device/film/latest.film?device_id={device.device_id}&sig={sig}&exp={exp}"


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
    heartbeat_interval: int | None = None,
    db: Session = Depends(get_db),
):
    if not device_id:
        raise HTTPException(status.HTTP_400_BAD_REQUEST, "缺少 device_id")
    device, is_new, token_changed = _authenticate(db, device_id, token)

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
    # 心跳间隔：设备上报其本地实际值（可被 BLE/指令修改），回写设备表供管理页显示
    if heartbeat_interval is not None and 5 <= heartbeat_interval <= 180:
        device.heartbeat_interval = heartbeat_interval
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

    # 2. 服务端主动推送：当前条目变化则下发下载指令（token 变更的设备暂不下发）
    if not is_new and not token_changed:
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
        heartbeat_interval=None,  # 间隔由设备本地/BLE/set_heartbeat 指令控制，响应不下发避免覆盖
        token=device.token if (is_new or token_changed) else None,
        commands=commands,
    ))


@router.get("/film/latest.film")
def get_latest_film(
    request: Request,
    device_id: str = "",
    token: str = "",
    sig: str = "",
    exp: str = "",
    db: Session = Depends(get_db),
):
    """设备 film 获取：按设备类型分辨率渲染轮播当前内容并转换返回

    鉴权：优先校验签名 URL（sig+exp，服务端下发），兼容旧版明文 token 方式。
    """
    if not device_id:
        raise HTTPException(status.HTTP_400_BAD_REQUEST, "缺少 device_id")
    if _verify_film_url(device_id, sig, exp):
        device = db.query(Device).filter(Device.device_id == device_id).first()
        if device is None:
            raise HTTPException(status.HTTP_401_UNAUTHORIZED, "设备不存在")
    else:
        # 旧版明文 token 鉴权（严格：token 必须完全匹配）
        device, _, _ = _authenticate(db, device_id, token, strict=True)

    film, item, preview = scheduler.resolve_film_for_device(db, device)
    if film is None:
        return Response(status_code=status.HTTP_204_NO_CONTENT)

    # 消费记录（pull 模式推进 / push 模式记录下发）
    stream = scheduler.active_stream_for_device(db, device)
    method = "push" if (stream and stream.mode == "server_push") else "pull"
    film_name = f"item{item.id}.film" if item else "latest.film"
    scheduler.record_push(db, device, item, film_name, method)

    return Response(
        content=film,
        media_type="application/octet-stream",
        headers={"Content-Disposition": f'attachment; filename="{film_name}"'},
    )
