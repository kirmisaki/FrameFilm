"""AI API：配置 / AI 创建模板 / AI 生图"""
import io
import json
import os

import httpx
from fastapi import APIRouter, Depends, HTTPException
from PIL import Image
from sqlalchemy.orm import Session

from ..config import ORIGINALS_DIR, THUMBS_DIR
from ..db import get_db
from ..models import Album, Photo, Template, User
from ..schemas.ai import AIImageRequest, AISettings, AITemplateRequest
from ..schemas.template import TemplateOut
from ..services import ai_client
from .deps import get_current_user

router = APIRouter(prefix="/api/v1/admin", tags=["ai"])


@router.get("/settings/ai")
def get_ai_settings(db: Session = Depends(get_db), _: User = Depends(get_current_user)):
    s = ai_client.load_ai_settings(db)
    return {
        "base_url": s.get("base_url", ""),
        "model": s.get("model", ""),
        "image_model": s.get("image_model", ""),
        "api_key_set": bool(s.get("api_key")),
    }


@router.put("/settings/ai")
def put_ai_settings(body: AISettings, db: Session = Depends(get_db),
                    _: User = Depends(get_current_user)):
    current = ai_client.load_ai_settings(db)
    new = {
        "base_url": body.base_url,
        "model": body.model,
        "image_model": body.image_model,
        "api_key": body.api_key or current.get("api_key", ""),
    }
    ai_client.save_ai_settings(db, new)
    return {"msg": "AI 配置已保存"}


@router.post("/settings/ai/test")
async def test_ai_connection(db: Session = Depends(get_db),
                             _: User = Depends(get_current_user)):
    """用已保存的配置做一次最小对话请求，验证连通性"""
    import time

    settings = ai_client.load_ai_settings(db)
    base_url = (settings.get("base_url") or "").rstrip("/")
    api_key = settings.get("api_key") or ""
    model = settings.get("model") or ""
    if not base_url or not api_key or not model:
        raise HTTPException(400, "AI 配置不完整（Base URL / API Key / 模型）")

    url = f"{base_url}/chat/completions"
    headers = {"Authorization": f"Bearer {api_key}"}
    payload = {"model": model, "messages": [{"role": "user", "content": "ping"}], "max_tokens": 1}
    t0 = time.monotonic()
    try:
        async with httpx.AsyncClient(timeout=15.0) as client:
            resp = await client.post(url, json=payload, headers=headers)
            resp.raise_for_status()
        return {"ok": True, "latency_ms": int((time.monotonic() - t0) * 1000)}
    except Exception as e:
        return {"ok": False, "latency_ms": int((time.monotonic() - t0) * 1000),
                "error": str(e)[:200]}


@router.post("/ai/template", response_model=TemplateOut)
async def ai_create_template(body: AITemplateRequest, db: Session = Depends(get_db),
                             _: User = Depends(get_current_user)):
    """AI 根据自然语言创建自定义模板"""
    try:
        definition = await ai_client.chat_json(db, body.prompt)
    except RuntimeError as e:
        raise HTTPException(400, str(e))
    except Exception as e:
        raise HTTPException(502, f"AI 调用失败：{e}")
    if not isinstance(definition, dict) or not isinstance(definition.get("layers"), list):
        raise HTTPException(502, "AI 返回的模板结构无效")
    name = definition.get("_name") or body.prompt[:30]
    t = Template(
        name=name, kind="custom", is_builtin=False,
        definition=json.dumps(definition, ensure_ascii=False),
        render_config=json.dumps({
            "dither_type": "floyd_steinberg", "dither_strength": 80,
            "contrast": 100, "brightness": 0,
        }),
    )
    db.add(t)
    db.commit()
    db.refresh(t)
    return TemplateOut(
        id=t.id, name=t.name, kind=t.kind, is_builtin=False,
        definition=json.loads(t.definition), render_config=json.loads(t.render_config),
        created_at=t.created_at,
    )


@router.post("/ai/image")
async def ai_generate_image(body: AIImageRequest, db: Session = Depends(get_db),
                            _: User = Depends(get_current_user)):
    """AI 生图并保存到相册"""
    try:
        png = await ai_client.generate_image(db, body.prompt, body.size)
    except RuntimeError as e:
        raise HTTPException(400, str(e))
    except Exception as e:
        raise HTTPException(502, f"AI 生图失败：{e}")

    # 目标相册
    if body.album_id:
        album = db.get(Album, body.album_id)
        if album is None:
            raise HTTPException(404, "相册不存在")
    else:
        album = Album(name=f"AI 生成 {body.prompt[:20]}")
        db.add(album)
        db.flush()

    img = Image.open(io.BytesIO(png)).convert("RGB")
    sort = max((p.sort for p in album.photos), default=-1) + 1
    photo = Photo(album_id=album.id, filename=f"ai_{int(__import__('time').time())}.png",
                  width=img.width, height=img.height, sort=sort)
    db.add(photo)
    db.flush()

    orig_path = ORIGINALS_DIR / f"{photo.id}.png"
    orig_path.write_bytes(png)
    photo.original_path = str(orig_path)

    thumb = img.copy()
    thumb.thumbnail((320, 320))
    thumb_path = THUMBS_DIR / f"{photo.id}.jpg"
    thumb.convert("RGB").save(thumb_path, "JPEG", quality=85)

    if album.cover_photo_id is None:
        album.cover_photo_id = photo.id
    db.commit()
    return {
        "photo_id": photo.id,
        "album_id": album.id,
        "original_url": f"/files/originals/{photo.id}.png",
        "thumb_url": f"/files/thumbs/{photo.id}.jpg",
        "width": img.width, "height": img.height,
    }
