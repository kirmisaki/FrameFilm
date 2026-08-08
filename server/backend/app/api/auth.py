"""认证 API：登录 / 修改密码"""
import datetime as dt

from fastapi import APIRouter, Depends, HTTPException
from sqlalchemy.orm import Session

from ..config import JWT_EXPIRE_HOURS
from ..db import get_db
from ..models import User
from ..schemas.auth import LoginRequest, PasswordChangeRequest, TokenResponse
from ..utils.security import create_jwt, hash_password, verify_password
from .deps import get_current_user

router = APIRouter(prefix="/auth", tags=["auth"])


@router.post("/login", response_model=TokenResponse)
def login(body: LoginRequest, db: Session = Depends(get_db)):
    user = db.query(User).filter(User.username == body.username).first()
    if user is None or not verify_password(body.password, user.password_hash):
        raise HTTPException(401, "用户名或密码错误")
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
