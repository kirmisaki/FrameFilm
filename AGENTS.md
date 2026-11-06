# AGENTS.md

FrameFilm 项目 AI 开发指南。

按当前任务和目标固件查阅下列索引；无需预读全部文档或加载不相关 Skills。结构分析不要求构建或测试；代码变更只验证受影响的功能。

## 项目身份

开源彩色电子纸冰箱贴。ESP32-S3 + EPD + BLE/WiFi，手机传照片显示。

- ESP-IDF v5.5.2 (C) · 微信小程序 (ES5) · Web 工具 (ES6)
- GPL-3.0 · Git 中文 commit: `type(scope): 描述`
- 三套固件：`firmware/frame_film/`（冰箱贴**三机型**，旧机型维护线）、`firmware/frame_film_ark/`（通行证版，**单机型**，见下方「FrameFilm Ark」）、`firmware/frame_film_dock/`（底座，见下方「Dock 底座」）
- **app 框架 / UI 层（`film_app` + `film_ui`）只存在于 `frame_film_ark`**：`frame_film` 是经典固件，没有这两个组件，也不实现 app 通道 BLE 命令（`0x45~0x4E`）

## 三机型（仅 `frame_film`）

> 本节与下属「屏幕」小节**只适用于 `firmware/frame_film/`**（旧机型维护线）。通行证版 `firmware/frame_film_ark/` 已收敛为单机型，没有机型宏与屏幕切换，见下方「FrameFilm Ark」。

统一固件 `firmware/frame_film/`。硬件版本由两处编译期配置决定：**机型**（`sys_cfg.h`）+ **屏幕**（`hal_epd.h`）。

### 机型（三选一）

| | 基础版 STD | Pro 版 | Max 版 |
|---|---|---|---|
| 机型宏 | `FRAMEFILM_STD` | `FRAMEFILM_PRO` | `FRAMEFILM_MAX` |
| sdkconfig | `sdkconfig_std` | `sdkconfig_pro`（另有 n16r8 的 `sdkconfig_pro_n16r8`，配 `partitions_16mb.csv`） | `sdkconfig_max` |
| 输入 | 旋转编码器 (GPIO6/4/5) | 三按键 (GPIO4/6/5) | 三按键 (GPIO12/14/13) |
| Flash/PSRAM | 16MB / Octal SPI | 4MB / Quad SPI（n16r8 变体 16MB / Octal） | 16MB / Octal SPI |
| RGB LED | WS2812 (GPIO17) | WS2812 (GPIO17) | 无 |
| 默认屏幕 | E6 3.6" 600×400 | E6 3.68" 792×528 | E6 7.09" 1200×1600 双面板 |

> 机型差异集中在输入设备、SD/电池/LED 引脚、Flash/PSRAM，代码用 `FRAMEFILM_STD/PRO/MAX` 宏编译隔离；改机型相关代码时确认三机型分支是否齐全。

### 屏幕（每机型可从列表选择）

屏幕不与机型绑定：在 `hal_epd.h` 的对应机型分支内，把目标 `EPD_SELECT_E6_*` 置 `1`、其余置 `0` 即可切换。

| 宏 | 屏幕 | 分辨率 | 面板 ID | EPD 驱动 |
|----|------|--------|---------|----------|
| `EPD_SELECT_E6_3_60_600_400` | E6 3.6" | 600×400 | 0x03 | `hal_epd_360.c` |
| `EPD_SELECT_E6_3_68_792_528` | E6 3.68" | 792×528 | 0x01 | `hal_epd_368.c` |
| `EPD_SELECT_E6_3_70_720_480` | E6 3.70" | 720×480 | 0x02 | `hal_epd_370.c` |
| `EPD_SELECT_E6_1_54_240_240` | E6 1.54" | 240×240 | 0x04 | — |
| `EPD_SELECT_E6_7_09_1600_1200` | E6 7.09" 双面板 | 1200×1600 | 0x05 | `hal_epd_709.c` |
| `EPD_SELECT_E6_3_98_768_552` | E6 3.98"（SE0398） | 768×552 | 0x07 | `hal_epd_398.c` |
| `EPD_SELECT_E4_3_98_768_552` | E4 3.98"（JD79665AA，四色） | 768×552 | 0x08 | `hal_epd_398e4.c` |

> 切换硬件版本 = 改 `sys_cfg.h` 机型宏（+ 对应 `sdkconfig`）+ 改 `hal_epd.h` 屏幕宏。`EPD_PANEL_ID` 随屏幕返回给连接端（BLE `0x42`），用于客户端匹配屏幕。

## FrameFilm Ark（通行证版，单机型）

`firmware/frame_film_ark/` 是从 `frame_film` 分叉的**通行证版（Arknights 通行证主题）**固件，也是 **app 框架 / UI 层的主线**。它已简化为**单机型**：无 `FRAMEFILM_STD/PRO/MAX` 机型宏、无 `EPD_SELECT_E6_*` 屏幕选择宏，硬件规格按编译期常量写死。

| 项 | 固定值 |
|---|---|
| BLE 设备名 / 厂商名 | `FRAMEFILMARK-<后缀>`（默认完整 BLE MAC；0x54/0x55 查询/修改）/ `FRAMEFILMARK` |
| 屏幕 | E6 3.70" **720×480**，面板 ID `0x02`，驱动 `hal_epd_370.c` |
| 输入 | 三按键：上 GPIO6 / 下 GPIO4 / 确认 GPIO5（**低有效**） |
| LED / 电池 / SD | 有 WS2812（白=未连接、绿=已连、红=低电，呼吸）、有电池检测、SD 有检测脚 |
| 唤醒脚 | GPIO5（低电平） |
| Flash / PSRAM | 默认 4MB / Quad SPI；N16R8 变体 16MB / Octal |
| sdkconfig | 默认 `sdkconfig`（4MB/Quad）；`sdkconfig_n16r8`（16MB/Octal + `partitions_16mb.csv`），用 `build.ps1` 切换 |

- **已删除**（相对 `frame_film`）：其余屏幕驱动 `hal_epd_{360,364,368,709}.c`、STD 专用的 `hal_encoder.c`（无旋转编码器）、`sdkconfig_{std,pro,max}` 与 `sdkconfig.old`
- 输入仍走 `film_hal` 的按键实现（`hal_input.h` 抽象 + iot_button）；`FRAMEFILM_*` 宏在 ark 里**不存在**，不要往这边带机型分支
- 组件比 `frame_film` **多 `film_ui`（LVGL UI 层）与 `film_app`（app 层）**——app 框架只在这里；`film_sys` / `film_hal` / `film_service` 三层与 `frame_film` 同构（少了编码器与其余屏驱动）。跨端常量改动两边都要落

## Dock 底座（独立固件）

`firmware/frame_film_dock/` 是**独立于三机型**的固件：屏幕固定 E6 3.64" **760×568**（面板 ID `0x06`），不再有 STD/PRO/MAX 分支。

比冰箱贴多出 USB 设备能力，插到电脑后是**复合设备**：USB 声卡(UAC) + HID 键盘 + CDC 虚拟串口。

- VID/PID：`0x303A` / `0x8000`（Windows 上会枚举出一个 COM 口）
- **USB 与 BLE 共用同一套命令解析**：`components/film_service/src/service_cmd.c`（帧格式、通道号与冰箱贴完全一致），两条链路只是收发适配层（`service_ble.c` / `service_usb.c`），新增命令只需改 `service_cmd.c`
- 新增命令：`0x43` 按键键值设置、`0x44` 按键键值查询（**仅 dock 有**；冰箱贴固件不使用这两个号，其 app 通道为 `0x45~0x4C`、另有 `0x4D` 时间同步与 `0x4E` 远程按键注入，dock 新增命令从 `0x45` 之后顺延且勿占用冰箱贴已用号）
- dock 的按键不操作本机，而是作为 **PC 键盘**：单击/双击/长按 → 按 `g_service_param.key` 发送 HID 键值（见 `service_monitor.c`）

> 给 dock 的 BLE 命令要同时考虑 USB 链路：回包按来源链路原路返回，传输状态机带链路归属校验。

### USB 上传图片到 Dock

用户要「把图片传到 dock / 通过 USB 上传到 dock」时，**直接用现成工具**，不要自己拼串口帧或重写图片转换：

```bash
# 上传一张图片（自动找串口 → 握手确认 → 转换 → 上传 → 回读校验）
python tools/framefilm-dock-upload/scripts/dock_upload.py photo.jpg

# 列出 / 校验设备上已有的 film 文件
python tools/framefilm-dock-upload/scripts/dock_upload.py --list
```

- 完整参数与排查表：`tools/framefilm-dock-upload/SKILL.md`
- 依赖 `pyserial` + `Pillow`；图片转换复用 `server/filmhub/backend/app/services/film_convert.py`，保证与 `tools/ForFilm` / 服务端出图一致
- **只适用 dock**：冰箱贴没有 USB 串口，仍走 `tools/ForFilm` 网页蓝牙

## 架构约束

### 分层依赖（单向，不可逆）

```
film_service → film_hal → film_sys → ESP-IDF
  (服务层)     (硬件层)   (系统层)
```

### GPIO 引脚表（机型差异）

| 外设 | STD（基础版） | Pro 版 | Max 版 |
|------|--------------|--------|--------|
| EPD | SCK48 / MOSI47, CS14/DC13/RST12/BUSY11 (SPI2 四线) | ←同 | SCK9 / SDIN41 / SDIO40, CS0=18/CS1=17/RST6/BUSY7 (双CS无DC), LOAD_SW45 |
| TF卡 | CLK40 / CMD41 / D0-3=39/38/2/42, DET45 | ←同 | CLK8 / CMD3 / D0-3=5/4/16/15, 无检测 |
| 输入 | 编码器 A6/B4/按键5 | 按键 上4/下6/确认5（低有效） | 按键 上12/下14/确认13（高有效） |
| 唤醒 | GPIO5（低电平） | ←同 | GPIO13（高电平） |
| RGB LED | GPIO17 (WS2812) | ←同 | 无 |
| 电池 | ADC使能8 / ADC_CH0 1 | ←同 | 无电池检测 |
| 外设供电 | GPIO21 | ←同 | 无 |

### 跨端一致性（必须）

修改以下共享协议或编码时，检查并同步**实际受影响的固件与客户端**；Ark 专有命令不要因此移植到经典固件，Dock 的共用协议变更也需检查其 BLE/USB 解析。

| 内容 | C 固件 | 小程序 | Web |
|------|--------|--------|-----|
| BLE 命令常量 | 各目标固件的 `service_ble.h`；Dock 另查 `service_cmd.c` | `ble-utils.js` | `frame.js` |
| film 颜色编码 | 各目标固件的 `hal_epd.h` 与对应解码实现 | `film-utils.js` | `convert.js` |

### 关键常量

- `BLE_CHUNK_SIZE = 192`（数据包大小）
- `BLE_CMD_HEAD = 0x55`（帧头）
- v1 单帧 film 文件大小 = **32B 头 + (宽×高/2) 像素**（标准版 600×400 为 120032 字节）；v2 与多帧按 `Format` / `FrameCount` 计算，见 `docs/film/film.md`
- 6 色编码：黑 0x00 | 白 0x11 | 绿 0x66 | 蓝 0x55 | 红 0x33 | 黄 0x22
- 新增 BLE 命令先查下方命令表与协议文档；`0x3E` 起已有已分配通道，不是空闲范围

## 命名约定

| | C 固件 | 小程序 JS |
|---|---|---|
| 文件/函数 | `snake_case` | `camelCase` (文件 `kebab-case`) |
| 全局变量 | `g_` 前缀 | — |
| 宏/枚举 | `UPPER_CASE` | `UPPER_CASE` (常量) |
| 类型 | `PascalCase_t` | — |
| 头保护 | `__NAME_H__` | — |

## 关键文件

> **路径口径**：下表按实际所属固件列出入口。三套固件各有 `film_sys` / `film_hal` / `film_service`，按任务选择对应目录；`film_app` / `film_ui` 及 app 通道只在 Ark，机型/屏幕选择宏只在经典固件。共享协议与常量变更同步实际受影响的实现。

| 要改什么 | 核心文件 |
|---|---|
| 机型/硬件规格配置 | `firmware/frame_film/components/film_sys/inc/sys_cfg.h` + `firmware/frame_film/sdkconfig_{std,pro,pro_n16r8,max}` + `CMakePresets.json` / `build.ps1`（`FRAMEFILM_MODEL`） |
| 屏幕选择 | `firmware/frame_film/components/film_hal/inc/hal_epd.h`（`EPD_SELECT_E6_*` 宏） |
| BLE 协议 | `firmware/frame_film/components/film_service/inc/service_ble.h` |
| 蓝牙遥控（0x4E 按键注入） | `firmware/frame_film_ark/components/film_service/src/service_ble.c`（发布）+ `film_app/core/app_manager.c`（`ble_key_to_press` 映射投递） |
| 时间同步（0x4D） | `firmware/frame_film_ark/components/film_service/src/service_time.c` + `service_param.h`（`tz_min`） |
| 全局事件总线 | `firmware/frame_film/components/film_sys/inc/sys_event.h` |
| EPD 驱动 | `firmware/frame_film/components/film_hal/src/hal_epd_{360,368,370,709}.c` |
| film 播放 | `firmware/frame_film/components/film_service/src/service_film.c` |
| 固件入口 | `firmware/frame_film/main/main.c` |
| 小程序 BLE | `tools/wechart/miniprogram/utils/ble-utils.js` |
| Web 连接（BLE/USB） | `tools/ForFilm/js/{bluetooth,usb}.js` |
| Dock 固件配置 | `firmware/frame_film_dock/components/film_sys/inc/sys_cfg.h` |
| Dock 命令解析（BLE/USB 共用） | `firmware/frame_film_dock/components/film_service/src/service_cmd.c` |
| Dock USB 描述符/CDC | `firmware/frame_film_dock/components/film_hal/src/hal_usb.c` |
| Dock USB 传图工具 | `tools/framefilm-dock-upload/scripts/dock_upload.py` |
| UI 层框架（LVGL 宿主/页面生命周期） | `firmware/frame_film_ark/components/film_ui/src/ui_core.c` |
| UI 层开关与显示链路 | `firmware/frame_film_ark/components/film_ui/inc/{ui_conf.h,ui_ops.h}` + `src/ui_display.c` |
| 开机画面 / 主菜单 / 系统设置（框架页面） | `firmware/frame_film_ark/components/film_app/pages/app_{boot,menu,settings}.c` |
| 时钟页主读数数字字库（60px，自备） | `firmware/frame_film_ark/components/film_app/apps/clock/font_clock_hero.c` + `tools/clock-font/gen_clock_font.py` |
| 休眠卡 + 入睡流程（主菜单长按） | `firmware/frame_film_ark/components/film_app/pages/app_sleep.c` |
| 手动休眠入口 / 占屏门闸 | `firmware/frame_film_ark/components/film_app/core/app_manager.c`（`app_manager_sleep_show` + `m_sleep_page`） |
| 进低功耗（deinit + deep sleep） | `firmware/frame_film_ark/components/film_service/src/service_monitor.c`（`service_monitor_request_sleep`） |
| UI 页公共外壳（状态栏 + 居中时间 + 提示行 + 周期 tick） | `firmware/frame_film_ark/components/film_app/core/app_shell.{h,c}` |
| 开机行为参数（BOOT PAGE / START APP） | `firmware/frame_film_ark/components/film_app/core/app_boot_cfg.{h,c}`（持久化在 `service_param` 的 `app7` 槽位） |
| 内容 app（图片/模板/时钟/动图/通行证） | `firmware/frame_film_ark/components/film_app/apps/<name>/app_<name>.{h,c}`（一个 app 一个文件夹） |
| Ark 硬件变体切换（n4r2 / n16r8） | `firmware/frame_film_ark/build.ps1` + `CMakePresets.json`（VS Code `ESP-IDF: 选择项目配置`） |
| SD 可替换图标（FFUI 容器） | `firmware/frame_film_ark/components/film_ui/src/ui_assets.c` + `tools/ui-assets/gen_ui_assets.py` |
| 协议文档 | `docs/blecmd/blecmd_protocol.md` |

## 常见陷阱（不要做）

> 以下条目多来自分叉前 frame_film 的经验；`frame_film_ark`（单机型）同样适用，但**凡涉及 app 层 / UI 层（`film_app` / `film_ui`）的条目只适用 ark** —— `frame_film` 没有 app 框架（无 UI 页、无 app 切换、无 `0x45~0x4E` 命令），例如任务线程模型、休眠卡、开机行为、遥控、`film_app` 目录分层这些条目对它都不适用。凡提到 STD/PRO/MAX 分支的地方，在 ark 里只有按键这一套（上6/下4/确认5，低有效），也没有编码器。

1. **不要只在 service 层调 esp_wifi_init 等 ESP-IDF driver** — 必须通过 HAL
2. **不要只改一个机型的宏分支** — 机型差异代码需覆盖 `FRAMEFILM_STD/PRO/MAX`（EPD 驱动、输入设备、SD 等按宏隔离）。**此条仅 `frame_film` 适用**：`frame_film_ark` 是刻意的单机型设计，不要往它里面引入机型宏或 `#if` 分支
3. **不要改 BLE 命令值** — 值一旦定义就固定，新增命令先核对协议表，避免复用已分配通道
4. **不要假设字符串编码** — 普通 BLE 字符串为 ASCII + `\0`；Ark 设备名称 0x54/0x55 为 UTF-8 + `\0`，具体以协议文档为准
5. **不要忘记更新 blecmd_protocol.md** — 协议文档必须与实际实现一致
6. **不要在 service 层直接操作 GPIO** — 所有硬件操作走 film_hal
7. **不要机型宏与 sdkconfig 不匹配** — 编译前确认 `sys_cfg.h` 机型宏与 `sdkconfig_{std,pro,max}` 对应一致（**仅 `frame_film`**；`frame_film_ark` 无机型宏，只有 n4r2 / n16r8 两个硬件变体配置，见 `build.ps1`）
8. **不要给 dock 随便加 USB IN 端点** — ESP32-S3 的 IN 端点上限是 5（含 EP0），dock 已用满：HID + 音频 mic + 音频反馈 + CDC 数据。新增 USB 功能前必须先释放等量 IN 端点（CDC 的「通知端点」就是因此省掉的）
9. **不要按"机型"猜 `0x42` 回包长度** — 三套固件现在**统一**返回 `面板ID(1)+宽(2)+高(2)`（LEN=5，见各自 `SCREEN_RESOLUTION_GET` 实现），协议文档 §4.6.1 也是这个格式，客户端按 LEN=5 解析即可。（历史上冰箱贴曾只回 `宽(2)+高(2)`（LEN=4），遇到老固件才需要按 LEN 兜底）
10. **不要在 app_task 里碰 `lv_*`** — LVGL 非线程安全，只在 `ui_task` 上下文调用；要向页面推数据走 `ui_core_post()`（下行）、要回写服务层走 `app_manager_post_ui_msg()`（上行），两者都在 `film_app`/`film_ui` 里
11. **不要在 ui_task 里读写 `g_service_param` / 电池 / WiFi / SD** — 那些没有跨任务保证。设置页只上报 `[行号, 候选下标]`，由 `settings_on_event()` 在 app 任务侧校验、写参数、落盘，再把整页快照回投给页面
12. **不要在唤醒条件还成立的时候进 deep sleep** — ext0 是**电平**触发（`hal_pwr.c`，STD/PRO = GPIO5 低有效、MAX = GPIO13 高有效）。长按是**按住期间**上报的，那一刻唤醒脚必然还满足条件 → 按着断电会当场醒回来（表现为"长按后闪一下就恢复"）。`app_sleep_run()` 入睡前要同时满足两件事，**缺一不可**：① `hal_pwr_wake_condition_met()` 为 false（等手指抬起）——它只会让入睡更晚、不会更早，是叠加项；② 距切页已过 **4s**（`SP_DRAW_WAIT_MS`）——`ui_core_page_enter()` 是异步的，**没有回调能告诉你"第一帧已上屏"**，只能按时间兜。这个值**不要**按"单帧 940ms"去推：上机实测 2s 不够（卡还没刷出来就断电，屏幕停在旧画面/半张卡），换页后第一次上屏的耗时明显大于稳态单帧。**EPD 挂在外设供电轨上，断电即停在半途**。别用按键库自己的状态顶替 ①：PRO/MAX 现在根本没发 press/release 事件，STD 的库虽有 `RE_ET_BTN_RELEASED` 但 HAL 的映射表把它丢了，而 HAL 里那份 `button_pressed` 在长按上报时就被置 false（手指还在键上）
13. **不要把菜单的"长按"与 app 内的"长按"搅在一起** — **长按确认键 = 手动休眠**（全局语义，三机型一致，且不看休眠模式开关）；**双击确认键 = 从 app 退回主菜单**（原长按语义）。两处只差"要不要画休眠卡"：主菜单长按画（`app_sleep_run(1)`，这帧要留在屏上直到唤醒），app 内长按不画（`app_manager_sleep_from_app()` → `app_sleep_run(0)`，屏上保持当前 app 画面）。菜单里双击没有语义（它就是"根"）。双击事件来自 `INPUT_PRESS_DOUBLE`：PRO/MAX 用 iot_button 的 `BUTTON_DOUBLE_CLICK`（库自己会消歧：双击只发 DOUBLE、不发 SINGLE），STD 用 hal_encoder 里的 one-shot 配对窗口补。**窗口值必须明显大于人手的双击间隔（现取 350ms），否则双击会被判成两次单击** —— 这一点在 PRO/MAX 上尤其反直觉：按钮库把 `short_press_time` 同时当作"单击结算窗口"和"双击配对窗口"（`iot_button.c` 的 state 2/3），而项目原来给它的是 50ms（只是个消抖阈值），比手速还短，所以必须先把这个值放开（见 `confirm_cfg`）。代价：确认键的单击下发晚一个窗口（上/下键不认双击，不受影响）
14. **不要让 app 在"进不去"的状态下被切过去** — 图片/动图目录为空时它们的 `on_enter` 只打日志、不绘制，切过去屏幕会停在上一帧，用户会以为已经进到那个 app 了（切换还会先把面板停一次、白闪一帧）。改用 `app_entry_t.enter_block_reason` 预检：返回 NULL 才切，否则放弃切换并把原因经 `APP_UI_MSG_MENU_NOTICE` 显示在主菜单面板下方那块留白里。预检的调用点必须在 `app_do_switch()` 里 `service_file_set_dir_sync()` **之后**（文件列表是异步刷新的，早于它就问不准），也必须在 `app_stop_current()` 之前（那之前的退回是无副作用的，只需把目录还原）
15. **不要在关闭状态下初始化蓝牙** — `service_ble_init()` 与 WiFi 同款：`ble_enable == 0` 直接跳过，不拉协议栈。运行期开关走 `service_ble_apply_enable()`（关 = disconnect + `gatt_server_uninit`；开 = 栈若活着就 `gatt_server_reinit`，只有从未拉起过才整栈 init）。设置页与心跳下发两条路径都用它，别只改参数
16. **不要把"开机行为"塞进 `ServiceParam_Def_t`** — BOOT PAGE / START APP 只描述"上电时 app 层怎么走"，与 service 层无关；改 `ServiceParam_Def_t` 布局会触发**整体重置**，连带清掉 WiFi 配网与时区。已放 app 层保留槽位 `app7`（`SERVICE_PARAM_APP_ID_BOOT_CFG`，见 `app_boot_cfg`），用现成的 app blob 持久化。同理，往 `app_start_t` **尾部追加**时别忘了同步设置页候选表（下标即存盘值）
17. **不要在遥控（0x4E）链路上另写一份按键语义** — 固件把键值经 `ble_key_to_press()` 映射成与 HAL 同构的 `INPUT_PRESS_*` 入队，因此菜单导航/单击确认/双击退回/长按休眠全部自动一致；新增按键功能只改 `app_manager` 那一处，不要在 BLE 层复刻交互。回显只表示"已收到"，开机卡/休眠卡占屏期间按键照本机语义被丢弃；dock 不支持该通道（其按键是 PC 键盘 HID）
18. **不要往 `film_app/` 根目录塞源码** — 分层已定：`inc/`（对外公共头，只有 `app_init.h` + `app_interface.h`）、`core/`（框架核心：调度/初始化/渲染/外壳/开机参数）、`pages/`（框架页面：开机画面/主菜单/系统设置/休眠卡，**系统设置属于框架而不是内容 app**）、`apps/<name>/`（内容 app，一个 app 一个文件夹，头文件与源文件同目录）。组件用的是**显式文件列表**（不是 GLOB），新增 `.c` 必须在 `CMakeLists.txt` 的 `srcs` 里登记，新增目录要加进 `INCLUDE_DIRS`，否则静默不编译/找不到头
19. **不要在 `frame_film_ark` 里重新引入机型/屏幕切换** — 该固件刻意收敛为单机型（`FRAMEFILMARK`、固定 720×480、三按键），`FRAMEFILM_STD/PRO/MAX` 与全部 `EPD_SELECT_E6_*` 已删除；要多机型请改 `firmware/frame_film/`。**裁剪屏幕驱动时务必同时去掉驱动文件里那层 `#if EPD_SELECT_... == 1` 外壳** —— 宏不存在时该条件会静默为假，整个驱动被编译掉，只在链接期报"未定义引用"
20. **不要用 `0x42` 的面板 ID 反推机型** — `0x02`（3.70" 720×480）既可能是 `frame_film` 的 Pro、也可能是通行证版 Ark，两者同屏同驱动，面板参数无法区分。客户端机型一律以 **BLE 广播名**为准（`FRAMEFILMARK` → Ark），面板参数只用来校正尺寸/像素排布，**不要覆盖**由名字判定的机型（见 `tools/ForFilm/js/utils.js` 的 `applyScreenParams`）。注意把「机型身份」与「能力可用性」分开：身份看广播名；**能力看它实际依赖什么** —— 46/55 色（8bpp 索引色 ColorFast/ColorQual）**当前只对 Ark 开放**（只有 ark 的 EPD 驱动实现了 8bpp 播放，`frame_film` 收到会按 4bpp 解出乱码；等其移植 film 2.0 后再放开成"仅按屏幕判定"，见 `tools/ForFilm/js/utils.js` 的 `is8bppAvailable()`），而 SZ 增强因需同屏标定、输出 v1 4bpp，按机型（Pro/Ark）开放

## BLE 协议速览

包格式: `0x55 CH LEN DATA[...] SUM`，SUM = 全部字节之和 & 0xFF, Big-Endian

| 分组 | 范围 | 示例 |
|------|------|------|
| 文件传输 | 0x00-0x08 | START→NAME→LEN→DATA→STOP |
| OTA | 0x10-0x13 | 0x10 LEN → 0x11 DATA×N → 0x13 STOP |
| 设备控制 | 0x20-0x2B | 电量0x23, 休眠0x25-0x2A, SD格式化0x2B |
| WiFi | 0x30-0x3D | 配网0x30-0x38, 下载0x3C-0x3D |
| 心跳/屏幕 | 0x3E-0x42 | 心跳0x3E-0x41, 屏幕参数0x42 |
| App | 0x45-0x4E | 参数0x45-0x4A, 切换0x4B/查询0x4C, 时间同步0x4D, 遥控按键0x4E（0x43/0x44 仅 dock 键盘键值） |

完整命令表: `docs/blecmd/blecmd_protocol.md` 或 `docs/knowledge/ble_commands.md`

## 任务模板

### 新增 BLE 命令
1. 核对协议表中的空闲通道，在目标固件的 `service_ble.h` 定义 `BLE_FILM_TRANS_CH_XXX`；共享命令同步受影响的固件
2. `service_ble.c` 添加 case 处理
3. `ble-utils.js` + `frame.js` 添加同名常量
4. `blecmd_protocol.md` 更新

### 新增 service 子服务
1. `inc/service_xxx.h` + `src/service_xxx.c`
2. `service_init.c` 调用 init
3. 如需 BLE 控制 → `service_ble.c` 添加命令
4. 确认机型差异分支：**仅 `frame_film`**（EPD/输入等用 `FRAMEFILM_*` 宏）；`frame_film_ark` 没有机型宏

### 新增内容 app（film_app/apps/）
1. 建目录 `apps/<name>/`，放 `app_<name>.{h,c}`（头文件与源文件同目录）
2. `CMakeLists.txt` 的 `srcs` 登记 `.c`、`INCLUDE_DIRS` 登记新目录
3. `app_interface.h` 的 `app_id_t` **尾部追加** id（中间插入会让老设备落盘的 id 错位）
4. `core/app_init.c` 注册 `g_app_<name>_entry`（按 `SYS_APP_SWITCH_MODE` 决定是否注册）
5. 菜单入口：`core/app_manager.c` 的 `m_menu_entries[]`（行为表）与 `pages/app_menu.c` 的 `MENU_ITEMS[]`（视觉表）**同序**追加，并同步 `APP_MENU_ENTRY_NUM`
6. 需持久化参数 → `service_param.h` 的 `SERVICE_PARAM_APP_ID_*` 取新槽位并同步 `SERVICE_PARAM_APP_NUM`

## 构建命令

```bash
# ── frame_film（三机型：预设或脚本切换机型与硬件规格）──
cd firmware/frame_film
.\build.ps1                          # Pro n4r2（默认）：sdkconfig_pro，4MB/Quad
.\build.ps1 pro n16r8 flash COM15    # Pro n16r8：sdkconfig_pro_n16r8，16MB/Octal
.\build.ps1 std                      # STD：sdkconfig_std，16MB/Octal
.\build.ps1 max                      # Max：sdkconfig_max，16MB/Octal
# VS Code：命令面板 → "ESP-IDF: 选择项目配置" → std / pro-n4r2 / pro-n16r8 / max
#   对应 CMakePresets.json 的四个预设；机型经 -DFRAMEFILM_MODEL 传入（直接 idf.py build 则取 sys_cfg.h 默认值）
# 屏幕仍在 components/film_hal/inc/hal_epd.h 的对应机型分支内选（EPD_SELECT_* 置 1）

# ── frame_film_ark（通行证版，硬件变体用脚本切换）──
cd firmware/frame_film_ark
idf.py build flash monitor                 # 默认 n4r2：4MB Flash / Quad PSRAM
.\build.ps1 n16r8 flash COM15             # n16r8：16MB Flash / Octal PSRAM
```

> `frame_film_dock` 只有一份 `sdkconfig`；`frame_film_ark` 有默认 `sdkconfig`（n4r2）与 `sdkconfig_n16r8` 两份，用 `build.ps1` → `idf.py -B build_<variant> -D SDKCONFIG=<配置文件>` 切换；VS Code ESP-IDF 插件用命令面板 → `ESP-IDF: 选择项目配置`（`CMakePresets.json` 的 `n4r2` / `n16r8`）。两者都不需要改机型/屏幕宏。CI（`ark-builds.yml`）会矩阵构建这两个变体，Release 仍只发布 n4r2 的 OTA 镜像。

## 文档索引

- `docs/blecmd/blecmd_protocol.md` — BLE 协议完整规范
- `docs/film/film.md` — film 文件格式
- `docs/hardware/hardware_spec.md` — 硬件规格 + 启动流程
- `docs/wifi/wifi_doc.md` — WiFi 功能说明（文档仍标注 Pro 版，待同步）
- `docs/knowledge/` — AI 知识库（项目总览/架构/规范/命令速查）
- `docs/knowledge/ui_layer.md` — UI 层设计（分层/线程模型/刷新策略/校准清单）
- `tools/ui-mockup/index.html` — 设备端 UI 视觉设计稿（双击可开，零依赖）
- `tools/ui-mockup/wallet.html` — NFC 卡包 App 交互原型（480×720 / 1-bit，含 3/4 键返回差异）
- `tools/ui-assets/gen_ui_assets.py` — UI 图标生成（SD 可替换资源 + 内置默认图）
- `tools/framefilm-dock-upload/SKILL.md` — Dock USB 传图工具（用法 / 参数 / 排查表）
