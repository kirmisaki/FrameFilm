"""安全工具：bcrypt 密码 + JWT"""
import datetime as dt

import bcrypt
import jwt

from ..config import JWT_ALGORITHM, JWT_EXPIRE_HOURS, JWT_SECRET


def hash_password(password: str) -> str:
    return bcrypt.hashpw(password.encode("utf-8"), bcrypt.gensalt()).decode("utf-8")


def verify_password(password: str, password_hash: str) -> bool:
    try:
        return bcrypt.checkpw(password.encode("utf-8"), password_hash.encode("utf-8"))
    except ValueError:
        return False


def create_jwt(user_id: int, ver: int) -> str:
    payload = {
        "sub": str(user_id),
        "ver": ver,
        "exp": dt.datetime.now(dt.timezone.utc) + dt.timedelta(hours=JWT_EXPIRE_HOURS),
    }
    return jwt.encode(payload, JWT_SECRET, algorithm=JWT_ALGORITHM)


def decode_jwt(token: str) -> dict:
    """解析 JWT，失败抛 jwt.PyJWTError"""
    return jwt.decode(token, JWT_SECRET, algorithms=[JWT_ALGORITHM])
