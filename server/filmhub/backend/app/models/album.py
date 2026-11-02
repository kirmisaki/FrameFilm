"""相册与照片模型"""
from datetime import datetime

from sqlalchemy import DateTime, ForeignKey, Integer, String, Text
from sqlalchemy.orm import Mapped, mapped_column, relationship

from ..db import Base


class Album(Base):
    __tablename__ = "albums"

    id: Mapped[int] = mapped_column(Integer, primary_key=True)
    name: Mapped[str] = mapped_column(String(128), nullable=False)
    description: Mapped[str] = mapped_column(Text, default="")
    cover_photo_id: Mapped[int | None] = mapped_column(Integer, nullable=True)
    # 相册独立抖动配置（e-ink 预览渲染用，默认自适应）
    dither_type: Mapped[str] = mapped_column(String(32), default="adaptive", server_default="adaptive")
    dither_strength: Mapped[int] = mapped_column(Integer, default=80, server_default="80")
    created_at: Mapped[datetime] = mapped_column(DateTime, default=datetime.now)

    photos: Mapped[list["Photo"]] = relationship(
        back_populates="album", cascade="all, delete-orphan", order_by="Photo.sort"
    )


class Photo(Base):
    __tablename__ = "photos"

    id: Mapped[int] = mapped_column(Integer, primary_key=True)
    album_id: Mapped[int] = mapped_column(ForeignKey("albums.id"), nullable=False)
    filename: Mapped[str] = mapped_column(String(255), default="")
    original_path: Mapped[str] = mapped_column(String(512), default="")
    film_path: Mapped[str] = mapped_column(String(512), default="")
    preview_path: Mapped[str] = mapped_column(String(512), default="")
    width: Mapped[int] = mapped_column(Integer, default=0)
    height: Mapped[int] = mapped_column(Integer, default=0)
    # 显示布局 {"scale": float, "x": int, "y": int}：相对 contain 适配的缩放倍数 + 画布像素偏移
    layout: Mapped[str] = mapped_column(Text, default="{}")
    sort: Mapped[int] = mapped_column(Integer, default=0)
    created_at: Mapped[datetime] = mapped_column(DateTime, default=datetime.now)

    album: Mapped[Album] = relationship(back_populates="photos")
