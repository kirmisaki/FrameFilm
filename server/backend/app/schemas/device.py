"""设备相关 schema"""
from datetime import datetime
from typing import Any

from pydantic import BaseModel, Field

from ..config import HEARTBEAT_MAX_INTERVAL, HEARTBEAT_MIN_INTERVAL

DEVICE_TYPES = ("basic", "pro")
DEVICE_STATES = ("idle", "displaying", "downloading", "sleep", "offline")


class DeviceOut(BaseModel):
    id: int
    device_id: str
    name: str
    device_type: str
    token: str
    is_claimed: bool
    wifi_enable: bool
    play_mode: int
    sleep_mode: bool
    sleep_auto: bool
    sleep_time: int
    ble_enable: bool
    current_file_id: int
    heartbeat_interval: int
    battery_percent: int
    voltage_mv: int
    state: str
    last_heartbeat_at: datetime | None
    last_ip: str
    created_at: datetime
    online: bool = False


class DeviceUpdate(BaseModel):
    name: str | None = Field(default=None, max_length=64)
    device_type: str | None = Field(default=None, pattern="^(basic|pro)$")
    heartbeat_interval: int | None = Field(
        default=None, ge=HEARTBEAT_MIN_INTERVAL, le=HEARTBEAT_MAX_INTERVAL
    )


class DeviceClaim(BaseModel):
    name: str = Field(min_length=1, max_length=64)
    device_type: str = Field(pattern="^(basic|pro)$")


class DeviceConfig(BaseModel):
    """待下发 set_config 指令的参数（与固件 ServiceParam_Def_t 对齐）"""

    play_mode: int | None = Field(default=None, ge=0, le=2)
    wifi_enable: bool | None = None
    sleep_mode: bool | None = None
    sleep_auto: bool | None = None
    sleep_time: int | None = Field(default=None, ge=0, le=2880)
    ble_enable: bool | None = None


class CommandRequest(BaseModel):
    """任意指令下发（管理端主动控制）"""

    cmd: str
    params: dict[str, Any] = Field(default_factory=dict)


class HeartbeatCommand(BaseModel):
    cmd: str
    params: dict[str, Any] = Field(default_factory=dict)


class HeartbeatResponseData(BaseModel):
    server_time: int
    heartbeat_interval: int
    token: str | None = None
    commands: list[HeartbeatCommand] = Field(default_factory=list)


class HeartbeatResponse(BaseModel):
    code: int = 0
    msg: str = "ok"
    data: HeartbeatResponseData | None = None
