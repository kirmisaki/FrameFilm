"""通用键值设置（settings 表，key="ui" 存前端 UI 偏好等）"""
import json

from sqlalchemy.orm import Session

from ..models.setting import Setting


def load_ui_settings(db: Session) -> dict:
    s = db.get(Setting, "ui")
    if not s:
        return {"tpl_preview_scale": 0.8}
    try:
        val = json.loads(s.value)
        return val if isinstance(val, dict) else {"tpl_preview_scale": 0.8}
    except (TypeError, ValueError):
        return {"tpl_preview_scale": 0.8}


def save_ui_settings(db: Session, ui: dict) -> None:
    s = db.get(Setting, "ui")
    if s is None:
        db.add(Setting(key="ui", value=json.dumps(ui, ensure_ascii=False)))
    else:
        s.value = json.dumps(ui, ensure_ascii=False)
    db.commit()
