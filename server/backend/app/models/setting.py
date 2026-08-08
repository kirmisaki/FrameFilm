"""键值设置（AI 配置等）"""
from sqlalchemy import String, Text
from sqlalchemy.orm import Mapped, mapped_column

from ..db import Base


class Setting(Base):
    __tablename__ = "settings"

    key: Mapped[str] = mapped_column(String(64), primary_key=True)
    value: Mapped[str] = mapped_column(Text, default="{}")
