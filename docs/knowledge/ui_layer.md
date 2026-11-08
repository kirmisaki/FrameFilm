# FrameFilm UI 层设计（LVGL 接入）

> 状态：**设计中**（本文档用于累积讨论结论，随迭代更新）
> 目标：在 app 层内引入**第二个显示层**（LVGL UI 层），与既有"直接显示层"并存；app 声明自己运行在哪一层。
> 关联文档：[app_layer.md](./app_layer.md)（app 框架）、[app_params.md](./app_params.md)（参数与持久化）
> 关联固件：`firmware/frame_film_ark`（通行证版，**单机型**；**UI 层 / app 框架仅此一处**）；`firmware/frame_film`（冰箱贴三机型，经典固件，**没有 `film_ui` / `film_app` 组件**）；LVGL 9.6.0

---

## 1. 背景与目标

现有 app 层只有一条显示路径：app 自己把整帧数据交给 `hal_epd_display_film()` / `hal_epd_display_mono()`。
时钟 app 就是手写 5×7 点阵逐像素绘制（[app_clock.c](../../firmware/frame_film_ark/components/film_app/apps/clock/app_clock.c)），能跑但难以扩展。

本次引入 **LVGL** 作为"UI 框架层"，让这类需要排版/文本/控件的 app 用声明式方式构建页面：

| 显示层 | 承载 app | 渲染方式 | 面板能力 |
|---|---|---|---|
| `APP_LAYER_DIRECT`（直接显示层） | 图片 / 模板 / 动图 | app 自绘整帧 → `hal_epd_display_film()` | 4bpp 彩色、8bpp 彩色、mono |
| `APP_LAYER_UI`（UI 框架层） | 时钟（及未来 UI app） | LVGL 渲染 → 1bpp 缓冲 → `hal_epd_display_mono()` | 仅 mono |

**核心约束**：两层**互斥**（不能同时驱动面板）。UI 层依赖 mono 能力，只在 3.7"（面板 ID `0x02`）可用；
进入 DIRECT 层 app 时 UI 层必须挂起，把面板让出去。

---

## 2. 硬约束：刷新率

这一节决定 UI 设计策略，必须先讲清。

### 2.1 mono 单帧的面板开销是"固定全屏"的

`epd_spectra_mono_write()`（[hal_epd_370.c#L1342-L1362](../../firmware/frame_film_ark/components/film_hal/src/hal_epd_370.c#L1342-L1362)）对每个像素编码
`(上一帧, 当前帧)` 的 4bit 跳变码，循环 `EPD_HEIGHT(480)` 行、每行 `EPD_INPUT_LINE(360)` 字节：

```
SPI 单帧数据量 = 480 × 360 = 172,800 字节
```

注意这是 **1bpp 源位图（43,200 字节）的 4 倍**——编码后每个字节只用到 `bit5,4` 与 `bit1,0`，
即有效信息仅 2bit/像素，另外 2bit 是固定填充，但**必须发**（硬件约定）。

| SPI 频率 | 传输耗时 | 说明 |
|---|---|---|
| 10 MHz | **138 ms** | 改造前，是刷新率第一瓶颈 |
| 20 MHz | 69 ms | |
| **40 MHz** | **35 ms** | 本次采用（若花屏回退 20MHz）|

单帧总耗时 = SPI 传输 + PON + REF（波形）+ POF。其中 **REF 是整屏扫描，与"改了多少像素"无关**。

**历史实测记录（3.7" Pro，SPI 40MHz；不是当前诊断固件的最新结果）**：同一 mono 会话内连续两帧（旧封面菜单翻页时测得）的间隔 **约 410 ms**，
即实际约 **2.4 Hz**；而彩色路径切入后的**首帧 mono 为 ~3350 ms**（含 spectra 会话建立 + 全屏 clear）。

> 结论：40MHz 的 SPI 提速**几乎没改变上限**——410ms 里 SPI 传输只占约 35ms，
> 其余全是波形时间。**不要指望靠提 SPI 频率拿到高刷新率**。

当前Ark可通过默认关闭的 `CONFIG_ARK_EPD_PROFILE` 分开记录INIT、LUT、RAM、REF、PON、POF。REF等阶段包含命令、BUSY与固定延时，不等于纯粒子运动时间。冷启动、彩色转黑白、连续会话分别统计；见 [同屏测量说明](../film/epd037a02.md)。

### 2.2 由此得出的三条 UI 铁律

1. **每帧代价恒定，与改动面积无关**。LVGL 的脏矩形优化只省 CPU，**不省面板开销**。
2. **禁止 LVGL 动画 / 滚动 / 过渡**。每个动画帧 = 一次全屏刷新，会直接打满面板带宽。
3. **静止的 UI 必须零刷新**。LVGL 只在 dirty 时渲染，天然满足；不要在 `on_tick` 里无条件 `set_text()`。

> 结论：UI 层按"**低频信息更新**"设计（时钟 1 次/分钟），而不是"实时动画"。
> `LV_DEF_REFR_PERIOD` 设为 `100`（内部 10Hz 节拍），上屏由阻塞式 flush 自然限速。

---

## 3. 分层架构

```
              app_manager（app_task：业务 / 事件 / 路由；绝不直接调用 lv_*）
                        │  按 app_entry.layer 分流
       ┌────────────────┴──────────────────┐
       ▼                                   ▼
 APP_LAYER_DIRECT                   APP_LAYER_UI
 图片 / 模板 / 动图                 时钟 / 主菜单 / 系统设置
       │                                   │
       │ app 自绘整帧                       ▼
       │ hal_epd_display_film()      film_ui：ui_core（独占 LVGL 的 ui_task）
       │ （4bpp / 8bpp 彩色）          ├ lv_timer_handler()
       │                              ├ flush_cb → 转置 → 1bpp 缓冲
       │                              └ hal_epd_display_mono()
       └────────────────┬──────────────────┘
                        ▼
                    film_hal（mono / 4bpp / 8bpp）
```

依赖方向（单向，无环）：

```
film_app ──→ film_ui ──→ lvgl
   │            │
   └────────────┴──→ film_hal ──→ film_sys
```

`film_ui` **不依赖** `film_app`：它只持有函数指针（`app_ui_ops_t`），因此不会形成环。

---

## 4. 组件与文件布局

```
components/film_ui/                  ← 新增组件（UI 框架层实现）
  inc/
    ui_core.h      ── lvgl-free 接口：前向声明 ops + ui_core_*（供 film_app 用）
    ui_ops.h       ── 唯一 include lvgl.h 的头：app_ui_ops_t 定义（供 UI app 实现）
    ui_conf.h      ── UI 层常量：逻辑分辨率、缓冲大小、主题、刷新周期
  src/
    ui_core.c      ── lv_init / display / tick / 任务 / 命令队列 / 状态机
    ui_display.c   ── flush 回调 + I1→mono 转置 + 极性处理
  CMakeLists.txt
```

`film_app` 侧新增/改动：

```
components/film_app/
  inc/app_interface.h   ← 改：加 layer / ui_ops（含 ui_core.h，仍不含 lvgl）
  src/app_manager.c     ← 改：按 layer 分流、主菜单接管/长按退出、跨线程消息
  src/app_boot.c        ← 新：开机画面（首帧 mono，顺带整屏清场）
  src/app_menu.c        ← 新：主菜单（轮播 + 指示点 + 层级面板）
  src/app_settings.c    ← 新：系统设置（设备信息 + 参数，写回经 post_ui_msg）
  src/app_clock.c       ← 改：从"自绘 mono 帧"改为"实现 app_ui_ops_t"
  CMakeLists.txt        ← 改：REQUIRES 增加 film_ui
```

**为什么单独开组件而不是塞进 film_app**：

1. LVGL 是重依赖，隔离后只有 `film_ui` 认识它；`film_app` 的头文件与业务代码保持干净。
2. UI 层有自己的任务、显存、生命周期，是独立子系统。`app_manager.c` 已 770 行，不宜再膨胀。
3. 便于编译期整体裁剪（见 §9.3）。

---

## 5. 接口设计

### 5.1 app 侧：声明运行在哪一层

[app_interface.h](../../firmware/frame_film_ark/components/film_app/inc/app_interface.h) 追加两个字段，
**不引入 lvgl 头**（用前向声明维持解耦）：

```c
#include "ui_core.h"        /* 只拿到 app_ui_ops_t 前向声明与 ui_core_* 声明 */

typedef enum {
    APP_LAYER_DIRECT = 0,   // 直接显示层：app 自绘整帧（默认值，可省略不写）
    APP_LAYER_UI,           // UI 框架层：由 ui_core 承载 LVGL 页面
} app_layer_t;

typedef struct {
    /* ... 既有字段 ... */
    app_layer_t layer;                  // 该 app 跑在哪一层
    const app_ui_ops_t *ui_ops;         // layer == APP_LAYER_UI 时必填
    app_sleep_face_t sleep_face;        // 睡后画休眠卡（默认）/ 留当前画面
} app_entry_t;
```

字段语义约定：

| 字段 | `APP_LAYER_UI` 时 | `APP_LAYER_DIRECT` 时 |
|---|---|---|
| `ui_ops` | 必填 | 忽略 |
| `on_enter` / `on_exit` | **不调用**（由 `ui_ops->create/destroy` 取代） | 照常 |
| `on_tick` / `tick_ms` | **不调用**（页面内部用 LVGL 定时器，见 §7.3） | 照常 |
| `on_event` | 仍会调用，但**运行在 app_task**，禁止碰 `lv_*`；需用 `ui_core_post()` 投递 | 照常 |
| `data_dir` / 状态持久化 / 参数通道 | 照常可用 | 照常可用 |
| `sleep_face` | 两层一致：`APP_SLEEP_FACE_CARD`（默认，画休眠卡）/ `APP_SLEEP_FACE_KEEP`（留当前画面，图片/动图用） | 同左 |

### 5.2 UI app 侧：页面契约

```c
/* film_ui/inc/ui_ops.h —— 需要 lvgl.h */
#include "lvgl.h"
#include "hal_input.h"

typedef struct app_ui_ops_t {
    /* 建页面：root 由 ui_core 创建的 screen，app 在其下挂控件。在 ui_task 上下文执行 */
    void (*create)(lv_obj_t *root);

    /* 可选：退出前的额外清理（控件由 ui_core 统一删除） */
    void (*destroy)(void);

    /* 可选：ui_task 上下文的跨线程消息入口（配合 ui_core_post 使用） */
    void (*on_msg)(uint32_t cmd, const void *data, uint8_t len);

    /* 可选：按键（UI 层独占输入时使用） */
    void (*on_key)(input_press_type_t key);
} app_ui_ops_t;
```

### 5.3 ui_core 对外接口

```c
/* film_ui/inc/ui_core.h —— lvgl-free */
typedef struct app_ui_ops_t app_ui_ops_t;   /* 前向声明 */

int  ui_core_init(void);                    /* lv_init + display + 队列 + ui_task */
int  ui_core_is_ready(void);                /* 当前屏是否支持 UI 层（mono 能力） */

void ui_core_page_enter(uint8_t app_id, const app_ui_ops_t *ops);   /* 建页面 */
void ui_core_page_exit(void);                                       /* 删页面 */

void ui_core_pause(void);    /* 暂停输出（直绘场景临时占屏）；保留页面 */
void ui_core_resume(void);   /* 恢复输出，并强制整屏重绘一次 */

int  ui_core_post(uint32_t cmd, const void *data, uint8_t len);     /* app_task → ui_task */
```

`ui_core_*` 全部**线程安全**（内部只做队列投递或状态标记，不直接操作 LVGL），
因此 `app_manager`（app_task）可自由调用；真正的 `lv_*` 只发生在 `ui_task`。

---

## 6. 显示链路：I1 → 转置 → mono（零格式转换）

这是"优雅接入"的核心：**让 LVGL 直接渲染成 EPD 需要的 1bpp 格式**。

```
LVGL 逻辑画布 480×720（竖屏）
  └ 显示缓冲 = 8 B 调色板 + I1 数据 480×720/8 = 43,200 B   ← LVGL 直接渲染目标
        │ flush_cb：跳过调色板前缀 → 一次 90° 转置
        ▼
  mono 位图 720×480/8 = 43,200 B      ← 直接喂给 hal_epd_display_mono()
```

**关键配置**：

| 项 | 值 | 理由 |
|---|---|---|
| `LV_COLOR_FORMAT_I1` | 启用 | 1bpp 索引色，显存 43,200 B（RGB565 要 675 KB）|
| `LV_DRAW_SW_SUPPORT_I1` | 已默认为 `y` | LVGL 有专门的 I1 混色实现 `lv_draw_sw_blend_to_i1.c` |
| `LV_DISPLAY_RENDER_MODE_FULL` | 使用 | 整屏单缓冲；`flush_cb` 一次拿到整屏，与 EPD 的整帧推送模型天然吻合 |
| 缓冲大小 | **43,208 B** = 8（调色板）+ 43,200 | 分配时**必须多这 8 字节**，否则 LVGL 会写越界 |
| 调色板 | 索引 0 = 黑、1 = 白 | 每次 flush 写一次，保证索引色语义确定 |

```c
/* ui_display.c 核心流程 */
static void ui_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    /* 索引色格式：px_map 指向缓冲区开头，前 8 字节是 LVGL 的调色板 */
    if(lv_display_flush_is_last(disp))
    {
        ui_i1_to_mono(px_map + UI_I1_PALETTE_BYTES, m_mono_frame);  /* 跳过调色板再转置 */
        hal_epd_display_mono(m_mono_frame);   /* 阻塞式：约 35ms @40MHz + 波形时间 */
    }
    lv_display_flush_ready(disp);             /* 必须在推送之后 */
}
```

> **`px_map` 前面有调色板 —— 这是踩过的坑，务必记住。**
> LVGL 把索引色的调色板**存在缓冲区开头**（[lv_draw_buf.c](../../firmware/frame_film/managed_components/lvgl__lvgl/src/draw/lv_draw_buf.c) 里的 `/*Skip palette*/`），
> 官方 SDL 驱动也是 `px_map += LV_COLOR_INDEXED_PALETTE_SIZE(I1) * 4` 之后才取像素。
> I1 有 2 个索引 → **8 字节**前缀。
>
> 漏跳这 8 字节会同时产生**两个**看似无关的现象：
> ① 8 字节 = 64 个 I1 像素 → 整幅画面**横移 64px 并环绕**（每行最右侧 64px 绕到最左）；
> ② 8 不是行跨度（480/8 = 60 字节）的整数倍 → 绕回的那 64px **跨了一行**，于是它相对主体**再差 1px 纵向**。
>
> 当时误判成"控制器 DTM1 行原点偏移"，加了两个补偿常量去凑（已删除，见 ui_conf.h 注记）。
> 判别捷径：**彩色图片正常、只有 UI 偏** → 说明驱动/面板都对，问题在 UI 层的缓冲读法。

> `hal_epd_display_mono()` 是阻塞的，且 `flush_ready()` 在其后 → LVGL **不会堆积帧**，
> 自动形成"面板能多快就多快"的节流。这正是我们要的行为，无需额外丢帧逻辑。

### 6.1 转置

竖屏逻辑坐标 `(px∈[0,480), py∈[0,720))` → 横屏面板 `(lx∈[0,720), ly∈[0,480))`。

采用**逐像素**实现（345,600 像素，240MHz 下约 1~3ms；时钟 1 次/分钟 → 完全无感），
不做位级快速转置，以降低出错概率。

旋转方向由**装配方向**决定，提供单一开关（`ui_conf.h`），首次点亮时校准：

```c
#define UI_ROTATE_90_CW     (1)   /* 1=顺时针 90°，0=逆时针 90° */
```

### 6.2 位序与极性

链路共三处极性/位序约定，必须对齐：

| 环节 | 约定 | 来源 |
|---|---|---|
| LVGL I1 缓冲 | 每字节 8 像素，MSB 在前；**位 `1` = 亮（白）、`0` = 暗（黑）** | `lv_draw_sw_blend_to_i1.c`：`src_color = lv_color_luminance(颜色) / (I1_LUM_THRESHOLD + 1)`，亮色置位（阈值默认 127） |
| mono 位图（`.film` Format `0x01`） | 每字节 8 像素，**MSB 在前**；`1`=黑、`0`=白 | 驱动 `film_parse_header` / film.md §10.3 |
| 面板 mono 跳变码 | 码位 `1`=白、`0`=黑 | 驱动 `MONO_CODE_BIT_WHITE` |

**关键结论：LVGL I1 与 `.film` 的位语义恰好相反**（前者白=1，后者黑=1），
故 `ui_i1_to_mono()` 必须做一次取反。两处极性都用常量集中控制：

```c
#define UI_I1_BIT_BLACK     (0)   /* I1 缓冲中"黑"的位值（源码推导，见上表） */
#define UI_MONO_BIT_BLACK   (1)   /* mono 位图中"黑"的位值（.film 约定） */
```

> 职责边界：UI 层只负责**产出符合 `.film` 约定的 mono 位图**（`1`=黑）；
> "mono 位图 → 面板码位"的极性由驱动侧 `MONO_CODE_BIT_WHITE` 处理，两者不重叠。

---

## 7. 任务与线程模型

### 7.1 为什么必须是独立任务

LVGL 当前配置为 `CONFIG_LV_USE_OS=0`（**不含内置线程支持**），`lv_*` 调用必须**串行且同一任务**。
而 `flush_cb` 里的 EPD 推送要阻塞 35ms+（含波形可能 100ms+），若放在 `app_task` 会
直接拖垮按键响应与 BLE 事件处理。

→ 结论：**独占一个 `ui_task`**，所有 `lv_*` 只在其中调用。

### 7.2 ui_task

```c
#define UI_TASK_PRIO    (4)      /* 低于 app_task(5)：UI 渲染让位于输入/事件 */
#define UI_TASK_STACK   (8192)   /* LVGL 控件树 + 绘图，4096 不够 */
#define UI_WAIT_MIN_MS  (10)
#define UI_WAIT_MAX_MS  (200)

for(;;)
{
    while (xQueueReceive(m_queue, &cmd, 0) == pdPASS) { ui_handle_cmd(&cmd); }   /* 1. 命令 */

    uint32_t idle = lv_timer_handler();                                          /* 2. 渲染 */
    if (idle < UI_WAIT_MIN_MS) idle = UI_WAIT_MIN_MS;
    if (idle > UI_WAIT_MAX_MS) idle = UI_WAIT_MAX_MS;

    xQueueReceive(m_queue, &cmd, pdMS_TO_TICKS(idle));                           /* 3. 等待 */
}
```

- **优先级 4 < app_task 5**：输入/事件永远可抢占 UI 渲染。
- tick 源用 `lv_tick_set_cb(esp_timer_get_time 派生)`，**不额外建定时器**。
- 队列深度小（8）即可；`ui_core_post` 满时丢弃并告警，绝不阻塞 app_task。

### 7.3 UI app 的周期行为归属

**UI app 的周期性行为由页面内的 LVGL 定时器承担，app_task 完全不参与**：

```c
/* app_clock.c：create() 在 ui_task 上下文执行，可安全创建 lv_timer */
static void clock_timer_cb(lv_timer_t *t)
{
    /* 仅在"分钟"变化时 set_text；LVGL 只在 dirty 时渲染 → 每分钟 1 次上屏 */
}

static void clock_ui_create(lv_obj_t *root)
{
    ...创建控件...
    lv_timer_create(clock_timer_cb, 1000, NULL);
}
```

这样时钟 app **零跨线程**，是最优雅的形态。
因此 UI 层 app 的 `tick_ms` 应设为 `0`（app_manager 不再给它发 tick）。

### 7.4 唯一跨线程通道

外部事件（BLE / WiFi / 文件落盘）到达 app 的 `on_event`（app_task 上下文）后，
若需更新 UI，必须投递：

```c
/* app_task 上下文 */
ui_core_post(CLOCK_MSG_SYNC_TIME, &ts, sizeof(ts));

/* ui_task 上下文（ops->on_msg） */
static void clock_ui_on_msg(uint32_t cmd, const void *data, uint8_t len) { ... }
```

### 7.5 面板互斥：pause / page_exit 的握手

这是 UI 层引入后新增的**并发正确性**问题，必须显式处理。

面板（EPD）是 app_task（DIRECT 直绘）与 ui_task（UI flush）**共享的硬件资源**。
两个任务同时进入 `hal_epd_display_mono()` 会让 SPI 事务交错 → 画面损坏、控制器状态被破坏。

单靠"输出闸门"不够：闸门只能挡住**尚未开始**的 flush，挡不住**正在进行**的那一次
（它可能已通过判断、正阻塞在 `hal_epd_display_mono()` 里）。

因此 `ui_core_pause()` / `ui_core_page_exit()` 采用**关闸门 + 等待应答**两步：

```
app_task                                ui_task
  │ ① 关闸门（同步，立即挡住后续 flush）
  │ ② 投递 PAUSE / PAGE_EXIT 命令
  │ ③ 等 ack（信号量，超时 UI_ACK_TIMEOUT_MS）
  │                                     │ 处理命令 → 关闸门 → 开闸后 give(ack)
  ▼ ④ 收到 ack：此刻保证无 flush 在飞行 → 可安全直绘
```

- ui_task 是单线程，**只要它处理完该命令，就保证没有任何 flush 在飞行中**
- 超时（默认 1000ms）只告警不阻塞：失败姿态是"可能多刷一帧"，不是卡死
- `ui_core_resume()` 不需要应答（它只开闸门，无并发访问风险）

---

## 8. 页面生命周期与状态机

### 8.1 状态机

```
        ui_core_init()
             │
             ▼
          [IDLE] ──page_enter──→ [ACTIVE] ──pause──→ [PAUSED]
             ▲                      │                  │
             └────page_exit─────────┘                  │
             ▲                                         │
             └────────────────resume───────────────────┘
```

| 状态 | `lv_timer_handler` 驱动 | 页面对象 | 说明 |
|---|---|---|---|
| `IDLE` | 否 | 无 | display 与缓冲已就绪，仅等命令 |
| `ACTIVE` | 是 | 有 | 正常 UI app 运行 |
| `PAUSED` | 否 | **保留** | 面板被直绘场景临时占用（当前无调用方，保留能力）|

**为什么 PAUSED 要保留页面**：暂停是一次短暂的临时占屏，销毁再重建页面毫无必要，也丢失页面内部状态
（页面内 `lv_timer`、滚动位置等）。

### 8.2 显示会话：什么时候会闪

面板在 mono 与彩色之间切换时**必须重建 mono 会话**——因为彩色路径开头的
`hal_epd_display_init()`（硬复位）与结尾的 `hal_epd_pwroff()`（DSLP 深睡）都会让
`m_mono_inited = false`（见 [hal_epd_370.c](../../firmware/frame_film_ark/components/film_hal/src/hal_epd_370.c)）。

| 场景 | 面板动作 | 是否闪 |
|---|---|---|
| DIRECT 彩色 app → UI app | UI 首帧重建 spectra 会话（`prepare()` 全屏 clear）| **闪一次** |
| UI app → DIRECT 彩色 app | 彩色路径自行硬复位 | 不涉及 |
| 进 UI 页 / 从暂停恢复 | `ui_clean_panel()` 强制复位（见下）| **闪一次** |
| UI app → UI app（如 主菜单 → 设置）| 同层换页，`ui_clean_panel()` 强制复位 | **闪一次** |

**为什么"进 UI 页"要强制清场**：直绘内容（各 app 的 mono 帧）与 UI 页同属一个 mono（`spectra state=2`）会话，
差分快刷只驱动变化像素，且快刷波形**没有彻底擦除的相位**——实测从旧封面菜单选中时钟后，
菜单里那张时钟封面（圆环+指针）会以淡痕留在屏上。

处置：`ui_page_build()` 与 `UI_CMD_RESUME` 都调 `ui_clean_panel()`（即 `hal_epd_display_init()`）。
硬复位会让驱动把 mono 会话置为无效（`reset()` 里 `m_mono_inited = false`），
于是下一次 mono 刷新重建会话并走 `epd_spectra_full_clear()`——用完整清场波形把残影擦掉。

- **代价**：多一次全清（实测数秒级 + 一次闪）
- **免费的情况**：从彩色路径进来时本来就已复位，不额外增加开销

### 8.3 app_manager 改造

`app_do_switch()` 统一采用"**先停旧层，再起新层**"的顺序，避免切换过程中 ui_task 把帧刷到屏幕上：

```c
/* ① 先把 UI 层停掉（无论新旧 app 属哪层），防止切换途中 UI 输出覆盖画面 */
if(old && old->layer == APP_LAYER_UI) { ui_core_page_exit(); }
else if(old && old->on_exit)          { old->on_exit(); }
if(m_app_running) { app_state_save(); }

m_current_app = id; m_app_running = 0; m_tick_acc_ms = 0;

if(app->data_dir) { service_file_set_dir_sync(app->data_dir); }

app_state_load();                                   /* ② 切入先载状态 */

if(app->layer == APP_LAYER_UI) {                    /* ③ 按层起 */
    ui_core_page_enter((uint8_t)id, app->ui_ops);
}
else {
    app_ensure_running();                           /* 原路径：调 on_enter */
}

service_param_app_current_set((uint8_t)id);
```

主菜单（`app_manager_process_input` 的 `APP_ID_MENU` 分支与 `app_menu_open`）：

```c
/* 菜单态：接管全部按键（页面只显示，on_key = NULL） */
if(m_current_app == APP_ID_MENU) {
    UP/DOWN: m_menu_sel 循环 + ui_core_post(APP_UI_MSG_MENU_SEL);   /* 页面重绘 */
    ENTER  : app_do_switch(m_menu_entries[m_menu_sel]);
    return APP_INPUT_CONSUMED;
}

/* 其余 app：长按确认键回主菜单 */
if(key == INPUT_PRESS_LONG && app_menu_available()) { app_menu_open(); return APP_INPUT_CONSUMED; }
```

> **主菜单是"同层切换"，不再需要 pause/resume。**
> 早先的设计是"封面菜单保持 DIRECT 直绘，进菜单时 `ui_core_pause()`、退出时 `ui_core_resume()`"；
> 落地时改成了"菜单本身就是 UI 层页面"，于是 UI app → 主菜单 = 同层换页（`page_exit` + `page_enter`），
> 只有 UI app → DIRECT app 才跨层。
> `ui_core_pause()/resume()` 仍保留（用户明确要求 "UI 框架可挂起"，且 `resume` 只在 `PAUSED` 态生效，
> 重复调用不额外付清场代价），当前唯一调用点是 `app_do_switch()` 的早退分支 —— 兜底"页面曾被直绘占屏暂停"。

> **踩过的坑：菜单里选中"当前 app"会让画面看起来卡死。**
> （针对旧的 DIRECT 封面菜单）`app_do_switch()` 开头有 `if(id == m_current_app && m_app_running) return;` 的早退，
> 于是"选中的就是当前 app"时既不会重绘、也不会恢复 UI 输出——菜单画面一直留在屏上。
> 新模型下这个现象不复存在：菜单态与内容 app 是**不同 app**，选中当前项时
> `app_do_switch(APP_ID_MENU)` 走的是正常的层间切换路径。

### 8.4 输入路由

| 当前状态 | 输入去向 |
|---|---|
| 主菜单（`APP_ID_MENU`）| app_manager 接管：上/下换选中索引、确认键进入 |
| UI 层 app，`ACTIVE` | 长按 → app_manager 回主菜单；其余 → `ui_core` → `ops->on_key` |
| UI 层 app，`PAUSED`（被直绘占屏）| app_manager 与 `ui_core` 都不驱动 UI（当前无调用方）|
| DIRECT 层 app | 各 app 自己的按键语义；长按 → app_manager 回主菜单 |

是否需要 `lv_indev`：时钟页无交互，**v1 先只用 `ops->on_key`**，不引入 indev。
若未来 UI app 需要焦点导航（列表/菜单），再加 `LV_INDEV_TYPE_ENCODER`，
用**单槽环形缓冲**把按键从 app_task 搬进 ui_task 的 `read_cb`。

---

## 9. LVGL 配置

### 9.1 必须修改的 Kconfig 项

当前 LVGL 走 Kconfig（`CONFIG_LV_CONF_SKIP=y`，仓库内**无 `lv_conf.h`**），
改动需落在 `sdkconfig` **以及** `sdkconfig_{std,pro,max}` 三份模板
（**仅 `frame_film` 适用**；`frame_film_ark` 只有一份 `sdkconfig`）。

| 配置项 | 现值 | 目标 | 理由 |
|---|---|---|---|
| `CONFIG_LV_COLOR_FORMAT_I1` | 未启用 | **`y`** | UI 层渲染格式 |
| `CONFIG_LV_COLOR_FORMAT_RGB565` | `y` | **取消** | 与 I1 互斥（choice）|
| `CONFIG_LV_COLOR_DEPTH_UNSET` | `y` | **取消** | |
| `CONFIG_LV_COLOR_DEPTH_1` | 未启用 | **`y`** | |
| `CONFIG_LV_COLOR_DEPTH` | `16` | **`1`** | 显存 675KB → 43KB |
| `CONFIG_LV_BUILD_EXAMPLES` | `y` | **取消** | 目前被编进库（`esp.cmake` 把 examples 列入 SRCS），白占编译与 flash |
| `CONFIG_LV_BUILD_DEMOS` | `y` | **取消** | 同上 |
| `CONFIG_LV_DEF_REFR_PERIOD` | `33` | **`100`** | 与"内部 10Hz 节拍"对齐 |
| `CONFIG_LV_MEM_SIZE` | `65536` | **保持 `65536`** | 曾试过 128KB，实测会挤爆内部 RAM 导致 bringup 失败，见 §9.2 |
| `CONFIG_LV_FONT_MONTSERRAT_48` | 未启用 | **`y`** | 时钟时间大字 |
| `CONFIG_LV_FONT_MONTSERRAT_24` | 未启用 | **`y`** | 日期行 |
| `CONFIG_LV_USE_LOG` | 未启用 | **开发期 `y`** | 裸调 LVGL 出问题很难查，稳定后再关 |
| `CONFIG_LV_USE_ASSERT_*` | 全关 | **开发期开** | 同上 |

> `CONFIG_LV_DRAW_SW_SUPPORT_I1` **已默认为 `y`**，无需改动。

### 9.2 内存布局

| 用途 | 大小 | 位置 | 生命周期 |
|---|---|---|---|
| LVGL I1 渲染缓冲 | 43,200 B | PSRAM（不可用则回退内部 RAM）| **仅 UI 页存在期间** |
| mono 帧缓冲（送驱动）| 43,200 B | PSRAM（不可用则回退内部 RAM）| **仅 UI 页存在期间** |
| LVGL 控件/样式堆 | 64 KB | 内部 RAM（`.bss`）| 常驻（`LV_MEM_SIZE`）|

**两块缓冲按需申请、用完即还**（`ui_display_acquire()` / `ui_display_release()`，随页面生命周期）：
UI 层与 DIRECT 层**互斥**，缓冲不该常驻——否则会与图片 app 需要的**大块连续** PSRAM 抢内存。

**渲染缓冲不走 LVGL 内存池**：否则 43,200 B 会吃掉 64KB 池的大半，控件只剩 ~20KB。
用 `lv_display_set_buffers(disp, our_buf, NULL, 43200, LV_DISPLAY_RENDER_MODE_FULL)` 外部分配。

#### 实测教训（这两个坑都踩过）

**坑一：`LV_MEM_SIZE` 不要盲目加大。** 曾设为 128KB（"保险性"）。时钟页只有 3 个 label，
64KB 绰绰有余，而 128KB 是实打实的 `.bss` 内部 RAM 占用。

**坑二：UI 缓冲不能常驻，否则抢走图片 app 的 PSRAM。** 切到时钟再切回图片 app 时实测：

```
E file: Allocate PSRAM buffer failed: size=345632 psram_free=181096 psram_largest=167936 internal_free=97384
```

8bpp v2 film 需 **345,632 B 连续**，而 PSRAM 最大连续块只剩 **167,936 B** ——
**连 172KB 的 4bpp v1 film 都放不下了**，图片 app 直接失败。
原因就是常驻的 86KB UI 缓冲占用了 PSRAM 并造成分片。改为按需申请/归还后，
切到图片 app 时这 86KB 已还给系统。

**坑三（更根本）：`SPIRAM_FETCH_INSTRUCTIONS` + `SPIRAM_RODATA` 会把整个 app 镜像搬进 PSRAM。**
仅修坑二还不够——把 UI 缓冲完全归还后，反推 PSRAM 最大连续块也只有约 254KB，
**依然装不下 8bpp v2 film（345,632 B）**。继续往下查才发现真正的大头：

```
CONFIG_SPIRAM_FETCH_INSTRUCTIONS=y   # 把 app 的指令段复制进 PSRAM 执行
CONFIG_SPIRAM_RODATA=y               # 只读数据也放 PSRAM
```

这两个是**性能**选项，代价是 PSRAM。实测本工程 `frame_film.bin` = **1,817,200 B（1.73 MB）**：

| | 值 |
|---|---|
| PSRAM 总容量 | 2 MB（2,097,152 B）|
| 被 app 镜像占用 | ≈ 1.73 MB |
| 再扣 WiFi/LWIP（`SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y`）与 UI 缓冲 | ~200 KB |
| **剩给 film 的连续空间** | **实测 181 KB** ❌ |

> **为什么"以前不紧张"**：LVGL 此前未被任何代码引用，链接器把 12.85MB 的
> `liblvgl__lvgl.a` 整个丢弃，镜像小得多；接入 UI 层后镜像骤增，PSRAM 就被镜像吃光了。

**处置：已关闭这两个选项**（`sdkconfig` 与三份机型模板同步；**仅 `frame_film` 适用**，`frame_film_ark` 只有一份 `sdkconfig`）。
代价是代码/只读数据改从 flash 经 cache 执行，性能略降；
收益是**释放约 1.7MB PSRAM**，345KB 的 8bpp film 与 86KB 的 UI 缓冲都能从容放下。
对冰箱贴这类应用性能不敏感，这个取舍是明确的。

> 排查这类问题不要只看"谁申请了内存"——启动时还会打印
> `heap: psram total/free/largest`（见 `film_app_init`），先把总账看清。

### 9.3 编译期开关

`film_ui` 只对具备 mono 能力的屏有意义。建议在 `sys_cfg.h` 增加：

```c
#define SYS_UI_ENABLE   (1)   /* 0 = 不编译 UI 层（ui_core_init 返回错误，app_manager 回落 DIRECT）*/
```

- `SYS_UI_ENABLE=0` 时 `film_ui` 内不引用任何 `lv_*` 符号 → LVGL 静态库**不会被链接**，flash 占用归零。
- 这保护了 4MB flash 的 PRO 机型（虽然 3.7" 屏当前正挂在 PRO 分支下，仍需保留退路）。
- `ui_core_is_ready()` 同时做**运行时**判定（`hal_epd_get_capabilities() & EPD_CAP_MONOFAST`），
  与 `app_render_has_cover_menu()` 的判定风格一致：编译期开关管"有没有编进来"，运行时判定管"这块屏能不能用"。

---

## 10. 时钟 app 改造

从"自绘 mono 帧"改为"实现 `app_ui_ops_t`"。

### 10.1 app_entry 变化

```c
const app_entry_t g_app_clock_entry = {
    .id = APP_ID_CLOCK,
    .name = "clock",
    .data_dir = NULL,
    .keys = APP_KEY_NONE,
    .tick_ms = 0,                          /* UI app 不用 app_task 的 tick */
    .events = NULL,
    .layer = APP_LAYER_UI,                 /* ← 新增：跑在 UI 框架层 */
    .ui_ops = &g_clock_ui_ops,             /* ← 新增 */
    .state = NULL, .state_size = 0,
    .param_ch = 0,                         /* 时钟无参数，不占通道 */
};
```

### 10.2 页面布局（竖屏 480×720）

用户已确认：**物理旋转 90°（真竖屏）**，显示 **年-月-日 + 时:分 + 星期**，**不使用中文**。

```
┌────────────────┐  480 × 720（竖）
│                │
│   2026-09-17   │  日期，Montserrat 24
│     THU        │  星期，Montserrat 24
│                │
│   14:30        │  时间，Montserrat 48（最大字号）
│                │
└────────────────┘
```

- 全部为 ASCII/数字 → 直接用内置 Montserrat，**无需自备字体**（中文需求已排除）。
- 星期用拉丁缩写（`MON`~`SUN`）；若倾向纯数字，改为 `D4` 之类的短标记即可。

### 10.3 刷新策略

```c
static void clock_timer_cb(lv_timer_t *t)
{
    time_t now = time(NULL);
    struct tm tmv; localtime_r(&now, &tmv);

    /* 只在"分钟"变化时更新文本 —— 每分钟 1 次上屏，而非每秒 */
    if(tmv.tm_min == m_last_min && tmv.tm_mday == m_last_mday) { return; }
    m_last_min = tmv.tm_min; m_last_mday = tmv.tm_mday;

    lv_label_set_text_fmt(m_time_label, "%02d:%02d", tmv.tm_hour, tmv.tm_min);
    lv_label_set_text_fmt(m_date_label, "%04d-%02d-%02d", tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday);
    ...
}
```

> 定时器周期 1s（用于及时捕获分钟跳变），但**上屏仍是每分钟 1 次**——因为只有
> `set_text` 真正改变内容时 LVGL 才标记 dirty。这正是 §2.2 铁律 3 的落地。

### 10.4 时间源缺口（需一并处理）

当前 [app_clock.c](../../firmware/frame_film_ark/components/film_app/apps/clock/app_clock.c) 直接用 `time()/localtime_r()`，
而 [app_layer.md](./app_layer.md) §13.3 计划的"**WiFi(SNTP) + 蓝牙 + 本地 RTC 兜底**时间抽象"
**至今未实现**。改造时应补上，否则设备重启后时间是随机的：

- `service_time`（或 `film_hal` 下的轻量抽象）：`time_sync_from_sntp()` / `time_set_from_ble()` / `time_get()`
- 落 RTC 保持跨重启有效
- UI 侧只调 `time_get()`，不关心来源

---

## 11. 关键决策汇总

| # | 决策 | 理由 |
|---|---|---|
| 1 | UI 层独立组件 `film_ui`，单向依赖 `film_ui → lvgl` | 隔离重依赖；`app_interface.h` 保持 lvgl-free；便于整层裁剪 |
| 2 | 独占 `ui_task`，所有 `lv_*` 只在其中 | LVGL `LV_USE_OS=0` 非线程安全；flush 阻塞不能拖住 app_task |
| 3 | `ui_task` 优先级(4) **低于** `app_task`(5) | 输入/BLE 事件优先于 UI 渲染 |
| 4 | `LV_COLOR_FORMAT_I1` + `RENDER_MODE_FULL` + 外部分配 43,200 B | 显存 43KB（vs RGB565 675KB），且**渲染结果即 EPD 所需 1bpp 格式**，无格式转换 |
| 5 | 转置用逐像素实现，不用位级快速算法 | 345,600 像素约 1~3ms，1 次/分钟场景无感；可读性/正确性优先 |
| 6 | app 用声明式 `layer` 字段归属显示层 | 与既有 `keys`/`events`/`state` 声明式风格一致；`app_manager` 单点分流 |
| 7 | UI app 的周期行为交给**页面内 LVGL 定时器** | app_task 完全不参与，零跨线程；比 app_manager 转发 tick 更简洁 |
| 8 | 跨线程只留 `ui_core_post` / `ops->on_msg` 一条通道 | 把"什么时候必须跨线程"收窄到唯一入口，便于审查 |
| 9 | **主菜单本身就是 UI 层页面**，`pause`/`resume` 退为兜底能力 | 菜单与内容 app 同层=同层换页，只有 UI↔DIRECT 才跨层；`pause`/`resume` 保留（"UI 框架可挂起"是硬需求），不再有唯一使用者 |
| 10 | 禁用 LVGL 动画/滚动（约定，非配置） | 每个动画帧 = 一次全屏刷新，会打满面板带宽 |
| 11 | 时钟不显示秒 | 上屏频率从 1Hz 降到 1/60Hz，代价差 60 倍，而秒针在 EPD 上本就不实用 |

---

## 12. 风险与首次点亮校准清单

| 项 | 说明 | 处置 |
|---|---|---|
| **SPI 40MHz 稳定性** | 本次直接从 10MHz 提到 40MHz | 出现花屏/丢帧 → 回退 20MHz（[hal_epd_370.c](../../firmware/frame_film_ark/components/film_hal/src/hal_epd_370.c) 的 `clock_speed_hz`）|
| **旋转方向** | 90° 顺时针 / 逆时针取决于装配方向 | ✅ 已上机确认 `UI_ROTATE_90_CW = 1` 正确 |
| **mono 极性** | 已从 `lv_draw_sw_blend_to_i1.c` 源码推导出"I1 白=1"，与 `.film` 相反 | ✅ 已上机确认 `UI_I1_BIT_BLACK = 0` 正确 |
| **I1 作为 display 格式** | LVGL 有 `lv_draw_sw_blend_to_i1.c`，且 `lv_display_set_color_format` 无格式限制 | ✅ 已上机跑通（见 §6 的调色板前缀陷阱）|
| **索引色调色板前缀** | `px_map` 前 8 字节是调色板，且显存要多申请这 8 字节 | ✅ 已修（`UI_I1_PALETTE_BYTES`）；漏跳会表现为"横移 64px + 绕回带差 1px 纵向" |
| **stride 假设** | 转置按"行字节数 = 宽/8"计算（480/8 = 60 字节），依赖 `CONFIG_LV_DRAW_BUF_STRIDE_ALIGN=1` | ✅ 已核对 sdkconfig（为 1）；若改成其他对齐值，需改按 `lv_draw_buf_width_to_stride()` 取行跨度 |
| **切页会打印警告** | LVGL 删除活动 screen 时会打 `the active screen was deleted` | 属正常（LVGL 内部把 `act_scr` 置 NULL，随后 `lv_screen_load` 复位）；仅日志噪音 |
| **I1 文字质量** | I1 无法抗锯齿，大字可能出现台阶 | 可接受则继续；不可接受退 `L8` + 阈值二值化 |
| **REF 波形时间未知** | PON/REF/POF 的真实耗时未测 | UI 只做低频更新，不依赖该数值；若将来要做动画，需先实测 |
| **PRO 4MB flash** | LVGL + 三档字体 | 关 EXAMPLES/DEMOS；必要时用 `SYS_UI_ENABLE` 整层裁掉 |
| **`LV_MEM_SIZE` 静态占用** | 128KB 进 `.bss`（内部 RAM）| 若紧张，改 `LV_USE_STDLIB_MALLOC` 指向 PSRAM |
| **`#if` 用未定义宏会静默失效** | `#if (UI_XXX == 1)` 里的 `UI_XXX` 若未定义，预处理器按 0 处理（本工程未开 `-Wundef`），分支被静默跳过 | 用任何 `UI_*` 配置宏的文件**必须 include `ui_conf.h`**；踩过一次（app_init.c 漏 include）|

---

## 13. 实施步骤

1. **SPI 提速**：`hal_epd_370.c` 的 `clock_speed_hz` → 40MHz。✅ 已完成
2. **LVGL 配置落地**：按 §9.1 改 `sdkconfig`，并同步 `sdkconfig_{std,pro,max}`（原本三份模板**没有任何 LV_ 配置**，已整体补入；**仅 `frame_film` 适用**，`frame_film_ark` 只有一份 `sdkconfig`）；`sys_cfg.h` 加 `SYS_UI_ENABLE`。✅ 已完成
3. **`film_ui` 骨架**：`ui_conf.h` / `ui_core.h` / `ui_ops.h` / `ui_display.h` + `ui_core.c`（lv_init、display、tick、队列、任务、状态机、pause/resume 握手）。✅ 已完成
4. **显示链路**：`ui_display.c` 的 `flush_cb` + `ui_i1_to_mono()`（转置 + 极性）。✅ 已完成
5. **接口接线**：`app_interface.h` 加 `layer`/`ui_ops`；`film_app` CMake 加 `REQUIRES film_ui`。✅ 已完成
6. **调度分流**：`app_manager` 的 `app_do_switch` 按层分流 + UI 层输入路由 + 主菜单接管/长按退出 + 跨线程消息（`app_manager_post_ui_msg` / `ui_core_post`）。✅ 已完成
7. **时钟改造**：`app_clock.c` 重写为 UI app（竖屏布局、分钟级刷新、页面内 `lv_timer`）。✅ 已完成
    - v0.3 按设计稿（`tools/ui-mockup` §07）重做为"计时仪表"：主读数（Montserrat 48 ± 实心三角）+ 分钟尺（30 格 × 2 分）+ 星期黑标（左下切角）+ 日期 + 星期寄存器（7 格）+ 取景框设备面板，并接入 `app_shell` 状态栏
    - LVGL 没有填充三角形图元（边框宽度全边统一，也没有 `clip-path`），故页面自带最简光栅化 `clock_tri_px()`，创建时生成 ▶ / ◀ / 切角三张 L8 小位图（代码内生成，不占 SD 资源）
8. **UI 页面三件套 + 资源层**：`app_boot.c`（开机画面：徽章/遥测/分段进度）、`app_menu.c`（主菜单：轮播/指示点/层级面板）、`app_settings.c`（设备信息 + 系统参数，写参数经 `post_ui_msg` 回 app 任务落盘）、`ui_assets.c`（SD 可替换图标，FFUI 容器 + 内置默认图回退）。✅ 已完成
9. **公共外壳**：`app_shell.c` 提供"顶部状态栏（品牌 + 电量/WiFi/蓝牙指示块）+ 底部操作提示行"，三个 UI 页共用同一套版式；状态栏数据由 app 任务侧采集后经 `APP_UI_MSG_STATUS` 回投（设置页复用已有的整页快照，不额外往返）。✅ 已完成
10. **上电流程**：FULL 模式固定 BOOT → MENU，首帧 mono 顺带完成整屏清场；非 FULL / UI 层不可用时沿用"恢复上次 app"。✅ 已完成
    - 进度条由**页面内 `lv_timer`** 推进，且**每次回调只点亮一格**（`APP_BOOT_SEG_NUM` = 16 格 × `APP_BOOT_STEP_MS` = 900ms ≈ 15s）。节拍刻意 ≥ mono 单帧耗时（**实测 ~940ms**），否则一帧内会跨过两格、看起来"跳格"
    - 走满后停留 `APP_BOOT_DONE_HOLD_TICKS` = 1 个节拍，再由页面 `app_manager_post_ui_msg(APP_UI_REQ_BOOT_DONE)` 上报切页 —— **切页时机由页面决定**，不用固定延时去猜首帧清场那 ~3.3s；`app_init` 只留一个 `APP_BOOT_FALLBACK_MS` = 35s 的兜底定时器（页面构建失败时也能出去，且必须**明显大于**正常路径 ≈18s，否则会抢在进度条走完前切页）
    - 反例（都踩过）：① 外部定时器往页面投"步骤消息" —— 首帧 flush 阻塞 3.3s，消息全堆队列里被一次吞掉，进度条不动；② 按墙上时间算百分比 —— 每帧 ~940ms 跨 1.4 格，会出现一次跳两格；③ 把"格数→百分比→格数"来回换算 —— 整除丢精度，第 1 格算出来的点亮数仍是 0，看起来像"卡一格才动"
    - 兜底回调跑在 **FreeRTOS Timer 服务任务**上，只投递消息、**不直接切页**（切页会做 SD 目录刷新 / NVS 落盘）；同时 `CONFIG_FREERTOS_TIMER_TASK_STACK_DEPTH` 已从 2048 提到 **3072** —— 否则那条 `sys_logw` 会把 Tmr Svc 栈打爆
11. **索引色调色板前缀修正**：flush 跳过 `px_map` 前 8 字节（I1 调色板），显存多申请 8 字节，并在每次 flush 写死调色板（索引 0 = 黑、1 = 白）。✅ 已完成（详见 §6）
12. **休眠卡（手动休眠）**：主菜单长按 ENTER → `app_sleep_run()`（`app_sleep.c`，设计见 `tools/ui-mockup` §08）。✅ 已完成
    - 页面是**全屏、不带 `app_shell` 外壳**的居中构图：徽章（与 BOOT 同一张 SD 资源）→ 细线 → `STANDBY` → 细线 → `PRESS ENTER TO WAKE`，屏幕底边锚一行 `AUTO WAKE hh:mm` / `AUTO WAKE OFF`
    - `app_manager` 侧加了 `m_sleep_page` 占屏门闸（与 `m_boot_page` 同构）—— 这一帧是**唯一**一帧，之后设备就断电，绝不能被别的 app 盖掉
    - **不受休眠模式开关（BLE 0x25）约束**：那个开关管的是自动休眠，用户明确按下的动作就该执行
    - 入睡前必须同时满足两件事（`app_sleep_run()` 的等待循环，**缺一不可**）：① `hal_pwr_wake_condition_met()` 为 false（等手指抬起 —— ext0 是电平触发，长按又是按住期间上报的，按着断电会当场醒回来；这条只会让入睡更晚，是叠加项而非替代品）；② 距切页已过 **4s**（`SP_DRAW_WAIT_MS`）—— `ui_core_page_enter()` 异步且**没有"第一帧已上屏"的回调**，只能按时间兜。**别按"单帧 940ms"推这个值**：上机实测 2s 不够（卡还没刷出来就断电，屏幕停在旧画面/半张卡），换页后第一次上屏明显慢于稳态单帧；EPD 在外设供电轨上，断电即停在半途。一直按着不放时 15s 兜底
    - 查询只放在 `hal_pwr`（`hal_pwr_wake_condition_met()`，读的就是 `hal_pwr_enter_sleep()` 里 ext0 配的那路 GPIO），**输入驱动零改动**：按键库回答不了这个问题 —— PRO/MAX 现在没注册 press/release 事件，STD 的库虽有 `RE_ET_BTN_RELEASED` 但 `hal_encoder.c` 的映射表把它丢了，而 HAL 里现成那份 `button_pressed` 在长按上报时就被置 false（此刻手指还在键上）
    - 低功耗仍走 monitor 任务既有路径：新增 `service_monitor_request_sleep()` + `MSG_ENTER_SLEEP`，只投消息，deinit + `hal_pwr_enter_sleep()` 都在 monitor 任务里做
13. **待上机验证**：见 §12 —— SPI 40MHz 稳定性、I1 渲染质量；并回归图片/模板/动图/时钟/设置页与主菜单来回切换（含长按退出的残影表现）、以及主菜单长按休眠 → 按 ENTER 唤醒的完整往返。
14. **主菜单优化轮（2026-09-19）**：✅ 已完成
    - **状态栏三档**（`app_shell`）：关闭 → 整项隐藏（`LV_OBJ_FLAG_HIDDEN`，隐藏的 flex 子项不占位）；已开启 → 文字 + 空框；已连接 → 文字 + 实心框。连接态来自 `service_wifi_get_connect_status()` / `service_ble_gatts_get_connect()`，只在 **create 与换选中项**时同步（1-bit 每次更新都是全帧 + 闪，不做周期刷新）。设置页复用快照，`settings_snapshot_t` 补了 `wifi_conn` / `bt_conn` 两字节
    - **蓝牙开关对齐 WiFi**：`service_ble_init()` 在 `ble_enable == 0` 时跳过（原来无条件初始化）；新增 `service_ble_apply_enable()` 供设置页与心跳下发做运行期起停（关 = disconnect + `gatt_server_uninit`，开 = `gatt_server_reinit`；只有从未拉起过才整栈 init，因为 `uninit` 不关 bluedroid）
    - **进入前预检**：`app_entry_t.enter_block_reason`（返回 NULL 才可进入）。图片/动图目录为空时原本 `on_enter` 只打日志不绘制 → 切过去屏幕停在上一帧，像"已经进去了"。现在 `app_do_switch()` 里在 `service_file_set_dir_sync()` 之后、`app_stop_current()` 之前判；不通过则还原目录并原路退回，原因经 `APP_UI_MSG_MENU_NOTICE` 显示在菜单面板下方的留白里（换选中项 / 离开菜单即作废）
    - **选中卡片左下角切角**：设计稿是 9×9 的 45° 拉削（`.chamfer-bl`）。`lv_obj_set_style_clip_corner()` 对 radius=0 的方角没有任何效果（原来那行等于没写），而 LVGL 边框宽度是全边统一的、也没有 `clip-path`；改为程序生成一张 9×9 白三角 L8 位图（`0xFF` 白 / `0x00` 黑，与 ui_assets 的 L8 约定一致），挂在**轮播容器**下用 `LV_OBJ_FLAG_IGNORE_LAYOUT` 自己定位 —— 卡片的子对象会被裁到内容区（3px 描边之内），盖不住外角
15. **按键语义重排：长按=休眠、双击=退出 app（2026-09-19）**：✅ 已完成
    - **长按确认键 = 手动休眠**（全局，三机型一致，不看休眠模式开关）；**双击确认键 = 从 app 退回主菜单**（原长按语义）。触发点：`app_manager_process_input()`
    - 睡后画面由每个 app 的 `app_entry_t.sleep_face` 声明：图片/动图 `APP_SLEEP_FACE_KEEP`（留当前画面），其余 app 与主菜单默认 `APP_SLEEP_FACE_CARD`（改画休眠卡）。手动长按、闲置自动休眠、低电三条路径统一按它裁决：主菜单长按 → `app_sleep_run(1)`；app 内长按 → `app_manager_sleep_from_app()` 按 `sleep_face` 传 0/1；自动/低电由 `APP_SLEEP_STOP_CMD` 在停屏前补画（`app_sleep_present()`）。**不画卡这条不需要等地板时间**：调用方先 `app_stop_current()`，它让 app 停止绘制、且对 UI 层是同步等 page_exit 完成的 → 之后不可能有半帧在途，故只等唤醒条件解除，松手即断电
    - 新增 `INPUT_PRESS_DOUBLE`（`hal_input.h`）：PRO/MAX 用 iot_button 的 `BUTTON_DOUBLE_CLICK`；STD 的 esp-idf-lib 只有"单击"事件，故在 `hal_encoder.c` 里用 `esp_timer` one-shot 做配对窗口（窗口内第二次 → 双击，到期 → 补发单击）
    - **⚠️ 窗口值是个坑（上机踩过）**：`BUTTON_DOUBLE_CLICK_WINDOW_MS = 350`（STD 对应的 `ENCODER_DOUBLE_CLICK_MS` 同值）。按钮库把 `short_press_time` 同时当"单击结算窗口"和"双击配对窗口"（`iot_button.c` state 2/3），而项目原来给它的是 `BUTTON_SHORT_PRESS_TIME_MS = 50`（消抖阈值），比人手的双击间隔还短 → 实测两次单击相隔 230ms 就没配上，表现为"双击按不出来"。所以确认键单独用 350，上/下键仍是 50（它们不认双击，单击保持灵敏）。代价：确认键单击下发晚一个窗口
    - 各页底部提示行同步：菜单 `... HOLD SLEEP`、时钟 `DBL ENTER EXIT   HOLD SLEEP`、设置 `UP/DOWN  ENTER TOGGLE  DBL EXIT  HOLD SLEEP`

> 时间源（§10.4）本次**未实现**：`app_clock.c` 仍直接使用 `time()`，设备重启后时间需依赖后续的
> SNTP / 蓝牙校时 + RTC 兜底补齐。这是本次改造遗留的已知缺口。

---

## 14. 后续可选（不在本次范围）

- ~~**封面菜单 UI 化**~~：✅ 已落地——主菜单就是 LVGL 页面（见 §8.3 / `app_menu.c`），旧的 DIRECT 封面菜单（`app_render_switch_menu` + 超时状态机）已删除
- **`lv_indev` 焦点导航**：UI app 需要列表/设置页时的输入方案（§8.4）
- **控制器轻量 mono 模式探索**：若能找到比 4bit/像素更紧凑的传输格式（`JD7601_CMD_UNK_41/E6` 等语义待确认命令），单帧 payload 可从 172,800 B 降到 86,400 B 甚至 43,200 B → 刷新率上限显著提高
- **局部刷新的可行性**：`EPD_CAP_PARTIAL` 已定义但无人实现；JD7601 是否有窗口命令仍待确认
