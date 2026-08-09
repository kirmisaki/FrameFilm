"""认证 API：登录 / 修改密码"""
import datetime as dt
import threading
import time

from fastapi import APIRouter, Depends, HTTPException, Request
from sqlalchemy.orm import Session

from ..config import JWT_EXPIRE_HOURS
from ..db import get_db
from ..models import User
from ..schemas.auth import LoginRequest, PasswordChangeRequest, TokenResponse
from ..utils.security import create_jwt, hash_password, verify_password
from .deps import get_current_user

router = APIRouter(prefix="/auth", tags=["auth"])

# 简单登录限流：同一 IP 15 分钟内失败超过 5 次则锁定 15 分钟（内存态，单进程有效）
_LOGIN_WINDOW = 15 * 60
_LOGIN_MAX_FAIL = 5
_login_failures: dict[str, list[float]] = {}
_login_lock = threading.Lock()


def _login_blocked(ip: str) -> bool:
    now = time.monotonic()
    with _login_lock:
        fails = [t for t in _login_failures.get(ip, []) if now - t < _LOGIN_WINDOW]
        _login_failures[ip] = fails
        return len(fails) >= _LOGIN_MAX_FAIL


def _login_fail(ip: str) -> None:
    with _login_lock:
        _login_failures.setdefault(ip, []).append(time.monotonic())


def _login_ok(ip: str) -> None:
    with _login_lock:
        _login_failures.pop(ip, None)


@router.post("/login", response_model=TokenResponse)
def login(body: LoginRequest, request: Request, db: Session = Depends(get_db)):
    ip = request.client.host if request.client else "unknown"
    if _login_blocked(ip):
        raise HTTPException(429, "尝试次数过多，请 15 分钟后再试")
    user = db.query(User).filter(User.username == body.username).first()
    if user is None or not verify_password(body.password, user.password_hash):
        _login_fail(ip)
        raise HTTPException(401, "用户名或密码错误")
    _login_ok(ip)
    return TokenResponse(
        access_token=create_jwt(user.id, user.ver),
        expires_in=int(dt.timedelta(hours=JWT_EXPIRE_HOURS).total_seconds()),
        username=user.username,
    )


@router.put("/password")
def change_password(
    body: PasswordChangeRequest,
    user: User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    if not verify_password(body.old_password, user.password_hash):
        raise HTTPException(400, "原密码错误")
    user.password_hash = hash_password(body.new_password)
    user.ver += 1  # 使旧 token 失效
    db.commit()
    return {"msg": "密码已修改"}
