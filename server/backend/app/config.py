"""全局配置"""
import os
from pathlib import Path

# 路径（server/backend/app/config.py -> server/）
SERVER_DIR = Path(__file__).resolve().parents[2]
DATA_DIR = SERVER_DIR / "data"
ORIGINALS_DIR = DATA_DIR / "originals"
FILMS_DIR = DATA_DIR / "films"
PREVIEWS_DIR = DATA_DIR / "previews"
THUMBS_DIR = DATA_DIR / "thumbs"
DB_PATH = DATA_DIR / "filmhub.db"
WEB_DIST_DIR = SERVER_DIR / "web" / "dist"

for _d in (DATA_DIR, ORIGINALS_DIR, FILMS_DIR, PREVIEWS_DIR, THUMBS_DIR):
    _d.mkdir(parents=True, exist_ok=True)

# 服务配置
HOST = os.getenv("FILMHUB_HOST", "0.0.0.0")
PORT = int(os.getenv("FILMHUB_PORT", "8000"))

# 安全
def _load_or_create_secret() -> str:
    """JWT 密钥：优先环境变量；否则使用持久化的随机密钥（首次生成后落盘），
    避免默认硬编码密钥导致可伪造 token。"""
    env = os.getenv("FILMHUB_SECRET")
    if env:
        return env
    secret_file = DATA_DIR / "jwt_secret"
    if secret_file.exists():
        return secret_file.read_text(encoding="utf-8").strip()
    import secrets as _secrets

    secret = _secrets.token_hex(32)
    secret_file.write_text(secret, encoding="utf-8")
    print(f"[config] 已生成 JWT 密钥并保存至 {secret_file}")
    return secret


JWT_SECRET = _load_or_create_secret()
JWT_ALGORITHM = "HS256"
JWT_EXPIRE_HOURS = 24

# 默认管理员（首次启动自动创建；生产环境务必通过环境变量覆盖）
DEFAULT_ADMIN_USERNAME = os.getenv("FILMHUB_ADMIN", "admin")
DEFAULT_ADMIN_PASSWORD = os.getenv("FILMHUB_ADMIN_PASSWORD", "filmhub")
if not os.getenv("FILMHUB_ADMIN_PASSWORD") and os.getenv("FILMHUB_ADMIN") is None:
    print("[config] 警告：正在使用默认管理员密码（admin/filmhub），请设置 FILMHUB_ADMIN_PASSWORD 环境变量")

# 屏幕配置（设备类型 -> 分辨率参数，对齐小程序 film-utils.js DEVICE_CONFIGS）
# - canvas_w/h：渲染/模板画布（竖屏，用户创作视角）
# - width/height：film 文件头与固件驱动分辨率（横屏）
# - pixel_layout：像素写入布局（rotated=列优先翻转 / row-major=行优先）
SCREENS = {
    "basic": {
        "canvas_w": 400, "canvas_h": 600,
        "width": 600, "height": 400,
        "pixel_layout": "rotated",
    },
    "pro": {
        "canvas_w": 528, "canvas_h": 792,
        "width": 792, "height": 528,
        "pixel_layout": "row-major",
    },
}

# 心跳参数
HEARTBEAT_DEFAULT_INTERVAL = 60  # 秒
HEARTBEAT_MIN_INTERVAL = 5
HEARTBEAT_MAX_INTERVAL = 180
ONLINE_FACTOR = 3  # 超过 3 倍间隔未心跳视为离线

# AI 配置默认值
AI_DEFAULT_BASE_URL = "https://api.deepseek.com/v1"
AI_DEFAULT_MODEL = "deepseek-chat"
