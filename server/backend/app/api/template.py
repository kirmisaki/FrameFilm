"""模板库 API"""
import json

from fastapi import APIRouter, Depends, HTTPException, Query, Response
from sqlalchemy.orm import Session

from ..config import PREVIEWS_DIR, SCREENS, THUMBS_DIR
from ..db import get_db
from ..models import Template, User
from ..schemas.template import TemplateCreate, TemplateOut, TemplateUpdate
from ..services import film_convert, renderer
from .deps import get_current_user

router = APIRouter(prefix="/api/v1/admin/templates", tags=["templates"])


def _template_out(t: Template) -> TemplateOut:
    return TemplateOut(
        id=t.id, name=t.name, kind=t.kind, is_builtin=t.is_builtin,
        definition=json.loads(t.definition or "{}"),
        render_config=json.loads(t.render_config or "{}"),
        thumb_url=f"/files/thumbs/tpl_{t.id}.png" if _thumb_exists(t) else "",
        created_at=t.created_at,
    )


def _thumb_exists(t: Template) -> bool:
    return (THUMBS_DIR / f"tpl_{t.id}.png").exists()


def _get_template(db: Session, template_id: int) -> Template:
    t = db.get(Template, template_id)
    if t is None:
        raise HTTPException(404, "模板不存在")
    return t


def _render_and_cache(t: Template, width: int, height: int, scale: float = 0.8) -> bytes:
    """渲染模板 -> 转换 -> 返回 preview PNG（scale 为预览倍率，0.8/1.0 分别对应快速/设备分辨率封面）"""
    rc = json.loads(t.render_config or "{}")
    img = renderer.render_template(t, width, height)
    _, preview = film_convert.convert_image(img, width, height, rc, preview_scale=scale)
    # 缩略图缓存
    thumb_path = THUMBS_DIR / f"tpl_{t.id}.png"
    thumb = img.copy()
    thumb.thumbnail((320, 480))
    thumb.save(thumb_path, "PNG")
    return preview


@router.get("", response_model=list[TemplateOut])
def list_templates(db: Session = Depends(get_db), _: User = Depends(get_current_user)):
    return [_template_out(t) for t in db.query(Template).order_by(Template.id).all()]


@router.get("/{template_id}", response_model=TemplateOut)
def get_template(template_id: int, db: Session = Depends(get_db),
                 _: User = Depends(get_current_user)):
    return _template_out(_get_template(db, template_id))


@router.post("", response_model=TemplateOut)
def create_template(body: TemplateCreate, db: Session = Depends(get_db),
                    _: User = Depends(get_current_user)):
    t = Template(
        name=body.name, kind=body.kind, is_builtin=False,
        definition=json.dumps(body.definition, ensure_ascii=False),
        render_config=json.dumps(body.render_config, ensure_ascii=False),
    )
    db.add(t)
    db.commit()
    db.refresh(t)
    return _template_out(t)


def _core_definition(d: dict) -> dict:
    """剔除数据配置后剩余的模板结构（用于内置模板结构校验）"""
    core = {k: v for k, v in d.items() if k != "data"}
    layers = []
    for layer in core.get("layers", []):
        l = dict(layer)
        src = l.get("source")
        if isinstance(src, dict):
            l["source"] = {k: v for k, v in src.items() if k != "album_id"}
        layers.append(l)
    core["layers"] = layers
    return core


@router.put("/{template_id}", response_model=TemplateOut)
def update_template(template_id: int, body: TemplateUpdate, db: Session = Depends(get_db),
                    _: User = Depends(get_current_user)):
    t = _get_template(db, template_id)
    data = body.model_dump(exclude_unset=True)
    if t.is_builtin and "definition" in data:
        # 内置模板：仅允许配置数据（如相册 album_id），结构锁定
        if _core_definition(data["definition"]) != _core_definition(json.loads(t.definition or "{}")):
            raise HTTPException(400, "内置模板不可修改结构，仅可配置数据（如相册）与渲染参数")
        t.definition = json.dumps(data["definition"], ensure_ascii=False)
    if "render_config" in data:
        t.render_config = json.dumps(data["render_config"], ensure_ascii=False)
    if "name" in data:
        t.name = data["name"]
    db.commit()
    db.refresh(t)
    return _template_out(t)


@router.delete("/{template_id}")
def delete_template(template_id: int, db: Session = Depends(get_db),
                    _: User = Depends(get_current_user)):
    t = _get_template(db, template_id)
    if t.is_builtin:
        raise HTTPException(400, "内置模板不可删除")
    # 清理渲染缓存（预览 PNG 与缩略图）
    for device_type in SCREENS:
        pv = PREVIEWS_DIR / f"tpl_{t.id}_{device_type}.png"
        if pv.exists():
            pv.unlink()
    thumb = THUMBS_DIR / f"tpl_{t.id}.png"
    if thumb.exists():
        thumb.unlink()
    db.delete(t)
    db.commit()
    return {"msg": "已删除模板"}


@router.get("/{template_id}/preview")
def preview_template(
    template_id: int,
    device_type: str = Query(default="basic", pattern="^(basic|pro)$"),
    refresh: bool = Query(default=False),
    scale: float = Query(default=0.8, ge=0.25, le=1.0),
    db: Session = Depends(get_db),
    _: User = Depends(get_current_user),
):
    """服务端渲染预览 PNG（默认缓存，refresh=true 强制重渲染；scale 指定渲染倍率，缓存按倍率区分）"""
    t = _get_template(db, template_id)
    scr = SCREENS[device_type]
    scale = max(0.25, min(1.0, scale))
    s_key = f"s{int(round(scale * 100))}"
    preview_path = PREVIEWS_DIR / f"tpl_{t.id}_{device_type}_{s_key}.png"
    if not preview_path.exists() or refresh:
        png = _render_and_cache(t, scr["canvas_w"], scr["canvas_h"], scale)
        preview_path.write_bytes(png)
    return Response(content=preview_path.read_bytes(), media_type="image/png")


@router.post("/{template_id}/preview")
def preview_template_with_params(
    template_id: int,
    body: dict | None = None,
    db: Session = Depends(get_db),
    _: User = Depends(get_current_user),
):
    """应用态实时预览：POST {"params": {...}, "render_config": {...}, "device_type": "basic|pro"}，临时参数不落库、不缓存"""
    body = body or {}
    device_type = body.get("device_type", "basic")
    if device_type not in SCREENS:
        raise HTTPException(400, "device_type 必须是 basic 或 pro")
    t = _get_template(db, template_id)
    scr = SCREENS[device_type]
    img = renderer.render_template(t, scr["canvas_w"], scr["canvas_h"], params=body.get("params"))
    rc = json.loads(t.render_config or "{}")
    if isinstance(body.get("render_config"), dict):
        rc = {**rc, **body["render_config"]}
    try:
        scale = max(0.25, min(1.0, float(body.get("preview_scale", 0.6))))
    except (TypeError, ValueError):
        scale = 0.6
    _, preview = film_convert.convert_image(img, scr["canvas_w"], scr["canvas_h"], rc, preview_scale=scale)
    return Response(content=preview, media_type="image/png")
