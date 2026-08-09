"""相册与照片管理 API"""
import io
import os
import shutil

from fastapi import APIRouter, Depends, File, HTTPException, UploadFile
from PIL import Image
from sqlalchemy.orm import Session

from ..config import ORIGINALS_DIR, THUMBS_DIR
from ..db import get_db
from ..models import Album, Photo, User
from ..schemas.album import (
    AlbumCreate,
    AlbumListOut,
    AlbumOut,
    AlbumUpdate,
    PhotoLayoutBody,
    PhotoOut,
    PhotoSortRequest,
    UploadResult,
)
from .deps import get_current_user

router = APIRouter(prefix="/api/v1/admin", tags=["albums"])

ALLOWED_EXT = {".jpg", ".jpeg", ".png", ".webp", ".bmp", ".gif"}
THUMB_WIDTH = 320  # 缩略图宽度
MAX_UPLOAD_BYTES = 20 * 1024 * 1024  # 单文件大小上限 20MB
MAX_IMAGE_PIXELS = 40_000_000  # 解压炸弹防护：解码像素上限 4000 万


def _photo_out(p: Photo) -> PhotoOut:
    ext = os.path.splitext(p.filename or "")[1] or ".jpg"
    return PhotoOut(
        id=p.id, album_id=p.album_id, filename=p.filename,
        width=p.width, height=p.height, sort=p.sort, created_at=p.created_at,
        layout=p.layout or "{}",
        thumb_url=f"/files/thumbs/{p.id}.jpg",
        original_url=f"/files/originals/{p.id}{ext}",
    )


def _album_out(a: Album, with_photos: bool = False) -> AlbumOut:
    photos = [_photo_out(p) for p in a.photos]
    cover = next((p for p in a.photos if p.id == a.cover_photo_id), None) or (a.photos[0] if a.photos else None)
    return AlbumOut(
        id=a.id, name=a.name, description=a.description,
        cover_photo_id=a.cover_photo_id, photo_count=len(a.photos),
        created_at=a.created_at,
        cover_url=f"/files/thumbs/{cover.id}.jpg" if cover else "",
        photos=photos if with_photos else [],
    )


def _get_album(db: Session, album_id: int) -> Album:
    a = db.get(Album, album_id)
    if a is None:
        raise HTTPException(404, "相册不存在")
    return a


@router.post("/albums", response_model=AlbumListOut)
def create_album(body: AlbumCreate, db: Session = Depends(get_db), _: User = Depends(get_current_user)):
    a = Album(name=body.name, description=body.description)
    db.add(a)
    db.commit()
    db.refresh(a)
    return AlbumListOut(id=a.id, name=a.name, description=a.description,
                        cover_photo_id=None, photo_count=0,
                        created_at=a.created_at).model_dump()


@router.get("/albums", response_model=list[AlbumListOut])
def list_albums(db: Session = Depends(get_db), _: User = Depends(get_current_user)):
    out = []
    for a in db.query(Album).order_by(Album.id).all():
        cover = next((p for p in a.photos if p.id == a.cover_photo_id), None) or (a.photos[0] if a.photos else None)
        out.append(AlbumListOut(
            id=a.id, name=a.name, description=a.description, cover_photo_id=a.cover_photo_id,
            photo_count=len(a.photos), created_at=a.created_at,
            cover_url=f"/files/thumbs/{cover.id}.jpg" if cover else "",
        ))
    return out


@router.get("/albums/{album_id}", response_model=AlbumOut)
def get_album(album_id: int, db: Session = Depends(get_db), _: User = Depends(get_current_user)):
    return _album_out(_get_album(db, album_id), with_photos=True)


@router.put("/albums/{album_id}", response_model=AlbumListOut)
def update_album(album_id: int, body: AlbumUpdate, db: Session = Depends(get_db),
                 _: User = Depends(get_current_user)):
    a = _get_album(db, album_id)
    data = body.model_dump(exclude_unset=True)
    for k, v in data.items():
        setattr(a, k, v)
    db.commit()
    db.refresh(a)
    return AlbumListOut(id=a.id, name=a.name, description=a.description,
                        cover_photo_id=a.cover_photo_id, photo_count=len(a.photos),
                        created_at=a.created_at)


@router.delete("/albums/{album_id}")
def delete_album(album_id: int, db: Session = Depends(get_db), _: User = Depends(get_current_user)):
    a = _get_album(db, album_id)
    for p in a.photos:
        for path in (p.original_path, p.preview_path):
            if path and os.path.exists(path):
                os.remove(path)
        thumb = THUMBS_DIR / f"{p.id}.jpg"
        if thumb.exists():
            thumb.unlink()
    db.delete(a)
    db.commit()
    return {"msg": "已删除相册"}


@router.post("/albums/{album_id}/photos/batch", response_model=list[UploadResult])
async def upload_photos(
    album_id: int,
    files: list[UploadFile] = File(...),
    db: Session = Depends(get_db),
    _: User = Depends(get_current_user),
):
    a = _get_album(db, album_id)
    results: list[UploadResult] = []
    for f in files:
        ext = os.path.splitext(f.filename or "")[1].lower()
        if ext not in ALLOWED_EXT:
            continue
        # 大小上限：分块读取，超限即拒绝
        raw = bytearray()
        while True:
            chunk = await f.read(1024 * 1024)
            if not chunk:
                break
            raw.extend(chunk)
            if len(raw) > MAX_UPLOAD_BYTES:
                raise HTTPException(413, f"文件超过大小上限 {MAX_UPLOAD_BYTES // 1024 // 1024}MB")
        if not raw:
            continue
        try:
            with Image.open(io.BytesIO(raw)) as img:
                # 解压炸弹防护：限制解码像素量
                if img.width * img.height > MAX_IMAGE_PIXELS:
                    raise HTTPException(413, "图片尺寸过大")
                img.load()
                img = img.convert("RGB")
                width, height = img.width, img.height
        except HTTPException:
            raise
        except Exception:
            continue

        # 先落库拿 id
        sort = max((p.sort for p in a.photos), default=-1) + 1
        photo = Photo(album_id=album_id, filename=f.filename or f"photo{ext}", sort=sort,
                      width=img.width, height=img.height)
        db.add(photo)
        db.flush()

        orig_path = ORIGINALS_DIR / f"{photo.id}{ext}"
        with open(orig_path, "wb") as out:
            out.write(raw)
        photo.original_path = str(orig_path)

        # 缩略图
        thumb = img.copy()
        thumb.thumbnail((THUMB_WIDTH, THUMB_WIDTH * 3))
        thumb_path = THUMBS_DIR / f"{photo.id}.jpg"
        thumb.convert("RGB").save(thumb_path, "JPEG", quality=85)

        if a.cover_photo_id is None:
            a.cover_photo_id = photo.id
        db.commit()
        results.append(UploadResult(id=photo.id, filename=photo.filename, width=img.width,
                                    height=img.height, original_url=f"/files/originals/{photo.id}{ext}"))
    if not results:
        raise HTTPException(400, "没有可导入的图片（仅支持 jpg/png/webp/bmp/gif）")
    return results


@router.delete("/photos/{photo_id}")
def delete_photo(photo_id: int, db: Session = Depends(get_db), _: User = Depends(get_current_user)):
    p = db.get(Photo, photo_id)
    if p is None:
        raise HTTPException(404, "照片不存在")
    for path in (p.original_path, p.preview_path):
        if path and os.path.exists(path):
            os.remove(path)
    thumb = THUMBS_DIR / f"{p.id}.jpg"
    if thumb.exists():
        thumb.unlink()
    album = db.get(Album, p.album_id)
    if album and album.cover_photo_id == p.id:
        album.cover_photo_id = None
    db.delete(p)
    db.commit()
    return {"msg": "已删除照片"}


@router.put("/albums/{album_id}/photos/sort")
def sort_photos(album_id: int, body: PhotoSortRequest, db: Session = Depends(get_db),
                _: User = Depends(get_current_user)):
    _get_album(db, album_id)
    by_id = {p.id: p for p in db.query(Photo).filter(Photo.album_id == album_id).all()}
    for pos, pid in enumerate(body.photo_ids):
        if pid in by_id:
            by_id[pid].sort = pos
    db.commit()
    return {"msg": "排序已更新"}


@router.put("/albums/{album_id}/photos/{photo_id}/layout")
def set_photo_layout(album_id: int, photo_id: int, body: PhotoLayoutBody,
                     db: Session = Depends(get_db), _: User = Depends(get_current_user)):
    """保存照片显示布局 {scale, x, y}（相对 400×600 画布），模板渲染与设备输出均按此布局"""
    import json

    _get_album(db, album_id)
    p = db.get(Photo, photo_id)
    if p is None or p.album_id != album_id:
        raise HTTPException(404, "照片不存在")
    scale = max(0.5, min(4.0, body.scale))
    rotate = int(body.rotate or 0) % 360
    p.layout = json.dumps({"scale": round(scale, 3), "x": int(body.x), "y": int(body.y),
                           "rotate": rotate})
    db.commit()
    return {"msg": "布局已保存", "layout": p.layout}
