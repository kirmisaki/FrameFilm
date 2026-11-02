"""轮播流相关 schema"""
from datetime import datetime

from pydantic import BaseModel, Field

STREAM_MODES = ("server_push", "device_pull")
SCHEDULE_TYPES = ("relative", "absolute")


class StreamCreate(BaseModel):
    name: str = Field(min_length=1, max_length=128)
    mode: str = Field(default="device_pull", pattern="^(server_push|device_pull)$")
    enabled: bool = True


class StreamUpdate(BaseModel):
    name: str | None = Field(default=None, min_length=1, max_length=128)
    mode: str | None = Field(default=None, pattern="^(server_push|device_pull)$")
    enabled: bool | None = None


class StreamItemCreate(BaseModel):
    template_id: int
    schedule_type: str = Field(default="relative", pattern="^(relative|absolute)$")
    duration_sec: int = Field(default=30, ge=1, le=86400)
    start_at: datetime | None = None
    enabled: bool = True


class StreamItemUpdate(BaseModel):
    template_id: int | None = None
    schedule_type: str | None = Field(default=None, pattern="^(relative|absolute)$")
    duration_sec: int | None = Field(default=None, ge=1, le=86400)
    start_at: datetime | None = None
    enabled: bool | None = None


class StreamItemOut(BaseModel):
    id: int
    template_id: int
    template_name: str = ""
    position: int
    schedule_type: str
    duration_sec: int
    start_at: datetime | None
    enabled: bool


class StreamOut(BaseModel):
    id: int
    name: str
    mode: str
    enabled: bool
    created_at: datetime
    items: list[StreamItemOut] = []


class StreamItemSortRequest(BaseModel):
    item_ids: list[int] = Field(min_length=1)


class StreamDevicesRequest(BaseModel):
    """流绑定的设备集：完整赋值（原本绑定该流、本次未勾选的设备会被解绑回退全局）"""

    device_ids: list[int] = Field(default_factory=list)
