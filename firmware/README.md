# 固件目录

本目录包含帧影（FrameFilm）电子胶片冰箱贴的设备固件代码。

## 固件工程

| 工程 | 用途 | 设备名 |
|------|------|--------|
| [`frame_film/`](frame_film/) | 冰箱贴本体固件（**三机型**，旧机型维护线） | `FRAMEFILM` / `FRAMEFILMPRO` / `FRAMEFILMMAX` |
| [`frame_film_ark/`](frame_film_ark/) | 通行证版固件（**单机型**，app 框架主线） | `FRAMEFILMARK-<后缀>`（旧版无后缀） |
| [`frame_film_dock/`](frame_film_dock/) | 充电底座固件（单机型） | `FRAMEFILMDOCK` |
| [`frame_film_c3/`](frame_film_c3/) | 独立 C3 + JD79665AA 四色机型（实验性、未实屏验收） | `FRAMEFILMC3` |

上述工程均基于 ESP-IDF 构建，采用相同的三层分层架构。C3 的存储与板级能力边界见其独立 README。

> **app 框架与 UI 层（`film_app` + `film_ui`）只存在于 `frame_film_ark`**；`frame_film` 是经典固件，没有这两个组件，也不实现 app 通道 BLE 命令（`0x45~0x4E`）。

## Ark 当前开发顺序（2026-10-07）

1. **基础功能收敛**：保留现有存储恢复、持续呼吸灯、BLE 与手机 WiFi 直传能力。复查补上入睡前等待 app/UI 停止和 BLE 文件关闭的确认，未完成时延后卸载存储与外设断电；文件已清理后重复取消按成功处理。保留此前的按键释放门闸与入睡事件冻结。
2. **电脑预览**：通过 [`tools/ark-ui-simulator`](../tools/ark-ui-simulator/) 编译实际 LVGL 页面源码，提供桌面窗口和键盘操作。布局与文案先在电脑预览，再烧录确认电子纸刷新效果；模拟器不代表无线、存储、电源和刷新时序通过实机验证。
3. **明日方舟本体风格与个人通行证**：按用户最新方向采用罗德岛档案风格，保留横向轮播、图标、反白卡片、切角与信息面板。设置页保留单页布局，在“系统参数”中加入“语言 / Language”，共 18 行；分组标题收紧高度并加宽黑底，参数行高不变。中文沿用当前文案，English 对照汉化前 `515b9b0` 的原始英文，新个人档案功能另补英文。状态栏、开机及休眠品牌均为罗德岛文字标识，主题不随语言退回终末地。逻辑画布为 480×720，中文使用子集字库。旧 `tools/ui-mockup/index.html` 仅供参考原有布局，不代表当前主题。
4. **最后完善 Android App**：现有原生版暂作联调工具，只修复妨碍固件联调的问题，高级功能和 UI 完善放在固件之后。

当前 Ark 固件版本为 **3.2.5**，设备显示、BLE 固件信息和 ESP-IDF 应用元数据保持一致。2026-10-08 的休眠修复版已用 ESP-IDF 5.5.2 编译通过：应用大小 **1,931,888 字节**，1900 KiB 应用分区剩余 **13,712 字节**。构建证据为本地 `.output/ark/logs/firmware-3.2.5-sleep-final-build.log`。此前针对实际 `service_monitor.c` 的 [`host_sleep`](frame_film_ark/tests/host_sleep/) 测试通过，覆盖关闭自动休眠、手动休眠、直传退出等待、按键释放、app/UI 与 BLE 未完成停止的门闸；[`host_storage`](frame_film_ark/tests/host_storage/) 通过，新增上传失败后重复取消的检查；本轮未重复运行这些无关测试。

语言默认简体中文，设置页单击确认切换后即时生效，保存于 app7 的 v2 三字节配置。读取兼容旧 v1 两字节配置，保留已有开机画面和启动应用选择；未改变服务层整体配置结构。个人资料图中的用户文字不自动翻译。真实 `app_boot_cfg.c` 的小范围 host test 已验证 v1 迁移、v2 保存和重载；模拟器中英文设置页切换通过，截图位于 `.output/ark/language-preview/`。

中英文版烧录后，COM15 实际启动日志确认 `boot_page=0 start=0 language=0`，电子纸初始化成功并进入主菜单 `app_id=5`，该次启动未见崩溃。日志为 `.output/ark/logs/language-device-boot.log`；这不代替物理屏幕观察或设备上切换到英文后的重启测试。

通行证支持头像、代号、编号、所属与签名。可编辑源数据（含裁剪后的头像）存在 SD 的 `/app/pass/profile.json`，显示资源存在 `/app/pass/profile.bin`（440×608、1bit FFUI）。[`tools/ark-pass-editor`](../tools/ark-pass-editor/) 提供本地网页编辑、导入导出、BLE 上传与 `0x53` 读取资料；后续 Android 可复用相同格式和接口，当前原生 App 尚未接入编辑页。设备进入通行证或单击确认时加载，读取失败保留已显示资料。预渲染避免任意中文输入占用完整中文字库。

个人档案最小验证通过：Edge 编辑器的中文/头像导入导出与草稿恢复；模拟 BLE 的资料读取、未配置时保留草稿、双文件上传和取消；实际 LVGL 源码模拟器的通行证显示、重载失败保留与重试。截图在 `.output/ark/logs/pass-device-preview/`。2026-10-07 已备份 COM15 Ark 的完整 Flash 并烧录通行证版及随后中英文版，写入哈希校验通过，未格式化 SD。随后网页与 Windows 原生 BLE 均成功读取设备资料；一次软件重启后读回与重启前逐字节一致。用户确认从中文切换后，重启主菜单仍显示英文，语言保存实机验证通过。上传中断/重试不因此视为完成实机验收。

3.2.5 新增设备名称：默认 `FRAMEFILMARK-` 加完整 BLE MAC 的 12 位十六进制后缀。网页编辑器、ForFilm 和原生 Android 设置页可通过 0x54/0x55 查询/修改后缀（1–16 个 UTF-8 字节），允许中文。名称独立保存到 NVS，不改变已有参数或 SD 资料；保存后重启生效。自定义名称不做全局查重，客户端仍通过设备地址/标识区分同名设备。新名称模块的 [`host_ble_name`](frame_film_ark/tests/host_ble_name/) 测试通过，覆盖默认名称、中文与边界、无效输入、NVS 重载与提交失败保留。固件与 Android APK 构建通过，网页修改的 JS 语法检查通过。2026-10-08 串口读 Flash 仍在握手阶段失败、未写设备；重新上电恢复后改用 BLE OTA 成功升级，GATT 0x2A26 已回读到 3.2.5，SD 资料与 OTA 前备份一致。实机验证中文改名、重启后实际广播与 0x2A00 名称一致，随后恢复默认唯一名称并再次验证；扫描脚本首次恢复检查因重复扫描超时，改为一次扫描后复核通过。证据在 `.output/ark/logs/upgrade-325-20261008.log`、`device-name-325-20261008.log` 与 `device-name-restored-20261008.log`。Android APK 尚未安装，网页/App 改名交互不因 BLE 脚本验证而视为完成实机验收。

（2026-10-12 变更：名称逻辑已并入 `service_ble`（`service_ble_device_name_init/set/get`），后缀随系统参数 blob `ServiceBle_Def_t.ble_name` 持久化，不再有独立 NVS key 与 `host_ble_name` 模块测试。）

2026-10-08 排查“长时间运行后灯灭且无法唤醒”：移除 `hal_pwr_enter_sleep()` 对 ext0 唤醒脚 GPIO5 的 `rtc_gpio_isolate()`，该调用会关闭输入并 hold 引脚，与唤醒输入冲突；同时修复 0x26/0x28/0x2A 查询多发送一个未初始化尾字节的问题。升级后严格帧解析已通过。实机通过遥控长按命令进入休眠并断开 BLE 后，用户仅轻按实体确认键，确认菜单与呼吸灯恢复；随后 BLE 回读验证菜单响应、自动休眠仍关闭且 SD 资料未变。升级后断开 BLE 空闲 180 秒，广播持续且重连查询成功。证据在 `.output/ark/logs/manual-sleep-325-20261008.log`、`manual-wake-325-20261008.log` 与 `idle-disconnected-325-20261008.log`。为何自行进入灯灭状态仍未确定：低于 5% 电量的保护性休眠不受自动休眠开关控制，此策略未修改。升级前实机读到电量 100%、自动休眠与定时唤醒均关闭，保持连接空闲 180 秒后仍可响应；这些短时测试不代表数小时运行故障已排除。

**休眠留屏规则**：图片或其他 app 内长按确认键保留当前画面，自动休眠也不主动绘制休眠卡；只有主菜单长按确认键会画休眠卡。入睡会关闭呼吸灯及无线连接。留屏指休眠期间的已完成画面，唤醒后的页面由开机行为配置决定。

2026-10-08 新增并已通过 BLE OTA 烧录的开机/休眠 UI：共用用户最新提供的 `罗德岛Logo.svg` 塔楼徽标（304×256、1-bit，按原比例显示，原青色转为黑色、白底，两侧小字按像素尺寸重新排版），替换此前 BMP 徽标；副标题为 `PRTS SYNTHESIZE INFORMATION ANALYSIS OS`，去掉“罗德岛 · 个人终端”。中文五句、英文四句已核实的游戏彩蛋，每次创建页面总触发率均为 10%，其余留空；首句没有可靠英文，按用户选择仅中文保留。状态栏统一 WIFI / BT，电量最右侧只保留百分比，无 BAT 前缀或电量方块。仅开机页的 ARKNIGHTS 与 PRTS 全称使用 Novecento Sans Wide 排版的固定文字图片，右倾 12°；其他字体不变，不嵌入原始字体或生成通用字库。

真实页面模拟器 `--prts-preview` 已生成普通/彩蛋的中英文开机与休眠卡及菜单共 24 张截图，位于 `.output/ark/prts-preview/`；新塔楼徽标完整显示，最长英文彩蛋两行完整显示。ESP-IDF 5.5.2 构建通过，当前 UI 版二进制 **1,849,888 字节**，应用分区剩余 **95,712 字节**；原先仅开机标题引用的 Montserrat 48px 字体不再被链接。SVG 细小边饰文字受实际像素数量限制；转换方法、字体与英文彩蛋来源见 [`boot-logo.md`](../tools/ui-assets/boot-logo.md)。

本次实机 OTA 在擦除备用分区时遇到 BLE 超时：串口记录整分区擦除约 7.4 秒，期间约第 5 秒以 `0x08` 连接超时断开，之后擦除正常完成且旧固件继续运行。本次诊断尚未发送固件正文；用户确认后保留该 OTA 会话，重连从第 0 字节发送同一镜像，不再触发擦除。传输约 1075 秒，设备确认接收 1,849,888 字节、OTA 成功并从 `ota_0`（0x30000）启动；启动 ELF SHA256 前缀 `6881139cf` 与本地产物匹配，进入开机页后转到主菜单。随后 BLE 回读确认资料与升级前一致、默认唯一名称保留、自动休眠和定时唤醒仍关闭。证据为 `.output/ark/logs/prts-ota-erase-diagnosis.log`、`prts-ota-authorized-serial.log` 和 `prts-ota-continuation-result.log`。此次只绕过了该次擦除超时，未修改固件 OTA 擦除策略；屏幕实际清晰度、新休眠卡及长时间稳定性仍待实机目视/使用验收。

中文适配继续编译同一份固件页面源码；模拟器最小导航检查通过，包括从通行证、设置、时钟返回时保留对应图标高亮，截图在 `.output/ark/logs/full-cn-preview/`。字库来自 LVGL 自带的 Source Han Sans SC（SIL OFL，许可证保存在 `tools/ark-font/OFL.txt`），修改中文文案后运行 `python tools/ark-font/gen_fonts.py`（需 Pillow）。14/18px 字库自动收集页面文案；24/36px 只包含标题，为节省 Flash，改大字号标题时也须更新生成器 `LARGE_TEXT`。修改布局使用稳定模式，优先保持原设计。

尚未完成的设备验收：长按/松手与直传取消期间的屏幕、供电行为，以及长时间运行后失响应的触发原因。Android 取消与同文件重试仍未通过完整实机验收；已有路由器连接的恢复、断电/满卡和长时间稳定性也未据此宣称完成。基础收敛以具体修复和证据逐项推进，不将编译或桌面预览等同于全部基础功能通过。

## 硬件平台

- 主控芯片：ESP32-S3
- 显示屏：彩色电子纸（E6 系列）
- 通信模块：蓝牙 BLE
- 开发框架：ESP-IDF v5.5.2（C 语言）

## 目录结构

三个工程的三层结构一致；`frame_film_ark` 另有 `film_ui`（LVGL UI 层）与 `film_app`（app 层）两个组件：

```
frame_film/
├── main/                 # 固件入口（app_main）
├── components/
│   ├── film_sys/         # 系统层：配置、日志、错误码、系统初始化
│   ├── film_hal/         # 硬件抽象层：EPD、输入、SD、LED、电池、电源
│   ├── film_service/     # 服务层：BLE、文件传输、film 播放、OTA、WiFi、参数
│   ├── film_ui/          # LVGL UI 层（仅 frame_film_ark）
│   └── film_app/         # app 层：开机画面/主菜单/内容 app/系统设置（仅 frame_film_ark）
├── partitions.csv        # 分区表（NVS + 双 OTA）
├── CMakeLists.txt
├── sdkconfig             # 当前生效的 SDK 配置
└── sdkconfig_{std,pro,max}  # 各机型 SDK 配置（仅 frame_film；ark/dock 只有一份 sdkconfig）
```

## 机型与屏幕（仅 `frame_film`：三机型，屏幕可自由选择）

> 本节及其中的屏幕支持列表**只适用于 `frame_film/`**。`frame_film_ark/`（通行证版）是单机型固件，没有机型宏与屏幕切换，规格固定，见本节末尾。

统一固件 `frame_film/` 的硬件由两处编译期配置共同决定：

1. **机型**（`sys_cfg.h`，三选一）——决定输入设备、SD/电池/LED 引脚、Flash/PSRAM、设备名等
2. **屏幕**（`hal_epd.h`）——在对应机型分支内，从屏幕支持列表里选一款，把对应 `EPD_SELECT_E6_*` 置 `1`、其余置 `0`

| 机型 | 机型宏 | sdkconfig | 输入 | 默认屏幕 |
|------|--------|-----------|------|----------|
| 基础版 STD | `FRAMEFILM_STD` | `sdkconfig_std` | 旋转编码器 | E6 3.6" 600×400 |
| Pro 版 | `FRAMEFILM_PRO` | `sdkconfig_pro` | 三按键 | E6 3.68" 792×528 |
| Max 版 | `FRAMEFILM_MAX` | `sdkconfig_max` | 三按键 | E6 7.09" 1200×1600 双面板 |

屏幕支持列表（`hal_epd.h` 内 `EPD_SELECT_E6_*` 宏）：

| 宏 | 屏幕 | 分辨率 | 面板 ID | EPD 驱动 |
|----|------|--------|---------|----------|
| `EPD_SELECT_E6_3_68_792_528` | E6 3.68" | 792×528 | 0x01 | `hal_epd_368.c` |
| `EPD_SELECT_E6_3_70_720_480` | E6 3.70" | 720×480 | 0x02 | `hal_epd_370.c` |
| `EPD_SELECT_E6_3_60_600_400` | E6 3.6" | 600×400 | 0x03 | `hal_epd_360.c` |
| `EPD_SELECT_E6_1_54_240_240` | E6 1.54" | 240×240 | 0x04 | — |
| `EPD_SELECT_E6_7_09_1600_1200` | E6 7.09" 双面板 | 1200×1600 | 0x05 | `hal_epd_709.c` |

> 例：Pro 版硬件若为 720×480 面板，在 `hal_epd.h` 的 `FRAMEFILM_PRO` 分支内把 `EPD_SELECT_E6_3_68_792_528` 改为 0、`EPD_SELECT_E6_3_70_720_480` 改为 1 即可。

机型差异（输入设备、SD/电池/LED 引脚）用 `FRAMEFILM_STD/PRO/MAX` 宏隔离；屏幕差异用 `EPD_SELECT_E6_*` 宏隔离。

### frame_film_ark（通行证版，单机型）

| 项 | 固定值 |
|----|--------|
| 设备名 | `FRAMEFILMARK` |
| 屏幕 | E6 3.70" 720×480（面板 ID `0x02`，驱动 `hal_epd_370.c`） |
| 输入 | 三按键：上 GPIO6 / 下 GPIO4 / 确认 GPIO5（**低电平有效**） |
| LED / 电池 / SD | 有 WS2812（呼吸灯：白=未连接、绿=已连、红=低电）、有电池检测、SD 有检测脚 |
| 唤醒脚 | GPIO5（低电平） |
| Flash / PSRAM | 4MB / Quad SPI |
| sdkconfig | 只有一份 `sdkconfig`，无需 `cp` |

相对 `frame_film` **已删除**：`FRAMEFILM_STD/PRO/MAX` 机型宏、全部 `EPD_SELECT_E6_*` 屏幕选择宏、其余屏幕驱动（`hal_epd_360.c` / `hal_epd_364.c` / `hal_epd_368.c` / `hal_epd_709.c`）、STD 专用编码器实现（`hal_encoder.c`）、`sdkconfig_{std,pro,max}`。

Ark 当前以板载 SDNAND 模式运行（`hal_sd.c` 中 `SD_USE_SDNAND=1`），此模式不启用插卡检测。正常开机挂载失败不会自动格式化；检查存储后可重启重试。需要初始化空白存储或明确清空数据时，使用现有 BLE `0x2B` 格式化命令；该命令会删除存储内容，未挂载但硬件可访问的存储也可通过此入口重新初始化。

Ark 上传先写临时文件，完整关闭后才提交正式文件；同名替换失败保留旧内容。BLE 中断后从 `FILE_START` 重传，WiFi 与 BLE 的保存会话互斥。文件命令尚无应用层保存结果 ACK，客户端的发送进度不等于设备保存或显示结果，详见 BLE 协议 §4.2。

存储专项测试：在带 C 编译器的 Linux/WSL 中，从 Ark 工程执行 `sh tests/host_storage/run.sh`。测试直接调用实际文件服务代码，使用临时目录模拟短写、关闭失败、提交失败和重启残留恢复；包含完整 MonoFast 帧、同名替换、错误文件头及跨目录静默保存。它不验证 FreeRTOS 并发、实际 BLE/WiFi 链路、SDNAND 断电行为或屏幕显示，也不代替 ESP-IDF 5.5.2 固件构建。

## 架构

```
film_service → film_hal → film_sys → ESP-IDF
  (服务层)     (硬件层)   (系统层)
```

依赖关系单向，不可逆：

- **film_sys（系统层）**：机型配置、日志、错误码、系统初始化
- **film_hal（硬件抽象层）**：EPD 驱动、按键/编码器、TF 卡、WS2812 LED、电池、电源管理
- **film_service（服务层）**：BLE 通信与 GATT、文件传输、film 播放、OTA、WiFi、参数存储

## 主要功能

- 彩色电子纸显示驱动（6 色编码：黑/白/绿/蓝/红/黄）
- BLE 连接与照片数据传输
- `.film` 文件解码与胶片滤镜处理
- OTA 固件升级
- WiFi 配网与图片下载
- 低功耗管理与电池电量监测
- LVGL UI 层与 app 框架（开机画面 / 主菜单 / 图片 / 模板 / 时钟 / 动图 / 通行证 / 系统设置）—— 仅 `frame_film_ark`

## 构建说明

使用 ESP-IDF v5.5.2 进行开发和烧录（非 PlatformIO）。

```bash
# frame_film（三机型：先选机型与屏幕）
cd firmware/frame_film
cp sdkconfig_std sdkconfig               # 按机型选 sdkconfig_{std,pro,max}
# 编辑 components/film_sys/inc/sys_cfg.h，置对应机型宏为 1（三选一）
# 编辑 components/film_hal/inc/hal_epd.h，在机型分支内选择目标屏幕（EPD_SELECT_E6_* 置 1）
idf.py build flash monitor

# frame_film_ark（通行证版，单机型：无配置步骤）
cd firmware/frame_film_ark
idf.py build flash monitor

# frame_film_dock（底座，单机型：无配置步骤）
cd firmware/frame_film_dock
idf.py build flash monitor
```

## 文档索引

- [`docs/blecmd/blecmd_protocol.md`](../docs/blecmd/blecmd_protocol.md) — BLE 协议完整规范
- [`docs/film/film.md`](../docs/film/film.md) — film 文件格式
- [`docs/hardware/hardware_spec.md`](../docs/hardware/hardware_spec.md) — 硬件规格 + 启动流程
- [`docs/knowledge/`](../docs/knowledge/) — AI 知识库（架构/规范/命令速查）
