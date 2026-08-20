# AGENTS.md

FrameFilm 项目 AI 开发指南。

## 项目身份

开源彩色电子纸冰箱贴。ESP32-S3 + EPD + BLE/WiFi，手机传照片显示。

- ESP-IDF v5.5.2 (C) · 微信小程序 (ES5) · Web 工具 (ES6)
- GPL-3.0 · Git 中文 commit: `type(scope): 描述`

## 三机型（单固件）

统一固件 `firmware/frame_film/`，通过 `sys_cfg.h` 机型宏 + 对应 `sdkconfig` 区分机型（三选一）：

| | 基础版 STD | Pro 版 | Max 版 |
|---|---|---|---|
| 机型宏 | `FRAMEFILM_STD` | `FRAMEFILM_PRO` | `FRAMEFILM_MAX` |
| sdkconfig | `sdkconfig_std` | `sdkconfig_pro` | `sdkconfig_max` |
| 屏幕 | E6 3.6" 600×400 (WFT) | E6 3.68" 792×528 (SE0368-C) | E6 7.09" 1600×1200 双面板 (GDEB0709E01) |
| EPD 驱动 | `hal_epd_360.c` | `hal_epd_368.c` | `hal_epd_709.c` |
| 输入 | 旋转编码器 (GPIO6/4/5) | 三按键 (GPIO4/6/5) | 三按键 (GPIO12/14/13) |
| Flash/PSRAM | 16MB / Octal SPI | 4MB / Quad SPI | 16MB / Octal SPI |
| RGB LED | WS2812 (GPIO17) | WS2812 (GPIO17) | 无 |

> 机型差异集中在 EPD 驱动、输入设备、SD/电池/LED 引脚，代码用 `FRAMEFILM_STD/PRO/MAX` 宏编译隔离；改机型相关代码时确认三机型分支是否齐全。

## 架构约束

### 分层依赖（单向，不可逆）

```
film_service → film_hal → film_sys → ESP-IDF
  (服务层)     (硬件层)   (系统层)
```

### GPIO 引脚表（机型差异）

| 外设 | STD（基础版） | Pro 版 | Max 版 |
|------|--------------|--------|--------|
| EPD | SCK48 / MOSI47, CS14/DC13/RST12/BUSY11 (SPI2 四线) | ←同 | SCK9 / SDIN41 / SDIO40, CS0=18/CS1=17/RST6/BUSY7 (双CS无DC), LOAD_SW45 |
| TF卡 | CLK40 / CMD41 / D0-3=39/38/2/42, DET45 | ←同 | CLK8 / CMD3 / D0-3=5/4/16/15, 无检测 |
| 输入 | 编码器 A6/B4/按键5 | 按键 上4/下6/确认5（低有效） | 按键 上12/下14/确认13（高有效） |
| 唤醒 | GPIO5（低电平） | ←同 | GPIO13（高电平） |
| RGB LED | GPIO17 (WS2812) | ←同 | 无 |
| 电池 | ADC使能8 / ADC_CH0 1 | ←同 | 无电池检测 |
| 外设供电 | GPIO21 | ←同 | 无 |

### 跨端一致性（必须）

修改以下内容时，**三个端必须同时更新**：

| 内容 | C 固件 | 小程序 | Web |
|------|--------|--------|-----|
| BLE 命令常量 | `service_ble.h` | `ble-utils.js` | `frame.js` |
| film 颜色编码 | `hal_epd.h` | `film-utils.js` | `convert.js` |

### 关键常量

- `BLE_CHUNK_SIZE = 192`（数据包大小）
- `BLE_CMD_HEAD = 0x55`（帧头）
- film 文件大小 = **32B 头 + (宽×高/2) 像素**（标准版 600×400 为 120032 字节）
- 6 色编码：黑 0x00 | 白 0x11 | 绿 0x66 | 蓝 0x55 | 红 0x33 | 黄 0x22
- BLE 可用命令范围：`0x3E` 起

## 命名约定

| | C 固件 | 小程序 JS |
|---|---|---|
| 文件/函数 | `snake_case` | `camelCase` (文件 `kebab-case`) |
| 全局变量 | `g_` 前缀 | — |
| 宏/枚举 | `UPPER_CASE` | `UPPER_CASE` (常量) |
| 类型 | `PascalCase_t` | — |
| 头保护 | `__NAME_H__` | — |

## 关键文件

| 要改什么 | 核心文件 |
|---|---|
| 机型配置 | `firmware/frame_film/components/film_sys/inc/sys_cfg.h` + `firmware/frame_film/sdkconfig_{std,pro,max}` |
| BLE 协议 | `firmware/frame_film/components/film_service/inc/service_ble.h` |
| EPD 驱动 | `firmware/frame_film/components/film_hal/src/hal_epd_{360,368,709}.c` |
| film 播放 | `firmware/frame_film/components/film_service/src/service_film.c` |
| 固件入口 | `firmware/frame_film/main/main.c` |
| 小程序 BLE | `tools/wechart/miniprogram/utils/ble-utils.js` |
| Web BLE | `tools/ForFilm/js/frame.js` |
| 协议文档 | `docs/blecmd/blecmd_protocol.md` |

## 常见陷阱（不要做）

1. **不要只在 service 层调 esp_wifi_init 等 ESP-IDF driver** — 必须通过 HAL
2. **不要只改一个机型的宏分支** — 机型差异代码需覆盖 `FRAMEFILM_STD/PRO/MAX`（EPD 驱动、输入设备、SD 等按宏隔离）
3. **不要改 BLE 命令值** — 值一旦定义就固定，新增命令从 `0x3E` 起
4. **不要假设字符串编码** — BLE 传输一律 ASCII + `\0` 结尾
5. **不要忘记更新 blecmd_protocol.md** — 协议文档必须与实际实现一致
6. **不要在 service 层直接操作 GPIO** — 所有硬件操作走 film_hal
7. **不要机型宏与 sdkconfig 不匹配** — 编译前确认 `sys_cfg.h` 机型宏与 `sdkconfig_{std,pro,max}` 对应一致

## BLE 协议速览

包格式: `0x55 CH LEN DATA[...] SUM`，SUM = 全部字节之和 & 0xFF, Big-Endian

| 分组 | 范围 | 示例 |
|------|------|------|
| 文件传输 | 0x00-0x08 | START→NAME→LEN→DATA→STOP |
| OTA | 0x10-0x13 | 0x10 LEN → 0x11 DATA×N → 0x13 STOP |
| 设备控制 | 0x20-0x2B | 电量0x23, 休眠0x25-0x2A, SD格式化0x2B |
| WiFi | 0x30-0x3D | 配网0x30-0x38, 下载0x3C-0x3D |

完整命令表: `docs/blecmd/blecmd_protocol.md` 或 `docs/knowledge/ble_commands.md`

## 任务模板

### 新增 BLE 命令 (如 0x3E)
1. `service_ble.h` 定义 `#define BLE_FILM_TRANS_CH_XXX 0x3E`
2. `service_ble.c` 添加 case 处理
3. `ble-utils.js` + `frame.js` 添加同名常量
4. `blecmd_protocol.md` 更新

### 新增 service 子服务
1. `inc/service_xxx.h` + `src/service_xxx.c`
2. `service_init.c` 调用 init
3. 如需 BLE 控制 → `service_ble.c` 添加命令
4. 确认机型差异分支（EPD/输入等用 `FRAMEFILM_*` 宏）

## 构建命令

```bash
cd firmware/frame_film
cp sdkconfig_std sdkconfig                 # 按机型选 sdkconfig_{std,pro,max}
# 编辑 components/film_sys/inc/sys_cfg.h，置对应机型宏为 1（三选一）
idf.py build flash monitor
```

## 文档索引

- `docs/blecmd/blecmd_protocol.md` — BLE 协议完整规范
- `docs/film/film.md` — film 文件格式
- `docs/hardware/hardware_spec.md` — 硬件规格 + 启动流程
- `docs/wifi/wifi_doc.md` — WiFi 功能说明（文档仍标注 Pro 版，待同步）
- `docs/knowledge/` — AI 知识库（项目总览/架构/规范/命令速查）
