---
name: "framefilm-dock-upload"
description: "Upload an image to a FrameFilm Dock over USB. Invoke when the user asks to send/push a photo or picture to the FrameFilm dock (底座) via USB, or to list and verify .film files stored on the dock."
---

# FrameFilm Dock 图片上传

把一张图片转换成 `.film` 文件，通过 **USB 虚拟串口**直接上传到 FrameFilm Dock，上传完成后设备会自动刷新墨水屏显示该图片。

> 位置说明：本 skill 与配套脚本一起放在项目 `tools/framefilm-dock-upload/` 下。

## 何时使用

- 用户说「把这张图/照片传到 dock」「通过 USB 上传图片到 dock」「让 dock 显示这张图」
- 需要查看 dock 上已有的 `.film` 文件列表
- 需要验证某个文件是否已经成功传到 dock

不适用：给冰箱贴（FrameFilm / Pro / Max）传图——那些机型没有 USB 串口，只有蓝牙，请用 `tools/ForFilm` 网页工具。

## 前置条件

1. **硬件**：dock 用 USB 线连到电脑，设备管理器里会出现一个串口（COM 口）。该串口是声卡/键盘/串口复合设备的一部分。
2. **Python 依赖**：
   ```
   pip install pyserial Pillow
   ```
3. **仓库位置**：图片转换复用仓库里的 `server/backend/app/services/film_convert.py`，脚本会从自身位置和当前目录向上自动查找仓库根。若在仓库外运行，用环境变量指定：
   ```
   FRAMEFILM_REPO=<仓库根> python tools/framefilm-dock-upload/scripts/dock_upload.py ...
   ```

## 使用方法

脚本路径：`tools/framefilm-dock-upload/scripts/dock_upload.py`

```bash
# 上传一张图片（自动查找串口、自动转换、自动校验）
python tools/framefilm-dock-upload/scripts/dock_upload.py photo.jpg

# 指定串口 / 指定设备上的文件名 / 换抖动算法
python tools/framefilm-dock-upload/scripts/dock_upload.py photo.jpg --port COM7 --name holiday.film --dither atkinson

# 只列出设备上已有的文件
python tools/framefilm-dock-upload/scripts/dock_upload.py --list

# 上传现成的 .film（跳过转换）
python tools/framefilm-dock-upload/scripts/dock_upload.py --film out.film --name demo.film

# 调整观感
python tools/framefilm-dock-upload/scripts/dock_upload.py photo.jpg --strength 120 --contrast 110 --brightness 5
python tools/framefilm-dock-upload/scripts/dock_upload.py photo.jpg --bw          # 黑白模式
```

### 常用参数

| 参数 | 说明 | 默认 |
| --- | --- | --- |
| `image` | 待上传图片（jpg/png/bmp/webp），自动按**竖版画布 568×760**裁剪填充，再逆时针旋转 90° 送屏（Dock 是竖屏使用） | — |
| `--film` | 直接上传现成 `.film`，跳过转换（不做旋转） | — |
| `--port` | 串口号；不填则按 VID=0x303A / PID=0x8000 自动查找 | 自动 |
| `--name` | 设备上的文件名，自动补 `.film` 后缀 | 图片名 |
| `--dither` | 抖动算法：`floyd_steinberg`/`atkinson`/`stucki`/`jarvis`/`bayer`/`gamma_floyd_steinberg`/`adaptive`/`smart_adaptive`/`none` | `floyd_steinberg` |
| `--strength` | 抖动强度 0-200 | 80 |
| `--contrast` / `--brightness` / `--saturation` | 画面调整 | 100 / 0 / 100 |
| `--bw` | 黑白（2 色）模式 | 关 |
| `--rotate` | 内容旋转角度：`90`=Dock 竖屏（默认）/ `0`=面板原生横屏 / `180` / `270` | 90 |
| `--delay` | 每 192 字节块的间隔（秒），用于限速 | 0.002 |
| `--save-film` | 把转换结果另存一份 `.film` | — |
| `--list` | 只列设备文件后退出 | 关 |
| `--no-verify` | 上传后不回头校验 | 关 |

## Agent 执行流程

1. 先检查依赖是否就绪（缺 pyserial/Pillow 就先 `pip install pyserial Pillow`）。
2. 直接运行脚本上传；**不要**自己去拼串口帧或手写转换。
3. 用 `--save-film` 留一份 `.film` 便于排查（可选）。
4. 读取脚本输出并向用户汇报：是否找到设备、转换后的字节数、上传是否成功、设备上的文件 ID。
5. 失败时按下一节的排查表处理。

## 排查

| 现象 | 原因与处理 |
| --- | --- |
| `未找到 FrameFilm Dock 串口` | dock 没插好/没枚举出 COM 口；或该口是 ESP32 内置 USB-Serial-JTAG（PID 0x1001）。确认固件里 `SYS_FUNC_USB_CDC_EN=1`，必要时用 `--port` 手工指定 |
| `错误: 文件大小 ... Dock 要求 215872 字节` | 用了非 Dock 的转换参数；Dock 固定 760×568、6 色、4bpp（32+760×568/2=215872） |
| `找不到仓库中的转换实现` | 在仓库外运行了；设置 `FRAMEFILM_REPO` 或回到仓库内运行 |
| 上传后屏幕没变化 | 检查文件列表输出；确认名字带 `.film` 后缀（设备只把 `.film` 计入列表）；EPD 刷新本身要几秒 |
| 文件传上去但花屏/错位 | 图片转换参数不匹配该机型；确认握手时收到的是 `panel=0x06` |
| 上屏内容横着/倒着 | 旋转角不对：默认 `--rotate 90` 适配 Dock 竖屏；设备改成横放时试 `0`/`180`/`270` |
| 大文件传输容易失败 | 加大 `--delay`（如 `0.005`）降速重试，避免设备接收队列溢出丢数据 |

## 实现说明（维护用）

- 帧格式：`HEAD(0x55) | CH | LEN | DATA | SUM`，SUM 为前面所有字节之和；与 BLE 链路完全一致。
- 上传时序：`FILE_START(0x03)` → `FILE_NAME(0x00)` → `FILE_LEN(0x01，大端 4 字节)` → `FILE_DATA(0x02，分块，≤255 字节/帧)` → `FILE_STOP(0x04)`。
- 屏幕参数查询 `0x42` 的应答为 `panelId(1) + width(2, 大端) + height(2, 大端)`，用于确认对端是 Dock。
- 方向：Dock 面板物理横置、产品竖屏使用，出图先把图片按 568×760 竖版画布裁剪填充，再逆时针旋转 90° 得到 760×568 面板数据（`--rotate`，对齐小程序 `film-utils.js` 的 `isPortraitPanel` 处理）。
- 文件列表 `0x06` 会按设备上每个文件回一帧：`id(1) + nameLen(1) + name(nameLen)`，帧之间有约 50ms 间隔。
- 必须限速：dock 侧接收队列有限（CDC FIFO + 两级任务队列），全速灌数据会丢包导致文件损坏。
