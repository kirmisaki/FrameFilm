# 固件目录

本目录包含帧影（FrameFilm）电子胶片冰箱贴的设备固件代码。

## 固件工程

| 工程 | 用途 | 设备名 |
|------|------|--------|
| [`frame_film/`](frame_film/) | 冰箱贴本体固件（多机型） | `FRAMEFILM` / `FRAMEFILMPRO` / `FRAMEFILMMAX` |
| [`frame_film_dock/`](frame_film_dock/) | 充电底座固件（单机型） | `FRAMEFILMDOCK` |

两个工程均基于 ESP-IDF 构建，采用相同的三层分层架构。

## 硬件平台

- 主控芯片：ESP32-S3
- 显示屏：彩色电子纸（E6 系列）
- 通信模块：蓝牙 BLE
- 开发框架：ESP-IDF v5.5.2（C 语言）

## 目录结构

每个固件工程内部结构一致：

```
frame_film/
├── main/                 # 固件入口（app_main）
├── components/
│   ├── film_sys/         # 系统层：配置、日志、错误码、系统初始化
│   ├── film_hal/         # 硬件抽象层：EPD、输入、SD、LED、电池、电源
│   └── film_service/     # 服务层：BLE、文件传输、film 播放、OTA、WiFi、参数
├── partitions.csv        # 分区表（NVS + 双 OTA）
├── CMakeLists.txt
├── sdkconfig             # 当前生效的 SDK 配置
└── sdkconfig_{std,pro,max}  # 各机型 SDK 配置
```

## 机型（frame_film 多机型，三选一）

统一固件 `frame_film/` 通过 `sys_cfg.h` 机型宏 + 对应 `sdkconfig` 区分机型：

| 机型 | 机型宏 | sdkconfig | 屏幕 | EPD 驱动 |
|------|--------|-----------|------|----------|
| 基础版 STD | `FRAMEFILM_STD` | `sdkconfig_std` | E6 3.6" 600×400 | `hal_epd_360.c` |
| Pro 版 | `FRAMEFILM_PRO` | `sdkconfig_pro` | E6 3.68" 792×528（默认） | `hal_epd_368.c` |
| Max 版 | `FRAMEFILM_MAX` | `sdkconfig_max` | E6 7.09" 1600×1200 双面板 | `hal_epd_709.c` |

Pro 版支持第二种屏幕面板：在 `sys_cfg.h` 置 `FRAMEFILM_PRO_PANEL_720x480 = 1` 即切换为 E6 3.7" 720×480（`hal_epd_370.c`，原 SE 版屏幕），其余与 Pro 完全一致。

机型差异集中在 EPD 驱动、输入设备、SD/电池/LED 引脚，代码通过 `FRAMEFILM_STD/PRO/MAX` 宏编译隔离。修改机型相关代码时需确认三个机型分支是否齐全。

## 架构

```
film_service → film_hal → film_sys → ESP-IDF
  (服务层)     (硬件层)   (系统层)
```

依赖关系单向，不可逆：

- **film_sys（系统层）**：机型配置、日志、错误码、系统初始化
- **film_hal（硬件抽象层）**：EPD 驱动、按键/编码器、TF 卡、WS2812 LED、电池、电源管理
- **film_service（服务层）**：BLE 通信与 GATT、文件传输、film 播放、OTA、WiFi、参数存储

## 主要功能

- 彩色电子纸显示驱动（6 色编码：黑/白/绿/蓝/红/黄）
- BLE 连接与照片数据传输
- `.film` 文件解码与胶片滤镜处理
- OTA 固件升级
- WiFi 配网与图片下载
- 低功耗管理与电池电量监测

## 构建说明

使用 ESP-IDF v5.5.2 进行开发和烧录（非 PlatformIO）。

```bash
cd firmware/frame_film
cp sdkconfig_std sdkconfig               # 按机型选 sdkconfig_{std,pro,max}
# 编辑 components/film_sys/inc/sys_cfg.h，置对应机型宏为 1（三选一）
# Pro 版如需 720×480 屏幕，另置 FRAMEFILM_PRO_PANEL_720x480 为 1
idf.py build flash monitor
```

底座固件同理：

```bash
cd firmware/frame_film_dock
idf.py build flash monitor
```

## 文档索引

- [`docs/blecmd/blecmd_protocol.md`](../docs/blecmd/blecmd_protocol.md) — BLE 协议完整规范
- [`docs/film/film.md`](../docs/film/film.md) — film 文件格式
- [`docs/hardware/hardware_spec.md`](../docs/hardware/hardware_spec.md) — 硬件规格 + 启动流程
- [`docs/knowledge/`](../docs/knowledge/) — AI 知识库（架构/规范/命令速查）
