"""前端 UI 偏好设置（渲染倍率等）"""
from fastapi import APIRouter, Depends
from sqlalchemy.orm import Session

from ..db import get_db
from ..models import User
from ..services.settings_service import load_ui_settings, save_ui_settings
from .deps import get_current_user

router = APIRouter(prefix="/api/v1/admin/settings", tags=["settings"])


@router.get("/ui")
def get_ui_settings(db: Session = Depends(get_db), _: User = Depends(get_current_user)):
    return load_ui_settings(db)


@router.put("/ui")
def put_ui_settings(body: dict, db: Session = Depends(get_db), _: User = Depends(get_current_user)):
    ui = load_ui_settings(db)
    if "tpl_preview_scale" in body:
        try:
            v = float(body["tpl_preview_scale"])
        except (TypeError, ValueError):
            v = 0.8
        ui["tpl_preview_scale"] = 1.0 if v >= 1.0 else 0.8
    save_ui_settings(db, ui)
    return ui
