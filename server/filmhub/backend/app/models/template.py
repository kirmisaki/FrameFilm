"""模板模型"""
from datetime import datetime

from sqlalchemy import Boolean, DateTime, Integer, String, Text
from sqlalchemy.orm import Mapped, mapped_column

from ..db import Base


class Template(Base):
    __tablename__ = "templates"

    id: Mapped[int] = mapped_column(Integer, primary_key=True)
    name: Mapped[str] = mapped_column(String(128), nullable=False)
    kind: Mapped[str] = mapped_column(String(32), default="custom")  # album/calendar/memo/weather/fridge/fortune/countdown/quote/custom
    is_builtin: Mapped[bool] = mapped_column(Boolean, default=False)
    definition: Mapped[str] = mapped_column(Text, default="{}")  # 模板描述 JSON
    render_config: Mapped[str] = mapped_column(Text, default="{}")  # 渲染算法参数 JSON
    thumb_path: Mapped[str] = mapped_column(String(512), default="")
    created_at: Mapped[datetime] = mapped_column(DateTime, default=datetime.now)
