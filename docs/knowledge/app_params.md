# FrameFilm App 参数与持久化设计

> 状态：**设计已定稿**（关键决策见 §10；仅余 `0x20`/`0x21` 处置待定，不阻塞主体实施）
> 关联文档：[app_layer.md](./app_layer.md)（app 框架）、[ble_commands.md](./ble_commands.md)、`docs/blecmd/blecmd_protocol.md`
> 关联固件：`firmware/frame_film`（单固件，三机型）

---

## 1. 背景

app 层（图片 / 模板 / 时钟 / 动图）落地后暴露出三个问题：

1. **service 侧的 `ServiceFilm_Def_t` 已成僵尸体**。
   `current_file_id` / `load_complete` / `play_mode` 三个字段在旧 `service_film` 时代是核心，app 层重构后 `play_mode` **已无任何消费者**，`current_file_id` / `load_complete` 也与 `service_file` 内部状态重复。
2. **app 无持久化能力**。
   当前 app 层状态（动图播放的文件/模式、图片当前下标）全部是 RAM 态，一进 deep sleep 全部丢失。设备"记忆上次看到哪张"这类最基本体验做不到。
3. **BLE 参数通道缺一层抽象**。
   现有参数命令（`0x20` 播放模式、`0x25~0x2A` 休眠、`0x4B` 切 app）是"一个命令一个参数"的散点式设计，命令号消耗快；且 service 层被迫理解业务语义（如 `service_wifi.c` 解析 `play_mode`），上下没有隔离。

本设计一次性解决这三件事：**参数下移到 app 层 → 由 app 自己持久化 → BLE 只做透明搬运**。

---

## 2. 设计原则

| 原则 | 含义 |
|---|---|
| **参数跟着 app 走** | 一个 app 的业务参数由该 app 自己定义、自己解释、自己持久化，service 层不出现任何 app 语义 |
| **框架管存储，app 管结构体** | app 只声明一个状态结构体（指针 + 大小 + 版本 + 默认值），NVS 读写全部由框架完成，app 侧零 NVS 代码 |
| **声明式接入** | 参数通道、状态结构体、默认值都写在 `app_entry_t` 里，与既有 `keys` / `events` 风格一致 |
| **上下隔离** | BLE 层只把 `(通道, 原始字节)` 丢上事件总线，不解析、不校验语义；app 只实现参数读写回调，不认识 BLE 报文 |
| **按 app 归属路由，不按"当前 app"** | 设置动图参数时设备可能正显示图片，参数事件必须能送达目标 app |

---

## 3. 数据流总览

```
手机 ──BLE──┐
            │  [0x55][CH][LEN][TLV...][SUM]   ← BLE 只认这层壳
            ▼
   service_ble.c
            │  sys_event_publish(SYS_EVT_BLE_APP_CMD, [CH][TLV...])
            ▼
   sys_event 总线（同步扇出）
            ▼
   app_manager ── 按 CH 查 owner app（注册表里谁的 param_ch 命中）
            │
            ├─ SET → owner->on_param_set(tlv, len)
            │           └─ app 改自己的结构体 + app_state_save() → 框架写 NVS
            │
            └─ GET → owner->on_param_get(buf, max)
                        └─ app_manager 拼 [0x55][CH][LEN][TLV...][SUM] 回发

   运行期（无手机参与）：
   app 自身 on_tick / on_boot 改自己的结构体 → app_state_save() → 框架写 NVS
```

---

## 4. 参数模型

### 4.1 删除 `ServiceFilm_Def_t`

`components/film_service/inc/service_param.h`：

```c
/* 整块删除 */
typedef struct {
    uint32_t current_file_id;
    uint8_t  load_complete;
    uint8_t  play_mode;
} ServiceFilm_Def_t;
```

`ServiceParam_Def_t` 同步去掉 `ServiceFilm_Def_t film;` 成员，并新增 `param_ver` 版本号：

```c
#define SERVICE_PARAM_VER  2   /* 1 = 含 film 成员的旧布局；2 = 移除 film + app 参数外置 */

#pragma pack(4)
typedef struct
{
    uint8_t  param_ver;        /* 新增：结构体版本，与 nvs 内不一致则重建默认值 */
    uint8_t  factory_flag;
    ServiceSleep_Def_t   sleep;
    ServiceNetwork_Def_t network;
    ServiceBle_Def_t     ble;
} ServiceParam_Def_t;
#pragma pack()
```

**版本校验**：`service_param_init()` 读出 blob 后，先比对 `param_ver == SERVICE_PARAM_VER`；不等（含长度不符、老固件遗留数据）→ 直接 `service_param_set_default()` + 保存。把原先"靠 `nvs_get_blob` 长度不符隐式自愈"变成**显式版本判断**。

**连带改动**：

| 位置 | 现状 | 改为 |
|---|---|---|
| `service_film.c` `film_display_event()` | 显示前后写 `g_service_param.film.*` + 两次 `service_param_save()` | 直接删除（`service_file` 内部已有等价状态） |
| `service_film_get_current_id()` | 返回 `g_service_param.film.current_file_id` | **删除**，调用方改用 `service_file_get_current_id()` |
| `service_film_get_load_complete()` | 返回 `g_service_param.film.load_complete` | **删除**，调用方改用 `service_file_get_load_complete()` |
| `app_image.c` | `service_film_get_load_complete()` | `service_file_get_load_complete()` |
| `service_ble.c` `0x08` FILE_DISPLAY_GET | `service_film_get_current_id()` | `service_file_get_current_id()` |
| `service_wifi.c` 心跳上报 | `g_service_param.film.current_file_id` | `service_file_get_current_id()` |
| `service_wifi.c` `set_config` 下发 | 写 `g_service_param.film.play_mode` | **移除**（该语义归 image app，详见 §4.2） |
| `service_param.c` `service_param_set_default()` | 3 行 film 默认值 | 删除 |

> **副作用：`service_film` 在两个调用方之外基本沦为纯渲染底座**，`MSG_FILM_DISPLAY` / `MSG_FILM_RENDER` 两种消息不变。这是符合 app_layer.md §13.1 既定方向的。

#### 旧 BLE 命令 `0x20` / `0x21`（CTRL_MODE / CTRL_MODE_GET）

按 AGENTS.md「BLE 命令值一旦定义不再变更」，**命令号保留不删**，标记为**废弃**：

- `0x20` 收到后不再写 `g_service_param`，改为忽略并打日志（方案 B 见 §10「待补充确认」）。
- `0x21` 回包值直接返回 `0xFF` 表示已废弃（方案 B 见 §10「待补充确认」）。

#### WiFi 心跳报文里的 `play_mode`

**决议：先移除，后续统一设计。**

- `service_wifi.c` 心跳上报 query string 中的 `play_mode=%d` **删除**。
- `service_wifi.c` `set_config` 下发里对 `g_service_param.film.play_mode` 的写入 **删除**。
- 服务端 / 小程序侧的对应解析一并下线（见 §7）。后续若要上报播放状态，走新参数通道 `0x46 APP_IMAGE_PARAM_GET` 主动查询，不再塞进心跳。
- **风险**：服务端若强依赖该字段会解析失败或写空。需与小程序 / 服务端同步发布。

---

### 4.2 图片 app（image）

```c
typedef struct {
    uint8_t  play_mode;      // 0=手动  1=自动
    uint16_t auto_interval;  // 自动切换间隔（分钟，1 ~ 120）
    uint32_t file_id;        // 当前播放的文件下标
} app_image_state_t;
```

**`play_mode == 自动` 的两种形态，由休眠开关 `g_service_param.sleep.sleep_mode` 决定**：

| `sleep_mode` | 自动形态 | 行为 |
|---|---|---|
| `0`（休眠关闭，设备常开） | **定时切换** | 运行期靠 `on_tick` 计时，每 `auto_interval` 分钟切下一张 |
| `1`（休眠开启） | **开机自动切换** | 每次芯片启动（冷启动 / 定时唤醒 / 按键唤醒）切下一张，之后进入正常休眠循环 |

> 这个二分法正好贴合硬件：休眠开启时设备靠 deep sleep + 唤醒周期性工作，"定时"天然由唤醒周期承担；休眠关闭时设备常开，才需要软件定时器。
>
> **注意**：休眠开启时定时唤醒的周期由 `sleep.sleep_time`（现有参数，单位分钟）决定，与 image 的 `auto_interval` 无关——后者只在休眠关闭时生效。

**"每次启动只切一次"的实现**：不能放在 `on_enter`（用户手动切回图片 app 也会触发）。新增 app 层内部事件 `APP_EVT_BOOT`，由 `app_manager` 在启动后**只投递一次**给首个 app，app 在 `on_event` 里处理：

```c
case APP_EVT_BOOT:
    if(m_image.play_mode == APP_IMAGE_PLAY_AUTO &&
       g_service_param.sleep.sleep_mode == 1) {   /* 休眠开启 → 开机自动切换 */
        image_show_next();
    }
    break;
```

**定时切换**（休眠关闭）：

```c
// entry.tick_ms = 1000，on_tick 内部早退，开销可忽略
case APP_EVT_TIMER:
    if(m_image.play_mode != APP_IMAGE_PLAY_AUTO) return;
    if(g_service_param.sleep.sleep_mode != 0) return;   /* 休眠开启走 boot 路径，不重复计时 */
    if(++m_elapsed_s >= (uint32_t)m_image.auto_interval * 60) { m_elapsed_s = 0; image_show_next(); }
    break;
```

**`file_id` 的维护**：
- 进入 app：`file_id >= count` 则回落 0，再 `service_film_display(file_id)`。
- 按键 UP/DOWN、`SYS_EVT_FILE_SAVED` 自动显示新图 → 同步更新 `file_id`。
- 每次变更调 `app_state_save()`（低频操作，直接写 NVS）。

---

### 4.3 模板 app（template）

原 `FILM_PLAY_MODE_WIFI`（网络拉取）语义并入模板 app。

```c
typedef struct {
    uint8_t  pull_enable;    // 网络拉取开关 0=关闭 1=开启
    uint16_t pull_interval;  // 定时拉取间隔（分钟，1 ~ 120）
} app_template_state_t;
```

**两套拉取逻辑（与 image 对称）**：

| `sleep_mode` | 形态 | 触发条件 |
|---|---|---|
| `1`（休眠开启） | **开机自动拉取** | 本次启动落在模板 app（即 `APP_EVT_BOOT` 时当前 app 为 template）且 `pull_enable == 1` → 触发一次 |
| `0`（休眠关闭） | **定时自动拉取** | `on_tick` 累计到 `pull_interval` 分钟 → 触发一次 |

**触发动作**：`service_wifi_download_start()`（使用已配置的 `film_api_url`）。

**WiFi 未就绪的处理**（替代旧逻辑的"死等 10s"）：

```
开机 → pull_enable && 未连接
     → 标记 pending，订阅 SYS_EVT_WIFI_STATE
     → 连上 → 触发拉取，清除 pending
     → 超时（如 30s，用 tick 计时）→ 放弃本次，交给心跳链路
```

> 旧实现是"阻塞轮询等 10s"，新实现改为事件驱动 + 超时兜底，不占用任务上下文。
>
> 模板内容由服务端推送（`download_film` 心跳指令或 `0x3C`），**不需要持久化"当前显示哪一帧"**——设备重进模板时按文件名找回（现有 `app_template.c` 已实现 `template_find_by_name()`）。

---

### 4.4 时钟 app（clock）

**无特殊参数**，不参与参数通道、不参与持久化。

若后续需要时区 / 24h 制等配置，按本设计的模式直接扩一个 `app_clock_state_t` + 一对通道即可，无需改框架。

---

### 4.5 动图 app（animation）

```c
typedef struct {
    uint8_t  play_mode;     // 0=单 film 循环  1=film 列表循环
    uint16_t frame_ms;      // 每帧间隔（毫秒，50 ~ 2000），即"播放速度"
    uint16_t loop_seconds;  // 循环间隔（秒，0 ~ 600）：一个 film 播完到播下一个之间的等待时间，0=不等待
    uint32_t file_id;       // 当前播放的文件下标
} app_anim_state_t;
```

| 播放模式 | 行为 |
|---|---|
| `单 film 循环` | 只播 `file_id` 指向的这一个 film，帧序列播完后等待 `loop_seconds` 秒，再从首帧重播（`loop_seconds=0` 则无间隔直接续播） |
| `film 列表循环` | 依次播放列表中每个 film，每个播完一整轮帧序列后等待 `loop_seconds` 秒，再切下一个；到列表末尾回到第 0 个 |

> **语义澄清**：`loop_seconds` 是**间隔等待时间**，不是"播放时长"。它不改变某个 film 播多久（播多久由帧数和 `frame_ms` 决定：`frames × frame_ms`），只决定两轮之间的停顿。

> **`frame_idx` 不持久化**。帧是 200ms 级的高频状态，逐帧写 NVS 会迅速耗尽擦写寿命。只持久化"哪个文件"，重进 app 时从首帧开始。

**落盘时机**（全部是低频事件）：
- `anim_step()` —— 切文件
- `anim_toggle_mode()` —— 切播放模式
- 参数经 BLE 设置后

**`frame_ms` 接入点**：现有 `entry.tick_ms = ANIM_FRAME_MS(200)` 是编译期常量。改为 `tick_ms = 50`（最小帧间隔），`on_tick` 内部按 `frame_ms` 累计分频：

```c
m_tick_acc += 50;
if(m_tick_acc < m_anim.frame_ms) return;
m_tick_acc = 0;
/* 推进帧 */
```

---

### 4.6 自动行为总表

| app | 参数 | 休眠**开启**（`sleep_mode=1`） | 休眠**关闭**（`sleep_mode=0`） |
|---|---|---|---|
| image | `play_mode=自动` | 每次开机切下一张 | 每 `auto_interval` 分钟切下一张 |
| image | `play_mode=手动` | — | — |
| template | `pull_enable=1` | 开机进入模板时拉取一次 | 每 `pull_interval` 分钟拉取一次 |
| template | `pull_enable=0` | — | — |
| clock | — | — | — |
| animation | — | 仅运行期循环（不参与开关机自动行为） | 同左 |

> 上表把"何时触发"与"是否当前 app"解耦：`APP_EVT_BOOT` 只发给首启 app，所以"开机落在模板才拉取"是天然满足的，不需要额外判断。

---

## 5. 持久化设计

### 5.1 存储布局

新增独立 namespace，与既有 `FRAMEFILM_NVS`（整包 `ServiceParam_Def_t`）隔离：

```c
// sys_cfg.h
#define SYS_M_NVS_APP_NAMESPACE   "FRAMEFILM_APP"   // 13 字符（NVS namespace 上限 15）
```

| key | 内容 |
|---|---|
| `"app0"` ~ `"app3"` | 每个 app 的状态 blob（按 `app_id_t` 数值下标） |
| `"cur_app"` | 框架当前 app id（1 字节，直接 `nvs_set_u8`） |

**不复用 `FRAMEFILM_NVS` 的理由**：那里是"单 key 整包覆写"模型，把 app 状态塞进去会让每次 app 状态更新都重写整个 `ServiceParam_Def_t`（含 64B SSID + 64B 密码 + 128B URL×2 + token，接近 500 字节），既慢又无谓消耗擦写寿命。

### 5.2 blob 格式

```
+--------+--------+--------+--------+------------------+
| magic  | size   | version| rsvd   | app 状态数据      |
| 2B LE  | 2B LE  | 1B     | 1B     | size 字节         |
+--------+--------+--------+--------+------------------+
```

```c
typedef struct __attribute__((packed)) {
    uint16_t magic;    /* 0xA55A，不匹配视为无数据 */
    uint16_t size;     /* 后续 app 数据有效字节数 */
    uint8_t  version;  /* app 状态结构体版本，字段变更时 +1 */
    uint8_t  rsvd;
} service_param_app_hdr_t;
```

**校验策略：任一不符即视为"无数据"，回落到默认值**。

| 情况 | 处理 |
|---|---|
| key 不存在 | 用 `state_default` 初始化 |
| `magic` 不匹配 | 同上 |
| `size != state_size` | 同上（结构体改版后旧数据自动作废） |
| `version != state_ver` | 同上 |

> app 状态是**非关键数据**，正确的失败姿态是"静默回到默认值"，而不是报错阻塞启动。所以不做迁移、不做兼容，靠 `state_ver` 一刀切。

### 5.3 service 侧接口（`service_param.h` / `.c`）

不新建文件，就放在 `service_param` 内（与既有 NVS 使用方同一处，便于审查 NVS 总量）。

```c
/* app_id 数值必须与 app_interface.h 的 app_id_t 对齐（service 层看不到 app_id_t） */
#define SERVICE_PARAM_APP_ID_IMAGE       (0)
#define SERVICE_PARAM_APP_ID_TEMPLATE    (1)
#define SERVICE_PARAM_APP_ID_CLOCK       (2)
#define SERVICE_PARAM_APP_ID_ANIMATION   (3)
#define SERVICE_PARAM_APP_NUM            (4)

int service_param_app_load(uint8_t app_id, void *buf, uint16_t size, uint8_t ver);
int service_param_app_save(uint8_t app_id, const void *buf, uint16_t size, uint8_t ver);
int service_param_app_erase(uint8_t app_id);
int service_param_app_current_get(void);
int service_param_app_current_set(uint8_t app_id);
void service_param_app_erase_all(void);
```

实现要点：

- `load()` 先读进临时栈缓冲校验头部，再 `memcpy` 到 app 结构体——避免校验失败时污染 app 状态。
- 内部共用 `app_nvs_open()` 助手（`nvs_open(SYS_M_NVS_APP_NAMESPACE, NVS_READWRITE, ...)`）。
- 失败一律返回负值并 `sys_logw`，**不 `SYS_ERROR_CHECK` 终止**——app 状态丢失不该让设备起不来。
- `service_param.h` 需补 `#include <stdint.h>`（当前依赖调用方先 include，是隐患）。

**出厂清空**：

```c
void service_param_reset(void)
{
    service_param_set_default();
    nvs_param_save();
    service_param_app_erase_all();   /* 新增 */
}
```

### 5.4 app 框架接入

`app_interface.h` 的 `app_entry_t` 追加：

```c
typedef struct {
    /* ... 既有字段 ... */

    void    *state;              /* 指向 app 状态结构体；NULL = 不持久化 */
    uint16_t state_size;         /* 结构体大小；0 = 不持久化 */
    uint8_t  state_ver;          /* 结构体版本 */
    const void *state_default;   /* load 失败时的默认值（同 state_size） */

    uint8_t param_ch;            /* 参数通道（0 = 无参数通道）；查询通道为 param_ch + 1 */

    void (*on_param_set)(const uint8_t *tlv, uint8_t len);  /* BLE 参数设置（可 NULL） */
    uint8_t (*on_param_get)(uint8_t *out, uint8_t max);     /* BLE 参数查询，返回写入字节数 */
} app_entry_t;
```

框架提供（`app_manager.h`）：

```c
int app_state_save(void);   /* 保存当前 app 状态到 NVS（无 state 时为 no-op） */
int app_state_load(void);   /* 从 NVS 载入当前 app 状态，失败则套用 state_default */
```

app 侧用法（以动图为例）：

```c
static app_anim_state_t m_anim;
static const app_anim_state_t m_anim_default = {
    .play_mode = APP_ANIM_PLAY_SINGLE, .frame_ms = 200, .loop_seconds = 0, .file_id = 0,
};

const app_entry_t g_app_animation_entry = {
    /* ... */
    .state = &m_anim, .state_size = sizeof(m_anim), .state_ver = 1,
    .state_default = &m_anim_default,
    .param_ch = BLE_FILM_TRANS_CH_APP_ANIM_PARAM,   /* 0x49 */
    .on_param_set = app_animation_param_set,
    .on_param_get = app_animation_param_get,
};
```

**app 完全看不到 NVS**：改完自己的结构体，调一次 `app_state_save()` 即可。

### 5.5 框架钩子位置

`app_manager.c` 的 `app_do_switch()`：

```c
const app_entry_t *old = m_app_registry[m_current_app];
if(old && old->on_exit) { old->on_exit(); }
app_state_save();                 /* ① 切出：兜底保存（幂等，无变化也是几次 NVS 写） */

m_current_app = id;
m_app_running = 0;
m_tick_acc_ms = 0;

const app_entry_t *app = m_app_registry[id];
if(app && app->data_dir != NULL) {
    service_file_set_dir_sync(app->data_dir);
}
app_state_load();                 /* ② 切入：先载入状态，再 on_enter */
app_ensure_running();
service_param_app_current_set((uint8_t)id);   /* ③ 记录当前 app（供下次启动恢复） */
```

`app_init.c` 启动恢复：

```c
app_id_t last = (app_id_t)service_param_app_current_get();
if(last < 0 || last >= APP_ID_MAX || !app_is_registered(last)) {
    last = APP_ID_IMAGE;   /* 简易模式裁剪掉的 app、或首次开机 → 回落图片 */
}
app_manager_switch(last);
app_manager_notify_boot();   /* 投递一次 APP_EVT_BOOT 给首个 app */
```

> `app_is_registered()` 目前是 `app_manager.c` 的内部静态函数（L168），需**对外暴露一个公开包装**（如 `app_manager_is_registered()`）供 `app_init.c` 使用。

### 5.6 写入时机

| 数据 | 时机 | 理由 |
|---|---|---|
| 参数类（play_mode / interval / speed / 开关） | BLE 设置后**立即**写 | 低频、用户显式操作，写完立即生效不怕断电 |
| 进度类（image `file_id`） | 变更时写 | 按键 / 自动切换都是低频（分钟级） |
| 进度类（anim `file_id`） | `anim_step()` 切文件时写 | 切文件是低频事件 |
| anim `frame_idx` | **不写** | 200ms 级高频，写了会烧掉 NVS |
| 结构体整体 | app 切出时兜底写一次 | 覆盖 app 内部未显式保存的改动 |

> **不做"休眠前落盘"钩子**。因为进度类数据本来就是"变更即写"，不存在"还差最后一次没写"的窗口；再加一个 service→app 的反向回调只会引入循环依赖和新的失败模式。

---

## 6. BLE 参数通道（上下隔离）

### 6.1 分层职责

| 层 | 职责 | 明确不做 |
|---|---|---|
| `service_ble.c` | 收包 → 拼 `[CH][TLV...]` → `sys_event_publish(SYS_EVT_BLE_APP_CMD)`；GET 时负责把 app_manager 给的 payload 拼成完整帧回发 | 不解析 TLV、不认识参数名、不知道哪个 app |
| `app_manager.c` | 按 `CH` 查 owner app → 调 `on_param_set` / `on_param_get`；拼回包 → 调 `service_ble_send_resp()` | 不解释参数语义 |
| 各 app | 解析自己那张 TAG 表，改自己的结构体，写 NVS | 不碰 BLE 报文头、不碰校验和 |

**复用现有事件 `SYS_EVT_BLE_APP_CMD`（`0x0303`）**，payload 布局不变：`[0]=CH, [1..]=数据`。零新增事件 ID。

> 现有 `app_manager.c#L297-304` 已经在用这个事件处理 `0x4B`（切换 app）——本设计只是把"参数通道"也接进同一条通路，按 CH 分流。

### 6.2 新增命令表

从 `0x45` 起（`0x43`/`0x44` 归 Dock 底座的键盘键值通道；APP_SWITCH / APP_CURRENT_GET 排在参数通道之后的 `0x4B`/`0x4C`）：

| 通道 | 名称 | 方向 | 归属 |
|---|---|---|---|
| `0x45` | `APP_IMAGE_PARAM` | 下行 | 图片 |
| `0x46` | `APP_IMAGE_PARAM_GET` | 上行 | 图片 |
| `0x47` | `APP_TEMPLATE_PARAM` | 下行 | 模板 |
| `0x48` | `APP_TEMPLATE_PARAM_GET` | 上行 | 模板 |
| `0x49` | `APP_ANIM_PARAM` | 下行 | 动图 |
| `0x4A` | `APP_ANIM_PARAM_GET` | 上行 | 动图 |

时钟无参数，**不占通道**。约定：`param_ch` 为设置通道，查询通道 = `param_ch + 1`（沿用全项目 `X` / `X_GET` 成对惯例）。

### 6.3 报文格式：TLV

```
+--------+--------+--------+---------------------------+--------+
| 0x55   |  CH    |  LEN   |  TLV 列表                 |  SUM   |
+--------+--------+--------+---------------------------+--------+
                             [TAG(1B)][LEN(1B)][VALUE(LEN B)] ...
```

**为什么用 TLV 而不是定长结构体**：

1. **增量设置**：手机只想改一个字段时，只发一个 TLV；定长结构体必须回填全部字段（手机要先 GET 再 SET，两次往返）。
2. **可扩展**：app 后续加参数只加一个 TAG，不改变命令号、不影响老客户端（不认识的 TAG 直接跳过）。
3. **上下隔离友好**：BLE 层对 payload 零解释，TLV 是 app 自己的约定。

**解析规则（app 侧统一约定）**：

- 遇到不认识的 TAG → 跳过（`LEN` 给了长度，可安全跳过）。
- `LEN` 与 TAG 定义不符 → 跳过该项并 `sys_logw`，**不整包丢弃**（避免一个坏字段废掉整包）。
- 所有 TAG 都没匹配上 → 返回错误回包。

### 6.4 TAG 定义

**图片 app（`0x45` / `0x46`）**

| TAG | 名称 | LEN | 取值 | 说明 |
|---|---|---|---|---|
| `0x01` | `play_mode` | 1 | 0=手动 1=自动 | 自动形态由 `sleep_mode` 决定 |
| `0x02` | `auto_interval` | 2 | 1 ~ 120（分钟，大端） | 仅休眠关闭时生效 |
| `0x03` | `file_id` | 4 | 0 ~ N-1（大端） | 设置后立即跳转显示 |

**模板 app（`0x47` / `0x48`）**

| TAG | 名称 | LEN | 取值 | 说明 |
|---|---|---|---|---|
| `0x01` | `pull_enable` | 1 | 0/1 | 网络拉取开关 |
| `0x02` | `pull_interval` | 2 | 1 ~ 120（分钟，大端） | 仅休眠关闭时生效 |

**动图 app（`0x49` / `0x4A`）**

| TAG | 名称 | LEN | 取值 | 说明 |
|---|---|---|---|---|
| `0x01` | `play_mode` | 1 | 0=单 film 循环 1=列表循环 | |
| `0x02` | `frame_ms` | 2 | 50 ~ 2000（毫秒，大端） | 播放速度 |
| `0x03` | `loop_seconds` | 2 | 0 ~ 600（秒，大端） | 循环间隔：一个 film 播完到播下一个的等待时间，0=不等待 |
| `0x04` | `file_id` | 4 | 0 ~ N-1（大端） | |

> 多字节整数沿用项目约定 **Big-Endian**（见 `blecmd_protocol.md` §4.1）。

### 6.5 路由：按 app 归属，不按当前 app

`app_manager` 在注册时构建一张反查表：

```c
static const app_entry_t *m_param_owner[256];   /* param_ch → app（或 param_ch+1 → app） */
```

收到 `SYS_EVT_BLE_APP_CMD`：

```c
uint8_t ch = e->payload[0];
const app_entry_t *owner = m_param_owner[ch];
if(owner == NULL) { /* 非参数通道，走既有逻辑（0x4B 切换等） */ }

if(ch == owner->param_ch) {                    /* SET */
    if(owner->on_param_set) owner->on_param_set(&e->payload[1], e->len - 1);
} else {                                       /* GET */
    uint8_t out[64];
    uint8_t n = owner->on_param_get ? owner->on_param_get(out, sizeof(out)) : 0;
    service_ble_send_resp(ch, out, n);
}
```

**关键**：目标 app **不需要是当前 app**。手机可以在显示图片时预设动图参数，事件照样送达。

> 安全性：`on_param_set` 在 app 任务上下文执行（事件经总线 → app 队列）。若目标 app 未 `on_enter`，其 `static` 状态变量仍是有效的全局对象，直接改结构体是安全的；但 app 的实现里**不应在参数回调中刷屏**（未 enter 时屏幕可能属于别的 app）。需要立即生效的字段（如 image 的 `file_id`）由 app 自行判断"是否是当前 app"再决定是否刷新显示。

### 6.6 回包 helper

新增（`service_ble.h` / `.c`），消除各命令重复手拼帧头/校验和：

```c
/**
 * @brief 按 BLE 帧格式回发一包数据（内部拼 0x55 / CH / LEN / 校验和）
 */
extern void service_ble_send_resp(uint8_t ch, const uint8_t *data, uint8_t len);
```

> 现状是每个 GET 命令都内联 `resp_buf[...] = ...; ble_checksum(...)`，本次新增 3 个多字段 GET 命令，抽一个 helper 是必要的（不是过度设计）。

**未注册 app 的 GET**：回 `LEN=0` 的空包（表示"该 app 不存在/无参数"）。

**SET 是否回包**：沿用现有下行命令惯例——**不回**。手机若要确认，用对应的 GET 通道读回。

### 6.7 上下隔离边界（自检清单）

- [ ] `service_ble.c` 中 `play_mode` / `interval` / `speed` 等业务词汇**零出现**
- [ ] `service_wifi.c` 中不再出现 `g_service_param.film.*`
- [ ] 新增 app 时**不需要**改 `service_ble.c`（只需在 `app_entry_t` 里填 `param_ch` + 两个回调）

---

## 7. 跨端一致性

按 AGENTS.md「修改 BLE 命令常量时三端必须同时更新」：

| 内容 | C 固件 | 小程序 | Web |
|---|---|---|---|
| 新增参数通道 `0x45~0x4A` | `service_ble.h` | `utils/ble-utils.js` | `js/frame.js` |
| `0x20`/`0x21` 废弃 | `service_ble.c` 标注 | 标注废弃 | 标注废弃 |
| 心跳报文 `play_mode` 移除 | `service_wifi.c` 删除上报/下发 | 心跳解析处同步下线 | — |
| `param_ver` 版本号 | `service_param.h` | 无（固件内部） | 无 |
| 协议文档 | `docs/blecmd/blecmd_protocol.md` 新增 §3.7 / §4.8 | — | — |

---

## 8. 实施步骤

按依赖顺序，每步可独立静态检查：

1. **`sys_cfg.h`**：新增 `SYS_M_NVS_APP_NAMESPACE`。
2. **`service_param`**：新增 `service_param_app_*` 接口 + `service_param_reset()` 追加清空；补 `#include <stdint.h>`。
3. **`service_param.h/.c` + 连带**：删除 `ServiceFilm_Def_t`、新增 `param_ver` 与版本校验，改 `service_film` / `service_wifi`（含心跳 `play_mode` 移除）/ `service_ble` 的引用（§4.1 表）。
4. **`app_interface.h`**：`app_entry_t` 追加 `state` / `state_size` / `state_ver` / `state_default` / `param_ch` / `on_param_set` / `on_param_get`；`app_evt_type_t` 追加 `APP_EVT_BOOT`。
5. **`app_manager`**：`app_state_save()` / `app_state_load()` / `app_manager_notify_boot()` / `app_manager_is_registered()`；`app_do_switch()` 三处钩子；`m_param_owner` 反查表 + CH 分流。
6. **`service_ble`**：`service_ble_send_resp()`；`0x45~0x4A` 通道走 `SYS_EVT_BLE_APP_CMD`；`0x20`/`0x21` 废弃处理。
7. **`app_init.c`**：读 last app + 注册校验回落 + `notify_boot()`。
8. **四个 app**：状态结构体 + 默认值 + 参数回调 + 自动行为（image / template / animation）；clock 补空实现。
9. **协议文档 + 三端常量**：`blecmd_protocol.md`、`ble-utils.js`、`frame.js`。

---

## 9. 风险与边界

| 风险 | 处置 |
|---|---|
| **改 `ServiceParam_Def_t` 布局导致老固件数据失效** | 现有 `nvs_get_blob` 长度不符 → 返回错误 → `factory_flag != 0x22` → 走 `service_param_set_default()` 重建，可自愈。**建议同时加 `param_ver` 字段做显式版本校验**（见 §10.6） |
| **NVS 擦写寿命** | 参数/进度类均为低频写入（用户操作级 / 分钟级）；高频的 `frame_idx` 明确排除 |
| **app 切出时兜底写 NVS 成热点** | app 切换是用户按键/蓝牙操作，频率极低；且 NVS 有磨损均衡。即便一天切 100 次，也在寿命范围内 |
| **参数回调在非当前 app 上执行** | 只改结构体 + 写 NVS，不刷屏；需要刷屏的字段由 app 自行判断当前 app 后处理 |
| **简易模式裁剪掉 clock / animation** | `m_param_owner` 只在注册时构建，被裁剪的 app 不占通道；GET 回空包；启动恢复时靠 `app_manager_is_registered()` 回落 IMAGE |
| **TLV 增量设置的原子性** | 一包内多个 TAG 逐个生效，不做事务。某字段非法只跳过该字段，其余照常写入（app 状态非关键数据，允许部分更新） |
| **`SYS_EVENT_PAYLOAD_MAX` 仅 16 字节** | 一包 TLV 上限 15 字节。够本设计的全部场景（最坏：image 3 个字段 = 6+2+1+1+1+1+1+1 = 14 字节）。若未来单包超限，需评估是否提高该宏（会增大 app 队列项尺寸） |

---

## 10. 设计决议

| # | 事项 | 决议 |
|---|---|---|
| 1 | 定时间隔的单位与范围 | **分钟，1 ~ 120**。image 的 `auto_interval` 与 template 的 `pull_interval` 同规格 |
| 2 | 动图 `loop_seconds` 语义 | **循环间隔**：一个 film 播完到播下一个之间的等待时间，`0`=不等待。不是"播放时长"；范围 0 ~ 600 秒 |
| 3 | TLV vs 定长结构体 | **TLV**（支持增量设置、易扩展、上下隔离友好） |
| 4 | WiFi 心跳 `play_mode` | **先移除**，后续统一设计；固件侧同时删除心跳上报与 `set_config` 下发 |
| 5 | `ServiceParam_Def_t` 加版本号 | **加** `param_ver`，`init` 时显式比对，不等则重建默认值 |

### 待补充确认

- **`0x20` / `0x21` 旧命令处置**（§4.1 末）：
  - 方案 A（默认）：**保留命令号、标记废弃**——`0x20` 忽略并打日志，`0x21` 回 `0xFF`。
  - 方案 B：**作为图片 app 参数通道的别名**——`0x20` 直接转发为 image 的 `play_mode` 设置，`0x21` 读回 image 的 `play_mode`，老客户端零改动。
  - 影响：方案 B 需在 `service_ble.c` 保留对 `play_mode` 语义的引用，与"上下隔离"原则（§6.7）冲突；若选 B 需在自检清单里显式豁免。
