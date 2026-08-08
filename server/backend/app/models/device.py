"""设备模型"""
from datetime import datetime

from sqlalchemy import Boolean, DateTime, Integer, String, Text
from sqlalchemy.orm import Mapped, mapped_column

from ..db import Base


class Device(Base):
    __tablename__ = "devices"

    id: Mapped[int] = mapped_column(Integer, primary_key=True)
    device_id: Mapped[str] = mapped_column(String(64), unique=True, nullable=False)  # ESP32 MAC
    name: Mapped[str] = mapped_column(String(64), default="")
    device_type: Mapped[str] = mapped_column(String(16), default="basic")  # basic / pro
    token: Mapped[str] = mapped_column(String(64), default="")
    is_claimed: Mapped[bool] = mapped_column(Boolean, default=False)  # 是否被用户认领

    # 设备上报的设置参数（与固件 ServiceParam_Def_t 对齐）
    wifi_enable: Mapped[bool] = mapped_column(Boolean, default=False)
    play_mode: Mapped[int] = mapped_column(Integer, default=0)
    sleep_mode: Mapped[bool] = mapped_column(Boolean, default=False)
    sleep_auto: Mapped[bool] = mapped_column(Boolean, default=False)
    sleep_time: Mapped[int] = mapped_column(Integer, default=0)
    ble_enable: Mapped[bool] = mapped_column(Boolean, default=False)
    current_file_id: Mapped[int] = mapped_column(Integer, default=0)

    # 服务端管理
    heartbeat_interval: Mapped[int] = mapped_column(Integer, default=60)
    battery_percent: Mapped[int] = mapped_column(Integer, default=-1)
    voltage_mv: Mapped[int] = mapped_column(Integer, default=0)
    state: Mapped[str] = mapped_column(String(16), default="")
    pending_config: Mapped[str] = mapped_column(Text, default="{}")  # 待下发的 set_config 指令 JSON
    pending_commands: Mapped[str] = mapped_column(Text, default="[]")  # 待下发的指令队列 JSON 数组
    last_heartbeat_at: Mapped[datetime | None] = mapped_column(DateTime, nullable=True)
    last_ip: Mapped[str] = mapped_column(String(64), default="")
    created_at: Mapped[datetime] = mapped_column(DateTime, default=datetime.now)
