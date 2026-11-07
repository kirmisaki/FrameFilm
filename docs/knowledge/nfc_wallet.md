# FrameFilm Ark NFC 卡包设计方案

> 状态：设计方案（待评审，尚未实现）
> 适用固件：`firmware/frame_film_ark/`（通行证版，单机型）
> 参考库：`assets/lib/st25r3916_lib/`（精简 ST25R3916 库，MIT）
> 参考工程：`H:\nfc_test\hello_world`（SPI 接线与芯片行为的实测来源）
> 关联文档：[ui_layer.md](./ui_layer.md)、[app_layer.md](./app_layer.md)、[app_params.md](./app_params.md)

---

## 0. 结论速览

| 项 | 结论 |
|---|---|
| 功能开关 | `sys_cfg.h` 的 `SYS_NFC_ENABLE` 由 `CONFIG_ARK_HAS_NFC` 派生（仅 n16r8 为 1） |
| 适用硬件 | **NFC 仅 n16r8（4 键）**；n4r2（3 键）没有 NFC，不编译、不注册卡包、不进菜单 |
| 库落地 | 整目录复制到 `firmware/frame_film_ark/components/film_libs/st25r3916_lib/`，原样使用、不在库里改业务 |
| 总线 | **SPI3_HOST**（SPI2 已被 EPD 占用），mode 1，10 MHz |
| 引脚 | SCLK=GPIO10、MOSI=GPIO19、MISO=GPIO9、CS=GPIO20、IRQ=GPIO7（与 nfc_test 完全一致） |
| 按键变体 | n16r8 4 键 + NFC：GPIO18 返回键，双击确认进入读卡页；n4r2 3 键无 NFC，保持双击返回 |
| 供电 | NFC 独立常供电 3.3V，深度休眠期间芯片保持 PICC 模拟状态 |
| M1 能力 | **仅 UID 身份**：复制/模拟 UID、ATQA、SAK；不读不模拟扇区（库的硬边界） |
| NTAG 能力 | 整片页镜像复制 + 模拟（NTAG213/215/216），手机可读 NDEF |
| 卡包入口 | 主菜单新增「卡包」，`APP_ID_WALLET = 7`（尾部追加） |
| 默认模式 | 进入即**卡模拟**，没有模式页签；**双击确认键**进入读卡二级页 |
| 读卡页 | 进入即自动扫描；读到卡后底部显示「复制卡」按钮，按 ENTER 才写入卡包 |
| 休眠唤醒 | 休眠前自动以当前卡进入模拟并武装 IRQ（EXT1）；贴卡唤醒后**跳过开机画面直接进卡包** |
| 存储 | `/sdcard/app/nfc/wallet.bin`（单文件、带 CRC、临时文件原子替换） |
| 远程建卡 | BLE `0x63~0x66`：BEGIN（元数据+CRC）→ DATA×N（≤180 B/包）→ COMMIT；写入设备卡包，不写实体卡 |
| 卡封面 | 每张卡可选 1-bit 封面（FFUI 1bpp，440×200），BLE `0x67~0x69` 上传；封面区不显示 UID |
| 远程管理 | `0x66 WALLET_CTRL`：列表 / 查询 / 选中 / **重命名** / 删除 / **清除封面**；`0x59` 能力位 bit7 宣告 |
| 新增服务层 | 不新增 `service_nfc`：BLE 命令经现有 `SYS_EVT_BLE_APP_CMD` 上浮到 app_task，由卡包模块落盘与回包 |

---

## 1. 目标与边界

### 1.1 目标

1. Ark 固件可选地编译 NFC 能力（不开启时零业务代码、不进菜单、不初始化芯片）。
2. 用现成 `st25r3916_lib` 完成：NFC-A 读卡取身份、按身份模拟卡片、IRQ 唤醒。
3. 新增「卡包」app：
   - 卡复制：进入读卡页自动扫描，读到后按「复制卡」才写入卡包；NTAG21x 整片内容 + M1 卡号；
   - 卡模拟：默认模式，支持多卡切换；
   - 界面沿用 Ark 现有 1-bit / 480×720 竖屏语言（状态栏 + 正文 + 提示行）。
4. 深度休眠支持 NFC 场唤醒，唤醒后直接落到卡包并恢复模拟。
5. 蓝牙端支持**远程创建卡片并写入卡信息**：类型、UID、ATQA/SAK、名称、NTAG 镜像或 NDEF 文本；并支持列表、选中、删除。
6. 每张卡支持**自定义 1-bit 封面**（BLE 上传），上位机支持卡片管理：重命名、删除、选中、上传/清除封面。

### 1.2 明确不做（本期）

| 项 | 原因 |
|---|---|
| M1 扇区读取 / Crypto-1 扇区模拟 | 库的 FIFO 帧引擎无法发 36-bit 自定义奇偶，透明模式在该模块上无负载调制输出；已确认本期只做 UID 身份 |
| NFC-B / NFC-F、212/424 kbit/s | 库未实现 |
| 手机向实体标签写卡 | 模拟按只读交付（`writable=false`）；远程建卡只写设备卡包，不驱动 PCD 写实体 NTAG |
| 远程导出整卡镜像 | 远程只提供元数据列表/查询，不下发完整内存（体积与克隆风险） |
| 本机重命名 / 删除 UI | 三按键交互成本高；重命名/删除/封面由上位机完成，本机只做复制、模拟、切卡 |

> M1 的 UI 与存储都要明确标注「仅卡号」，不能让用户误以为复制了门禁扇区数据。
> 卡封面随 UI 层走 **1-bit**（UI 只渲染 mono）；需要彩色封面时应改用直绘层，本期不做。

---

## 2. 已确认前提

| 项 | 取值 | 影响 |
|---|---|---|
| M1 深度 | 仅 UID/ATQA/SAK | 存储不存扇区；模拟用通用 PICC（回调返回 false 的 ID 卡行为） |
| NFC 供电 | 独立常供电 3.3V | 休眠不需要保留 `PERI_PWR`（GPIO21），芯片可跨 deep sleep 保持模拟 |
| SPI 接线 | 复用 nfc_test：10/19/9/20 + IRQ7 | 19/20 在 Ark 上可用，因为 `CONFIG_ESP_CONSOLE_SECONDARY_NONE=y` 且 USB-JTAG 已关闭 |
| n16r8 按键 | 4 键：上 6 / 下 4 / 确认 5 / **返回 18**（低有效） | NFC 不得占用 GPIO18；4 键固件取消双击确认返回 |

硬件侧必须补的两件事（不写进固件也要确认）：

1. **IRQ（GPIO7）加 10 kΩ 下拉**：芯片未初始化/复位期间避免悬空导致误唤醒。
2. **NFC 3.3V 常供电电流实测**：深度休眠期间芯片处于 PICC 监听，电流会计入整机静态功耗。

---

## 3. 架构与目录

### 3.1 分层

```
┌─────────────────────────────────────────────────────────────┐
│ film_app                                                    │
│   apps/wallet/app_wallet.c        卡包 UI 页面（LVGL，ui_task）│
│   apps/wallet/app_wallet_store.c  卡库文件读写（app_task）     │
│   core/app_manager.c              休眠前 standby / 离开时停模拟│
│   core/app_init.c                 NFC 唤醒路由                 │
├─────────────────────────────────────────────────────────────┤
│ film_hal                                                    │
│   hal_nfc.c/.h                    库封装 + 读卡任务 + 模拟任务  │
│   hal_pwr.c/.h                    唤醒原因 + IRQ 入睡门闸      │
├─────────────────────────────────────────────────────────────┤
│ film_libs/st25r3916_lib（第三方，原样）                       │
│   st25r3916_nfc.h / st25r3916_*.c                            │
└─────────────────────────────────────────────────────────────┘
```

- **不新增 `film_service/service_nfc.c`**：远程建卡命令由 `service_ble.c` 原样上浮为 `SYS_EVT_BLE_APP_CMD`，`app_manager` 按通道号转给卡包模块（与 0x45~0x4A 参数通道路由同构），落盘/回包都在 app_task。跨层需求只有「休眠前进入模拟 + 武装 IRQ」，由 `app_manager` 调用卡库、`service_monitor` 调用 `hal_nfc` 完成。后续若出现与 app 无关的 NFC 策略（如自动寻卡后台上报），再评估 service 层。
- app 层直接包含 `hal_nfc.h` 与现有 `hal_epd.h`/`hal_input.h` 的用法一致，不违反项目现状。

### 3.2 库放哪里

**推荐**：`firmware/frame_film_ark/components/film_libs/st25r3916_lib/`（整目录原样复制）。

理由：

- 第三方库集中放在 `components/film_libs/`，顶层 `CMakeLists.txt` 把它注册进 `EXTRA_COMPONENT_DIRS`；目录名即组件名，与其 `CMakeLists.txt` 一致；
- `film_hal` 只需在 `REQUIRES` 里加 `st25r3916_lib`，`#include "st25r3916_nfc.h"` 即可；
- 注意链接顺序：`EXTRA_COMPONENT_DIRS` 里的组件会排在项目组件之前，普通静态链接可能先于 `film_hal` 被处理而丢符号，因此该库在 `CONFIG_ARK_HAS_NFC` 下用 `WHOLE_ARCHIVE` 链接；
- `assets/lib/st25r3916_lib` 保留为上游参考，升级时整目录覆盖，不把业务代码写进库。

不建议：

- 放进 `film_hal` 子目录：IDF 不递归扫描组件，需要额外 `EXTRA_COMPONENT_DIRS`；
- 放进 `managed_components`：它不是 IDF 托管依赖，会被 `idf.py reconfigure` 的依赖清单覆盖；
- 直接从 `assets/lib` 引用：固件工程会依赖仓库外的相对路径，单独拷贝/CI 构建容易断。

**关于「开关关闭时完全不编译」**：

- 我方源码（`hal_nfc.c`、`app_wallet*.c`）用 CMake 读取 `sys_cfg.h` 条件加入 `srcs`，关闭时这些文件**不参与编译**（见 §4.4）。
- 第三方库仍在 `components/film_libs/` 下，IDF 会编译其 4 个 `.c`，但 `film_hal` 不 REQUIRE 它、也没有任何符号引用，静态库中未引用的目标文件**不会链接进镜像**，Flash 占用为 0。
- 若要求「连第三方库都不要编译」，把目录移到 `firmware/frame_film_ark/ext_components/st25r3916_lib/`，在顶层 `CMakeLists.txt` 按同一宏条件 `list(APPEND EXTRA_COMPONENT_DIRS ...)` 即可（本方案默认不做，保持目录惯例）。

---

## 4. 编译开关与硬件配置

### 4.1 `sys_cfg.h`：功能开关（由变体派生）

NFC 只存在于 n16r8。用 Kconfig 的 `ARK_HAS_NFC` 派生，避免在共享头里手改：

```c
#include "sdkconfig.h"

#if CONFIG_ARK_HAS_NFC
#define SYS_NFC_ENABLE                 1
#else
#define SYS_NFC_ENABLE                 0
#endif
```

- `CONFIG_ARK_HAS_NFC` 在 `film_hal/Kconfig` 定义，默认 n；`sdkconfig_n16r8` 置 y，`sdkconfig`（n4r2）不置。
- n4r2 上 `SYS_NFC_ENABLE=0`：`hal_nfc.c` / `app_wallet*.c` 不编译、卡包不注册、菜单不出现。
- 其它代码只判断 `SYS_NFC_ENABLE`，不再单独判断变体。

### 4.2 `hal_nfc.h`：接线与模块参数

与 `hal_epd.h` 把屏幕引脚写在 HAL 头里同一套做法：

```c
#include "driver/gpio.h"
#include "driver/spi_master.h"

// 与 nfc_test / M5 Unit NFC 实测接线一致
#define HAL_NFC_SPI_HOST                SPI3_HOST   // SPI2 已被 EPD 占用
#define HAL_NFC_PIN_SCLK                GPIO_NUM_10
#define HAL_NFC_PIN_MOSI                GPIO_NUM_19
#define HAL_NFC_PIN_MISO                GPIO_NUM_9
#define HAL_NFC_PIN_CS                  GPIO_NUM_20
#define HAL_NFC_PIN_IRQ                 GPIO_NUM_7   // RTC GPIO，EXT1 唤醒
#define HAL_NFC_SPI_CLOCK_HZ            10000000

#define HAL_NFC_WAKE_ENABLE             1           // 休眠期间保持模拟 + IRQ 唤醒
#define HAL_NFC_PICC_TASK_PRIO          6           // 高于 app_task(5)，低于 monitor(7)
#define HAL_NFC_PICC_TASK_CORE          1           // 避开 core0 的 WiFi/BLE
#define HAL_NFC_PICC_TASK_STACK         4096

#define HAL_NFC_MAX_CARDS               16
#define HAL_NFC_MAX_CARD_BYTES          4096        // M1 4K 预留；NTAG216 仅 924
#define HAL_NFC_SCAN_TIMEOUT_MS         30000       // 读卡模式总超时
```

- `film_hal` 本来就可以包含 `driver/*` 头（`hal_epd.h` 已如此），因此引脚直接用 `GPIO_NUM_x` / `SPI3_HOST`，不再做数值中转。
- 任务参数、最大卡数、扫描超时也归入 `hal_nfc.h`：它们描述“这颗 NFC 模块怎么跑”，不是系统级配置。
- 上层（`app_wallet`、`service_monitor`、`app_init`）通过 `#include "hal_nfc.h"` 使用 `HAL_NFC_WAKE_ENABLE` 等常量。

### 4.3 引脚占用核对

| GPIO | Ark 现有用途 | NFC |
|---|---|---|
| 7 | 空闲 | IRQ（RTC GPIO 0~21，可 EXT1） |
| 9 | 空闲 | MISO |
| 10 | 空闲 | SCLK |
| 19 / 20 | USB D-/D+，当前 USB-JTAG 关闭、无 USB 设备 | MOSI / CS |
| 4/5/6 | 三按键 + 唤醒 | 不冲突 |
| 11~14、47、48 | EPD | 不冲突 |
| 1、8、17、21 | 电池/LED/外设供电 | 不冲突 |
| 2、38~42、45 | SD | 不冲突 |
| 35~37 | n16r8 Octal PSRAM | 不冲突 |

> 约束：不要打开 `CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG`、不要启用 TinyUSB；否则 19/20 会被 USB 占用。

### 4.4 CMake 条件编译

`film_hal/CMakeLists.txt`：

```cmake
if(CONFIG_ARK_HAS_NFC)
    list(APPEND srcs "src/hal_nfc.c")
    list(APPEND requires st25r3916_lib)
endif()
```

`film_app/CMakeLists.txt` 用同一个条件：

```cmake
if(CONFIG_ARK_HAS_NFC)
    list(APPEND srcs "apps/wallet/app_wallet.c" "apps/wallet/app_wallet_store.c")
    list(APPEND private_include "apps/wallet")
endif()
```

同时 `.c` 内部仍保留 `#if SYS_NFC_ENABLE` 守卫（防止有人绕过 CMake 单独编译，也让头文件可以被安全包含）。

---

## 5. HAL 层设计（`hal_nfc`）

### 5.1 设计原则

- **厂商类型不外泄**：`hal_nfc.h` 不出现 `st25r_handle_t` / `st25r_card_t`，上层只看到中性的卡身份结构。
- **一次只做一件事**：读卡与模拟互斥（库的硬约束），HAL 用显式状态机阻止并发。
- **阻塞接口 + 独立任务**：读卡扫描由卡包模块创建一次性任务调用；模拟由库内部任务驱动。

### 5.2 对外接口

```c
typedef enum {
    HAL_NFC_KIND_M1_UID = 0,   // MIFARE Classic：仅身份
    HAL_NFC_KIND_NTAG213 = 1,
    HAL_NFC_KIND_NTAG215 = 2,
    HAL_NFC_KIND_NTAG216 = 3,
    HAL_NFC_KIND_M1_1K  = 4,   // 完整块镜像（预留）
    HAL_NFC_KIND_M1_2K  = 5,
    HAL_NFC_KIND_M1_4K  = 6,
} hal_nfc_kind_t;

typedef enum {
    HAL_NFC_DATA_NONE       = 0,   // M1 UID 模式
    HAL_NFC_DATA_NTAG_PAGES = 1,   // 页 0..last
    HAL_NFC_DATA_M1_BLOCKS  = 2,   // block 0..last（预留，完整 M1 模拟）
} hal_nfc_data_format_t;

typedef struct {
    hal_nfc_kind_t kind;
    hal_nfc_data_format_t data_format;
    uint8_t  atqa[2];     // 空中顺序（小端）
    uint8_t  sak;
    uint8_t  uid_len;     // 4 / 7
    uint8_t  uid[10];
    const uint8_t *mem;   // NTAG 页镜像 / M1 块镜像（可为 NULL）
    uint16_t mem_len;     // ≤ HAL_NFC_MAX_CARD_BYTES
} hal_nfc_card_t;

/* 生命周期（幂等；失败只告警，不阻塞启动） */
esp_err_t hal_nfc_init(void);
void      hal_nfc_deinit(void);
bool      hal_nfc_ready(void);
esp_err_t hal_nfc_chip_info(uint8_t *ic_type, uint8_t *rev);

/* 读卡（PCD）：调用前先 reader_begin；poll 成功后可 transceive 读页 */
esp_err_t hal_nfc_reader_begin(void);
esp_err_t hal_nfc_reader_end(void);
esp_err_t hal_nfc_reader_poll(hal_nfc_card_t *card, uint32_t timeout_ms);
esp_err_t hal_nfc_reader_transceive(const uint8_t *tx, uint16_t tx_len,
                                    uint8_t *rx, uint16_t *rx_len, uint32_t timeout_ms);
esp_err_t hal_nfc_reader_halt(void);

/* 模拟（PICC） */
esp_err_t hal_nfc_emu_start(const hal_nfc_card_t *card);  // 内部区分 M1 / NTAG
esp_err_t hal_nfc_emu_stop(void);
esp_err_t hal_nfc_emu_set_card(const hal_nfc_card_t *card);
bool      hal_nfc_emu_running(void);

/* 休眠 */
bool      hal_nfc_irq_active(void);
esp_err_t hal_nfc_sleep_arm(void);    // st25r_wakeup_enable(DEEP_SLEEP)
void      hal_nfc_sleep_disarm(void);
```

### 5.3 实现要点

| 项 | 取值/做法 |
|---|---|
| 初始化时机 | `film_hal_init()` 末尾调用 `hal_nfc_init()`（SD 挂载之后、EPD 之后均可；SPI3 独立） |
| SPI 配置 | `st25r_config_t`：host=SPI3、mode 1、10 MHz；库内部 `spi_bus_initialize`，**不要**与 EPD 共用 SPI2 |
| 失败策略 | `st25r_init` 返回 `ESP_ERR_NOT_FOUND`（芯片无响应）时只打日志，`hal_nfc_ready()=false`；卡包 app 用 `enter_block_reason` 提示「NFC 硬件未就绪」 |
| 模拟任务 | `task_priority=HAL_NFC_PICC_TASK_PRIO`、`task_core=HAL_NFC_PICC_TASK_CORE`、stack=4096 |
| M1 模拟 | `st25r_picc_begin()`，`callback=NULL` → 库在收到应用层帧时回到 IDLE，即「只回 UID 的 ID 卡」 |
| NTAG 模拟 | `st25r_ntag_begin(type, uid, NULL, writable=false)`，再用 `st25r_ntag_write_memory(4..last)` 恢复用户页；页 0~3 由库按 type/uid 重建，CC 也由库按型号写入 |
| M1 完整模拟（预留） | `HAL_NFC_DATA_M1_BLOCKS` 已能存/传 1K/2K/4K 原始块镜像；Crypto-1 移植前，`hal_nfc_emu_start()` 按 `M1_UID` 降级为 ID 卡，不读 `mem` |
| 切卡 | `emu_stop()` + `emu_start()`（NTAG 换型号/换镜像不能靠 `set_card`）；切卡后读卡器需重新贴卡 |
| 读卡扫描 | 由卡包模块建任务调用；HAL 只提供阻塞原语。本机只产出 `data_format=0/1`，格式 2 只能由 BLE 导入 |
| 日志 | 沿用 `sys_logi/...`，TAG 用 `HAL_NFC` |

> `st25r_picc_end()` 内部最多等 1 s 删任务；模式切换（模拟→读卡）会有一次可感知的停顿，UI 在切换前先显示状态，避免用户以为按键没响应。

### 5.4 唤醒原因（`hal_pwr`）

新增：

```c
typedef enum {
    HAL_PWR_WAKE_NONE = 0,
    HAL_PWR_WAKE_KEY,     // EXT0：GPIO5 按键
    HAL_PWR_WAKE_TIMER,   // 定时器
    HAL_PWR_WAKE_NFC,     // EXT1：GPIO7 IRQ
    HAL_PWR_WAKE_OTHER,
} hal_pwr_wake_cause_t;

hal_pwr_wake_cause_t hal_pwr_get_wakeup_cause(void);
```

- `ESP_SLEEP_WAKEUP_EXT1` 时用 `esp_sleep_get_ext1_wakeup_status()` 判断 GPIO7 位，避免以后新增 EXT1 源时误判。
- `hal_pwr_wake_condition_met()` 扩展为 `GPIO5 低有效 || hal_nfc_irq_active()`：贴卡场还在时不要入睡，等卡片离开再断电。
- IDF 5.5 的 `esp_sleep_enable_ext0_wakeup()` 与 `esp_sleep_enable_ext1_wakeup_io()` 配置互不覆盖（已核对 `sleep_modes.c`），可以同时用：按键走 EXT0，NFC 走 EXT1。

---

## 6. 卡片数据与存储

### 6.1 文件

| 文件 | 内容 | 体积 |
|---|---|---|
| `/sdcard/app/nfc/wallet.bin` | 索引 + 每张卡的元数据 / 卡数据镜像 | ≤ 16 × (60 + 4096) ≈ 66.5 KB（按 M1 4K 预留） |
| `/sdcard/app/nfc/cover_<id>.bin` | 该卡的 1-bit 封面（FFUI 容器） | 16 + 11,000 = 11,016 B / 张 |

- 单条记录按 **4096 B 卡数据**预留（M1 4K 的完整块镜像），当前 NTAG 最大 924 B、M1 UID 模式为 0 B。
- 封面**不塞进 wallet.bin**：单张 11 KB，若内联会让每次切卡/改名都重写十几万字节，且掉电窗口变大。
- 封面按需加载：进入卡包或切卡时只读当前卡那一张，1bpp 展开成 LVGL L8 后约 **88 KB**（440×200），只保留当前一张。
- **不放进 NVS**：`app_entry_t.state` 的落盘 key 是 `app<id>`，卡包 id=7 会与保留槽位 `app7`（开机行为）冲突。卡包因此**不声明 state**，当前卡下标存在 wallet.bin 头部。

### 6.2 wallet.bin 格式（小端）

```text
Header 32 B
0   magic     'N','F','W','1'
4   version   1
5   count     0..16
6   current   0..count-1；0xFF = 无当前卡
7   next_id   下一个可用的卡记录 id（1..255，跳过已占用）
8   crc32     u32，覆盖 [32, EOF)
12  reserved  20 B

Record（60 B 头 + 数据）
0   id          u8   （**稳定记录 id**，BLE 管理用；删除不重排，UI 另显示列表位置）
1   kind        u8   hal_nfc_kind_t
2   uid_len     u8
3   sak         u8
4   atqa[2]     u8[2]
6   uid[10]     u8[10]
16  name_len    u8
17  name[23]    UTF-8，无 '\0'
40  data_format u8   0=none / 1=NTAG 页镜像 / 2=M1 原始块镜像
41  data_ver    u8   格式版本，当前 1
42  flags       u8   bit0 = has_cover
43  reserved    u8
44  mem_len     u16  卡数据字节数（见 §6.2.1）
46  cover_len   u16  （0 = 无封面；有封面时 11016）
48  cover_crc32 u32  （对封面 payload 11000 字节做 CRC32）
52  reserved    8 B  预留给未来格式扩展
60  mem[mem_len]     卡数据（NTAG 页镜像 / M1 块镜像）
```

### 6.2.1 卡数据格式（为完整 M1 模拟预留）

`kind` 与 `data_format` 组合决定 `mem` 的语义。**本期只实现格式 0 / 1；格式 2 先打通存储与 BLE，模拟在 Crypto-1 移植后启用。**

| kind | 值 | data_format | mem_len | 语义 |
|---|---|---|---|---|
| `M1_UID` | 0 | 0 | 0 | 仅 UID/ATQA/SAK，ID 卡模拟 |
| `NTAG213` | 1 | 1 | 180 | 页 0..44 的 1bpp 原始镜像 |
| `NTAG215` | 2 | 1 | 540 | 页 0..134 |
| `NTAG216` | 3 | 1 | 924 | 页 0..230 |
| `M1_1K` | 4 | 2 | 1024 | 64 个 block × 16 B（预留） |
| `M1_2K` | 5 | 2 | 2048 | 128 个 block × 16 B（预留） |
| `M1_4K` | 6 | 2 | 4096 | 256 个 block × 16 B（预留） |

格式 2 的约定：

- `mem` 按 **block 0..last 的原始顺序**存放，每块 16 B；包含 block 0（UID/BCC/SAK/ATQA）与各扇区 trailer（Key A/access bits/Key B）。
- 1K = 16 扇区 × 4 块；2K = 32 扇区 × 4 块；4K = 扇区 0..31 为 4 块、32..39 为 16 块，总计 256 块。存储不区分扇区结构，按 block 线性排列即可。
- 不额外存 Key 表：Key A/B 已包含在 trailer block 中；未来模拟器加载时自行提取/校验。
- 格式 2 的 `mem_len` 必须精确等于上表；`data_ver` 当前固定 1。
- **当前固件的降级行为**：可以接收、保存格式 2（BLE 远程建卡），但模拟时只使用 `uid/atqa/sak` 走 ID 卡路径；完整扇区模拟要等 Crypto-1 加密帧发送能力落地。这样未来只需替换模拟实现，**不需要改 wallet.bin 或 BLE 格式**。
- 本机读卡仍只产出格式 0 / 1；格式 2 只能由上位机导入。

### 6.3 封面文件格式（FFUI 1bpp）

与 `tools/ui-assets/gen_ui_assets.py` 的 FFUI 容器一致：

```text
0   magic    'F','F','U','I'
4   version  1
5   format   1（1bpp，1=黑 0=白，MSB 在左）
6   width    u16 LE = 440
8   height   u16 LE = 200
10  reserve  6 B
16  payload  11,000 B = 440 × 200 / 8
```

- 只接受 440×200、1bpp；其它尺寸/格式直接拒绝（固件不缩放）。
- 载荷 CRC 与 record 的 `cover_crc32` 一致；文件缺失或 CRC 不符时按「无封面」处理并清除 flags（下次保存时落盘）。
- 封面区**只画图**，不叠加 UID；名称/类型放在封面下方的信息条里（见 §10）。

### 6.4 写入与管理策略

**wallet.bin**（与之前一致）：

1. 写 `wallet.tmp` → `fflush`/`fsync` → `fclose`；
2. `wallet.bin` → `wallet.bak`（失败可忽略）；
3. `wallet.tmp` → `wallet.bin`；
4. 任一步失败：保留旧文件并返回错误，UI 显示「保存失败」。

**封面**：

1. 写 `cover_<id>.tmp` → 校验长度/CRC → close；
2. `cover_<id>.tmp` → `cover_<id>.bin`；
3. 再更新 wallet.bin 的 `has_cover/cover_len/cover_crc32`；
4. 封面写成功但 wallet.bin 更新失败：封面成为孤儿文件，下次加载按 CRC/长度重新校验，不影响卡本身。

**管理语义**：

| 操作 | 行为 |
|---|---|
| 重命名 | 名称 1..23 字节 UTF-8，不含 `\0`；只改 record + 落盘，不碰封面/镜像 |
| 删除 | 删除 record + `cover_<id>.bin`；若删的是当前卡，自动选下一张；没有剩余卡则停止模拟（`current=0xFF`）。`id` 删除后不重排，新卡取 `next_id` |
| 选中 | 更新 `current`；卡包为当前 app 时重启模拟并重新加载封面 |
| 上传/替换封面 | 覆盖 `cover_<id>.bin`，更新 CRC/flags；当前卡时立即刷新页面 |
| 清除封面 | 删除文件 + flags 清零；页面回退到默认卡片板 |

CRC 用 `esp_rom_crc32_le`；加载时 magic/version/size/crc 任一不符即视为空卡包（不阻塞启动）。

### 6.5 去重与命名

- 去重键：`kind + uid`。重复读同一张卡时更新数据并选中它，不新增条目；封面保留。
- 默认名：
  - M1：`M1-XXXX`（UID 末 2 字节十六进制）
  - NTAG：`NTAG213-XXXX` / `NTAG215-XXXX` / `NTAG216-XXXX`
- 显示类型：`M1 / UID`、`M1 1K / BLOCKS`、`M1 2K / BLOCKS`、`M1 4K / BLOCKS`、`NTAG213 / NDEF` 等；M1 UID 模式必须在结果页显示「仅卡号，扇区不可读」；格式 2 的卡在 Crypto-1 落地前标注「完整模拟待支持」。

---

## 7. 读卡复制流程

### 7.1 进入读卡页

1. 模拟页 **双击确认键** → `APP_UI_REQ_WALLET_CMD(WALLET_CMD_READ)`；
2. app_task：`hal_nfc_emu_stop()`（模拟与读卡互斥）→ `hal_nfc_reader_begin()` → 创建扫描任务，**进入即自动扫描**；
3. 扫描任务（prio 4 / core 1 / stack 4096）循环 `hal_nfc_reader_poll(&card, 250)`，直到：
   - 读到卡；
   - 超时 `HAL_NFC_SCAN_TIMEOUT_MS`；
   - 用户取消（原子 flag）。
4. 读卡期间页面显示「正在读卡…」，单击确认键取消并回到模拟页。

### 7.2 类型判定

| 条件 | 处理 |
|---|---|
| SAK bit2=1（0x20 位，ISO14443-4 / DESFire 等） | 不支持，提示「该卡类型暂不支持」 |
| SAK ∈ {0x08, 0x18, 0x38, 0x88, 0x98}（MIFARE Classic） | 本机读卡仅产出 `M1_UID`（UID/ATQA/SAK，`data_format=0`）；完整块镜像只能从上位机导入（格式 2，预留） |
| SAK=0x00 且 `GET_VERSION(0x60)` 返回 8 字节，version[2]=0x04 | NTAG21x：按 version[6] 取型号 `0x0F/0x11/0x13` |
| SAK=0x00 且 `GET_VERSION` 被 NAK | Ultralight/其它 Type 2：本期提示不支持（可后续按 UID 卡支持） |

### 7.3 NTAG 镜像读取

1. `READ` 命令 `{0x30, page}`，一次 4 页（16 B），从 page 0 到 last；
2. 页数：NTAG213=45、NTAG215=135、NTAG216=231；
3. 校验：
   - page0：`BCC0 = 0x88 ^ UID0 ^ UID1 ^ UID2 ^ UID3`；
   - page2：`BCC1 = UID3 ^ UID4 ^ UID5 ^ UID6`；
   - page3 的 CC 首字节 `0xE1`；
4. 成功后 `hal_nfc_reader_halt()` + `hal_nfc_reader_end()`；
5. 结果放入 PSRAM，通过 `_Atomic(wallet_scan_card_t *)` 交给 app_task（与 `app_pass.c` 的 pending 指针同一模式），并发布 `SYS_EVT_NFC_SCAN_DONE`（payload 1 字节状态）。

> 事件负载只有状态码，镜像不塞进 `sys_event`（上限 192 B）；所有权通过原子指针转移，失败/取消时由扫描任务或 `app_wallet_deactivate()` 释放。

### 7.4 结果落地（复制卡按钮）

- 扫描成功后**先不落盘**：结果保存在 app_task 的 pending 缓冲里，页面显示类型/UID/容量/内容；
- 页面底部出现 **「复制卡」** 按钮（黑底白字动作条，标注 `ENTER`）；
- 按确认键 → `APP_UI_REQ_WALLET_CMD(WALLET_CMD_ACTION)`：
  1. 按 `kind + uid` 去重，写入 wallet.bin；
  2. 设为当前卡、加载封面、`hal_nfc_reader_end()`；
  3. 回到模拟页并立即以新卡开始模拟；
- 读卡失败/超时：页面显示原因与「单击重试」；单击重新扫描；
- 在读卡页按返回/取消：丢弃 pending 结果，回到模拟页，不改卡包。

> 「读到卡」与「加入卡包」是两个独立动作。这样误贴一张卡不会直接把卡包改掉，也符合「复制卡按钮」的确认语义。

---

## 8. 卡模拟与切卡

### 8.1 进入卡包即模拟

- 卡包是 UI 层 app，框架不调用 `on_enter`；用 `enter_block_reason()` 作为 app_task 侧的进入钩子：
  1. 加载 `wallet.bin`（失败 → 空卡包，仍可进入）；
  2. `hal_nfc_ready()` 为假 → 返回「NFC 硬件未就绪」，阻止切页；
  3. 有当前卡 → `hal_nfc_emu_start(current)`，并加载该卡封面；
  4. 无当前卡 → 停在空状态，不启动模拟。
- 默认模式固定为 `EMULATE`；不持久化「上次在 COPY」。

### 8.2 切卡

- EMULATE 下 UP/DOWN 直接切到相邻卡（首尾环绕），一次按键 = 一次全屏刷新（约 410 ms），可以接受；
- 切卡动作：`hal_nfc_emu_stop()` → `hal_nfc_emu_start(new)` → 更新 `wallet.bin` 的 `current` → 加载新卡封面；
- 状态行提示「切卡完成，请重新贴卡」——库在字段关闭/重开后才应用新身份。

### 8.3 封面加载与跨任务移交

- 封面在 **app_task** 读取并解码：`cover_<id>.bin` → 校验 FFUI/CRC → 1bpp 展开为 L8（440×200，约 88 KB PSRAM）。
- 通过 `_Atomic(wallet_cover_t *)` 把所有权交给 ui_task（与 `app_pass.c` 的 pending 指针同一模式）；页面 `lv_image_set_src()` 后释放旧图。
- 无封面 / 文件损坏：传 `NULL` 标记，页面回退默认卡片板（不显示 UID）。
- 同一时刻只保留当前卡一张封面；切卡/离开卡包时释放，避免 PSRAM 碎片。

### 8.4 离开卡包

UI 层 app 没有 `on_exit`，因此由 `app_manager` 在两个出口显式调用 `app_wallet_deactivate()`：

- `app_do_switch_target()` 停旧 app 之前（当前 app == 卡包）；
- `app_manager_sleep_from_app()` 停 app 之前。

`app_wallet_deactivate()`：

1. 取消扫描任务并释放 pending 镜像；
2. `hal_nfc_emu_stop()`；
3. 保留卡库缓存（下次进入免 SD 读）。

> 例外：入睡路径不停模拟。`APP_SLEEP_STOP_CMD` 处理器在停 app 前调用 `app_wallet_sleep_prepare()`，让芯片以当前卡继续模拟（见 §9）。

---

## 9. 休眠与唤醒

### 9.1 入睡时序（手动长按 / 自动休眠共用）

```text
app_task                  monitor_task                    hal
   │  APP_SLEEP_STOP_CMD        │
   ├─ app_wallet_sleep_prepare()│
   │    · 有当前卡 → emu_start  │
   │    · 无卡 → emu_stop       │
   ├─ ui_core_stop_for_sleep()  │
   └─ stopped = true ──────────►│
                                ├─ hal_pwr_wakeup_prepare()   （EXT0 按键）
                                ├─ hal_nfc_sleep_arm()       （EXT1 GPIO7）
                                ├─ hal_nfc_irq_active()?
                                │    是 → pending，200ms 后重试
                                ├─ 停心跳 / deinit LED 电池 SD EPD 输入
                                ├─ 不 deinit NFC
                                └─ hal_pwr_enter_sleep()
```

关键点：

- NFC 是独立常供电，`hal_pwr_enter_sleep()` **不需要**改 `PERI_PWR`（GPIO21 仍按原逻辑断电）；
- 只有 `hal_nfc_sleep_arm()` 成功才保留 NFC；武装失败则 `hal_nfc_deinit()` 后照常睡（退化为纯按键/定时唤醒）；
- `hal_nfc_irq_active()` 为真表示当前有读卡器场，延迟入睡（沿用 monitor 的 pending 重试机制）；
- 深度休眠会停掉 MCU，但芯片已由 `st25r_picc_begin/ntag_begin` 配好，硬件自动应答 UID/ATQA/SAK，因此「贴卡即唤醒」。

### 9.2 唤醒路由

`app_init.c` 在解析 BOOT PAGE / START APP **之前**：

```c
#if (SYS_NFC_ENABLE == 1) && (HAL_NFC_WAKE_ENABLE == 1)
if (hal_pwr_get_wakeup_cause() == HAL_PWR_WAKE_NFC &&
    app_manager_is_registered(APP_ID_WALLET))
{
    app_manager_switch(APP_ID_WALLET);   // 跳过开机画面
    return;
}
#endif
```

- 卡包 `enter_block_reason()` 在 app_task 里加载卡库并**先启动模拟**，再让 ui_task 去建页面；因此 NFC 内容恢复早于 EPD 首帧（约 3 s 清场）；
- 若卡包未注册（UI 层不可用 / 模式裁剪 / NFC 关闭），回落到正常启动路径；
- `service_monitor_init()` 把 EXT1 归入「手动唤醒」，沿用 60 s 自动休眠计时，无需改动。

### 9.3 需要向用户明示的行为

- 深度休眠期间芯片只能硬件应答身份；**NDEF/扇区数据要等 MCU 醒来并恢复模拟后才可读**。手机若在 1~2 s 内读完整个标签，可能提示失败 —— 重贴一次即可。
- 唤醒后若场仍存在，`hal_nfc_init()` 会重置芯片；等卡包重新 `emu_start` 后内容才完整。

### 9.4 功耗

| 场景 | 预估 | 备注 |
|---|---|---|
| 深度休眠 + NFC 模拟监听 | 待实测 | 独立 3.3V 常供电，必须计入整机静态功耗 |
| 深度休眠 + 未武装 NFC | 与现状一致 | `hal_nfc_deinit()` 后芯片停止模拟 |

若实测不可接受，可退化为「仅按键/定时唤醒」，或硬件改为可控供电（此时 NFC 唤醒不可用）。

---

## 10. 卡包 App 与 UI 设计

> 交互原型：`tools/ui-mockup/wallet.html`（零依赖，双击可开；含模拟 / 读卡 / 空状态 / NFC 未就绪）。

### 10.1 视觉规范（沿用现有 Ark UI）

| 项 | 取值 |
|---|---|
| 逻辑画布 | 480 × 720（竖屏），`UI_LOGICAL_W/H` |
| 外壳 | `app_shell_build()`：状态栏 30 px、提示行 28 px、正文上下留白 18 px |
| 正文区 | x = 20~460（440 宽），y = 48~674 |
| 颜色 | 纯黑 / 纯白；选中态用「黑底白字」反白，与主菜单选中卡片一致 |
| 字体 | 中文 `ui_font_14/18/24`，英文 `lv_font_unscii_8/16`；卡名 18 px，类型 10 px mono |
| 动效 | 禁止动画/滚动；每次按键一整帧（约 410 ms） |
| 图标 | 80 × 80 1-bit 线稿，新增 `UI_ICON_WALLET` |
| 卡封面 | 440 × 200 的 1-bit 图（FFUI 1bpp），下方 48 px 信息条；**封面区不显示 UID** |
| 默认封面 | 无封面时用几何默认图 + 信息条；同样不显示 UID |

### 10.2 页面 A：模拟模式（默认）

```text
┌──────────────────────────────────────────┐ 0
│ ● 82%      WIFI  BT      12:30           │ 状态栏（app_shell）
├──────────────────────────────────────────┤ 30
│ NFC WALLET                      01 / 05  │ 48
│ ──────────────────────────────────────── │ 78
│ ┌──────────────────────────────────────┐ │ 96
│ │                                      │ │
│ │            封面  440 × 200            │ │ 200 高
│ │                                      │ │
│ ├──────────────────────────────────────┤ │ 296
│ │ 门禁卡                        NTAG215 │ │ 48 高
│ └──────────────────────────────────────┘ │ 344
│ 双击确认键 · 进入读卡          4 张卡片   │ 360
│                                          │
│ 卡片 01 / 05                  上下切卡    │ 418
│ █ 01  门禁卡                    NTAG215  │ 442
│   02  公司卡                    M1-UID   │ 480
│   03  家门                      NTAG213  │ 518
│   …                                      │
│ 模拟中 · 等待读卡器 · 可贴卡唤醒          │ 646
├──────────────────────────────────────────┤ 692
│ 上下切卡   双击读卡   BACK 退出   长按休眠 │ 提示行
└──────────────────────────────────────────┘ 720
```

- 当前卡：列表行黑底反白；卡片板 = **封面 440×200 + 信息条 48**；**没有「模拟卡 / 读卡」页签**；
- 画面上的编号是**列表位置**（01..N），不是 BLE 的稳定 `card_id`；删除后位置会重排，但 `card_id` 不变；
- 封面区**只画图，不显示 UID**；名称与类型放在信息条里。无封面时用几何默认图 + 信息条，同样不显示 UID；
- UID 只出现在读卡结果页与上位机详情里，不进入卡片封面；
- 读卡页通过**双击确认键**进入；提示条固定显示「双击确认键 · 进入读卡」；
- 列表最多显示 5 行（超出时首尾显示 `↑`/`↓` 与「…」），不做滚动动画；
- 状态行文案：`模拟中 · 等待读卡器` / `未选择卡片` / `NFC 硬件未就绪` / `切卡完成，请重新贴卡`；
- 底部提示行由 `app_shell_build()` 固定，页面名显示 `卡包` / `WALLET`。

### 10.3 页面 B：读卡页（双击进入，自动扫描）

```text
┌──────────────────────────────────────────┐
│ ● 82%      WIFI  BT      12:30           │
├──────────────────────────────────────────┤
│ 读取卡片                          COPY   │ 48
│ ──────────────────────────────────────── │ 78
│ ┌──────────────────────────────────────┐ │ 96
│ │              [NFC 图标]              │ │
│ │              正在读卡…                │ │ 300 高
│ │         将卡片贴在设备 NFC 区域        │ │
│ │              单击取消                 │ │
│ └──────────────────────────────────────┘ │
│ （成功后才显示结果）                       │
│ 类型    NTAG215                          │ 416
│ UID     04 11 22 33 44 55 66             │ 452
│ 容量    135 页 / 504 B                   │ 488
│ 内容    已读取整片镜像                    │ 524
│ 仅卡号 · 扇区加密不可读（M1 时显示）       │ 570
│ ┌──────────────────────────────────────┐ │
│ │          复制卡          ENTER        │ │ 44 高 / 黑底白字
│ └──────────────────────────────────────┘ │ 620
├──────────────────────────────────────────┤
│ 单击取消/复制   返回退出   长按休眠      │ 提示行
└──────────────────────────────────────────┘
```

状态机（**没有 idle**，进入即扫描）：

| 状态 | 主文案 | 副文案 | 单击确认 |
|---|---|---|---|
| SCANNING | 正在读卡… | 将卡片贴在设备 NFC 区域 | 取消，回模拟页 |
| DONE | 读取成功 | 类型 / UID / 容量 / 内容 | **复制卡**：写入卡包并回模拟页 |
| FAIL | 读取失败 | 超时 | 重新扫描 |
| UNSUPPORTED | 暂不支持 | ISO14443-4 / 加密卡 | 重新扫描 |

- 扫描中只刷新一次状态（开始/结束），不显示进度动画；
- **读到卡不等于入包**：成功后只保留 pending 结果，底部显示黑底白字的「复制卡」动作条；按 ENTER 才写 `wallet.bin`；
- 取消 / 返回会丢弃 pending 结果，不修改卡包；
- 返回（GPIO18 BACK）与长按休眠仍由 `app_manager` 全局消费；读卡页的 BACK 先回模拟页。

### 10.4 交互总表（n16r8）

| 按键 | 模拟页 | 读卡页 |
|---|---|---|
| UP / DOWN | 上一张 / 下一张（立即切换） | 无 |
| 单击确认 | 无 | 取消 / 重试 / 复制卡 |
| 双击确认 | **进入读卡页（自动扫描）** | 无 |
| 返回键（GPIO18） | 回主菜单 | 回模拟页 |
| 长按确认 | 手动休眠（全局） | 手动休眠（全局） |

> 卡包只在 n16r8 注册；n4r2 没有 NFC，不会出现这两个页面。

### 10.5 页面与 app 的跨线程协议

```c
/* 上行 ui_task → app_task（app_manager_post_ui_msg） */
#define APP_UI_REQ_WALLET_CMD      (0x21)   // payload: cmd(1)
enum {
    WALLET_CMD_NEXT   = 1,   // 上一张 / 下一张
    WALLET_CMD_PREV   = 2,
    WALLET_CMD_READ   = 3,   // 双击进入读卡页（自动扫描）
    WALLET_CMD_ACTION = 4,   // 读卡页单击：取消 / 重试 / 复制卡
};

/* 下行 app_task → ui_task（ui_core_post） */
#define APP_UI_MSG_WALLET_SNAP     (0x22)   // payload: wallet_snapshot_t
#define APP_UI_MSG_WALLET_COVER    (0x23)   // payload: wallet_cover_t*（所有权转移）
typedef struct {
    uint8_t mode;        // 0=EMU 1=COPY
    uint8_t scan_state;  // 0=scanning 1=done 2=fail 3=unsupported
    uint8_t count;
    uint8_t current;
    uint8_t kind;
    uint8_t uid_len;
    uint8_t uid[10];
    uint8_t has_cover;   // 1 = 随后会投递 COVER；0 = 页面清掉旧图
    char    name[24];
    char    type[12];
    char    status[32];
    char    detail[40];
} wallet_snapshot_t;
```

- 快照总长约 125 B，低于 `UI_CMD_DATA_MAX(160)`；
- 页面只渲染快照，不读 SD/HAL/参数；
- app_task 是卡库唯一写者；扫描任务通过原子指针交付完整镜像；
- **pending 结果只在 `WALLET_CMD_ACTION`（复制卡）时落盘**；页面离开/取消时 app_task 释放 pending；
- 封面走独立消息：`APP_UI_MSG_WALLET_COVER` 的 payload 是 `wallet_cover_t*`（含 `lv_image_dsc_t` + L8 像素），页面接收后拥有所有权并负责释放旧图；投递失败时 app_task 自己释放。
- 顺序保证：同一 `ui_core` FIFO 里先 `SNAP` 后 `COVER`；`has_cover=0` 时页面不等待 COVER，直接回退默认板。
- **双击进入读卡页**由 `app_entry_t.on_double` 实现（app_task），不由页面处理；4 键 BACK 走 `on_back`。

### 10.6 空状态

- 无 SD / 无卡：卡片板区域改为居中空状态：钱包图标 + 「卡包」+ 细线 + 「尚未添加卡片」+「双击确认键进入读卡」；
- NFC 未就绪：空状态文案改为「NFC 硬件未就绪」，双击不进入读卡；
- 未选择当前卡但已有卡：显示第一张为选中，但不启动模拟，状态行提示「上下键选择并模拟」。

---

## 11. BLE 远程建卡与写卡

### 11.1 范围与能力宣告

手机通过 BLE 在设备卡包里**创建一条虚拟卡记录**，写入：类型、UID、ATQA/SAK、名称、NTAG 内容（整片镜像或 NDEF 文本）；并可**上传 1-bit 封面**、**重命名**、**删除**、**选中**。**不操作实体 NFC 标签**；写实体标签属于另一条 PCD WRITE 流程，本期不做。

> 本期 Web 端**只提供 NTAG 建卡**（NTAG213/215/216，NDEF 文本或整片镜像）；M1 远程建卡协议保留，但 Web 不放入口。M1 UID 卡仍可通过本机读卡复制。

- 能力位：`0x59 CAPABILITIES_GET` 的 flags 增加 **bit7 = NFC wallet remote provisioning**，仅在 `SYS_NFC_ENABLE=1` 时置位。
- 旧固件该位为 0，客户端据此隐藏入口或提示升级。
- 安全提示：BLE 建卡等价于允许连接端指定任意 UID，属于克隆能力。本期沿用现有 BLE 连接权限（无额外认证），客户端必须做一次用户确认；后续如需收紧，可在 BEGIN 增加一次性 token 或要求设备端物理确认。

### 11.2 通道分配

`0x5A` / `0x5B` 已被通用 WiFi 文件上传与只读目录占用；NFC 独立使用 `0x63~0x66`（卡包与建卡）与 `0x67~0x69`（封面上传），**不再与 0x5C SoftAP 准备 / 0x5D 硬件身份查询共用通道**：

| CH | 名称 | 方向 | 用途 |
|---|---|---|---|
| 0x63 | NFC_CARD_BEGIN | 下行 | 开始建卡会话：元数据 + 内容长度 + CRC |
| 0x64 | NFC_CARD_DATA | 下行 | 追加内容块：offset + bytes |
| 0x65 | NFC_CARD_COMMIT | 下行 | 提交落盘，返回卡 id |
| 0x66 | NFC_WALLET_CTRL | 双向 | op 多路复用：列表 / 查询 / 选中 / 重命名 / 删除 / 清除封面 / 统计 |
| 0x67 | NFC_COVER_BEGIN | 下行 | 开始封面会话：card_id + 格式 + 尺寸 + 长度 + CRC |
| 0x68 | NFC_COVER_DATA | 下行 | 封面分块：offset + bytes |
| 0x69 | NFC_COVER_COMMIT | 下行 | 提交封面并更新卡记录 |

不复用 `0x45~0x4A`：那组是 TLV 文本参数通道，与 `param_ch + 1` 的查询约定绑定；建卡是二进制分块会话，语义不同。

### 11.3 会话模型

```text
BEGIN ──► DATA(offset=0) ──► DATA(offset=n) ──► ... ──► COMMIT
  │                                                       │
  └──── 任一步失败 / 超时 60s / 重新 BEGIN ───────────────┘（丢弃会话）
```

- **单会话**：同一时刻只允许一个会话（建卡或封面）；新的 BEGIN 直接丢弃未完成会话。
- **缓冲**：PSRAM；建卡最大 4096 B（M1 4K 预留，NTAG 最大 924 B），封面固定 11,000 B；60 s 未提交由 app 的 100 ms tick 回收。
- **落盘**：建卡在 COMMIT 时写 `wallet.bin`；封面在 COMMIT 时先写 `cover_<id>.bin`，再更新 `wallet.bin`。
- **串行化**：全部命令在 app_task 执行（经 `SYS_EVT_BLE_APP_CMD`），与本地读卡复制共用「卡库唯一写者」约束。
- **与本地模式共存**：建卡/封面期间允许设备正在模拟；若本地正在读卡扫描，COMMIT 只落盘、不重启模拟，等扫描结束或用户切卡后再应用。

### 11.4 报文格式

多字节整数一律 **大端**；ATQA 例外，按空中顺序原样 2 字节（M1 1K = `04 00`，NTAG = `44 00`）。

#### 11.4.1 `0x63` NFC_CARD_BEGIN（下行）

| 偏移 | 长度 | 字段 | 说明 |
|---|---|---|---|
| 0 | 1 | kind | 0=M1_UID，1=NTAG213，2=NTAG215，3=NTAG216，4=M1_1K，5=M1_2K，6=M1_4K |
| 1 | 1 | content_mode | 0=完整页镜像；1=NDEF 文本（UTF-8）；2=M1 原始块镜像（预留） |
| 2 | 1 | uid_len | M1=4，NTAG=7 |
| 3 | 1 | sak | M1：0x08/0x18/0x38；NTAG 固定 0x00 |
| 4 | 2 | atqa | 空中顺序：M1 1K=`04 00`，NTAG=`44 00` |
| 6 | uid_len | uid | 卡片 UID |
| 6+uid_len | 1 | name_len | 0..23；0 表示用设备默认名 |
| 7+uid_len | n | name | UTF-8，不含 `\0` |
| 7+uid_len+n | 2 | total | 内容字节数 |
| 9+uid_len+n | 4 | crc32 | 对 `total` 字节内容的 CRC-32/IEEE（zlib 兼容，初值 0xFFFFFFFF、结果取反） |

约束：

- M1：`content_mode=0`、`total=0`、`crc32=0`；`uid_len=4`。
- **M1 完整块镜像（预留）**：`kind=4/5/6`、`content_mode=2`、`total=1024/2048/4096`；设备按原始 block 顺序保存，不重建 block 0。当前固件可保存，但模拟降级为 `M1_UID`（见 §6.2.1）。
- NTAG 完整镜像：`content_mode=0`，`total` 必须精确等于所选型号容量（180 / 540 / 924）；页 0~3 由设备按 `kind+uid` 重建（客户端内容里的 0~3 被忽略），页 4 起写入。
- NTAG NDEF 文本：`content_mode=1`，`total` = UTF-8 字节数；设备生成 Text 记录与 TLV，其余用户页清零。容量不足返回参数错误。
- `total` 上限 `HAL_NFC_MAX_CARD_BYTES(4096)`；超出返回参数错误。
- `atqa/sak` 与 `kind` 不符、`uid_len` 不符、`name` 含 `\0` → 参数错误。
- 无 SD / 存储不可用 → 无 SD；分配缓冲失败 → 内存不足。

响应：

| 字段 | 长度 | 说明 |
|---|---|---|
| status | 1 | 见 §11.5 状态码 |
| chunk_max | 2 | 建议单包最大内容字节数，固定 180 |

#### 11.4.2 `0x64` NFC_CARD_DATA（下行）

| 偏移 | 长度 | 字段 | 说明 |
|---|---|---|---|
| 0 | 2 | offset | 必须等于已接收字节数，否则拒绝 |
| 2 | n | data | 1..180 字节 |

响应：`status(1) + received(2 BE)`。客户端据此续传/重试；`received` 始终是设备当前已收长度。

#### 11.4.3 `0x65` NFC_CARD_COMMIT（下行）

无 payload。设备校验 `received == total` 且 CRC 一致后：

1. 按 kind 计算去重键 `kind + uid`，已存在则更新（与本地复制一致）；
2. 写 `wallet.bin`，把新卡设为 `current`；
3. 若当前 app 是卡包，重启模拟到新卡；否则只落盘，等进入卡包时生效。

响应：

| 字段 | 长度 | 说明 |
|---|---|---|
| status | 1 | 0 成功；7 已保存但恢复模拟失败 |
| card_id | 1 | 卡记录 id（显示编号） |
| count | 1 | 当前卡总数 |
| current | 1 | 当前选中下标，0xFF = 无 |
| uid_len | 1 | 4 / 7 |
| uid | uid_len | UID |

#### 11.4.4 `0x66` NFC_WALLET_CTRL（双向）

请求：`op(1) + args`。

| op | 名称 | args | 响应 |
|---|---|---|---|
| 0x00 | LIST | `start(1)` | `status total current start n` + n 条条目 |
| 0x01 | SELECT | `card_id(1)` | `status current` |
| 0x02 | DELETE | `card_id(1)` | `status count current` |
| 0x03 | GET | `card_id(1)` | `status` + 条目 + `mem_len(2 BE)` + `cover_len(2 BE)` |
| 0x04 | STATS | 无 | `status count current max_cards(1)` |
| 0x05 | RENAME | `card_id(1) name_len(1) name(n)` | `status card_id name_len name` |
| 0x06 | CLEAR_COVER | `card_id(1)` | `status card_id` |

列表条目（变长，固定 UID 区 7 字节，`uid_len` 指示有效长度）：

```text
id(1) kind(1) uid_len(1) sak(1) atqa(2) uid(7) flags(1) name_len(1) name(name_len)
```

`flags` bit0 = `has_cover`，其余保留。

- 条目固定开销 15 B；响应总长不得超过 192 B，设备按剩余空间打包，客户端用 `start` 续读。
- `SELECT`：写回 `current`；卡包为当前 app 时立即重启模拟并加载封面，否则下次进入生效。
- `DELETE`：删除 record 与 `cover_<id>.bin`；若删的是当前卡，自动选中下一张；没有剩余卡则 `current=0xFF` 并停止模拟。
- `RENAME`：名称 1..23 字节 UTF-8，不含 `\0`；只改名称，不动封面/镜像。
- `CLEAR_COVER`：删除封面文件并把 `has_cover` 清零；当前卡时页面立即回退默认板。
- `GET` 不含内存镜像与封面内容，只给类型/身份/名称/镜像长度/封面长度，避免远程导出整卡。

#### 11.4.5 `0x67~0x69` 卡封面（下行）

封面固定为 **FFUI 1bpp、440×200、payload 11,000 B**；设备不缩放、不接受其它尺寸。

`0x67 NFC_COVER_BEGIN`：

| 偏移 | 长度 | 字段 | 说明 |
|---|---|---|---|
| 0 | 1 | card_id | 目标卡（1..16） |
| 1 | 1 | format | 1 = FFUI 1bpp |
| 2 | 2 | width | 必须 440 |
| 4 | 2 | height | 必须 200 |
| 6 | 2 | total | 必须 11000（payload 字节数） |
| 8 | 4 | crc32 | 对 11,000 字节 payload 的 CRC-32/IEEE |

响应：`status(1) + chunk_max(2 BE)`（chunk_max = 180）。

`0x68 NFC_COVER_DATA`：`offset(2 BE) + data(1..180)`；响应 `status(1) + received(2 BE)`。

`0x69 NFC_COVER_COMMIT`：无 payload。设备校验长度/CRC 后：

1. 写 `cover_<id>.tmp` → `cover_<id>.bin`；
2. 更新 wallet.bin 的 `has_cover/cover_len/cover_crc32`；
3. 当前卡时重新加载封面并投递 `APP_UI_MSG_WALLET_COVER`。

响应：`status(1) + card_id(1) + has_cover(1)`（成功固定 `has_cover=1`）。

> 11000 / 180 ≈ 62 个 DATA 包。上传期间同样只允许一个会话；中途断连由 60 s 超时回收，旧封面保持不变。

#### 11.4.6 示例：远程创建一张 M1 卡

BEGIN（kind=0、content_mode=0、uid_len=4、sak=0x08、atqa=`04 00`、uid=`11 22 33 44`、name=`door`、total=0、crc=0）：

```text
DATA = 00 00 04 08 04 00 11 22 33 44 04 64 6F 6F 72 00 00 00 00 00 00   （21 B）
帧   = 55 5C 15 <DATA> SUM
回包 = 55 5C 03 00 00 B4 SUM        （status=0，chunk_max=180）
```

COMMIT（无 payload）：

```text
帧   = 55 5E 00 SUM
回包 = 55 5E 09 00 01 01 00 04 11 22 33 44 SUM
       status=0, card_id=1, count=1, current=0, uid_len=4, uid=11 22 33 44
```

NTAG 卡把 `kind` 改为 1/2/3、`uid_len=7`、`sak=0x00`、`atqa=44 00`，再按 180 B 分包发送内容后 COMMIT。

### 11.5 状态码

| 值 | 含义 | 典型场景 |
|---|---|---|
| 0 | OK | 成功 |
| 1 | 忙 | 休眠流程中 / 上一次提交未结束 |
| 2 | 参数错误 | kind/uid_len/atqa/sak/name/total/offset/CRC 不合法 |
| 3 | 无 SD | 卡库不可用 |
| 4 | 未找到 | SELECT/DELETE/GET/RENAME/CLEAR_COVER/COVER_BEGIN 的 card_id 不存在 |
| 5 | 内存不足 | 会话缓冲分配失败 |
| 6 | 保存失败 | wallet.bin 写入/替换失败 |
| 7 | 已保存但恢复模拟失败 | 卡已入库，模拟启动失败（可重试 SELECT） |

### 11.6 固件实现落点

| 文件 | 改动 |
|---|---|
| `components/film_service/inc/service_ble.h` | 新增 `0x63~0x69` 通道宏与 `BLE_NFC_CHUNK_MAX (180)` |
| `components/film_service/src/service_ble.c` | 七个 case 归为一组：校验长度 → 以 `[ch, data...]` 发布 `SYS_EVT_BLE_APP_CMD`；不在此解析语义、不回包 |
| `components/film_app/core/app_manager.c` | 在 `SYS_EVT_BLE_APP_CMD` 分支里把 `0x63~0x69` 转给 `app_wallet_on_ble_cmd()`（`SYS_NFC_ENABLE` 守卫） |
| `components/film_app/apps/wallet/app_wallet.c` | 新增 `app_wallet_on_ble_cmd()`：建卡/封面两种会话、CRC、超时回收、回包；封面加载与 `APP_UI_MSG_WALLET_COVER` |
| `components/film_app/apps/wallet/app_wallet_store.c` | 新增 `upsert_remote` / `list` / `select` / `delete` / `rename` / `cover_set` / `cover_clear` / `cover_load`；NDEF Text 构建；记录 `data_format/data_ver` |
| `components/film_service/src/service_ble.c` | `0x59` flags 在 `SYS_NFC_ENABLE` 时置 bit7 |

实现要点：

- `service_ble_send_resp(ch, ...)` 可从 app_task 调用（app 参数通道已是同一用法）。
- `ble_cmd_conflicts_with_direct()` 需要把 `0x63~0x69` 加入：WiFi 直传期间直接回 `1`（忙），避免两路同时写 SD。入睡流程中 BLE 层已统一拦截，不回包，客户端按超时处理。
- `SYS_EVENT_PAYLOAD_MAX = 192`，事件里还要放 1 字节 ch，因此命令数据上限 191；DATA 的 `offset(2)` 之后最多 189，协议取 **180** 留余量。
- 建卡会话缓冲按 `HAL_NFC_MAX_CARD_BYTES(4096)` 分配；COMMIT 保存 `data_format/data_ver`。格式 2（M1 块镜像）先入库，模拟暂按 `M1_UID` 降级。
- CRC 用 `esp_rom_crc32_le(0xFFFFFFFF, data, len) ^ 0xFFFFFFFF`，与 zlib `crc32` 对齐。
- NDEF Text 记录：
  - 消息 < 256 B：短记录 `D1 01 <payload_len> 54 02 'e' 'n' <text>`；
  - 消息 ≥ 256 B：长记录 `C1 01 <payload_len u32 BE> 54 02 'e' 'n' <text>`（SR=0，MB|ME|TNF=1）；
  - 再包 TLV：<255 用 `03 <len> ... FE`，≥255 用 `03 FF <len16 BE> ... FE`；
  - 写入从页 4 开始，剩余用户页清零；总长超过型号用户容量返回参数错误。
- 设备端始终保存「页 0..last 的完整镜像」：页 0~3 由 kind/uid 重建，保证远程数据不会破坏 BCC/CC。
- 封面 payload 校验：11,000 B、CRC 一致；写 `cover_<id>.bin` 使用 tmp+rename；wallet.bin 只记 `has_cover/cover_len/cover_crc32`。
- 封面加载：1bpp → L8（440×200，约 88 KB），只保留当前卡；通过 `APP_UI_MSG_WALLET_COVER` 把指针所有权交给页面。
- 若 `m_boot_page` / `m_sleep_page` 占屏期间收到命令：boot 页可正常处理；入睡流程中 BLE 层已拦截（不回包），客户端按超时处理。

### 11.7 客户端改造

| 端 | 文件 | 改动 |
|---|---|---|
| Web | `tools/ForFilm/js/bluetooth.js` | 增加 `0x63~0x69` 常量；复用现有包/校验/通知等待逻辑 |
| Web | `tools/ForFilm/js/nfc-wallet.js`（新增） | 封装 `walletInfo/list/create/select/rename/delete/clearCover/uploadCover`；分块发送与重试；图片→440×200 1bpp |
| Web | `tools/ForFilm/index.html` + 样式 | **底部导航新增「卡包」独立 tab**，进入后是卡包页：列表（封面缩略图）、创建、重命名、删除、选中、上传/清除封面 |
| Web | 卡包页表单 | 本期只提供 NTAG213/215/216 + NDEF 文本/整片镜像；不提供 M1 建卡入口 |
| 小程序 | `tools/wechart/miniprogram/utils/ble-utils.js` | 同步常量与 `BLE_NFC_CHUNK_MAX` |
| 小程序 | 新增卡包页面（建议 `pkgCreate` 下） | 同上管理界面；封面用 canvas 二值化后分包 |
| 两端 | 能力判断 | 先读 `0x59`，bit7 为 0 则不显示入口 |

客户端流程：

1. `0x59` 检查 bit7；
2. `0x66 op=0` 拉列表（按 `start` 续读；用 `flags.bit0` 判断是否有封面）；
3. 用户填表：类型、UID（十六进制，M1 4 字节 / NTAG 7 字节）、名称、NTAG 内容（NDEF 文本或整片镜像）；
4. `0x63 BEGIN` → 按 180 B 分包 `0x64`（每包等回包，超时重发同 offset，最多 2 次）→ `0x65 COMMIT`；
5. 可选封面：把图片缩放到 440×200、二值化为 1bpp（建议阈值 128，照片可用有序抖动），按 FFUI payload 计算 CRC → `0x67/0x68/0x69` 上传；封面失败不影响已创建的卡；
6. 管理：`0x66 op=1` 选中、`op=5` 重命名、`op=2` 删除、`op=6` 清除封面；
7. 每次成功后用返回的 `card_id/current` 与列表刷新界面。

> 客户端不要用「发完即认为成功」：DATA 与 COMMIT 都有明确回包，必须逐包确认；`received` 可用于断点续传。
> 封面由客户端负责二值化，固件只接受 1bpp、440×200，不做灰度/缩放。封面里不要再叠 UID，UID 只在上位机详情里展示。

### 11.8 兼容性

- 旧 Ark 固件：`0x59` 无 bit7 → 客户端隐藏入口；直接发 `0x63`/`0x67` 会落进 `default:` 被忽略，无回包。旧 NFC 固件使用的 0x5C/0x5D/0x5E/0x5F/0x60/0x61/0x62 已废弃，新客户端不再兼容。
- `frame_film` / `frame_film_dock`：不使用 `0x63~0x69`，收到按未知通道忽略；客户端先做能力判断。
- 协议文档需同步：`docs/blecmd/blecmd_protocol.md` 的 §3.19 / §4.20 与 0x59 flags 表。

---

## 12. 框架接入改动

### 12.1 app id 与菜单

| 文件 | 改动 |
|---|---|
| `components/film_app/inc/app_interface.h` | 尾部追加 `APP_ID_WALLET`（=7，**不要插中间**）；`APP_MENU_ENTRY_NUM` 在 `SYS_NFC_ENABLE` 时 +1 |
| `components/film_app/core/app_manager.c` | `m_menu_entries[]` 在「通行证」后条件插入 `APP_ID_WALLET`；离开卡包时调用 `app_wallet_deactivate()` |
| `components/film_app/pages/app_menu.c` | `MENU_ITEMS[]` / `MENU_ITEMS_EN[]` 同序插入「卡包 / WALLET」，图标 `UI_ICON_WALLET` |
| `components/film_app/core/app_init.c` | 条件注册 `g_app_wallet_entry`；NFC 唤醒路由 |
| `components/film_app/core/app_boot_cfg.h/.c` | 尾部追加 `APP_START_WALLET`，`resolve_target()` 增加 case；未注册自动回落图片 |
| `components/film_app/pages/app_settings.c` | `STARTAPP_OPTS[]` / `START_EN[]` 尾部追加「卡包 / WALLET」；行数不变（18 行），不动 `ROW_H` |

> 菜单顺序建议：图片、通行证、**卡包**、模板、时钟、动图、设置。菜单下标不落盘，插入不影响老设备。
> `app_start_t` 是落盘值，只能尾部追加。

### 12.2 图标与字体

| 文件 | 改动 |
|---|---|
| `components/film_ui/inc/ui_assets.h` | 枚举插入 `UI_ICON_WALLET`（位置与 `m_icons[]` 初始化顺序一致） |
| `components/film_ui/src/ui_assets.c` | `m_icons[]` 增加 `{ .code="wallet", ... .def=ui_def_icon_wallet }` |
| `components/film_ui/inc/ui_defaults.h` + `src/ui_defaults.c` | 由脚本生成 `ui_def_icon_wallet` |
| `tools/ui-assets/png/ui_icon_wallet.png` | 新增 80×80 线稿源图 |
| `tools/ui-assets/gen_ui_assets.py` | ① 增加 wallet 资源项；② 修正 `OUT_C_INC/OUT_C_SRC` 当前指向 `firmware/frame_film/...` 的旧路径，改为 `frame_film_ark` |
| `tools/ark-font/gen_fonts.py` + `ui_fonts.c` | 卡包新增中文文案后重跑；若空状态用 36 px hero，需把「卡包」加入 `LARGE_TEXT[36]` |
| `assets/fonts/source-han-sans/` | 下载并保存 `SourceHanSansCN-Bold.otf` / `-Medium.otf`（OFL）；`gen_fonts.py` 默认路径改到这里，避免依赖临时 `.output/ark` |

### 12.3 事件与持久化

- `sys_event.h` 新增 `SYS_EVT_NFC_SCAN_DONE = 0x0501`（0x05xx 段空闲）。
- BLE 建卡/封面/管理命令复用 `SYS_EVT_BLE_APP_CMD`（不新增事件 ID），由 `app_manager` 按通道号转给卡包模块。
- 卡包 app **不声明** `state/state_size`，避免写 NVS `app7` 覆盖开机配置。
- `service_param.h` 不需要新增槽位；卡库与封面全部在 SD（`wallet.bin` + `cover_<id>.bin`）。
- 删除卡片/清除封面时必须同时删除对应 `cover_<id>.bin`，避免孤儿文件占用 SD。
- `service_monitor.c`：
  - 入睡前调用 `hal_nfc_sleep_arm()`，按结果决定是否 `hal_nfc_deinit()`；
  - 不 deinit 已武装的 NFC；
  - EXT1 唤醒打日志（可选，`service_monitor_init` 已按手动唤醒处理）。

### 12.4 文档与协议

- `docs/blecmd/blecmd_protocol.md`：
  - §3.6 / §4.7 的 app_id 表追加 `0x07 卡包 wallet`，并把「FULL 注册 7 个 app」更新为 8 个；
  - 新增 §3.19 / §4.20：`0x63~0x69` NFC 卡包 / 建卡 / 封面通道；
  - 更新 §3.15 / §4.15 的 `0x59` flags 表（新增 bit7）与变更记录；
- `docs/knowledge/ble_commands.md`、`AGENTS.md` 的命令范围说明同步补充 `0x63~0x69`；
- 本文档作为 NFC 设计基线。

### 12.5 按键与 n16r8 四键变体

n16r8 硬件多一个 **GPIO18 返回键**，并且只有 n16r8 有 NFC；n4r2 仍是三键、无 NFC、无卡包。两套固件的按键语义：

| 项 | n4r2（3 键，无 NFC） | n16r8（4 键 + NFC） |
|---|---|---|
| 卡包 | 不注册、不显示 | 主菜单可进 |
| 返回方式 | 双击确认键（沿用旧行为） | **短按 GPIO18 返回键** |
| 确认键双击 | 有（350 ms，返回） | 有（350 ms，卡包内用于进入读卡页；其它 app 忽略） |
| 上/下/确认 | 6 / 4 / 5 | 6 / 4 / 5 |
| 返回键 | 无 | 18（只注册 SINGLE_CLICK） |

实现落点：

| 文件 | 改动 |
|---|---|
| `components/film_hal/Kconfig` | 新增 `config ARK_HAS_NFC`（bool，default n）与 `config ARK_INPUT_BACK_KEY`（bool，default n） |
| `sdkconfig_n16r8` | 加 `CONFIG_ARK_HAS_NFC=y`、`CONFIG_ARK_INPUT_BACK_KEY=y`；`sdkconfig`（n4r2）两者都不置 |
| `components/film_hal/inc/hal_input.h` | `input_press_type_t` 尾部、`INPUT_PRESS_MAX` 之前追加 `INPUT_PRESS_BACK`；导出 `HAL_INPUT_HAS_BACK_KEY` |
| `components/film_hal/src/hal_button.c` | 条件创建第 4 个按键（GPIO18）；`buttons[3]` 与 deinit 循环按数量条件编译；**两个变体都保留确认键 `BUTTON_DOUBLE_CLICK` 与 350 ms 窗口** |
| `components/film_app/inc/app_interface.h` | `app_entry_t` 尾部追加可选 `bool (*on_back)(void)` 与 `bool (*on_double)(void)` |
| `components/film_app/core/app_manager.c` | `BACK` → `on_back`/回菜单；`DOUBLE` → `on_double`，未消费时 n16r8 忽略、n4r2 走旧双击返回 |
| `components/film_app/core/app_init.c` | 注册 `INPUT_PRESS_BACK` 回调 |
| `components/film_service/src/service_monitor.c` | 把 BACK 也注册为「有操作」，重置休眠计数 |
| `components/film_app/apps/wallet/app_wallet.c` | `on_double`：模拟页 → 进入读卡；`on_back`：读卡页 → 回模拟 |
| `components/film_app/pages/app_settings.c`、`apps/pass/app_pass.c`、`apps/clock/app_clock.c` | 提示行按变体显示「返回键退出」或「双击确认退出」 |

语义细节：

- **返回键只在 n16r8 存在**：主菜单是根，按下无动作；开机画面/休眠卡占屏期间按键仍按既有规则丢弃。
- **返回键不是唤醒源**：深度休眠只保留 GPIO5（EXT0）+ NFC IRQ（EXT1）；休眠时按返回键不会唤醒（硬件上不参与唤醒配置）。
- **分层返回**：`on_back` 返回 true 表示 app 自己处理（卡包读卡页先回模拟），false 才由 `app_manager` 回主菜单；`on_double` 只由卡包实现（模拟页 → 读卡页）。
- **n4r2 不受影响**：没有 NFC/卡包，确认键双击仍按旧语义返回，且没有 GPIO18/BACK。
- **n16r8 的双击**：只用于卡包读卡入口；其它 app 不响应双击（返回统一走 BACK）。
- **BLE 遥控兼容**：`0x4E` 的 `BLE_KEY_DOUBLE(0x04)` 跟随物理双击语义（n16r8 卡包内=读卡）；`BLE_KEY_BACK(0x05)` 是显式返回，两种固件都可用。
- **单击延迟**：n16r8 确认键要认双击（读卡入口），保留 350 ms 配对窗口；n4r2 本来就要 350 ms 认双击返回，行为不变。
- **GPIO18 与 NFC 互斥**：NFC 固定用 10/19/9/20 + IRQ7，不要采用「MOSI=18」的备选接线。

---

## 13. 文件改动清单

### 新增

```text
firmware/frame_film_ark/components/film_libs/st25r3916_lib/ 整目录复制（供应商库）
firmware/frame_film_ark/components/film_hal/inc/hal_nfc.h
firmware/frame_film_ark/components/film_hal/src/hal_nfc.c
firmware/frame_film_ark/components/film_app/apps/wallet/app_wallet.h
firmware/frame_film_ark/components/film_app/apps/wallet/app_wallet.c
firmware/frame_film_ark/components/film_app/apps/wallet/app_wallet_store.h
firmware/frame_film_ark/components/film_app/apps/wallet/app_wallet_store.c
tools/ui-assets/png/ui_icon_wallet.png
tools/ui-mockup/wallet.html                                卡包交互原型（480×720 / 1-bit）
tools/ForFilm/js/nfc-wallet.js                            BLE 建卡客户端封装
tools/wechart/miniprogram/pkgCreate/nfc/                 小程序卡包页面（建议路径）
docs/knowledge/nfc_wallet.md（本文）
```

### 修改

```text
components/film_sys/inc/sys_cfg.h              SYS_NFC_ENABLE 功能开关
components/film_sys/inc/sys_event.h            SYS_EVT_NFC_SCAN_DONE
components/film_hal/CMakeLists.txt             条件编译 + REQUIRES
components/film_hal/Kconfig                    ARK_HAS_NFC / ARK_INPUT_BACK_KEY
components/film_hal/inc/hal_api.h              汇总头
components/film_hal/inc/hal_input.h            INPUT_PRESS_BACK / HAS_BACK_KEY
components/film_hal/src/hal_button.c           第 4 键 / 按键数量条件编译
components/film_hal/inc/hal_pwr.h              唤醒原因 API
components/film_hal/src/hal_pwr.c              唤醒原因 + IRQ 门闸
components/film_hal/src/hal_init.c             hal_nfc_init()
components/film_service/src/service_monitor.c  入睡武装 / 不 deinit
components/film_service/inc/service_ble.h      0x63~0x69 通道宏
components/film_service/src/service_ble.c      通道上浮 / 0x59 bit7
components/film_app/CMakeLists.txt             条件编译 wallet
components/film_app/inc/app_interface.h        APP_ID_WALLET / 菜单数
components/film_app/core/app_manager.c         菜单表 / 离开卡包 / 入睡 standby
components/film_app/core/app_init.c            注册 / NFC 唤醒路由
components/film_app/core/app_boot_cfg.h/.c     APP_START_WALLET
components/film_app/pages/app_menu.c           视觉表
components/film_app/pages/app_settings.c       启动应用候选 / 返回提示
components/film_app/apps/pass/app_pass.c       返回提示
components/film_app/apps/clock/app_clock.c     返回提示
sdkconfig_n16r8                                CONFIG_ARK_HAS_NFC / ARK_INPUT_BACK_KEY=y
components/film_ui/inc/ui_assets.h             图标枚举
components/film_ui/src/ui_assets.c             图标槽位
components/film_ui/inc/ui_defaults.h           生成
components/film_ui/src/ui_defaults.c           生成
tools/ui-assets/gen_ui_assets.py               资源项 + 路径修正
tools/ark-font/gen_fonts.py / ui_fonts.c       字库重生成
assets/fonts/source-han-sans/*.otf             Source Han Sans CN Bold/Medium（OFL）
tools/ForFilm/js/bluetooth.js                  通道常量
tools/ForFilm/index.html / 样式                卡包面板
tools/wechart/miniprogram/utils/ble-utils.js   通道常量
docs/blecmd/blecmd_protocol.md                 app_id 表 / 0x63~0x69 / 0x59 bit7
docs/knowledge/ble_commands.md                 命令速查
AGENTS.md                                      命令范围说明
```

---

## 14. 实施阶段

| 阶段 | 内容 | 完成判据 |
|---|---|---|
| P0 硬件 bring-up | 复制库、`CONFIG_ARK_HAS_NFC` + `HAL_NFC_*` 接线、`hal_nfc_init` + `chip_info` 日志 | 串口打印 `IC_IDENTITY type=0x05`；n4r2 无 NFC 代码、n16r8 正常 |
| P1 读卡与存储 | 扫描任务、NTAG 整片读取、M1 身份、wallet.bin 读写 | 读 NTAG215 后 PC 解析 dump 与手机读到的内容一致 |
| P2 模拟与切卡 | `hal_nfc_emu_*`、卡包页面、快照协议 | 手机识别模拟卡；切卡后 UID 变化；重启后当前卡保留 |
| P3 框架接入 | app id/菜单/设置/开机候选 | 主菜单 7 项（中英）；`0x4B` 可切到 0x07；START APP 可选卡包 |
| P3.5 BLE 远程建卡 | `0x63~0x66` 通道、会话/CRC/落盘、0x59 bit7、Web/小程序端 | 手机建 M1/NTAG 卡成功；列表/选中/重命名/删除可用；旧固件入口隐藏 |
| P3.6 封面与管理 | `0x67~0x69` 封面通道、重命名/删除/清除封面、封面加载与 UI 移交 | 上传 440×200 封面后设备显示；封面区无 UID；改名/删除立即刷新 |
| P4 休眠唤醒 | standby、EXT1 武装、唤醒路由、IRQ 门闸 | 贴卡唤醒直接进卡包；按键/定时唤醒不受影响；不贴卡不误唤醒 |
| P5 打磨与文档 | 图标/字库、协议文档、功耗实测、异常路径 | 功耗数据归档；无卡/无 SD/无 NFC 三条降级路径可用 |

---

## 15. 验证清单

### 编译

- [ ] `SYS_NFC_ENABLE=1`：n4r2 与 n16r8 两套配置都能构建；镜像增量合理。
- [ ] `SYS_NFC_ENABLE=0`：`hal_nfc.c`/`app_wallet*.c` 不参与编译；镜像中无 st25r 符号；主菜单仍为 6 项。
- [ ] 两种模式下 `idf.py size-components` 对比（NFC 关闭时应无 st25r3916_lib 组件占用）。

### 功能

- [ ] 上电 `chip_info` 正常；拔掉 NFC 模块时不阻塞启动，卡包提示「NFC 硬件未就绪」。
- [ ] NTAG213/215/216 复制：页数、CC、BCC 校验通过；模拟后手机可读 NDEF。
- [ ] M1 复制：UID/ATQA/SAK 正确；手机可识别同 UID；UI 明示「仅卡号」。
- [ ] 重复读同一张卡不产生重复条目。
- [ ] 切卡：UP/DOWN 环绕；当前卡写回 wallet.bin；断电重启后保持。
- [ ] 卡包进入即模拟，画面上没有「模拟卡 / 读卡」页签。
- [ ] 双击确认键进入读卡页并自动扫描；单击取消能回到模拟页。
- [ ] 读卡成功不落盘；底部「复制卡」按 ENTER 后才写入卡包并切到新卡。
- [ ] 读卡成功后退回/取消，卡包不新增记录。
- [ ] n4r2 构建：无 NFC、不注册卡包、不初始化 `st25r`；双击确认仍按旧行为返回。
- [ ] 无 SD：空卡包、读卡不可用、休眠不武装 NFC。

### 休眠/唤醒

- [ ] 手动休眠与自动休眠两条路径都能在睡前启动当前卡模拟。
- [ ] 贴卡唤醒：跳过开机画面，直接进卡包；模拟在页面首帧前恢复。
- [ ] 按键唤醒（EXT0）与定时唤醒（TIMER）行为不变。
- [ ] 休眠时有卡在场：不立即入睡，等场消失；无卡时正常入睡。
- [ ] IRQ 下拉确认：无卡、无场时整夜无自唤醒。
- [ ] 实测深度休眠电流（NFC 模拟监听 / NFC 关闭两组）。

### UI

- [ ] 中英文菜单均显示「卡包 / WALLET」，图标正确，选中反白。
- [ ] 状态栏电量/WiFi/蓝牙/时间正常；页面无动画、无残影。
- [ ] 卡片板 = 封面 440×200 + 48 px 信息条；**封面区不出现 UID**。
- [ ] 无封面时回退默认几何板，同样不显示 UID。
- [ ] 切卡后封面立即切换；无封面卡切过去时旧封面被清掉。
- [ ] 返回：GPIO18 BACK 在模拟页回主菜单、读卡页回模拟。
- [ ] 字体无缺字（含「卡包」「模拟卡」「读卡」「扇区不可读」等新文案）。

### BLE 远程建卡与管理

- [ ] `0x59` flags bit7 在 NFC 编译开启时为 1，关闭时为 0。
- [ ] 创建 M1 卡：BEGIN → COMMIT 无 DATA 也成功；手机识别同 UID。
- [ ] 创建 NTAG215 NDEF 文本卡：分包 3 包以上，CRC 校验通过，手机读到文本。
- [ ] 创建 NTAG 整片镜像卡：页 0~3 由设备重建，页 4 起与下发一致。
- [ ] 远程导入 M1 1K 块镜像（1024 B）：记录 `data_format=2/mem_len=1024`，重启后仍在；当前模拟按 UID 降级。
- [ ] 未知 `data_format`/`data_ver` 不会被误删或误解析；加载时按「无卡」跳过。
- [ ] 重复 UID 建卡不产生重复条目（更新并选中）。
- [ ] DATA 乱序 offset / CRC 错误 / 长度不匹配均返回对应状态码，不落盘。
- [ ] 建卡中途断连/超时 60 s 后会话被回收，再次建卡正常。
- [ ] LIST 分页、GET、SELECT、DELETE、RENAME、CLEAR_COVER 的 id/current/name 行为正确。
- [ ] 重命名：1..23 字节 UTF-8 生效；0 字节、超长、含 `\0` 被拒绝。
- [ ] 删除当前卡后自动选中下一张；删除最后一张后停止模拟并显示空状态。
- [ ] 删除卡/清除封面后对应 `cover_<id>.bin` 被删除。
- [ ] 上传 440×200 1bpp 封面：62 包左右完成，设备立即显示；封面区无 UID。
- [ ] 封面尺寸/格式/CRC 错误被拒绝，旧封面保持不变。
- [ ] 替换封面与清除封面都能立即刷新当前卡页面。
- [ ] 建卡时设备正在模拟：COMMIT 后新卡成为当前并重启模拟。
- [ ] 旧固件（无 bit7）客户端不显示入口；`frame_film`/dock 不受影响。

### 按键变体（n4r2 / n16r8）

- [ ] n4r2 构建：GPIO18 不初始化、无卡包；双击确认返回保持旧行为。
- [ ] n16r8 构建：GPIO18 短按返回；模拟页单击无动作、双击进入读卡；确认单击延迟约 350 ms。
- [ ] 卡包读卡态按返回 → 回模拟；再按返回 → 回主菜单。
- [ ] 主菜单按返回无动作；开机画面/休眠卡占屏期间返回被丢弃。
- [ ] `0x4E` 的 `BLE_KEY_BACK(0x05)` 与 `BLE_KEY_DOUBLE(0x04)` 在两种构建下都能返回。
- [ ] 深度休眠时按 GPIO18 不唤醒；GPIO5/NFC 唤醒不受影响。
- [ ] 返回键电气确认：低有效、内部/外部上拉与现有三键一致，无抖动误触。

---

## 16. 风险与待确认

| 风险 | 说明 | 缓解 |
|---|---|---|
| 唤醒后手机读不到 NDEF | 深度休眠时芯片只能硬件应答 UID；MCU 启动前无法处理 READ | 唤醒后尽早 `emu_start`（已提前到进入卡包预检）；UI 提示「请重新贴卡」 |
| 深度休眠功耗 | NFC 常供电 + PICC 监听会计入静态电流 | P4 实测；必要时提供「NFC 唤醒」运行期开关或改可控供电 |
| IRQ 误唤醒 | 芯片未初始化/复位时 IRQ 可能悬空 | 硬件 10 kΩ 下拉；软件入睡前检查 IRQ 电平 |
| 19/20 被 USB 占用 | 打开 USB-JTAG/TinyUSB 会冲突 | 保持 `CONFIG_ESP_CONSOLE_SECONDARY_NONE=y`，在 `sys_cfg.h` 注释中标注 |
| 切卡时读卡器仍在场 | `emu_stop/start` 期间身份短暂失效 | 状态行提示重新贴卡；`emu_set_card` 不适用于换 NTAG 型号 |
| wallet.bin 掉电损坏 | FAT 写入中断 | tmp + bak + CRC；校验失败按空卡包处理，不阻塞启动 |
| app7 NVS 冲突 | 卡包若声明 state 会写 `app7`，覆盖开机行为 | 卡包不声明 state；当前下标存 wallet.bin |
| 远程建卡 = 克隆能力 | 任意连接端可指定 UID/NDEF，缺少额外认证 | 客户端二次确认；0x59 bit7 能力位；后续可加 token/物理确认 |
| 分块传输中断 | 924 B 需要 6 包以上，BLE 断连/丢包会半途而废 | 单会话 + 60 s 超时；offset/received 支持重发；COMMIT 前 CRC 校验 |
| 封面占 PSRAM | 单张 1bpp 展开成 L8 约 88 KB；16 张同时驻留会挤爆 n4r2 | 只保留当前卡一张，切卡/离开释放；封面文件本身仅 11 KB |
| 封面上传包数多 | 11,000 B ≈ 62 包，弱信号下耗时较长 | 逐包 ACK + 重发；封面失败不回滚已建好的卡；独立 60 s 会话超时 |
| 封面文件孤儿 | 删除卡/清除封面与文件删除不是原子事务 | 加载时校验长度/CRC；删除失败留待下次清理；命名按 id 固定，复用即覆盖 |
| 重命名编码 | 名称是 UTF-8，长度按字节限制 23 | 客户端按字节截断/校验，禁止 `\0`；设备端二次校验 |
| 完整 M1 格式先行 | 格式 2 能存/传，但 Crypto-1 未实现，用户可能以为已完整模拟 | UI 明确标注「完整模拟待支持」；模拟按 UID 降级并在状态行提示 |
| 远程数据破坏标签结构 | 客户端可能发送错误 BCC/CC | 设备只采用页 4 起的内容，页 0~3 按 kind/uid 重建 |
| 建卡与本地读卡并发 | 两个流程都动卡库/模拟 | 全部在 app_task 串行；扫描中 COMMIT 只落盘不重启模拟 |
| GPIO18 被 NFC 占用 | 备选接线曾把 MOSI 放在 18 | NFC 固定 10/19/9/20；n16r8 的 GPIO18 只做返回键 |
| n4r2 无 NFC | 3 键老硬件没有卡包；若误把 `CONFIG_ARK_HAS_NFC` 打开会编译出不存在的硬件功能 | Kconfig 默认 n，仅 `sdkconfig_n16r8` 置 y；CI 同时构建两变体 |
| 字库/图标漏更新 | 新中文/新图标会导致方块或缺图 | 合入前跑 `gen_fonts.py --check` 与 `gen_ui_assets.py` |

### 仍需确认

1. 卡包是否需要在「模拟模式」下常驻模拟（离开卡包后继续模拟）？本方案选择「离开即停，仅休眠期间保持」。
2. 读卡成功后是否自动保存？本方案改为**不自动保存**：读到后只显示结果，按「复制卡」才写入卡包。
3. 本机是否也要删除卡片？BLE 端已支持删除（`0x66 op=2`）；本机三按键暂不提供，如需要建议加「管理模式」第三态。
4. BLE 建卡是否需要额外认证？本方案仅要求客户端二次确认，沿用现有 BLE 连接权限。
