"""模板相关 schema"""
from datetime import datetime
from typing import Any

from pydantic import BaseModel, Field, field_validator

RENDER_CONFIG_KEYS = {"dither_type", "dither_strength", "contrast", "brightness", "saturation", "palette"}
DITHER_TYPES = {"none", "floyd_steinberg", "atkinson", "stucki", "jarvis",
                "gamma_floyd_steinberg", "bayer", "adaptive"}
PALETTES = {"6color", "bw"}
TEMPLATE_KINDS = (
    "album", "calendar", "memo", "weather", "fridge",
    "fortune", "countdown", "quote", "custom",
)


class TemplateCreate(BaseModel):
    name: str = Field(min_length=1, max_length=128)
    kind: str = Field(default="custom", pattern="^(album|calendar|memo|weather|fridge|fortune|countdown|quote|custom)$")
    definition: dict[str, Any] = Field(default_factory=dict)
    render_config: dict[str, Any] = Field(default_factory=dict)

    @field_validator("definition")
    @classmethod
    def _check_definition(cls, v: dict) -> dict:
        if not isinstance(v.get("layers"), list):
            raise ValueError("definition.layers 必须为数组")
        return v

    @field_validator("render_config")
    @classmethod
    def _check_render_config(cls, v: dict) -> dict:
        if "dither_type" in v and v["dither_type"] not in DITHER_TYPES:
            raise ValueError(f"dither_type 必须是 {sorted(DITHER_TYPES)} 之一")
        if "palette" in v and v["palette"] not in PALETTES:
            raise ValueError(f"palette 必须是 {sorted(PALETTES)} 之一")
        return v


class TemplateUpdate(BaseModel):
    name: str | None = Field(default=None, min_length=1, max_length=128)
    definition: dict[str, Any] | None = None
    render_config: dict[str, Any] | None = None


class TemplateOut(BaseModel):
    id: int
    name: str
    kind: str
    is_builtin: bool
    definition: dict[str, Any]
    render_config: dict[str, Any]
    thumb_url: str = ""
    created_at: datetime
