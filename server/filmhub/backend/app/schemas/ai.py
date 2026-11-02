"""AI 相关 schema"""
from pydantic import BaseModel, Field


class AISettings(BaseModel):
    base_url: str = Field(min_length=1, max_length=256)
    api_key: str = Field(default="", max_length=512)
    model: str = Field(min_length=1, max_length=64)
    image_model: str = Field(default="", max_length=64)  # 图像生成模型（可空）


class AITemplateRequest(BaseModel):
    prompt: str = Field(min_length=1, max_length=2000)


class AIImageRequest(BaseModel):
    prompt: str = Field(min_length=1, max_length=1000)
    album_id: int | None = None  # 为空则保存到新相册
    size: str = "1024x1024"
