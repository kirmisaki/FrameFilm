# Dock 音频驱动设计（ES8311 + NS4150B）

> 版本：v0.1
> 状态：设计中 —— GPIO 引脚待原理图定稿，以可配置宏方式规划
> 主导思想：借鉴 **xiaozhi-esp32** 的 `Es8311AudioCodec` 集成范式，遵守本项目 `film_service → film_hal → film_sys` 单向分层。

## 1. 模块概览

| 器件 | 作用 | 接口 |
|------|------|------|
| ES8311 | 音频编解码器（ADC 采集 + DAC 播放，全双工） | I2C（控制）+ I2S（数据） |
| NS4150B | D 类功放，驱动 4Ω 喇叭（3W@5V） | 独立 `PA_EN` 使能脚 |

### 1.1 ES8311 关键特性
- 默认 I2C 地址 `0x18`（可通过 AD0 改写）
- I2S 数据位：16/24 bit，采样率 16k – 64k
- 全双工：ADC（麦克风输入）与 DAC（喇叭输出）可同时工作
- 供电 2.5 – 3.6V

### 1.2 NS4150B 关键特性
- D 类功放，需独立 `PA_EN` 使能
- **应与 CODEC 使能分开**，避免上/下电时的 POP 音

## 2. 硬件接线（可配置宏）

> 说明：以下为已确认的实际接线（用户提供模块引脚），**以 `hal_audio.h` 中的宏定义为准**。已与现有外设核对无冲突（EPD 用 SPI GPIO48/47/11/12/13/14，音频用 I2C GPIO4/5 + I2S GPIO1/8/18/21/45）。

| 模块引脚 | 方向 | ESP32-S3 GPIO（已确认） | MCU 侧对应 |
|----------|------|------------------------|-----------|
| ES8311 SDA | I/O | GPIO_NUM_4 | I2C SDA |
| ES8311 SCL | I | GPIO_NUM_5 | I2C SCL |
| ES8311 SCLK/BCLK | I | GPIO_NUM_8 | I2S0 BCLK |
| ES8311 LRCK/WS | I | GPIO_NUM_1 | I2S0 WS（默认单声道） |
| ES8311 DIN | I | GPIO_NUM_45 | I2S0 DOUT（TX，主控→CODEC） |
| ES8311 DOUT | O | GPIO_NUM_21 | I2S0 DIN（RX，CODEC→主控） |
| ES8311 MCLK | I | GPIO_NUM_18 | I2S0 MCLK（已接，启用） |

> **模块无外部 PA_EN / CODEC_EN**（原理图确认）：模块对外仅引出 J41 的 9 根线（SDA/SCL/MCLK/SCLK/DOUT/LRCK/DIN/5V/GND）。NS4150B（U3）的 `CTRL` 高=On / 低=Shutdown，被板上电阻（R11 10K 等）固定为默认态、未引出；ES8311（U1）的 `RESET` / `PWDN` 也未引出。故主控无法用 GPIO 控制功放/CODEC 通断，复位走 **I2C 软复位（写寄存器 0x00）**，POP 音规避须改为软件方式（见 §3.3）。

> **引脚映射原则**：一律通过 `hal_audio.h` 内宏定义（如 `AUDIO_I2S_BCLK_PIN`、`AUDIO_I2C_SDA_PIN`），不写死在实现中。因无外部使能脚，不定义 `PA_EN` / `CODEC_EN` 宏。

## 3. 驱动框架（HAL 层）

### 3.1 新增文件

```
components/film_hal/
├── inc/hal_audio.h      # 接口 + 引脚/参数宏
└── src/hal_audio.c      # I2C/I2S/GPIO 初始化 + 读写流
```

### 3.2 接口设计（hal_audio.h）

```c
// 引脚/参数宏（实际接线，已确认；无 PA_EN/CODEC_EN 外部使能脚）
#define AUDIO_TAG                 "HAL_AUDIO"
#define AUDIO_I2C_ADDR            0x18
#define AUDIO_SAMPLE_RATE         16000         // 采样率：输入必须==输出
#define AUDIO_I2S_HOST            I2S_NUM_0
#define AUDIO_I2S_SCLK_PIN        GPIO_NUM_8    // BCLK
#define AUDIO_I2S_WS_PIN          GPIO_NUM_1    // LRCK（默认单声道）
#define AUDIO_I2S_DOUT_PIN        GPIO_NUM_45   // TX（主控→CODEC，接模块 DIN）
#define AUDIO_I2S_DIN_PIN         GPIO_NUM_21   // RX（CODEC→主控，接模块 DOUT）
#define AUDIO_I2S_MCLK_PIN        GPIO_NUM_18   // MCLK 已接，启用
#define AUDIO_I2S_SDA_PIN         GPIO_NUM_4
#define AUDIO_I2S_SCL_PIN         GPIO_NUM_5

// 接口（使能/通断通过 I2S 通道启停 + ES8311 寄存器控制，无外部 GPIO 使能脚）
void hal_audio_init(void);                                        // 初始化 I2C/I2S/GPIO + codec
void hal_audio_deinit(void);
int32_t hal_audio_read_stream(int16_t *buf, uint32_t frames);     // ADC 采集（mono）
int32_t hal_audio_write_stream(const int16_t *buf, uint32_t frames); // DAC 播放（mono）
void hal_audio_capture_start(void);                               // 开启 ADC（寄存器/enable，供 LISTEN）
void hal_audio_capture_stop(void);                                // 关闭 ADC（省电）
void hal_audio_playback_start(void);                              // 先输出静音帧再启用 DAC（规避 POP）
void hal_audio_playback_stop(void);                               // 输出静音帧后关 DAC（规避 POP）
void hal_audio_set_input_gain(uint8_t db);                        // 输入增益，默认 30dB
void hal_audio_set_volume(uint8_t vol);                           // 输出音量（软音量 0-100）
```

### 3.3 实现要点（借鉴 xiaozhi-esp32 `Es8311AudioCodec`）

1. **I2C 总线初始化**：配置 I2C 控制器（同 EPD 插入检测所用 I2C 总线，若引脚冲突则独立 `i2c_port`）。
2. **I2S 全双工通道**：创建 `I2S` 标准模式（`I2S_MODE_MASTER | TX | RX`），16bit / **单声道** / `AUDIO_SAMPLE_RATE`。ESP32-S3 的 RX/TX 共享时钟，可同时启停。
3. **codec 复位时序**：I2C 写 ES8311 寄存器 `0x00` 后延时 ~5ms，再按规格配置（关键：先复位再 open，否则读不稳定）。
4. **打开 codec**：按 **16bit / mono / 采样率** 配置 ADC 与 DAC 路径。
5. **增益与音量**：
   - 输入增益：默认 30dB（麦克风边沿触发，可调）
   - 输出音量：软件音量控制，避免直接改 DAC 满幅导致的爆音
6. **POP 音规避（无外部使能脚）**：模块功放常开，主控只能靠软件时序：**播放前先输出一小段零值/静音帧并稳定 I2S 时钟，再写入有效数据；播放结束先输出静音帧再停 DAC**。ES8311 上电默认处于低功耗/静音态，配置完成后才允许输出，可挡住大部分上电爆音；配合 ES8311 寄存器控制 ADC/DAC 通断（`hal_audio_capture_*` / `hal_audio_playback_*`），实现"按需启停 + 软静音"。

> 可选：若采用 Espressif **`esp_codec_dev`** 组件（已内置 ES8311 驱动），可 `esp_codec_dev_new(ESP_CODEC_DEV_TYPE_IN_OUT)` + 回调 I2S 读写，简化寄存器配置。本项目默认走 **裸 I2S + 手动 I2C 寄存器**（与 xiaozhi 早期思路一致），以降低组件依赖与可控性；后续若需要可切换为 `esp_codec_dev`。

## 4. 服务层框架（service_audio）

> 服务层只调用 HAL 接口（`hal_audio_*`），不直接操作 GPIO / I2S / I2C，遵守分层约束。

### 4.1 新增文件

```
components/film_service/
├── inc/service_audio.h   # 语音状态机接口
└── src/service_audio.c   # 采集/播放/唤醒/双工调度
```

### 4.2 状态机

```
IDLE ──(唤醒事件/唤醒词)──► LISTEN ──(VAD/按键/超时)──► PROCESS
                                   ▲                          │
                                   │                          ▼
                                   └──(重试)◄── FAIL ── SEND(上传后端)
                                                             │
                                                             ▼ (后端返回 TTS 音频/显示指令)
                                                          PLAYBACK ──(播放完)──► IDLE
```

| 状态 | 动作 | HAL 调用 |
|------|------|---------|
| `IDLE` | 关 ADC/DAC 通道省电（无外部使能脚） | `capture_stop` / `playback_stop` |
| `LISTEN` | 开启 ADC，采集麦克风流，VAD/能量检测判断说话结束 | `capture_start` / `playback_stop` / `read_stream` |
| `PROCESS` | 停止采集，组包上传后端（WiFi） | `capture_stop` |
| `PLAYBACK` | 软静音开机后播放 TTS 音频流 | `playback_start` / `write_stream` |
| `FAIL` | 播放错误提示音或静默，回 `IDLE` | `playback_stop` |

### 4.3 唤醒机制（阶段二实现）

- 阶段二初版：以 **编码器按键/触摸** 作为唤醒触发（`hal_input` 事件驱动），避免前置离线唤醒词的工程量。
- 后续：接入离线唤醒词（如 ESP-SR 或本地方案），或常驻低功耗 VAD 能量检测。

### 4.4 双工调度要点（借鉴 AudioService）

- ADC 与 DAC 共享 I2S 时钟，**同一时刻只允许一条数据流向运行**（采集或播放），避免时钟打架。
- 空闲立即关闭 codec 的 ADC/DAC 通道（无外部使能脚，用寄存器/通道停用），降低功耗（EPD 相框为电池/低功耗场景）。
- 播放与采集之间需做足够的 gap，防止回声/爆音串扰。

## 5. 构建配置改动

### 5.1 `film_hal/CMakeLists.txt`

在 `set(requires ...)` 末尾追加：

```cmake
esp_driver_i2s   # I2S 标准模式驱动
```

（若采用 `esp_codec_dev` 组件，则无需单独加 `espressif/esp_codec_dev` 到 idf_component.yml 后再 requires。）

### 5.2 初始化挂载

- `hal_init.c`：`hal_epd_init()` 前后追加 `hal_audio_init()`。
- `service_init.c`：追加 `service_audio_init()`（在 `service_wifi_init()` 之前，便于语音链路复用 WiFi 传输）。

## 6. 待确认项

- [x] **I2C/I2S 引脚分配**：已确认（SDA=4 / SCL=5 / SCLK=8 / LRCK=1 / DOUT=21 / DIN=45 / MCLK=18）
- [x] **PA_EN / CODEC_EN**：已确认模块**无外部使能脚**，上电即工作，POP 音用软件静音规避
- [ ] **I2C 总线**：复用现有 I2C 或独立端口（EPD 检测已用 I2C）
- [ ] **采样率**：默认 16kHz（语音够用），确认是否需 8k 以省带宽
- [ ] **驱动路径**：裸 I2S + 手动寄存器（默认） vs `esp_codec_dev` 组件
- [ ] **麦克风增益**：默认 30dB，按实际灵敏度微调
- [ ] **唤醒方案**：按键触发（初版） vs 离线唤醒词（后续）
