"""用户模型（单用户）"""
from datetime import datetime

from sqlalchemy import DateTime, Integer, String
from sqlalchemy.orm import Mapped, mapped_column

from ..db import Base


class User(Base):
    __tablename__ = "users"

    id: Mapped[int] = mapped_column(Integer, primary_key=True)
    username: Mapped[str] = mapped_column(String(64), unique=True, nullable=False)
    password_hash: Mapped[str] = mapped_column(String(128), nullable=False)
    ver: Mapped[int] = mapped_column(Integer, default=1)  # JWT 版本号，改密后 +1 使旧 token 失效
    created_at: Mapped[datetime] = mapped_column(DateTime, default=datetime.now)
