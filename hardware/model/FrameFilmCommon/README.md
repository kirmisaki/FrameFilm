# FrameFilmCommon 主板硬件说明

本文按 `Netlist_PCB_CommonMB_2026-11-27.tel` 整理 FrameFilmCommon 主板的硬件逻辑与引脚定义。
物料/封装以网表为准；引脚号均为 **ESP32-S3-WROOM-1 模块引脚号（U1.x）**，括号内为实际 GPIO。

- 主控：`ESPS3-32-WROOM-1-N16R8`（ESP32-S3-WROOM-1，16MB Flash / 8MB Octal PSRAM）
- 屏幕接口：10Pin FPC（`U8`），单面板 SPI，另留 `CS2/BAK` 两路
- 存储：TF 卡（4-bit SDMMC，无卡检测）
- 音频：10Pin FPC（`U6`），7 路 GPIO + 3V3_C
- 交互：4Pin FPC（`U7`），1 个按键 + 1 颗 WS2812
- 供电：USB-C 5V / 单节锂电池，TP4054 充电 + SGM61020 升压 + SGM2576 负载开关
- 下载：CH343P USB-UART（Q1/Q2 自动下载）＋ 原生 USB（`U3`）

引脚速查见 [`pinmap.csv`](./pinmap.csv)。

---

## 1. 电源树

```
USB-C VBUS (5V, USB1)
  ├─ VCC(5V) ─┬─ CH343P VDD5 / VBUS          USB 转串口
  │           ├─ TP4054 VCC                 线性锂电充电
  │           └─ CHG LED (R14 4.7K)
  │
  └─ TP4054 BAT ── BAT_I ──┬─ BAT1 (电池座)
                           └─ PWR1 拨动开关 ── BAT_O
                                                  │
                                    SGM61020 boost (L1 2.2uH)
                                                  │
                                                 3V3 (always-on)
                                                  ├─ U1 ESP32-S3-WROOM-1
                                                  ├─ U7 按键/LED 模块
                                                  └─ SGM2576 IN
                                                       │  EN = 3V3CTRL (GPIO9)
                                                       │  ILIM = R18 4.7K → GND
                                                       └─ 3V3_C
                                                          ├─ U8 EPD
                                                          ├─ CARD1 TF 卡 + 上拉 R7~R12
                                                          ├─ U6 音频模块
                                                          └─ U9 自身输出电容
```

| 器件 | 型号 | 作用 | 关键参数 |
|---|---|---|---|
| U10 | TP4054 | 单节锂电线性充电 | `PROG` 经 R16 4K 到地 → 约 **250mA**；`CHRG` 开漏经 R14 4.7K 驱动 CHG1 |
| U11 | SGM61020XN5G | 同步升压 | 2.2µH（L1）；FB 分压 R22 220K / R21 49.9K → 约 **3.25V** |
| U9 | SGM2576YN5G | 负载开关（3V3 → 3V3_C） | `EN`=3V3CTRL 高有效；`ILIM` 经 R18 4.7K 到地 |
| PWR1 | SK-3296S | 电池总开关 | 1=BAT_I，2/3=BAT_O，EH=固定脚 |
| Q3 | AO3400A | 电池分压采样开关 | 栅极由 `ADC_CTRL` 经 R19 1K 驱动，R20 1M 下拉 |

> **3V3_C 是可控外设电源**：EPD、TF 卡、音频模块都挂在它上面；`GPIO9` 拉高才上电。
> 固件必须在访问 EPD/SD 前把 `GPIO9` 置高，休眠时置低可彻底断开外设漏电。
> `U7`（按键/LED）接的是常电 3V3，所以休眠后按键仍能唤醒。

### 1.1 电池电压采样

```
BAT_O ── R13 20K ──┬── R15 1K ── ADC (GPIO6 / ADC1_CH5)
                   │
                   ├── C12 100nF ──┐
                   │               │
                   └── R17 10K ────┴── Q3 漏极（Q3 源极 ── GND）
                                       Q3 栅极 ── R19 1K ── ADC_CTRL (GPIO7)
                                                  R20 1M ── GND
```

- `ADC_CTRL = 1` → Q3 导通，把 C12/R17 的下端拉到地 → 分压比 `10/(20+10) = 1/3`；`ADC_CTRL = 0` → 采样支路断开，静态漏电几乎为 0。C12 与 R17 并联，兼作采样滤波。
- 采样前需给 C12 稳定时间（现有固件取 100ms）。
- 理论换算：`Vbat = Vadc × 3`；固件里沿用 3.06 的经验修正系数。

---

## 2. USB 与下载

板上有 **两个 USB 口**，功能不同：

| 接口 | 器件 | 连接到 | 用途 |
|---|---|---|---|
| USB1（USB-C 16P） | CH343P (U2) | UART0：`GPIO43`(TXD0) / `GPIO44`(RXD0) | 烧录 / 串口日志 / `idf.py monitor` |
| U3（4Pin 排针/座） | 直连 | `GPIO19`(D-) / `GPIO20`(D+) | ESP32-S3 原生 USB（CDC/JTAG） |

- USB1 的 CC1/CC2 各经 R2/R4（5.1K）下拉，配置为 **Sink**；D+/D- 经 U5（PRTR5V0U2X）ESD 保护后进 CH343P，原生 `USB_D±` 经 U4 保护后到 U3。
- CH343P 由 5V 供电，内部 3.3V 稳压（V3，U2.6）与 VIO（U2.1）相连，外接 C2 1µF + C5 100nF。
- **自动下载**：CH343P 的 `DTR`/`RTS` 经 Q1/Q2（S8050）交叉网络分别拉 `EN` 与 `GPIO0`：
  - `RTS` → R1 12K → Q1 基极；Q1 集电极 → `nRST`(EN)
  - `DTR` → Q2 发射极；Q1 发射极 → R5 12K → Q2 基极；Q2 集电极 → `GPIO0`
  - `GPIO0` 另有 R6 10K 上拉到 3V3、C8 1µF 到地；`EN` 有 R3 10K 上拉、C1 1µF 到地
- CH343P 的 `CTS/DSR/DCD/RI/ACT#` 未使用。

> `GPIO0` 是下载 strapping 脚，`GPIO3`/`GPIO45`/`GPIO46` 也是 strapping；本板 `GPIO3`（模块脚 15）与 `GPIO46`（模块脚 16）**未连接**，`GPIO45` 接音频（`AUDIO7`），下载时需注意其电平。

---

## 3. 屏幕接口（U8，10Pin FPC）

| U8 脚 | 网络 | GPIO | 说明 |
|---|---|---|---|
| 1 | GND | — | 地 |
| 2 | 3V3_C | — | 受控 3.3V |
| 3 | EPD_BAK | GPIO10 | 备用/第二面板信号，单面板不用 |
| 4 | EPD_BUSY | GPIO11 | 忙信号（本屏 **低=忙**） |
| 5 | EPD_RST | GPIO12 | 复位，低有效 |
| 6 | EPD_DC | GPIO13 | 命令/数据选择 |
| 7 | EPD_CS | GPIO14 | 片选，低有效 |
| 8 | EPD_CS2 | GPIO21 | 第二片选，单面板不用 |
| 9 | EPD_DIN | GPIO47 | SPI MOSI |
| 10 | EPD_SCK | GPIO48 | SPI SCK |
| 11/12 | GND | — | 固定脚 |

- 现用屏：**GDEM075T41WT，7.5 英寸，800×480，黑白 1bpp**，单面板 4 线 SPI（CS/DC/RST/BUSY）。固件面板 ID 为 `0x09`（BLE `0x42` 回包）。
- `EPD_BAK` / `EPD_CS2` 在单面板方案中悬空；固件不要把它们配置成会干扰总线的输出。
- EPD 由 `3V3_C` 供电，**断电即停在当前画面**（电子纸特性），休眠前必须保证最后一帧已刷完。

---

## 4. TF 卡（CARD1）

4-bit SDMMC，**没有卡检测脚**（CARD1 pin 9 未连接）；插拔状态只能靠挂载结果判断。

| 信号 | GPIO | 说明 |
|---|---|---|
| SD_CLK | GPIO40 | 时钟 |
| SD_CMD | GPIO41 | 命令（SD_MOSI） |
| SD_D0 | GPIO39 | 数据0（SD_MISO） |
| SD_D1 | GPIO38 | 数据1 |
| SD_D2 | GPIO2 | 数据2 |
| SD_D3 | GPIO42 | 数据3（SD_CS） |

- 全部信号经 R7~R12（10K）上拉到 `3V3_C`。
- TF 座电源同样来自 `3V3_C`，`GPIO9` 未上电时挂载必然失败。

> 网表里 `GPIO45` 是音频 `AUDIO7`，不要把它当 TF 检测脚使用。

---

## 5. 音频接口（U6，10Pin FPC）

| U6 脚 | 网络 | GPIO |
|---|---|---|
| 1/2 | GND | — |
| 3 | 3V3_C | — |
| 4 | AUDIO7 | GPIO45 |
| 5 | AUDIO6 | GPIO8 |
| 6 | AUDIO5 | GPIO18 |
| 7 | AUDIO4 | GPIO17 |
| 8 | AUDIO3 | GPIO16 |
| 9 | AUDIO2 | GPIO15 |
| 10 | AUDIO1 | GPIO1 |
| 11/12 | GND | — |

音频模块由 `3V3_C` 供电，7 根信号全部是普通 GPIO（具体功能由模块定义，主板不做电平转换）。

---

## 6. 按键 / RGB LED（U7，4Pin FPC）

| U7 脚 | 网络 | GPIO | 说明 |
|---|---|---|---|
| 1 | 3V3 | — | 常电 3.3V |
| 2 | GND | — | 地 |
| 3 | KEY | GPIO4 | 单按键，**低有效**（按下接地） |
| 4 | RGB | GPIO5 | WS2812 数据 |
| 5/6 | GND | — | 固定脚 |

- 按键同时作为 **deep sleep 唤醒源**（EXT0，低电平）。
- RGB 为单颗 WS2812（GRB 顺序），固件按 1 颗灯配置即可。
- `U7` 常电，休眠时仍可检测按键；`3V3_C` 关闭不影响它。

---

## 7. 引脚总表

完整机器可读版本见 [`pinmap.csv`](./pinmap.csv)。按功能汇总：

| 功能 | GPIO | 模块脚 | 方向 | 备注 |
|---|---|---|---|---|
| KEY | 4 | 4 | 输入 | 低有效，EXT0 唤醒 |
| RGB | 5 | 5 | 输出 | WS2812 |
| BAT_ADC | 6 | 6 | 模拟输入 | ADC1_CH5，分压 ×3 |
| ADC_CTRL | 7 | 7 | 输出 | 高=打开电池采样 |
| AUDIO6 | 8 | 12 | 双向 | 音频模块 |
| 3V3CTRL | 9 | 17 | 输出 | 高=3V3_C 上电 |
| EPD_BAK | 10 | 18 | 输出/不用 | 单面板悬空 |
| EPD_BUSY | 11 | 19 | 输入 | 低=忙 |
| EPD_RST | 12 | 20 | 输出 | 低有效 |
| EPD_DC | 13 | 21 | 输出 | 0=命令 1=数据 |
| EPD_CS | 14 | 22 | 输出 | 低有效 |
| AUDIO2 | 15 | 8 | 双向 | 音频模块 |
| AUDIO3 | 16 | 9 | 双向 | 音频模块 |
| AUDIO4 | 17 | 10 | 双向 | 音频模块 |
| AUDIO5 | 18 | 11 | 双向 | 音频模块 |
| USB_D- | 19 | 13 | 双向 | 原生 USB |
| USB_D+ | 20 | 14 | 双向 | 原生 USB |
| EPD_CS2 | 21 | 23 | 输出/不用 | 单面板悬空 |
| EPD_DIN | 47 | 24 | 输出 | SPI MOSI |
| EPD_SCK | 48 | 25 | 输出 | SPI SCK |
| AUDIO7 | 45 | 26 | 双向 | 音频模块 |
| GPIO0 | 0 | 27 | 输入/ Strapping | 下载模式选择 |
| SD_D1 | 38 | 31 | 双向 | SDMMC |
| SD_D0 | 39 | 32 | 双向 | SDMMC |
| SD_CLK | 40 | 33 | 输出 | SDMMC |
| SD_CMD | 41 | 34 | 双向 | SDMMC |
| SD_D3 | 42 | 35 | 双向 | SDMMC |
| U0RXD | 44 | 36 | 输入 | CH343P |
| U0TXD | 43 | 37 | 输出 | CH343P |
| SD_D2 | 2 | 38 | 双向 | SDMMC |
| AUDIO1 | 1 | 39 | 双向 | 音频模块 |
| EN | 3 | 3 | 输入 | 复位，低有效 |

> 模块脚 1/40/41 为 GND，2 为 3V3。

---

## 8. 与旧机型的差异（移植提示）

| 项 | 旧机型（frame_film / ark） | 本板（FrameFilmCommon） |
|---|---|---|
| 屏幕 | E6 多色 4bpp | **GDEM075T41WT 800×480 1bpp** |
| 屏幕电源 | 常电或独立脚 | **GPIO9 负载开关（3V3_C）** |
| 按键 | 3 键（上/下/确认） | **1 键（GPIO4）** |
| LED | GPIO17 | **GPIO5** |
| 电池采样 | GPIO1 / 使能 GPIO8 | **GPIO6 / 使能 GPIO7** |
| 外设电源脚 | GPIO21 | **GPIO9** |
| 唤醒脚 | GPIO5 低有效 | **GPIO4 低有效** |
| SD | 4-bit + 检测脚 | **4-bit，无检测** |
| 音频 | 无 | **U6 7 路信号** |
| 第二 USB | 无 | **U3 原生 USB（IO19/20）** |
| EPD 额外信号 | 无 | **EPD_BAK(IO10) / EPD_CS2(IO21)** |

## 9. 注意

1. `3V3_C` 由 `GPIO9` 控制，**上电后第一件事是拉高 GPIO9**，否则 EPD/SD/音频都不可用。
2. EPD 的 BUSY 是低电平忙，等待逻辑与旧屏（高电平忙）相反。
3. 单面板方案中 `GPIO10`、`GPIO21` 保持高阻，不要输出固定电平去驱动不存在的第二面板。
4. TF 卡无检测脚，`GPIO45` 已被音频占用，不能复用为卡检测。
5. 电池采样必须“先开 Q3、等电容稳定、再采样、再关 Q3”，否则读数会被 C12 和漏电流带偏。
