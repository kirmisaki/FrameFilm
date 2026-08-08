"""film-hub 后端入口

- 启动时初始化数据库、默认管理员、内置模板
- 挂载管理 API / 设备 API / 静态资源
"""
import json
from contextlib import asynccontextmanager

from fastapi import Depends, FastAPI
from fastapi.middleware.cors import CORSMiddleware
from fastapi.staticfiles import StaticFiles
from sqlalchemy.orm import Session

from .api import ai, album, auth, device, device_proto, settings as settings_api, stream, template
from .config import DATA_DIR, DEFAULT_ADMIN_PASSWORD, DEFAULT_ADMIN_USERNAME, WEB_DIST_DIR
from .db import SessionLocal, get_db, init_db
from .models import Album, Device, Stream, Template, User
from .schemas.template import TemplateOut
from .services.builtin_templates import builtin_definitions
from .utils.security import hash_password


def seed_admin():
    db = SessionLocal()
    try:
        if db.query(User).count() == 0:
            db.add(User(username=DEFAULT_ADMIN_USERNAME,
                        password_hash=hash_password(DEFAULT_ADMIN_PASSWORD)))
            db.commit()
            print(f"[seed] 已创建默认管理员 {DEFAULT_ADMIN_USERNAME}（初始密码 {DEFAULT_ADMIN_PASSWORD}）")
    finally:
        db.close()


def _core_definition(d: dict) -> dict:
    """剔除 data 后的模板结构（内置模板结构锁与升级比较用）"""
    return {k: v for k, v in d.items() if k != "data"}


def _merge_old_data(old_def: dict, new_def: dict) -> dict:
    """内置模板升级：保留旧应用态参数（仍在新 params 定义中的 key）与相册绑定"""
    import copy

    merged = copy.deepcopy(new_def)
    old_data = (old_def or {}).get("data") or {}
    new_keys = {p.get("key") for p in new_def.get("params", [])}
    old_params = old_data.get("params") or {}
    merged["data"]["params"] = {
        **new_def.get("data", {}).get("params", {}),
        **{k: v for k, v in old_params.items() if k in new_keys},
    }
    if "album" in old_data:
        merged["data"]["album"] = old_data["album"]
    return merged


# 旧版内置模板默认渲染参数（用于检测是否需要切换到自适应）
_OLD_DEFAULT_RC = {"dither_type": "floyd_steinberg", "dither_strength": 80, "contrast": 100, "brightness": 0}


def seed_builtin_templates():
    db = SessionLocal()
    try:
        for bt in builtin_definitions():
            t = db.query(Template).filter(
                Template.is_builtin.is_(True), Template.name == bt["name"]).first()
            if t is None:
                db.add(Template(
                    name=bt["name"], kind=bt["kind"], is_builtin=True,
                    definition=json.dumps(bt["definition"], ensure_ascii=False),
                    render_config=json.dumps(bt["render_config"], ensure_ascii=False),
                ))
                continue
            # 同名内置模板：结构变化则升级，保留用户配置
            old_def = json.loads(t.definition or "{}")
            if _core_definition(old_def) != _core_definition(bt["definition"]):
                t.definition = json.dumps(_merge_old_data(old_def, bt["definition"]), ensure_ascii=False)
                t.kind = bt["kind"]
                t.render_config = json.dumps(bt["render_config"], ensure_ascii=False)
                print(f"[seed] 内置模板「{bt['name']}」已升级")
                continue
            # 仍为旧默认渲染参数的内置模板：切换为当前默认（自适应），保留用户自定义
            if json.loads(t.render_config or "{}") == _OLD_DEFAULT_RC:
                t.render_config = json.dumps(bt["render_config"], ensure_ascii=False)
                print(f"[seed] 内置模板「{bt['name']}」渲染算法已切换为自适应")
        db.commit()
        print("[seed] 内置模板就绪")
    finally:
        db.close()


@asynccontextmanager
async def lifespan(app: FastAPI):
    init_db()
    seed_admin()
    seed_builtin_templates()
    yield


app = FastAPI(title="film-hub", version="0.1.0", lifespan=lifespan)

# 局域网跨域（开发期前端 vite dev server 直连）
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_methods=["*"],
    allow_headers=["*"],
)


@app.middleware("http")
async def no_cache_html(request, call_next):
    """HTML 页面禁用缓存：避免改版后浏览器仍显示旧页面（需硬刷新）"""
    response = await call_next(request)
    if response.headers.get("content-type", "").startswith("text/html"):
        response.headers["Cache-Control"] = "no-cache, no-store, must-revalidate"
    return response

# 管理 / 设备 API
app.include_router(auth.router, prefix="/api/v1/admin")
app.include_router(device.router)
app.include_router(album.router)
app.include_router(template.router)
app.include_router(stream.router)
app.include_router(ai.router)
app.include_router(settings_api.router)
app.include_router(device_proto.router)


@app.get("/api/v1/admin/stats/dashboard")
def dashboard(db: Session = Depends(get_db)):
    return {
        "devices": db.query(Device).count(),
        "albums": db.query(Album).count(),
        "templates": db.query(Template).count(),
        "streams": db.query(Stream).count(),
    }


@app.get("/api/v1/admin/system/info")
def system_info():
    import datetime as dt
    import sys

    return {
        "version": app.version,
        "python": sys.version.split()[0],
        "platform": sys.platform,
        "data_dir": str(DATA_DIR),
        "server_time": dt.datetime.now().astimezone().isoformat(),
    }


# 静态资源
app.mount("/files", StaticFiles(directory=str(DATA_DIR)), name="files")
if WEB_DIST_DIR.exists():
    app.mount("/", StaticFiles(directory=str(WEB_DIST_DIR), html=True), name="web")
else:
    @app.get("/")
    def root():
        return {"msg": "film-hub backend is running", "docs": "/docs"}
