# AGENTS.md

FrameFilm 项目 AI 开发指南。

## 项目身份

开源彩色电子纸冰箱贴。ESP32-S3 + EPD + BLE/WiFi，手机传照片显示。

- ESP-IDF v5.5.2 (C) · 微信小程序 (ES5) · Web 工具 (ES6)
- GPL-3.0 · Git 中文 commit: `type(scope): 描述`

## 双版本

| | 基础版 `firmware/frame_film/` | Pro 版 `firmware/frame_film_pro/` |
|---|---|---|
| 屏幕 | WFT 600×400, SPI 四线 | SE0368-C 792×528, SPI 三线半双工 |
| 输入 | 旋转编码器 (GPIO6/4/5) | 三按键 (GPIO4/6/5) |
| WiFi | 无 | STA + HTTP 下载, 播放模式多一个"WiFi轮播" |
| Flash/PSRAM | 4MB / Quad SPI | 16MB / Octal SPI |

> 修改固件时确认是否需要双版同步。EPD 驱动和输入设备是核心差异，其余代码基本一致。

## 架构约束

### 分层依赖（单向，不可逆）

```
film_service → film_hal → film_sys → ESP-IDF
  (服务层)     (硬件层)   (系统层)
```

### GPIO 引脚表（两版相同）

| GPIO | 基础版 | Pro 版 | 外设 |
|------|--------|--------|------|
| 48 | SPI2 SCK | ←同 | EPD |
| 47 | SPI2 MOSI | SDIN(半双工) | EPD |
| 14/13/12/11 | CS/DC/RST/BUSY | ←同 | EPD |
| 40-42,38-39,2 | SDMMC | ←同 | SD卡 |
| 6 | 编码器 A | 按键下 | 输入 |
| 4 | 编码器 B | 按键上 | 输入 |
| 5 | 编码器按键/唤醒 | 确认键/唤醒 | 输入 |
| 17 | WS2812 DIN | ←同 | RGB LED |
| 8/1 | ADC使能/ADC_CH0 | ←同 | 电池 |
| 21 | 外设电源控制 | ←同 | 电源 |

### 跨端一致性（必须）

修改以下内容时，**三个端必须同时更新**：

| 内容 | C 固件 | 小程序 | Web |
|------|--------|--------|-----|
| BLE 命令常量 | `service_ble.h` | `ble-utils.js` | `frame.js` |
| film 颜色编码 | `hal_epd.c` | `film-utils.js` | `convert.js` |

### 关键常量

- `BLE_CHUNK_SIZE = 192`（数据包大小）
- `BLE_CMD_HEAD = 0x55`（帧头）
- film 文件固定大小：**120032 字节** = 32B 头 + (600×400/2) 像素
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
| BLE 协议 | `firmware/*/components/film_service/inc/service_ble.h` |
| EPD 驱动 | `firmware/*/components/film_hal/src/hal_epd.c` |
| film 播放 | `firmware/*/components/film_service/src/service_film.c` |
| 固件入口 | `firmware/*/main/main.c` |
| 小程序 BLE | `tools/wechart/miniprogram/utils/ble-utils.js` |
| Web BLE | `tools/ForFilm/js/frame.js` |
| 协议文档 | `docs/blecmd/blecmd_protocol.md` |

## 常见陷阱（不要做）

1. **不要只在 service 层调 esp_wifi_init 等 ESP-IDF driver** — 必须通过 HAL
2. **不要只改一个版本的固件** — 除非是版本专有功能（WiFi/EPD 命令/输入设备）
3. **不要改 BLE 命令值** — 值一旦定义就固定，新增命令从 `0x3E` 起
4. **不要假设字符串编码** — BLE 传输一律 ASCII + `\0` 结尾
5. **不要忘记更新 blecmd_protocol.md** — 协议文档必须与实际实现一致
6. **不要在 service 层直接操作 GPIO** — 所有硬件操作走 film_hal

## BLE 协议速览

包格式: `0x55 CH LEN DATA[...] SUM`，SUM = 全部字节之和 & 0xFF, Big-Endian

| 分组 | 范围 | 示例 |
|------|------|------|
| 文件传输 | 0x00-0x08 | START→NAME→LEN→DATA→STOP |
| OTA | 0x10-0x13 | 0x10 LEN → 0x11 DATA×N → 0x13 STOP |
| 设备控制 | 0x20-0x2B | 电量0x23, 休眠0x25-0x2A, SD格式化0x2B |
| WiFi (Pro) | 0x30-0x3D | 配网0x30-0x38, 下载0x3C-0x3D |

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
4. 两版固件同步检查

## 构建命令

```bash
cd firmware/frame_film && idf.py build flash monitor     # 基础版
cd firmware/frame_film_pro && idf.py build flash monitor  # Pro 版
```

## 文档索引

- `docs/blecmd/blecmd_protocol.md` — BLE 协议完整规范
- `docs/film/film.md` — film 文件格式
- `docs/hardware/hardware_spec.md` — 硬件规格 + 启动流程
- `docs/wifi/wifi_doc.md` — WiFi 功能（Pro）
- `docs/knowledge/` — AI 知识库（项目总览/架构/规范/命令速查）
