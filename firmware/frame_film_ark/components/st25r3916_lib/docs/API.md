# st25r3916_lib API 参考

公共头文件：`#include "st25r3916_nfc.h"`

所有接口都是 C 接口，可在 C++ 工程中直接包含（内部有 `extern "C"`）。

---

## 1. 数据类型

### 1.1 `st25r_config_t`

```c
typedef struct {
    spi_host_device_t host;
    gpio_num_t pin_sclk;
    gpio_num_t pin_mosi;
    gpio_num_t pin_miso;
    gpio_num_t pin_cs;
    gpio_num_t pin_irq;
    uint32_t   clock_hz;
} st25r_config_t;
```

| 字段 | 说明 |
|---|---|
| `host` | `SPI2_HOST`（默认）。`SPI3_HOST` 在目标芯片存在 `GPSPI3` 时可用 |
| `pin_sclk/mosi/miso` | SPI 三线，必须是有效 GPIO |
| `pin_cs` | 片选，库手动驱动，初始化时拉高 |
| `pin_irq` | 芯片 IRQ 输出，未接用 `GPIO_NUM_NC` |
| `clock_hz` | 0 表示默认 10 MHz |

初始化宏：

```c
st25r_config_t cfg = ST25R_CONFIG_DEFAULT();  // 全部引脚为 GPIO_NUM_NC，需自行填写
```

### 1.2 `st25r_tech_t`

```c
ST25R_TECH_NFC_A = 0,
ST25R_TECH_NFC_B,      // 预留
ST25R_TECH_NFC_F,      // 预留
```

### 1.3 `st25r_card_t`

```c
typedef struct {
    st25r_tech_t tech;
    uint16_t     atqa;      // 空中顺序，低字节在前（M1: 0x0004）
    uint8_t      sak;
    uint8_t      uid_len;   // 4 或 7
    uint8_t      uid[10];
} st25r_card_t;
```

`st25r_reader_poll()` 填出该结构；`st25r_picc_*` 用它作为输入。

### 1.4 `st25r_card_type_t`

由 SAK 粗略映射（便于卡包分类）：

| 枚举 | 典型 SAK |
|---|---|
| `ST25R_CARD_MIFARE_CLASSIC_1K` | 0x08 / 0x88 |
| `ST25R_CARD_MIFARE_CLASSIC_2K` | 0x18 |
| `ST25R_CARD_MIFARE_CLASSIC_4K` | 0x38 / 0x98 |
| `ST25R_CARD_MIFARE_ULTRALIGHT` | 0x00 |
| `ST25R_CARD_MIFARE_PLUS` | 0x10 / 0x11 |
| `ST25R_CARD_MIFARE_DESFIRE` | 0x20 / 0x28 |
| `ST25R_CARD_ISO14443_4` | 其它带 0x20 位 |

`const char *st25r_card_type_name(st25r_card_type_t)` 返回可打印名称。

---

## 2. 生命周期

### `esp_err_t st25r_init(const st25r_config_t *cfg, st25r_handle_t *out)`

- 配置 CS / IRQ GPIO，初始化 SPI 总线并写入裸寄存器传输所需的时钟/帧格式。
- 执行芯片上电时序：
  1. 等待 50 ms，读 `IC_IDENTITY`（type 必须为 0x05），最多重试 5 次；
  2. `STOP_ALL_ACTIVITIES` → `SET_DEFAULT`；
  3. `TEST_ACCESS` 写 `04 10`（过热保护）；
  4. `IO_CONF1/2`、TX driver、AAT、场检测阈值、PT 调制、天线调谐；
  5. 开振荡器（等 `I_osc` 或 `OSC_OK`）、`ADJUST_REGULATORS`。
- 成功后 `h->ready = true`。

返回：`ESP_OK`、`ESP_ERR_NOT_FOUND`（芯片无响应）、`ESP_ERR_INVALID_ARG`、`ESP_ERR_NOT_SUPPORTED`（SPI host 不支持裸寄存器路径）、`ESP_FAIL`。

### `esp_err_t st25r_deinit(st25r_handle_t h)`

停止模拟/读卡 → 关场 → 释放 SPI 总线 → 删除锁与句柄。句柄不可再用。

### `esp_err_t st25r_chip_info(st25r_handle_t h, uint8_t *ic_type, uint8_t *rev)`

读 `IC_IDENTITY`：`ic_type = (v >> 3) & 0x1F`（ST25R3916 为 0x05），`rev = v & 0x07`。

---

## 3. 读卡（PCD）

### `esp_err_t st25r_reader_begin(st25r_handle_t h)`

- 关闭唤醒模式，配置为 **NFC-A Initiator + 自动防冲突**；
- 设置接收链路（600 kHz 滤波、动态静噪、AGC、增益）、相关器、过冲/下冲保护；
- 打开中断；`NFC_INITIAL_FIELD_ON` 做 RF 冲突避免后开载波。

成功后 `reader_active = true`。再次调用会重新初始化（可用来在异常后恢复）。

### `esp_err_t st25r_reader_end(st25r_handle_t h)`

`STOP_ALL_ACTIVITIES`，清 `tx_en/rx_en`，`reader_active = false`。

### `esp_err_t st25r_reader_poll(st25r_handle_t h, st25r_card_t *card, uint32_t timeout_ms)`

流程（循环直到超时）：

```
WUPA ──▶ ATQA(2B)
   │
   ├─ 若 ATQA 合法：ANTICOLLISION lv1（可选 lv2/lv3 级联）
   │       └─ SELECT lvN ──▶ SAK
   │              ├─ SAK.bit2 = 0：选卡完成
   │              └─ SAK.bit2 = 1：继续下一级
   └─ 失败：重发 WUPA
```

- 使用 **WUPA**（而非 REQA），可唤醒之前被 HALT 的卡。
- 防冲突处理位碰撞（读 `COLLISION` 寄存器，重组 NVB 后重试，最多 32 次）。
- 成功后填充 `card->tech/atqa/sak/uid/uid_len/type`。
- `timeout_ms == 0` 按 50 ms 处理。

返回：`ESP_OK`、`ESP_ERR_NOT_FOUND`（超时无卡）、`ESP_ERR_INVALID_STATE`（未 `reader_begin`）、`ESP_ERR_INVALID_ARG`。

> 选卡成功后卡处于 ACTIVE 状态，不再响应 WUPA 之外的轮询帧；继续读同一张卡请直接
> `st25r_reader_transceive()`；要允许别的卡进入，先 `st25r_reader_halt()` 或关场重开。

### `esp_err_t st25r_reader_transceive(...)`

```c
esp_err_t st25r_reader_transceive(st25r_handle_t h,
                                  const uint8_t *tx, uint16_t tx_len,
                                  uint8_t *rx, uint16_t *rx_len,
                                  uint32_t timeout_ms);
```

- 发送 `tx`（CRC 由芯片追加，`TRANSMIT_WITH_CRC`），等待 `RXE`；
- 接收内容去掉 2 字节 CRC 后写入 `rx`，`*rx_len` 返回实际长度；
- 4 bit 的 ACK/NAK 原样返回（长度 1），不做 CRC 处理；
- `*rx_len` 入参为缓冲区容量；`timeout_ms == 0` 按 20 ms。
- 返回 `ESP_ERR_TIMEOUT` / `ESP_ERR_INVALID_RESPONSE`（CRC/奇偶错误）/ `ESP_FAIL`。

示例（读 NTAG/Ultralight 第 0 页）：

```c
uint8_t cmd[2] = {0x30, 0x00};
uint8_t buf[16];
uint16_t len = sizeof(buf);
if (st25r_reader_transceive(nfc, cmd, 2, buf, &len, 20) == ESP_OK) {
    // buf[0..len) 为页数据
}
```

### `esp_err_t st25r_reader_halt(st25r_handle_t h)`

发送 `50 00`（HLTA，芯片自动加 CRC），只等 `TXE`，不等应答。返回 `ESP_OK` 表示帧已发出。

---

## 4. 卡模拟（PICC）

### 4.1 事件与回调

```c
typedef enum {
    ST25R_PICC_EVT_FIELD_ON,   // 检测到读卡器场
    ST25R_PICC_EVT_FIELD_OFF,  // 场消失
    ST25R_PICC_EVT_SELECTED,   // 读卡器完成了 UID 选择
    ST25R_PICC_EVT_FRAME,      // 应用层帧（已去掉 CRC）
} st25r_picc_event_t;

typedef bool (*st25r_picc_cb_t)(st25r_handle_t h, st25r_picc_event_t evt,
                                const uint8_t *data, uint16_t len, void *ctx);
```

- 由内部任务或 `st25r_picc_update()` 调用，**必须短**（FDT 约 87 µs）。
- `ST25R_PICC_EVT_FRAME` 的返回值：
  - `false`：结束本次会话，回到 IDLE（自动重新响应 UID 选择）——"ID 卡"行为；
  - `true`：保持 ACTIVE，可继续收帧。
- 在回调内可调用 `st25r_picc_transmit()` 应答。

### 4.2 `st25r_picc_config_t`

```c
typedef struct {
    st25r_card_t    card;
    st25r_picc_cb_t callback;
    void           *user_ctx;
    uint32_t        task_stack_size;  // 0 = 4096
    UBaseType_t     task_priority;    // 0 = 不建任务（手动 update）
    BaseType_t      task_core;        // tskNO_AFFINITY 或 0/1
} st25r_picc_config_t;
```

### `esp_err_t st25r_picc_begin(st25r_handle_t h, const st25r_picc_config_t *cfg)`

内部步骤：

1. 开振荡器，`STOP_ALL_ACTIVITIES`；
2. `MODE = 0xC8`（target + ISO14443A + 速率检测）、`NFCIP1_PT = 0x5C`、MRT 100 µs；
3. 写 PT 存储器 A（15 B）：UID(4/7) + 0 填充 + ATQA(LE) + SAK(L1/L2/L3)；
4. 关闭自动应答，配置 ISO14443-A 奇偶、天线调谐、过冲保护；
5. 进入 OFF 状态等待场；
6. 应用已验证的调制参数（`OP_CONTROL=0xC3`、`MODE=0x88`、`ANT_TUNE=0x82`、
   `AUX_MOD=0x70`、`PT_MOD=0x0F`、`IO_CONF2=0x04`、`fdel=5`、`MRT=0x02`、`TX_DRIVER` d_res=0、
   `RES_AM_MOD` fa3_f）；
7. `task_priority != 0` 时创建 `st25r_picc` 任务；否则 `external_step = true`。

返回：`ESP_OK`、`ESP_ERR_NOT_SUPPORTED`（B/F 或 UID 长度非 4/7）、`ESP_ERR_INVALID_STATE`（已在运行或 reader 激活）。

### `esp_err_t st25r_picc_end(st25r_handle_t h)`

停止任务（最多等 1 s）→ 关场 → 状态置 OFF。幂等。

### `bool st25r_picc_is_running(st25r_handle_t h)`

### `esp_err_t st25r_picc_update(st25r_handle_t h)`

驱动状态机走一步。`task_priority == 0` 时必须在自己的循环里高频调用：

```c
while (1) {
    st25r_picc_update(nfc);
    if (!gpio_get_level(IRQ_PIN)) vTaskDelay(1);  // 无中断时让出 CPU
}
```

### `esp_err_t st25r_picc_set_card(st25r_handle_t h, const st25r_card_t *card)`

运行中替换身份：重写 PT 存储器；下一次场开/关后生效。

### `esp_err_t st25r_picc_transmit(st25r_handle_t h, const uint8_t *data, uint16_t len)`

- 仅在 `ACTIVE` 状态有效；
- 清 FIFO → 写 FIFO → `NUM_TX_BYTES` → `TRANSMIT_WITH_CRC`；
- 可从回调内调用（内部锁可重入），也可在回调返回 `true` 后由应用任务调用。

示例：把 0x30 READ 变成一个"假"只读页：

```c
static bool on_event(st25r_handle_t h, st25r_picc_event_t evt,
                     const uint8_t *data, uint16_t len, void *ctx)
{
    if (evt == ST25R_PICC_EVT_FRAME && len >= 2 && data[0] == 0x30) {
        uint8_t page[16] = {0};
        page[0] = 0x04;  // 自定义内容
        st25r_picc_transmit(h, page, sizeof(page));
        return true;     // 保持会话
    }
    return false;
}
```

> 注意：这里的应答是明文 + 芯片 CRC，适用于 Ultralight/NTAG 类明文协议，
> **不能**用于 MIFARE Classic 的 Crypto-1 加密帧（原因见 README 6.1）。

---

## 5. NTAG21x 模拟

NTAG21x 是 NFC-A 标签，UID 固定 7 字节、ATQA `0x0044`、SAK `0x00`。库内置了完整的内存镜像与命令处理，
因此手机可以直接读取 NDEF；`writable = true` 时也可以写 NDEF。

### 5.1 `st25r_ntag_config_t`

```c
typedef struct {
    st25r_ntag_type_t type;    // NTAG213 / NTAG215 / NTAG216
    uint8_t uid[7];            // 7 字节 UID
    uint8_t *memory;           // 可选外部镜像（>= 型号容量），NULL = 句柄内
    bool writable;             // 允许 READER 写页
    st25r_ntag_cb_t callback;  // 页写入 / NDEF 变更通知
    void *user_ctx;
    uint32_t task_stack_size;  // 0 = 4096
    UBaseType_t task_priority; // 0 = 不建任务
    BaseType_t task_core;
} st25r_ntag_config_t;
```

| 型号 | 总页 | 用户页 | 用户容量 | CC byte2 | GET_VERSION size |
|---|---|---|---|---|---|
| NTAG213 | 45 | 4..39 | 144 B | 0x12 | 0x0F |
| NTAG215 | 135 | 4..129 | 504 B | 0x3E | 0x11 |
| NTAG216 | 231 | 4..225 | 888 B | 0x6D | 0x13 |

### 5.2 `esp_err_t st25r_ntag_begin(st25r_handle_t h, const st25r_ntag_config_t *cfg)`

- 生成镜像：UID 页 + BCC0/BCC1 + Capability Container（`E1 10 <size> 00`）+ 空用户区；
  最后一页写入 `AUTH0 = 0xFF`（不启用密码保护）。
- 以 `ATQA=0x0044 / SAK=0x00 / 7 字节 UID` 调用 `st25r_picc_begin()`，内部回调接管应用层命令。
- `callback` 为 `NULL`、`task_priority` 为 0 时仍可工作（后者需手动 `st25r_picc_update()`）。
- 返回：`ESP_OK`、`ESP_ERR_INVALID_ARG`、`ESP_ERR_INVALID_STATE`（已有模拟/读卡在运行）。

### 5.3 支持的命令

| 命令 | 码 | 行为 |
|---|---|---|
| GET_VERSION | 0x60 | 返回 8 字节版本（含容量字节） |
| READ | 0x30 | ACK + 16 字节（4 页） |
| FAST_READ | 0x3A | 起始页..结束页数据（无 ACK）；一次最多 500 字节（FIFO 限制） |
| WRITE | 0xA2 | 写 4 字节，ACK；越界/只读返回 NAK |
| COMPAT_WRITE | 0x3E | 写 1 字节，ACK |
| READ_CNT | 0x39 | 3 字节计数器（固定 0） |
| READ_SIG | 0x3C | 32 字节签名（固定 0，占位） |
| PWD_AUTH | 0x1B | AUTH0=0xFF，返回 NAK |
| HLTA | 0x50 0x00 | 由监听状态机处理，进入 HALT |

ACK/NAK 为 4 bit + 奇校验；READ 的 ACK 与数据之间插入 90 µs FDT。

### 5.4 数据接口

```c
esp_err_t st25r_ntag_set_ndef(st25r_handle_t h, const uint8_t *message, uint16_t len);
esp_err_t st25r_ntag_set_ndef_text(st25r_handle_t h, const char *text);
esp_err_t st25r_ntag_read_memory(st25r_handle_t h, uint8_t page, uint8_t *data, uint8_t pages);
esp_err_t st25r_ntag_write_memory(st25r_handle_t h, uint8_t page, const uint8_t *data, uint8_t pages);
```

- `set_ndef()` 在用户区写入 `03 <len> <message> FE`，长度超过 254 时使用 3 字节 TLV 长度；超出用户容量返回 `ESP_ERR_INVALID_SIZE`。
- `set_ndef_text()` 生成标准 Text 记录：`D1 01 <len> 54 02 'e' 'n' <text>`（UTF-8）。
- `read_memory()/write_memory()` 直接访问镜像；写只允许用户页（4..last_user）。

### 5.5 事件

```c
typedef void (*st25r_ntag_cb_t)(st25r_handle_t h, st25r_ntag_event_t evt,
                                uint8_t page, uint16_t len, void *ctx);
```

| 事件 | 含义 |
|---|---|
| `ST25R_NTAG_EVT_PAGE_WRITTEN` | 读卡器写入一页（`page` 为页号，`len` 为 4 或 1） |
| `ST25R_NTAG_EVT_NDEF_CHANGED` | 应用侧调用了 `set_ndef*()` |

回调在模拟任务里执行（约 87 µs 的 FDT 窗口内），持久化等耗时操作请转交其它任务。

### 5.6 示例

见 [examples/ntag_ndef](../examples/ntag_ndef/main/main.c)：模拟 NTAG215，预置一条 Text NDEF，
并把手机写入的每一页打印出来。

---

## 6. IRQ 唤醒

### `esp_err_t st25r_wakeup_enable(st25r_handle_t h, st25r_wake_mode_t mode)`

| mode | 实现 | 说明 |
|---|---|---|
| `ST25R_WAKE_LIGHT_SLEEP` | `gpio_wakeup_enable(irq, GPIO_INTR_HIGH_LEVEL)` + `esp_sleep_enable_gpio_wakeup()` | 外设保持供电，芯片配置不丢 |
| `ST25R_WAKE_DEEP_SLEEP` | `esp_sleep_enable_ext1_wakeup(1<<irq, ESP_EXT1_WAKEUP_ANY_HIGH)` | 需 RTC GPIO；醒来后重新 `st25r_init()` |

### `esp_err_t st25r_wakeup_disable(st25r_handle_t h, st25r_wake_mode_t mode)`

对应 `gpio_wakeup_disable()` + `esp_sleep_disable_wakeup_source()`。

**唤醒有效的前提**：芯片处于模拟状态（IDLE/READY）且 `I_eon` 未被屏蔽——`st25r_picc_begin()`
之后即是此状态。场出现 → IRQ 拉高 → 唤醒。

推荐流程：

```c
st25r_picc_begin(nfc, &picc);
st25r_wakeup_enable(nfc, ST25R_WAKE_LIGHT_SLEEP);
esp_light_sleep_start();                       // 贴卡唤醒
st25r_wakeup_disable(nfc, ST25R_WAKE_LIGHT_SLEEP);
// 继续跑 emulation task，下一帧周期会完成 READY/ACTIVE
```

若使用内部任务：进入 light sleep 时 FreeRTOS 暂停，任务不会跑；醒来后自动继续，
无需额外处理。deep sleep 会重启，需要重新初始化。

---

## 7. 错误码

| 返回 | 含义 |
|---|---|
| `ESP_OK` | 成功 |
| `ESP_ERR_INVALID_ARG` | 参数为空/引脚非法/缓冲区为 0 |
| `ESP_ERR_INVALID_STATE` | 模式不对（未 `reader_begin` 就 poll、非 ACTIVE 发送、重复 begin） |
| `ESP_ERR_NOT_FOUND` | `st25r_init` 无芯片响应 / `reader_poll` 无卡 |
| `ESP_ERR_NOT_SUPPORTED` | NFC-B/F 模拟、SPI host 不支持、deep sleep EXT1 不可用 |
| `ESP_ERR_TIMEOUT` | 等待芯片 IRQ 超时 |
| `ESP_ERR_INVALID_RESPONSE` | 收到 CRC/奇偶错误帧 |
| `ESP_ERR_INVALID_SIZE` | NDEF 消息超过标签用户容量 |
| `ESP_ERR_NO_MEM` | 句柄/任务创建失败 |
| `ESP_FAIL` | SPI 传输或寄存器访问失败 |

---

## 8. 内部状态机（模拟侧）

```
        ┌──────────── EON ─────────────┐
        ▼                              │
     [ IDLE ] ── PTA(>IDLE) ──▶ [ READY ] ── WU_A ──▶ [ ACTIVE ]
        ▲                          │                    │
        │◀───────── EOF ───────────┴──── EOF ───────────┤
        │                                               │
     [ OFF ] ◀── EOF/无场 ── [ HALT ] ◀── HLTA/错误 ────┘
                               ▲                       │
                               └──── WU_A(唤醒) ◀──────┘
```

- OFF：前端断电，等 `I_eon`；
- IDLE：`GO_TO_SENSE`，硬件自动应答 REQA/WUPA/防冲突；
- READY：读卡器选中 UID（SAK 由硬件给出）；
- ACTIVE：关闭自动应答，应用可收发帧；
- HALT：`GO_TO_SLEEP`，只响应 WUPA 与场变化。

---

## 9. 线程与实时性

| 场景 | 建议 |
|---|---|
| 内部任务 | 默认优先级 5；ACTIVE 时忙等（`vTaskDelay(0)`），空闲时每 tick 检查一次 IRQ |
| 回调 | 不做日志/串口/flash 操作，必要时把数据拷出后在别的任务处理 |
| 与读卡混用 | 同一时刻只能一种模式；`st25r_reader_end()` 后再 `st25r_picc_begin()` |
| 唤醒 | light sleep 前后无需重建句柄；deep sleep 需要完整重新初始化 |

---

## 10. 与 v89 验证固件的对应关系

| 库位置 | 已验证实现 |
|---|---|
| `st25r3916_hal.c`：SPI 裸传输 | `nfc_test.cpp` 的 `fastWrite/fast_spi_transmit` |
| `st25r3916_hal.c`：初始化 | `UnitST25R3916::begin()` |
| `st25r3916_picc.c`：PT/调制参数 | `start_emulation()` + v89 的 RE-APPLY 块 |
| `st25r3916_picc.c`：状态机 | `ListenerST25R3916ForA` |
| `st25r3916_ntag.c`：NTAG 命令层 | 新增（复用 A 状态机，按 NTAG21x 数据手册实现） |
| `st25r3916_pcd.c`：读卡 | `nfca_request_wakeup/nfca_anti_collision/nfcaSelect*` |
