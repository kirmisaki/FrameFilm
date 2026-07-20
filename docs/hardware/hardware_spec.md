# FrameFilm 硬件规格说明

## 1. 概述

FrameFilm（帧影）是一款电子胶片冰箱贴设备，核心功能是通过彩色电子纸显示屏展示照片。设备由 ESP32-S3 主控芯片驱动，支持 BLE 无线通信与手机小程序交互，使用可充电锂电池供电，具备深度睡眠低功耗管理能力。

设备有两个硬件/固件版本：

- **FrameFilm（基础版）**：使用 WFT 系列 3.6" 电子纸 (600×400)，旋转编码器交互
- **FrameFilm Pro（Pro 版）**：使用 SE0368-C 3.68" 电子纸 (792×528)，三按键交互，支持温度补偿

***

## 2. 核心硬件组件

| 组件       | 型号/规格                                 | 说明                            |
| -------- | ------------------------------------- | ----------------------------- |
| 主控芯片     | Espressif ESP32-S3                    | 双核 Xtensa LX7, 最高 240MHz      |
| 显示屏      | 3.6 英寸彩色电子纸 (E-Ink)                   | 基础版 600×400 / Pro 版 792×528, 6 色 |
| 显示屏驱动    | WFT 系列 (W21) / SE0368-C              | SPI 接口, 控制 EPD 刷新             |
| 存储       | SDNAND（默认）/ 标准 SD 卡                   | SDMMC 高速模式, FATFS 文件系统, 自动格式化  |
| LED      | WS2812 可寻址 RGB LED × 2                | 状态指示，默认白色 10% 亮度              |
| 编码器      | 旋转编码器（带按键）/ 三按键                       | 基础版编码器 / Pro 版按键              |
| 电池       | 可充电锂电池                                | ADC 采样监测电量                    |
| 外部 RAM   | SPI PSRAM                             | 扩展内存                          |
| 外部 Flash | 16MB SPI Flash (QIO, 80MHz)           | 固件与文件存储                       |

***

## 3. 主控芯片 — ESP32-S3

### 3.1 芯片特性

- **CPU**：双核 Xtensa 32 位 LX7 微处理器，最高主频 240MHz
- **内存**：内置 512KB SRAM，外挂 SPI PSRAM
- **Flash**：外挂 16MB SPI Flash，QIO 模式 80MHz
- **无线**：Wi-Fi 4 (802.11 b/g/n) + 蓝牙 5.0 (BLE)
- **固件平台**：ESP-IDF v5.5.2
- **标准版数据手册**：`docs/datasheet/esp32-s3_datasheet_cn.pdf`
- **Pro版数据手册**：`docs/datasheet/esp32-s3-mini-1_mini-1u_datasheet_cn.pdf`

### 3.2 使用的外设

| 外设       | 用途            | 连接设备             |
| -------- | ------------- | ---------------- |
| SPI2     | 电子纸显示通信       | EPD (WFT 驱动)     |
| SDMMC    | SD 卡读写        | SDNAND / SD 卡    |
| ADC1 CH0 | 电池电压采样        | 锂电池分压电路          |
| RMT      | WS2812 LED 驱动 | RGB LED          |
| PCNT     | 旋转编码器正交解码     | 旋转编码器            |
| GPIO     | 通用 I/O 控制     | CS/DC/BUSY/RST 等 |
| BLE      | 蓝牙低功耗通信       | 手机微信小程序          |
| Wi-Fi    | 无线网络（已启用）     | OTA 升级等          |

***

## 4. 电子纸显示屏 (EPD)

### 4.1 规格参数

| 参数     | FrameFilm (基础版)                         | FrameFilm Pro                          |
| ------ | ---------------------------------------- | -------------------------------------- |
| 尺寸     | 3.6 英寸                                    | 3.6 英寸                                 |
| 分辨率    | 600 × 400 像素                              | 792 × 528 像素                           |
| 颜色     | 6 色                                       | 6 色                                    |
| 颜色定义   | 黑、白、绿、蓝、红、黄                               | 黑、白、绿、蓝、红、黄                            |
| 通信接口   | SPI 四线标准模式                                | SPI 三线半双工模式 (SDIN 复用 MOSI/MISO)          |
| 驱动芯片   | WFT 系列 (W21 指令集)                          | SE0368-C                               |
| 刷新方式   | 单次刷新 (single pass)                         | 双次刷新 (dual pass)，支持温度补偿                 |
| 温度传感器  | 无                                        | 内置温度传感器，根据温度选择波形参数                       |
| 数据手册   | `docs/datasheet/3.6inch_e-Paper_HAT+.pdf` | `docs/datasheet/SE0368NW34-CNG-A0_SPEC_V0.2.pdf` |

### 4.2 SPI 信号定义

显示屏通过 ESP32-S3 的 SPI2 外设通信，时钟 10MHz，模式 0。

- **基础版**：标准四线 SPI (`miso_io_num = -1`，MISO 未连接)
- **Pro 版**：硬件半双工三线模式 (`SPI_DEVICE_HALFDUPLEX | SPI_DEVICE_3WIRE`)

### 4.3 WFT 驱动命令寄存器 (基础版)

| 命令      | 地址     | 说明                         |
| ------- | ------ | -------------------------- |
| PSR     | `0x00` | Panel Setting              |
| PWR     | `0x01` | Power Setting              |
| POF     | `0x02` | Power Off                  |
| POFS    | `0x03` | Power Off Sequence Setting |
| PON     | `0x04` | Power On                   |
| BTST1   | `0x05` | Booster Soft Start 1       |
| BTST2   | `0x06` | Booster Soft Start 2       |
| DSLP    | `0x07` | Deep Sleep                 |
| BTST3   | `0x08` | Booster Soft Start 3       |
| DTM     | `0x10` | Data Transmission Mode     |
| DRF     | `0x12` | Display Refresh            |
| PLL     | `0x30` | PLL Control                |
| CDI     | `0x50` | VCOM and Data Interval     |
| TCON    | `0x60` | TCON Setting               |
| TRES    | `0x61` | Resolution Setting         |
| REV     | `0x70` | Revision                   |
| VDCS    | `0x82` | VCOM DC Setting            |
| T\_VDCS | `0x84` | VCOM DC Time               |
| PWS     | `0xE3` | Power Saving               |

### 4.4 SE0368-C 驱动命令寄存器 (Pro 版)

| 命令    | 地址     | 说明                         |
| ----- | ------ | -------------------------- |
| PSR   | `0x00` | Panel Setting              |
| PWR   | `0x01` | Power Setting              |
| POF   | `0x02` | Power Off                  |
| POFS  | `0x03` | Power Off Sequence Setting |
| PON   | `0x04` | Power On                   |
| BTST1 | `0x05` | Booster Soft Start 1       |
| BTST2 | `0x06` | Booster Soft Start 2       |
| DSLP  | `0x07` | Deep Sleep                 |
| BTST3 | `0x08` | Booster Soft Start 3       |
| DTM   | `0x10` | Data Transmission Mode     |
| REF   | `0x17` | Display Refresh            |
| PLL   | `0x30` | PLL Control                |
| TSE   | `0x40` | Temperature Sensor Enable  |
| TSD   | `0x41` | Temperature Sensor Data    |
| CDI   | `0x50` | VCOM and Data Interval     |
| RES2  | `0x62` | Resolution Setting 2       |
| RSET  | `0x83` | Resolution Extended        |
| WFT   | `0xE0` | Waveform Temperature       |
| VCOM2 | `0xE1` | VCOM2                      |
| PWS   | `0xE3` | Power Saving               |
| WFD   | `0xE6` | Waveform Temperature Data  |
| VCOM  | `0xE7` | VCOM                       |
| BOD   | `0xE9` | Border                     |

> Pro 版刷新命令使用 `REF(0x17)` + `0xA5` 参数，根据不同温度使用不同波形参数。

### 4.5 颜色编码（两版通用）

| 颜色 | 编码     |
| -- | ------ |
| 黑色 | `0x00` |
| 白色 | `0x11` |
| 绿色 | `0x66` |
| 蓝色 | `0x55` |
| 红色 | `0x33` |
| 黄色 | `0x22` |

***

## 5. SD 卡存储 (SDMMC)

### 5.1 接口配置

- **通信接口**：SDMMC（非 SPI 模式）
- **传输模式**：高速模式 (`SDMMC_FREQ_HIGHSPEED`)
- **文件系统**：FATFS，挂载点 `/sdcard`
- **数据宽度**：4 线 (D0-D3)
- **存储类型**：SDNAND（默认），挂载失败时自动格式化
- **最大文件数**：5
- **分配单元**：16KB

### 5.2 功能说明

SD 卡用于存储 `.film` 格式的帧数据文件。设备上电后挂载文件系统，读取 SD 卡中的照片文件并通过 EPD 显示。默认使用 SDNAND 存储方案，无需物理 SD 卡插入即可工作。

***

## 6. 用户输入

### 6.1 旋转编码器（基础版）

旋转编码器使用 ESP32-S3 的脉冲计数 (PCNT) 外设进行正交解码，支持旋转和按键两种交互方式。基于 esp-idf-lib 的 `encoder` 组件 (v3.0.2)。

- **信号类型**：正交编码 (A/B 相)
- **按键**：支持按压检测
- **按键有效电平**：低电平 (0)
- **消抖时间**：50ms
- **长按时间**：2s
- **轮询间隔**：2ms

### 6.2 三按键（Pro 版）

Pro 版使用三个独立按键替代旋转编码器，基于 espressif `iot_button` 驱动库。

- **按键有效电平**：低电平 (0)
- **短按时间**：50ms
- **长按时间**：2s

***

## 7. WS2812 RGB LED

### 7.1 规格参数

| 参数   | 值                  |
| ---- | ------------------ |
| 型号   | WS2812 可寻址 RGB LED |
| 数量   | 1 个                |
| 驱动方式 | ESP32 RMT 外设       |
| 颜色格式 | GRB                |
| 时钟频率 | 10 MHz             |
| 默认颜色 | 白色 (R=255, G=255, B=255) |
| 默认亮度 | 10%                |
| 用途   | 设备状态指示             |

***

## 8. 电池与电源管理

### 8.1 电池规格

- **类型**：可充电锂电池
- **供电管理**：通过 `PERI_PWR_PIN` (GPIO21) 控制外设供电

### 8.2 电量监测 (ADC)

| 参数          | 值                        |
| ----------- | ------------------------ |
| ADC 单元      | ADC\_UNIT\_1             |
| ADC 通道      | ADC\_CHANNEL\_0 (GPIO1)  |
| 衰减          | ADC\_ATTEN\_DB\_12       |
| 采样使能引脚      | GPIO8 (`BAT_ADC_EN_PIN`) |
| 分压比例        | 20k-10k 分压，软件修正系数 3.06     |
| 最大电压        | 4140mV                   |
| 最小电压        | 3300mV                   |
| 采样次数        | 10 次，去掉最大最小值后取平均          |
| 采样间隔        | 2ms                      |
| 电容稳定时间      | 100ms                    |
| 校准方案        | 优先曲线拟合，回退线性拟合             |

**电压-电量对照表：**

| 电压(mV) | 3300 | 3680 | 3733 | 3770 | 3790 | 3840 | 3890 | 3920 | 3970 | 4070 | 4140 |
| ------- | ---- | ---- | ---- | ---- | ---- | ---- | ---- | ---- | ---- | ---- | ---- |
| 电量(%)  | 0    | 10   | 20   | 30   | 40   | 50   | 60   | 70   | 80   | 90   | 100  |

### 8.3 低功耗管理

- **深度睡眠唤醒**：GPIO5，EXT0 低电平唤醒
- **定时器唤醒**：支持分钟级定时唤醒
- **睡眠时外设处理**：关闭外设电源 (GPIO21)，配置为输入下拉；唤醒脚使用 `rtc_gpio_isolate()` 隔离减少漏电

***

## 9. 无线通信

| 功能     | 说明              |
| ------ | --------------- |
| 蓝牙 BLE | 与手机微信小程序通信      |
| Wi-Fi  | 已启用，可用于 OTA 升级等 |

***

## 10. 完整管脚定义

### 10.1 GPIO 引脚总表

| GPIO | 功能                     | 连接设备    | 所属 HAL 模块           |
| ---- | ---------------------- | ------- | ------------------- |
| 48   | SPI2 SCK (时钟)          | EPD     | hal\_epd            |
| 47   | SPI2 SDIN (数据，Pro 版半双工) | EPD     | hal\_epd            |
| 14   | SPI2 CS (片选)           | EPD     | hal\_epd            |
| 13   | DC (数据/命令)             | EPD     | hal\_epd            |
| 12   | RES (复位)               | EPD     | hal\_epd            |
| 11   | BUSY (忙状态)             | EPD     | hal\_epd            |
| 40   | SDMMC CLK (时钟)         | SD 卡    | hal\_sd             |
| 41   | SDMMC CMD (命令)         | SD 卡    | hal\_sd             |
| 39   | SDMMC D0 (数据 0)        | SD 卡    | hal\_sd             |
| 38   | SDMMC D1 (数据 1)        | SD 卡    | hal\_sd             |
| 2    | SDMMC D2 (数据 2)        | SD 卡    | hal\_sd             |
| 42   | SDMMC D3 (数据 3)        | SD 卡    | hal\_sd             |
| 45   | SD\_DET (卡检测)          | SD 卡    | hal\_sd             |
| 6    | 编码器 A 相 / 按键下 (Pro 版)    | 旋转编码器/按键 | hal\_encoder / hal\_button |
| 4    | 编码器 B 相 / 按键上 (Pro 版)    | 旋转编码器/按键 | hal\_encoder / hal\_button |
| 5    | 编码器按键 / 确认键 (Pro 版) / 唤醒 | 旋转编码器/按键 | hal\_encoder / hal\_button / hal\_pwr |
| 17   | WS2812 数据线             | RGB LED | hal\_led            |
| 8    | ADC 采样使能               | 电池分压电路  | hal\_bat            |
| 1    | ADC\_CH0 (采样输入)        | 电池分压电路  | hal\_bat            |
| 21   | 外设电源控制                 | 电源管理    | hal\_pwr            |

### 10.2 EPD 显示屏接口

| 信号   | GPIO | 方向 | 说明                              |
| ---- | ---- | -- | ------------------------------- |
| SCK  | 48   | 输出 | SPI 时钟                          |
| SDIN | 47   | 输出 | SPI 数据 (基础版 MOSI / Pro 版半双工)     |
| CS   | 14   | 输出 | 片选，低有效                          |
| DC   | 13   | 输出 | 数据/命令选择                         |
| RST  | 12   | 输出 | 复位，低有效                          |
| BUSY | 11   | 输入 | 忙状态指示，高电平空闲                     |

### 10.3 SDMMC 接口

| 信号  | GPIO | 方向 | 说明       |
| --- | ---- | -- | -------- |
| CLK | 40   | 输出 | SDMMC 时钟 |
| CMD | 41   | 双向 | 命令/响应线   |
| D0  | 39   | 双向 | 数据线 0    |
| D1  | 38   | 双向 | 数据线 1    |
| D2  | 2    | 双向 | 数据线 2    |
| D3  | 42   | 双向 | 数据线 3    |
| DET | 45   | 输入 | 卡检测      |

### 10.4 用户输入接口

**基础版 — 旋转编码器：**

| 信号  | GPIO | 方向 | 说明           |
| --- | ---- | -- | ------------ |
| A   | 6    | 输入 | 编码器 A 相 (正交) |
| B   | 4    | 输入 | 编码器 B 相 (正交) |
| KEY | 5    | 输入 | 编码器按键 / 唤醒   |

**Pro 版 — 三按键：**

| 信号      | GPIO | 方向 | 说明     |
| ------- | ---- | -- | ------ |
| KEY\_UP | 4    | 输入 | 上 / 右按键 |
| KEY\_DN | 6    | 输入 | 下 / 左按键 |
| KEY\_OK | 5    | 输入 | 确认键 / 唤醒 |

### 10.5 电源与电池接口

| 信号            | GPIO | 方向 | 说明          |
| ------------- | ---- | -- | ----------- |
| BAT\_ADC\_EN  | 8    | 输出 | ADC 采样使能    |
| BAT\_ADC      | 1    | 输入 | 电池电压 ADC 采样 |
| PERI\_PWR\_EN | 21   | 输出 | 外设电源控制      |
| WAKEUP        | 5    | 输入 | 深度睡眠唤醒      |

### 10.6 RGB LED 接口

| 信号  | GPIO | 方向 | 说明          |
| --- | ---- | -- | ----------- |
| DIN | 17   | 输出 | WS2812 数据输入 |

***

## 11. 固件分区表

设备 Flash 采用以下分区方案，支持 OTA 双区升级：

| 分区名       | 类型   | 子类型    | 偏移     | 大小   |
| --------- | ---- | ------ | ------ | ---- |
| nvs       | data | nvs    | 0x9000 | 128K |
| otadata   | data | ota    | —      | 8K   |
| phy\_init | data | phy    | —      | 4K   |
| ota\_0    | app  | ota\_0 | —      | 3M   |
| ota\_1    | app  | ota\_1 | —      | 3M   |

***

## 12. 初始化启动流程

设备上电后调用链：`app_main()` → `sys_init()` → `hal_init()`，HAL 层按以下顺序初始化各外设：

**基础版：**
```
hal_pwr_init()       → 使能外设电源 (GPIO21 高)
hal_bat_init()       → 配置 ADC 电池检测，读取一次电量
hal_led_init()       → 初始化 RGB LED (GPIO17，白色 10% 亮度)
hal_sd_init()        → 挂载 SDNAND 到 /sdcard，自动格式化
hal_encoder_init()   → 初始化旋转编码器 (GPIO6/4/5)
hal_epd_init()       → 初始化 WFT 电子纸 (SPI2 四线)
```

**Pro 版：**
```
hal_pwr_init()       → 使能外设电源 (GPIO21 高)
hal_bat_init()       → 配置 ADC 电池检测，读取一次电量
hal_led_init()       → 初始化 RGB LED (GPIO17，白色 10% 亮度)
hal_sd_init()        → 挂载 SDNAND 到 /sdcard，自动格式化
hal_button_init()    → 初始化三按键 (GPIO4/6/5)
hal_epd_init()       → 初始化 SE0368-C 电子纸 (SPI2 半双工三线)
```

***

## 13. 物理规格

| 参数   | 值                |
| ---- | ---------------- |
| 尺寸   | 约 92 × 60 × 7 mm |
| 重量   | 约 150g（含电池）      |
| 安装方式 | 背部磁吸             |
| 显示屏  | 3.6 英寸彩色电子纸 (基础版 600×400 / Pro 版 792×528) |

***

## 14. 硬件设计文件

| 文件                                         | 说明            |
| ------------------------------------------ | ------------- |
| `hardware/pcb/Schematic_framefilm_2.0.pdf` | 电路原理图 v2.0    |
| `hardware/pcb/Schematic_framefilm_3.0.pdf` | 电路原理图 v3.0    |
| `hardware/pcb/Schematic_framefilm_3.1.pdf` | 电路原理图 v3.1 (最新) |
| `hardware/model/外壳_v1/`                    | 外壳 3D 模型 v1   |
| `hardware/model/外壳_v2/`                    | 外壳 3D 模型 v2   |
| `docs/datasheet/esp32-s3_datasheet_cn.pdf` | ESP32-S3 数据手册 |
| `docs/datasheet/3.6inch_e-Paper_HAT+.pdf`  | EPD 显示屏数据手册   |

***

## 15. 硬件架构框图

```
┌─────────────────────────────────────────────────────────┐
│                      FrameFilm 硬件架构                   │
├─────────────────────────────────────────────────────────┤
│                                                         │
│  ┌──────────┐     SPI2           ┌──────────────────┐   │
│  │          │ ◄───────────────── │   EPD 电子纸      │   │
│  │          │   SCK/SDIN         │   基础版 600×400   │   │
│  │          │   CS/DC/RST        │   Pro 版 792×528  │   │
│  │          │   BUSY             │   (WFT/SE0368-C)  │   │
│  │          │                    └──────────────────┘   │
│  │          │                                           │
│  │          │   SDMMC 4-bit      ┌──────────────────┐   │
│  │          │ ◄─────────────────►│   SDNAND / SD 卡  │   │
│  │  ESP32   │                    └──────────────────┘   │
│  │   -S3    │                                           │
│  │          │   RMT              ┌──────────────────┐   │
│  │          │ ──────────────────►│   WS2812 LED × 2 │   │
│  │          │                    └──────────────────┘   │
│  │          │                                           │
│  │          │   PCNT / GPIO      ┌──────────────────┐   │
│  │          │ ◄───────────────── │   旋转编码器/按键   │   │
│  │          │                    │   (基础版/Pro 版)  │   │
│  │          │                    └──────────────────┘   │
│  │          │                                           │
│  │          │   ADC1 CH0         ┌──────────────────┐   │
│  │          │ ◄───────────────── │   锂电池          │   │
│  │          │                    │   (电量检测)      │   │
│  │          │                    └──────────────────┘   │
│  │          │                                           │
│  │  BLE/WiFi│ - - - - - - - - -►  手机微信小程序        │
│  │          │ - - - - - - - - -►  OTA 升级              │
│  └──────────┘                                           │
│                                                         │
│  供电：可充电锂电池 → 电源管理 (GPIO21) → 外设           │
│                                                         │
└─────────────────────────────────────────────────────────┘
```

***

## 16. 软件依赖

| 组件库                   | 版本     | 用途                      |
| --------------------- | ------ | ----------------------- |
| ESP-IDF               | 5.5.2  | 固件开发框架                  |
| esp-idf-lib/encoder   | 3.0.2  | 旋转编码器驱动 (仅基础版)           |
| espressif/led\_strip  | 3.0.1  | WS2812 LED 驱动           |
| espressif/button      | —      | 按键驱动 (仅 Pro 版)           |
| fatfs                 | —      | SD 卡文件系统                |
| esp\_adc              | —      | ADC 驱动        |

> 完整依赖信息见 `firmware/frame_film/dependencies.lock`

