"""相册相关 schema（相册为纯照片容器，渲染算法由引用它的模板 render_config 决定）"""
from datetime import datetime

from pydantic import BaseModel, Field


class AlbumCreate(BaseModel):
    name: str = Field(min_length=1, max_length=128)
    description: str = ""


class AlbumUpdate(BaseModel):
    name: str | None = Field(default=None, min_length=1, max_length=128)
    description: str | None = None
    cover_photo_id: int | None = None


class PhotoOut(BaseModel):
    id: int
    album_id: int
    filename: str
    width: int
    height: int
    sort: int
    created_at: datetime
    layout: str = "{}"
    thumb_url: str = ""
    original_url: str = ""


class PhotoLayoutBody(BaseModel):
    """照片显示布局：scale 为相对 contain 适配的缩放倍数，x/y 为 400×600 画布像素偏移，rotate 为旋转角度（90 的倍数）"""

    scale: float = 1.0
    x: int = 0
    y: int = 0
    rotate: int = 0


class AlbumOut(BaseModel):
    id: int
    name: str
    description: str
    cover_photo_id: int | None
    photo_count: int = 0
    created_at: datetime
    cover_url: str = ""
    photos: list[PhotoOut] = []


class AlbumListOut(BaseModel):
    id: int
    name: str
    description: str
    cover_photo_id: int | None
    photo_count: int = 0
    created_at: datetime
    cover_url: str = ""


class PhotoSortRequest(BaseModel):
    """按 id 顺序重排"""

    photo_ids: list[int] = Field(min_length=1)


class UploadResult(BaseModel):
    id: int
    filename: str
    width: int
    height: int
    original_url: str
