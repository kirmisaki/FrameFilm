#!/usr/bin/env python3
"""FrameFilm Dock 图片上传工具

把一张图片转换成 .film 文件，并通过 USB 虚拟串口(CDC) 上传到 FrameFilm Dock。

用法示例:
    python tools/framefilm-dock-upload/scripts/dock_upload.py photo.jpg
    python tools/framefilm-dock-upload/scripts/dock_upload.py photo.jpg --port COM7 --name holiday.film --dither atkinson
    python tools/framefilm-dock-upload/scripts/dock_upload.py --list        # 只列出设备上的文件
    python tools/framefilm-dock-upload/scripts/dock_upload.py --film x.film # 上传现成的 .film 文件

依赖: pyserial, Pillow
    pip install pyserial Pillow

协议: 帧格式 HEAD(0x55) | CH | LEN | DATA | SUM，与 BLE 链路完全一致。
"""

import argparse
import os
import struct
import sys
import time

# ---------------------------------------------------------------- 协议常量
CMD_HEAD = 0x55

CH_FILE_NAME = 0x00
CH_FILE_LEN = 0x01
CH_FILE_DATA = 0x02
CH_FILE_START = 0x03
CH_FILE_STOP = 0x04
CH_FILE_DELETE = 0x05
CH_FILE_LIST = 0x06
CH_FILE_DISPLAY = 0x07
CH_FILE_DISPLAY_GET = 0x08
CH_SCREEN_RES_GET = 0x42

# Dock 屏幕参数（3.64" 760x568，面板 ID 0x06）
DOCK_PANEL_ID = 0x06
DOCK_WIDTH = 760
DOCK_HEIGHT = 568
DOCK_FILE_SIZE = 32 + (DOCK_WIDTH * DOCK_HEIGHT) // 2  # 215872

# USB 设备标识（与固件 hal_usb.c 一致）
ESPRESSIF_VID = 0x303A
DOCK_PID = 0x8000

# 单帧数据块大小与块间隔：固件接收队列有限，必须限速，否则丢数据
CHUNK_SIZE = 192
CHUNK_DELAY = 0.002


# ---------------------------------------------------------------- 帧编解码
def build_frame(ch, data=b""):
    """组一个命令帧"""
    body = bytes([CMD_HEAD, ch, len(data)]) + bytes(data)
    return body + bytes([sum(body) & 0xFF])


def _parse_frames(buf):
    """从字节流缓冲中解析完整帧，就地消费已解析部分"""
    out = []
    i = 0
    while len(buf) - i >= 4:
        if buf[i] != CMD_HEAD:
            i += 1
            continue
        data_len = buf[i + 2]
        total = data_len + 4
        if len(buf) - i < total:
            break
        if (sum(buf[i:i + 3 + data_len]) & 0xFF) == buf[i + 3 + data_len]:
            out.append((buf[i + 1], bytes(buf[i + 3:i + 3 + data_len])))
            i += total
        else:
            i += 1
    if i:
        del buf[:i]
    return out


def read_frames(ser, duration):
    """在 duration 秒内持续读取并解析帧"""
    end = time.time() + duration
    buf = bytearray()
    frames = []
    while time.time() < end:
        pending = ser.in_waiting
        chunk = ser.read(pending if pending else 1)
        if chunk:
            buf.extend(chunk)
            frames.extend(_parse_frames(buf))
    return frames


# ---------------------------------------------------------------- 串口
def find_dock_port():
    """按 VID/PID 自动查找 Dock 的串口"""
    try:
        from serial.tools import list_ports
    except ImportError:
        return None, []

    candidates = []
    for p in list_ports.comports():
        if p.vid == ESPRESSIF_VID and p.pid == DOCK_PID:
            return p.device, []
        candidates.append(p)
    return None, candidates


def open_port(port=None, baud=115200):
    try:
        import serial  # pyserial
    except ImportError:
        raise RuntimeError("缺少依赖 pyserial，请先执行: pip install pyserial Pillow")

    if port is None:
        port, others = find_dock_port()
        if port is None:
            names = ", ".join("%s(%s)" % (p.device, p.description) for p in others) or "无"
            raise RuntimeError(
                "未找到 FrameFilm Dock 串口（VID=0x%04X PID=0x%04X）。\n"
                "请确认：1) dock 已用 USB 连到电脑；2) 设备管理器里出现了对应的 COM 口。\n"
                "当前可用串口: %s\n可用 --port 手工指定。" % (ESPRESSIF_VID, DOCK_PID, names)
            )

    ser = serial.Serial(port=port, baudrate=baud, timeout=0.1, write_timeout=5)
    time.sleep(0.3)
    ser.reset_input_buffer()
    return ser


def query_screen(ser):
    """查询屏幕参数，用于确认对端确实是 Dock"""
    ser.reset_input_buffer()
    ser.write(build_frame(CH_SCREEN_RES_GET))
    for ch, data in read_frames(ser, 1.5):
        if ch == CH_SCREEN_RES_GET and len(data) == 5:
            return {
                "panel": data[0],
                "width": (data[1] << 8) | data[2],
                "height": (data[3] << 8) | data[4],
            }
    return None


def list_files(ser, wait=1.5):
    """查询设备文件列表，返回 [(id, name), ...]"""
    ser.reset_input_buffer()
    ser.write(build_frame(CH_FILE_LIST))
    files = []
    for ch, data in read_frames(ser, wait):
        if ch == CH_FILE_LIST and len(data) >= 2:
            file_id = data[0]
            name_len = data[1]
            if name_len > 1 and 2 + name_len <= len(data):
                name = data[2:2 + name_len].split(b"\x00")[0].decode("utf-8", "replace")
                files.append((file_id, name))
    return files


def display_file(ser, file_id):
    ser.write(build_frame(CH_FILE_DISPLAY, bytes([file_id & 0xFF])))
    time.sleep(0.2)


# ---------------------------------------------------------------- 上传
def upload_film(ser, film_bytes, filename, chunk_delay=CHUNK_DELAY, progress=True):
    """按 FILE_START -> NAME -> LEN -> DATA* -> STOP 的顺序上传"""
    name_bytes = filename.encode("utf-8")
    if len(name_bytes) > 255:
        raise ValueError("文件名过长（最多 255 字节）")

    total = len(film_bytes)
    ser.write(build_frame(CH_FILE_START))
    time.sleep(0.05)
    ser.write(build_frame(CH_FILE_NAME, name_bytes))
    time.sleep(0.05)
    # 文件长度为大端 4 字节
    ser.write(build_frame(CH_FILE_LEN, struct.pack(">I", total)))
    time.sleep(0.05)

    sent = 0
    for i in range(0, total, CHUNK_SIZE):
        ser.write(build_frame(CH_FILE_DATA, film_bytes[i:i + CHUNK_SIZE]))
        sent += min(CHUNK_SIZE, total - i)
        if progress and (sent % (CHUNK_SIZE * 20) == 0 or sent == total):
            sys.stdout.write("\r  上传中 %d/%d 字节 (%d%%)" % (sent, total, sent * 100 // total))
            sys.stdout.flush()
        time.sleep(chunk_delay)
    if progress:
        sys.stdout.write("\n")

    ser.write(build_frame(CH_FILE_STOP))
    ser.flush()
    return total


# ---------------------------------------------------------------- 图片转换
def find_repo_root():
    """定位仓库根目录（需要其中的 server/backend 转换实现）"""
    marker = os.path.join("server", "backend", "app", "services", "film_convert.py")

    env = os.environ.get("FRAMEFILM_REPO")
    if env and os.path.isfile(os.path.join(env, marker)):
        return env

    starts = [os.path.dirname(os.path.abspath(__file__)), os.getcwd()]
    for start in starts:
        cur = start
        while True:
            if os.path.isfile(os.path.join(cur, marker)):
                return cur
            parent = os.path.dirname(cur)
            if parent == cur:
                break
            cur = parent
    return None


def image_to_film(path, dither="floyd_steinberg", strength=80, contrast=100,
                  brightness=0, saturation=100, palette="6color"):
    """复用仓库已有的转换实现，保证与 ForFilm / 服务端出图一致"""
    repo = find_repo_root()
    if repo is None:
        raise RuntimeError(
            "找不到仓库中的转换实现 server/backend/app/services/film_convert.py。\n"
            "可在仓库根目录下运行，或设置环境变量 FRAMEFILM_REPO 指向仓库根。"
        )

    backend = os.path.join(repo, "server", "backend")
    if backend not in sys.path:
        sys.path.insert(0, backend)

    try:
        from PIL import Image
    except ImportError:
        raise RuntimeError("缺少依赖 Pillow，请先执行: pip install pyserial Pillow")
    from app.services.film_convert import convert_image

    img = Image.open(path)
    film, _preview = convert_image(
        img, DOCK_WIDTH, DOCK_HEIGHT,
        {
            "dither_type": dither,
            "dither_strength": strength,
            "contrast": contrast,
            "brightness": brightness,
            "saturation": saturation,
            "palette": palette,
        },
    )
    return film


# ---------------------------------------------------------------- 主流程
def main():
    ap = argparse.ArgumentParser(
        description="把图片转成 .film 并通过 USB 上传到 FrameFilm Dock",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    ap.add_argument("image", nargs="?", help="要上传的图片（jpg/png/bmp/webp）")
    ap.add_argument("--film", help="直接上传现成的 .film 文件（跳过转换）")
    ap.add_argument("--port", help="串口号（如 COM7 / /dev/ttyACM0），默认按 VID/PID 自动查找")
    ap.add_argument("--name", help="设备上的文件名，默认取图片名并补 .film")
    ap.add_argument("--dither", default="floyd_steinberg",
                    choices=["none", "floyd_steinberg", "atkinson", "stucki", "jarvis",
                             "bayer", "gamma_floyd_steinberg", "adaptive", "smart_adaptive"],
                    help="抖动算法")
    ap.add_argument("--strength", type=int, default=80, help="抖动强度 0-200")
    ap.add_argument("--contrast", type=int, default=100, help="对比度 0-200")
    ap.add_argument("--brightness", type=int, default=0, help="亮度 -100~100")
    ap.add_argument("--saturation", type=int, default=100, help="饱和度 0-200")
    ap.add_argument("--bw", action="store_true", help="黑白模式（2 色）")
    ap.add_argument("--delay", type=float, default=CHUNK_DELAY, help="每块数据间隔秒数（限速）")
    ap.add_argument("--save-film", help="把转换出的 .film 另存到该路径")
    ap.add_argument("--list", action="store_true", help="只列出设备上的文件后退出")
    ap.add_argument("--no-verify", action="store_true", help="上传后不回头校验文件列表")
    args = ap.parse_args()

    if not args.list and not args.image and not args.film:
        ap.error("请给出要上传的图片，或用 --film 指定 .film，或用 --list 列出设备文件")

    ser = open_port(args.port)
    print("串口已打开: %s" % ser.port)
    try:
        screen = query_screen(ser)
        if screen is None:
            print("提示: 未收到屏幕参数应答，对端可能不是 Dock 或固件过旧，继续尝试上传。")
        else:
            print("设备屏幕: panel=0x%02X %dx%d" % (screen["panel"], screen["width"], screen["height"]))
            if screen["panel"] != DOCK_PANEL_ID:
                print("警告: 面板 ID 不是 0x%02X，本工具按 Dock(%dx%d) 转换，结果可能不对。"
                      % (DOCK_PANEL_ID, DOCK_WIDTH, DOCK_HEIGHT))

        if args.list:
            files = list_files(ser)
            if not files:
                print("设备上没有 .film 文件。")
            else:
                print("设备文件列表:")
                for file_id, name in files:
                    print("  [%d] %s" % (file_id, name))
            return 0

        # 1) 得到 .film 数据
        if args.film:
            with open(args.film, "rb") as f:
                film = f.read()
            filename = args.name or os.path.basename(args.film)
            print("读取 %s (%d 字节)" % (args.film, len(film)))
        else:
            filename = args.name or (os.path.splitext(os.path.basename(args.image))[0] + ".film")
            print("转换图片: %s (dither=%s)" % (args.image, args.dither))
            film = image_to_film(
                args.image,
                dither=args.dither,
                strength=args.strength,
                contrast=args.contrast,
                brightness=args.brightness,
                saturation=args.saturation,
                palette="bw" if args.bw else "6color",
            )

        if not filename.lower().endswith(".film"):
            filename += ".film"

        if len(film) != DOCK_FILE_SIZE:
            print("错误: 文件大小 %d 字节，Dock 要求 %d 字节（%dx%d）。"
                  % (len(film), DOCK_FILE_SIZE, DOCK_WIDTH, DOCK_HEIGHT))
            return 2

        if args.save_film:
            with open(args.save_film, "wb") as f:
                f.write(film)
            print("已保存: %s" % args.save_film)

        # 2) 上传
        print("上传到设备: %s (%d 字节)" % (filename, len(film)))
        upload_film(ser, film, filename, chunk_delay=args.delay)

        # 3) 校验
        if not args.no_verify:
            time.sleep(0.5)
            files = list_files(ser)
            match = [i for i, n in files if n == filename]
            if match:
                print("上传成功，设备上文件 ID = %d，正在刷新屏幕..." % match[0])
            else:
                names = ", ".join(n for _, n in files) or "（空）"
                print("警告: 文件列表里没找到 %s，当前列表: %s" % (filename, names))
        else:
            print("上传完成（未校验）。")

        print("完成。")
        return 0
    finally:
        ser.close()


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        sys.exit(130)
    except Exception as exc:  # noqa: BLE001 - CLI 需要把原因直接告诉调用方
        print("错误: %s" % exc, file=sys.stderr)
        sys.exit(1)
