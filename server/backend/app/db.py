"""数据库引擎与会话"""
from sqlalchemy import create_engine
from sqlalchemy.orm import DeclarativeBase, sessionmaker

from .config import DB_PATH


class Base(DeclarativeBase):
    pass


engine = create_engine(
    f"sqlite:///{DB_PATH}",
    connect_args={"check_same_thread": False},
)
SessionLocal = sessionmaker(bind=engine, autoflush=False, autocommit=False)


def get_db():
    db = SessionLocal()
    try:
        yield db
    finally:
        db.close()


def init_db():
    from . import models  # noqa: F401  确保模型注册
    Base.metadata.create_all(bind=engine)
    _migrate()


def _migrate():
    """轻量迁移：为已有库补齐新增列（SQLite ALTER TABLE）"""
    from sqlalchemy import text

    with engine.connect() as conn:
        cols = {row[1] for row in conn.execute(text("PRAGMA table_info(albums)"))}
        pcols = {row[1] for row in conn.execute(text("PRAGMA table_info(photos)"))}
    with engine.begin() as conn:
        if "dither_type" not in cols:
            conn.execute(text("ALTER TABLE albums ADD COLUMN dither_type VARCHAR(32) NOT NULL DEFAULT 'adaptive'"))
        if "dither_strength" not in cols:
            conn.execute(text("ALTER TABLE albums ADD COLUMN dither_strength INTEGER NOT NULL DEFAULT 80"))
        if "layout" not in pcols:
            conn.execute(text("ALTER TABLE photos ADD COLUMN layout TEXT NOT NULL DEFAULT '{}'"))
