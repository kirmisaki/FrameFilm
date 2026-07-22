# FrameFilm 架构设计

> 固件三层架构 + 双版本差异 + 组件依赖关系

## 固件分层架构

```
┌─────────────────────────────────────────────────┐
│  app_main()  ── 应用入口                         │
├─────────────────────────────────────────────────┤
│  film_service  ── 服务层 (最上层)                 │
│  ├─ service_ble      BLE 通信服务               │
│  ├─ service_ble_gatts GATT 服务注册              │
│  ├─ service_file     SD 卡文件管理               │
│  ├─ service_film     照片播放逻辑               │
│  ├─ service_param    NVS 参数持久化              │
│  ├─ service_ota      OTA 固件升级               │
│  ├─ service_monitor  系统监控/休眠管理            │
│  ├─ service_wifi     WiFi STA (Pro 版)          │
│  └─ service_port     系统移植层                  │
├─────────────────────────────────────────────────┤
│  film_hal  ── 硬件抽象层 (中间层)                 │
│  ├─ hal_epd          电子纸驱动 (WFT/SE0368)     │
│  ├─ hal_sd           SDMMC 存储                  │
│  ├─ hal_led          WS2812 RGB LED             │
│  ├─ hal_bat          电池 ADC 检测               │
│  ├─ hal_pwr          外设电源控制 (GPIO21)        │
│  ├─ hal_encoder      旋转编码器 (仅基础版)         │
│  └─ hal_button       三按键 (仅 Pro 版)           │
├─────────────────────────────────────────────────┤
│  film_sys  ── 系统层 (最底层)                     │
│  ├─ sys_init         系统初始化入口               │
│  ├─ sys_log          统一日志                    │
│  ├─ sys_cfg          系统配置                    │
│  └─ sys_err          错误码定义                  │
└─────────────────────────────────────────────────┘
```

## 启动流程

```
app_main()
  ├─ film_sys_init()
  │   └─ NVS 初始化, 日志系统
  ├─ film_hal_init()
  │   ├─ hal_pwr_init()         # 开启外设电源
  │   ├─ hal_bat_init()         # ADC 电池检测
  │   ├─ hal_led_init()         # RGB LED 白色 10%
  │   ├─ hal_sd_init()          # 挂载 SD 卡
  │   ├─ hal_encoder_init()     # (基础版) 编码器
  │   │   或 hal_button_init()  # (Pro 版) 三按键
  │   └─ hal_epd_init()         # 电子纸初始化
  └─ film_service_init()
      ├─ service_ble_init()     # BLE GATT 服务
      ├─ service_file_init()    # 文件系统
      ├─ service_film_init()    # 播放逻辑
      ├─ service_param_init()   # NVS 参数加载
      ├─ service_ota_init()     # OTA
      ├─ service_monitor_init() # 监控任务
      └─ service_wifi_init()    # (Pro 版) WiFi
```

## 基础版 vs Pro 版关键差异

| 组件 | 基础版 | Pro 版 | 差异位置 |
|------|--------|--------|----------|
| EPD 驱动 | WFT 系列, SPI 四线 | SE0368-C, SPI 三线半双工 | `hal_epd.h/c` |
| 分辨率 | 600×400 | 792×528 | `hal_epd.h` 宏 |
| 刷新命令 | `DRF(0x12)` | `REF(0x17)+A5` + 温度补偿 | `hal_epd.c` |
| 温度传感器 | 无 | 内置 TSE/TSD/WFT/WFD | `hal_epd.c` |
| 输入设备 | 旋转编码器 | 三按键 | `hal_encoder` vs `hal_button` |
| WiFi | 无 | STA + HTTP 下载 | `service_wifi.c` |
| 播放模式 | 0:手动, 1:本地轮播 | 0:手动, 1:本地轮播, 2:WiFi轮播 | `service_film.c` |
| Flash | 4MB | 16MB | `sdkconfig` |
| PSRAM | Quad SPI | Octal SPI | `sdkconfig` |

## GPIO 引脚分配 (两版相同)

| GPIO | 功能 (基础版) | 功能 (Pro 版) | 外设 |
|------|-------------|-------------|------|
| 48 | SPI2 SCK | SPI2 SCK | EPD |
| 47 | SPI2 MOSI | SPI2 SDIN (半双工) | EPD |
| 14 | CS | CS | EPD |
| 13 | DC | DC | EPD |
| 12 | RST | RST | EPD |
| 11 | BUSY | BUSY | EPD |
| 40-42,38-39,2 | SDMMC | SDMMC | SD 卡 |
| **6** | **编码器 A** | **按键下** | 输入 |
| **4** | **编码器 B** | **按键上** | 输入 |
| **5** | **编码器按键/唤醒** | **确认键/唤醒** | 输入 |
| 17 | WS2812 DIN | WS2812 DIN | RGB LED |
| 8 | ADC 使能 | ADC 使能 | 电池分压 |
| 1 | ADC_CH0 | ADC_CH0 | 电池电压 |
| 21 | 外设电源 | 外设电源 | 电源控制 |

## 参数存储 (NVS)

`service_param.h` 定义持久化参数结构 `ServiceParam_Def_t`:

```c
typedef struct {
    uint8_t factory_flag;                  // 出厂标志
    ServiceFilm_Def_t film;                // 播放参数
    ServiceSleep_Def_t sleep;              // 休眠参数
    ServiceNetwork_Def_t network;          // 网络参数 (Pro版)
} ServiceParam_Def_t;

// 网络子结构 (Pro版)
typedef struct {
    uint8_t wifi_enable;                   // WiFi 开关
    char wifi_ssid[64];                    // SSID
    char wifi_password[64];                // 密码
    char film_api_url[128];                // API URL
} ServiceNetwork_Def_t;
```

## 依赖组件 (idf_component.yml)

| 组件 | 版本 | 用途 |
|------|------|------|
| esp-idf-lib/encoder | 3.0.2 | 旋转编码器 (基础版) |
| espressif/led_strip | 3.0.1 | WS2812 LED |
| espressif/button | - | 按键驱动 (Pro 版) |
