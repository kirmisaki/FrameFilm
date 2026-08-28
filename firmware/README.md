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

## 机型与屏幕（三机型，屏幕可自由选择）

统一固件 `frame_film/` 的硬件由两处编译期配置共同决定：

1. **机型**（`sys_cfg.h`，三选一）——决定输入设备、SD/电池/LED 引脚、Flash/PSRAM、设备名等
2. **屏幕**（`hal_epd.h`）——在对应机型分支内，从屏幕支持列表里选一款，把对应 `EPD_SELECT_E6_*` 置 `1`、其余置 `0`

| 机型 | 机型宏 | sdkconfig | 输入 | 默认屏幕 |
|------|--------|-----------|------|----------|
| 基础版 STD | `FRAMEFILM_STD` | `sdkconfig_std` | 旋转编码器 | E6 3.6" 600×400 |
| Pro 版 | `FRAMEFILM_PRO` | `sdkconfig_pro` | 三按键 | E6 3.68" 792×528 |
| Max 版 | `FRAMEFILM_MAX` | `sdkconfig_max` | 三按键 | E6 7.09" 1200×1600 双面板 |

屏幕支持列表（`hal_epd.h` 内 `EPD_SELECT_E6_*` 宏）：

| 宏 | 屏幕 | 分辨率 | 面板 ID | EPD 驱动 |
|----|------|--------|---------|----------|
| `EPD_SELECT_E6_3_68_792_528` | E6 3.68" | 792×528 | 0x01 | `hal_epd_368.c` |
| `EPD_SELECT_E6_3_70_720_480` | E6 3.70" | 720×480 | 0x02 | `hal_epd_370.c` |
| `EPD_SELECT_E6_3_60_600_400` | E6 3.6" | 600×400 | 0x03 | `hal_epd_360.c` |
| `EPD_SELECT_E6_1_54_240_240` | E6 1.54" | 240×240 | 0x04 | — |
| `EPD_SELECT_E6_7_09_1600_1200` | E6 7.09" 双面板 | 1200×1600 | 0x05 | `hal_epd_709.c` |

> 例：Pro 版硬件若为 720×480 面板，在 `hal_epd.h` 的 `FRAMEFILM_PRO` 分支内把 `EPD_SELECT_E6_3_68_792_528` 改为 0、`EPD_SELECT_E6_3_70_720_480` 改为 1 即可。

机型差异（输入设备、SD/电池/LED 引脚）用 `FRAMEFILM_STD/PRO/MAX` 宏隔离；屏幕差异用 `EPD_SELECT_E6_*` 宏隔离。

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
# 编辑 components/film_hal/inc/hal_epd.h，在机型分支内选择目标屏幕（EPD_SELECT_E6_* 置 1）
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
