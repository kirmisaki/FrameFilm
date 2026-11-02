"""轮播流模型"""
from datetime import datetime

from sqlalchemy import Boolean, DateTime, ForeignKey, Integer, String
from sqlalchemy.orm import Mapped, mapped_column, relationship

from ..db import Base


class Stream(Base):
    __tablename__ = "streams"

    id: Mapped[int] = mapped_column(Integer, primary_key=True)
    name: Mapped[str] = mapped_column(String(128), nullable=False)
    mode: Mapped[str] = mapped_column(String(16), default="device_pull")  # server_push / device_pull
    enabled: Mapped[bool] = mapped_column(Boolean, default=True)
    created_at: Mapped[datetime] = mapped_column(DateTime, default=datetime.now)

    items: Mapped[list["StreamItem"]] = relationship(
        back_populates="stream", cascade="all, delete-orphan", order_by="StreamItem.position"
    )


class StreamItem(Base):
    __tablename__ = "stream_items"

    id: Mapped[int] = mapped_column(Integer, primary_key=True)
    stream_id: Mapped[int] = mapped_column(ForeignKey("streams.id"), nullable=False)
    template_id: Mapped[int] = mapped_column(ForeignKey("templates.id"), nullable=False)
    position: Mapped[int] = mapped_column(Integer, default=0)
    schedule_type: Mapped[str] = mapped_column(String(16), default="relative")  # relative / absolute
    duration_sec: Mapped[int] = mapped_column(Integer, default=30)  # 相对：显示时长
    start_at: Mapped[datetime | None] = mapped_column(DateTime, nullable=True)  # 绝对：定点时刻
    enabled: Mapped[bool] = mapped_column(Boolean, default=True)

    stream: Mapped[Stream] = relationship(back_populates="items")
    template: Mapped["Template"] = relationship()


class PushRecord(Base):
    __tablename__ = "push_records"

    id: Mapped[int] = mapped_column(Integer, primary_key=True)
    device_id: Mapped[int] = mapped_column(ForeignKey("devices.id"), nullable=False)
    stream_item_id: Mapped[int] = mapped_column(ForeignKey("stream_items.id"), nullable=True)
    film_path: Mapped[str] = mapped_column(String(512), default="")
    method: Mapped[str] = mapped_column(String(16), default="pull")  # push / pull
    pushed_at: Mapped[datetime] = mapped_column(DateTime, default=datetime.now)
