# st25r3916_lib — 精简 ST25R3916 NFC 库（ESP-IDF 5.5）

面向"卡包"类产品的最小 NFC 库：**读卡取身份** + **按身份模拟卡片** + **IRQ 唤醒**。
芯片驱动、ISO14443-A 防冲突/选卡、PICC 状态机都在库内完成；**卡片数据完全由应用侧管理**，
库只提供输入（要模拟的 UID/ATQA/SAK）和输出（读到的卡片身份、事件回调）。

> 目录名沿用需求中的 `st25r3916_lib`；芯片实际型号为 **ST25R3916**（B 版寄存器兼容）。

---

## 1. 特性

| 功能 | 说明 |
|---|---|
| 芯片初始化 | 上电时序、过热保护、稳压器校准、自动场检测、SPI 10 MHz 裸寄存器传输 |
| 读卡（PCD） | WUPA → ATQA → 防冲突（含 7 字节 UID 级联） → SELECT → UID/SAK/卡类型 |
| 卡模拟（PICC） | NFC-A：UID/ATQA/SAK 由芯片 PT 存储器硬件应答；软件状态机处理场开/关、HALT/唤醒、应用层帧 |
| MIFARE Classic | **按 ID 卡模拟**：只回 UID/ATQA/SAK，不响应 0x30 等扇区命令（原因见第 6 节） |
| NTAG21x | 内置 NTAG213/215/216：GET_VERSION / READ / FAST_READ / WRITE / COMPAT_WRITE / READ_CNT / READ_SIG，含 NDEF 读写与页写入回调 |
| 通用身份 | 任意 NFC-A 身份（Ultralight/NTAG/DESFire 的 SAK）都可按同一接口模拟 |
| 应用接口 | 事件回调 + `st25r_picc_transmit()` 应答自定义命令，可实现只读 NDEF 等扩展 |
| IRQ 唤醒 | IRQ 引脚可作为 ESP light-sleep / deep-sleep 唤醒源 |
| 依赖 | 仅 ESP-IDF（无 M5UnitUnified / M5HAL 依赖） |

---

## 2. 目录结构

```
st25r3916_lib/
├── CMakeLists.txt              IDF 组件定义
├── include/
│   └── st25r3916_nfc.h         唯一公共头文件
├── src/
│   ├── st25r3916_regs.h        寄存器/命令/中断位定义
│   ├── st25r3916_priv.h        内部接口
│   ├── st25r3916_hal.c         SPI 裸传输、寄存器、FIFO、IRQ、芯片初始化、唤醒
│   ├── st25r3916_pcd.c         读卡：REQA/WUPA、防冲突、SELECT
│   ├── st25r3916_picc.c        卡模拟：PT 存储、监听状态机
│   └── st25r3916_ntag.c        NTAG21x：内存镜像、命令处理、NDEF
├── examples/
│   ├── picc_m1/                模拟 MIFARE Classic 1K（ID 卡）
│   ├── ntag_ndef/              模拟 NTAG215（NDEF 可读可写）
│   └── pcd_read/               读卡并把身份交给"卡包"回调
└── docs/
    └── API.md                  API 详解
```

---

## 3. 接入工程

### 3.1 作为组件

把整个 `st25r3916_lib` 目录放到工程可搜索到组件的位置，二选一：

- 复制/软链到工程的 `components/` 下（IDF 会自动扫描 `components/` 的子目录）；
- 或在自己的工程顶层 `CMakeLists.txt` 里直接指向该目录本身
  （IDF 允许 `EXTRA_COMPONENT_DIRS` 中的目录自身是一个组件）：

```cmake
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/path/to/st25r3916_lib")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
```

然后在 `main/CMakeLists.txt` 的 `REQUIRES` / `PRIV_REQUIRES` 中加入 `st25r3916_lib`。

组件依赖已写好：`esp_driver_spi`、`esp_driver_gpio`（公共），`esp_timer`、`esp_hw_support`、`soc`（私有）。
目标版本为 **ESP-IDF v5.5**（使用 `esp_driver_*` 组件与 5.5 的 `esp_sleep_*_ext1_wakeup_io` API）。

### 3.2 直接编译示例

```bash
cd examples/picc_m1
idf.py set-target esp32s3
idf.py build
idf.py -p <PORT> flash monitor
```

示例工程的 `EXTRA_COMPONENT_DIRS` 已指向库所在目录，无需复制组件。

---

## 4. 硬件接线

示例默认使用 M5 Unit NFC（ESP32-S3）：

| 信号 | GPIO |
|---|---|
| SCLK | 10 |
| MOSI | 19 |
| MISO | 9 |
| CS | 20 |
| IRQ | 7 |

- SPI 固定 **mode 1**、默认 **10 MHz**（`st25r_config_t.clock_hz` 可改，建议不超过 10 MHz）。
- CS 由库手动控制，不经过 `spi_bus_add_device`；该 SPI host 需要由 `st25r_init()` 初始化
  （若 host 已被其它驱动初始化，`spi_bus_initialize` 会返回 `ESP_ERR_INVALID_STATE`）。
- 同一 host 上可以再挂其它设备，但库的裸寄存器传输不参与 SPI 驱动的总线锁，
  多设备访问必须由应用串行化。
- IRQ 可不接（`GPIO_NUM_NC`）；不接时库改为轮询中断寄存器，功能不变，延迟略高。
- 示例的 `sdkconfig.defaults` 已设置 `CONFIG_ESP_CONSOLE_SECONDARY_NONE=y`：
  ESP32-S3 上 GPIO19/20 同时也是 USB-Serial-JTAG 引脚，不关闭会和 MOSI/CS 冲突。

---

## 5. 快速上手

### 5.1 读卡 → 建卡包

```c
#include "st25r3916_nfc.h"

st25r_config_t cfg = ST25R_CONFIG_DEFAULT();
cfg.pin_sclk = 10; cfg.pin_mosi = 19; cfg.pin_miso = 9; cfg.pin_cs = 20; cfg.pin_irq = 7;

st25r_handle_t nfc = NULL;
ESP_ERROR_CHECK(st25r_init(&cfg, &nfc));
ESP_ERROR_CHECK(st25r_reader_begin(nfc));

st25r_card_t card;
if (st25r_reader_poll(nfc, &card, 500) == ESP_OK) {
    // card.uid / card.uid_len / card.atqa / card.sak / card.type
    // 在这里把身份写入你自己的卡包存储
    st25r_reader_halt(nfc);
}
```

`st25r_reader_poll()` 返回 `ESP_ERR_NOT_FOUND` 表示超时内没卡；其余错误直接看 `esp_err_to_name()`。

### 5.2 模拟卡片（M1 → ID 卡）

```c
st25r_picc_config_t picc = {
    .card = {
        .tech = ST25R_TECH_NFC_A,
        .atqa = 0x0004,
        .sak  = 0x08,
        .uid_len = 4,
        .uid = {0x11, 0x22, 0x33, 0x44},
    },
    .callback = on_event,        // 可 NULL
    .task_priority = 5,          // 0 = 不建任务，由 st25r_picc_update() 手动驱动
};
ESP_ERROR_CHECK(st25r_picc_begin(nfc, &picc));
```

`on_event` 收到 `ST25R_PICC_EVT_FIELD_ON/OFF/SELECTED/FRAME`。默认返回 `false` 即"只做 ID 卡"：
选卡完成后不再响应数据命令。返回 `true` 可以保持会话，并用 `st25r_picc_transmit()` 应答（例如实现只读 NDEF）。

### 5.3 模拟 NTAG21x（NDEF 可读可写）

```c
st25r_ntag_config_t ntag = {
    .type = ST25R_NTAG_215,
    .uid = {0x04, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66},
    .writable = true,                 // 允许手机写 NDEF
    .callback = on_ntag_event,        // 每次写页/本地改 NDEF 都会通知
};
ESP_ERROR_CHECK(st25r_ntag_begin(nfc, &ntag));
ESP_ERROR_CHECK(st25r_ntag_set_ndef_text(nfc, "Hello from ST25R3916"));
```

- 支持命令：`0x60 GET_VERSION`、`0x30 READ`、`0x3A FAST_READ`、`0xA2 WRITE`、
  `0x3E COMPAT_WRITE`、`0x39 READ_CNT`、`0x3C READ_SIG`、`0x50 HLTA`。
- 内存镜像默认放在句柄内；也可以把 `cfg.memory` 指向自己的缓冲区（容量不小于所选型号的镜像大小），
  让页面数据完全由外部管理。
- 手机写入后触发 `ST25R_NTAG_EVT_PAGE_WRITTEN`（带页号），应用可据此把镜像持久化到卡包。
- `AUTH0 = 0xFF`（不启用密码保护），因此 `PWD_AUTH` 返回 NAK；计数器与签名固定返回 0。

### 5.4 IRQ 唤醒

```c
// 进入 light sleep 前
ESP_ERROR_CHECK(st25r_wakeup_enable(nfc, ST25R_WAKE_LIGHT_SLEEP));
esp_light_sleep_start();
ESP_ERROR_CHECK(st25r_wakeup_disable(nfc, ST25R_WAKE_LIGHT_SLEEP));
// 醒来后芯片仍在模拟状态，读一次事件即可

// deep sleep（芯片需保持供电，醒来后重新 st25r_init）
ESP_ERROR_CHECK(st25r_wakeup_enable(nfc, ST25R_WAKE_DEEP_SLEEP));
esp_deep_sleep_start();
```

- light sleep：`gpio_wakeup_enable()` + `esp_sleep_enable_gpio_wakeup()`，IRQ 高电平唤醒。
- deep sleep：`esp_sleep_enable_ext1_wakeup()`，需要 IRQ 引脚是 RTC GPIO（ESP32-S3 的 GPIO7 可以）。
- 模拟状态下，读卡器一出现场，芯片就会拉高 IRQ（`I_eon`），因此"贴卡唤醒"不需要 MCU 参与。
- 唤醒后如果走 deep sleep，SPI/芯片配置会丢失，需要重新 `st25r_init()` + `st25r_picc_begin()`。

---

## 6. 设计说明与边界

### 6.1 M1 为什么是"ID 卡"

MIFARE Classic 的扇区读取需要 **Crypto-1 加密 + 加密奇偶位**。ST25R3916 的 FIFO 帧引擎只能发
"整字节 + 芯片自动奇偶"的帧，无法发 36 bit 自定义奇偶；透明模式（唯一能发任意比特的路径）在
本模块上负载调制无输出（已用示波器逐项验证，详见工程根目录 `NFC_M1_问题说明.md`）。

因此本库把 M1 定位为 **ID 卡**：`ATQA=0x0004 / SAK=0x08 / UID` 由硬件应答，读卡器/手机都能识别并读到 UID，
但不参与扇区认证与读取。卡包场景（按 UID 识别身份）不受影响。

### 6.2 已支持 / 未支持

| 项 | 状态 |
|---|---|
| NFC-A 读卡（106 kbit/s，4/7 字节 UID） | ✅ |
| NFC-A 模拟（任意 UID/ATQA/SAK） | ✅ |
| NTAG213/215/216（读写 + NDEF） | ✅ |
| 应用层帧回调 + 自定义应答 | ✅ |
| IRQ light/deep sleep 唤醒 | ✅ |
| NFC-B / NFC-F 模拟 | ⛔ 预留接口，返回 `ESP_ERR_NOT_SUPPORTED` |
| 212/424 kbit/s | ⛔ 仅 106 kbit/s |
| MIFARE Crypto-1 / 扇区读写 | ⛔（见 6.1） |

### 6.3 并发模型

- 一个句柄对应一颗芯片；读卡和模拟不能同时进行（`st25r_picc_begin()` 在 reader 激活时返回 `ESP_ERR_INVALID_STATE`）。
- 内部用递归互斥锁保护 SPI 访问，回调里可以安全调用 `st25r_picc_transmit()`。
- `st25r_picc_update()` 未持锁调用时是安全的；内部任务模式下无需手动调用。

---

## 7. 关键时序与调参

| 项 | 值 | 位置 |
|---|---|---|
| SPI 时钟 | 10 MHz | `st25r_config_t.clock_hz` |
| MRT（发后接收屏蔽） | 0x02（64/fc ≈ 9.4 µs） | `st25r3916_picc.c` |
| fdel（被动目标帧延迟补偿） | 5 | `st25r3916_picc.c` |
| AUX_MOD | 0x70（lm_ext_pol\|lm_ext\|lm_dri） | `st25r3916_picc.c` |
| PT_MOD | 0x0F（最大调制深度） | `st25r3916_picc.c` |
| ANT_TUNE | 0x82 / 0x82 | `st25r3916_picc.c` |
| 读卡超时 | REQA/WUPA 4 ms、防冲突 8 ms、SELECT 8 ms | `st25r3916_pcd.c` |

这些值与本仓库已验证的 v89 固件一致。若换天线/模块，优先调 `ANT_TUNE` 与 `PT_MOD`。

---

## 8. 故障排查

| 现象 | 排查 |
|---|---|
| `st25r_init` 返回 `ESP_ERR_NOT_FOUND` | 检查 SPI 接线、CS/IRQ、3.3 V 供电；读 `IC_IDENTITY` 应为 `0x2A` 左右（type=0x05） |
| 读不到卡 | 确认 `st25r_reader_begin()` 成功；用手机 NFC 靠近看是否有场；检查 `ANT_TUNE` |
| 读卡器识别不到模拟卡 | 确认使用的是 NFC-A 身份；手机/读卡器需支持 106 kbit/s；IRQ 建议接上以降低状态机延迟 |
| 回调里做耗时操作后卡片"消失" | 回调必须短（< 80 µs 量级），日志/串口输出会破坏 FDT |
| 同一张卡反复读不到 | 读卡后调用 `st25r_reader_halt()`，或让场关闭再开（本库轮询用 WUPA，可唤醒 HALT 卡） |

---

## 9. 相关文档

- [API.md](docs/API.md)：逐个接口的参数、返回值、时序与示例
- 工程根目录 `NFC_M1_问题说明.md`：M1 扇区不可读的完整证据链（透明模式、奇偶位、示波器结论）

## 10. 许可

MIT。寄存器定义参考 ST 数据手册与 M5Stack M5Unit-NFC（MIT）实现。
