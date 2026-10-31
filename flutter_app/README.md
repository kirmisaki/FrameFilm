# FrameFilm Ark · Flutter

Flutter 共用界面、Android BLE 桥接与 film / 固件 Wi-Fi Direct 传输。使用 Flutter widgets，不使用 WebView；先在电脑 Web 检查功能和交互，功能集中完成后再编译 Android 并验收实机无线链路，最后统一手机与网页的主题细节。

## 当前范围

### App 统一 WiFi 上传（当前开发）

Android App 的文件上传统一使用 Wi-Fi Direct：Frame（含批量）、Film 和动画使用既有 film 直传，固件升级使用 WiFi OTA，通行证位图及 JSON 使用新增的指定路径文件直传。BLE 保留连接、控制、状态查询与资料回读。ForFilm 网页上传保持 BLE，未增加 WiFi 上传入口。

通行证要求固件 `0x59` 能力 bit1；不支持时提示先升级固件，不自动退回 BLE。App 依次上传 `app/pass/profile.bin`、`app/pass/profile.json`，逐个等待设备保存与网络清理，再回读 JSON 核对。两文件不是原子事务；中断会显示部分完成状态，“重试上次上传”使用缓存从第一份文件重传。临时文件在替换、成功或退出后释放，进程重启清理遗留缓存。编译与模拟测试不代表真实 WiFi/SD/屏幕验收。

本轮验证：通行证取消、重试和清理未确认时的恢复入口共 3 项 Flutter 测试通过；通行证上传与 Film/OTA 页面隔离测试通过；原始文件 HTTP 字节及路径 host probe 通过；ESP-IDF 5.5.2 固件构建通过。Android 使用当前 Flutter Gradle wrapper 检查 arm64 Kotlin 编译，未打包或安装 APK。实机仍需同时更新 App 与固件后验证两文件上传、取消与重试。

- 六页：连接、Frame、Film、动画、通行证、设置。
- 两套视觉：原 ForFilm 暖色圆角、明日方舟 PRTS 炭灰黑白冷蓝。
- 默认自动模式：离线显示 ForFilm，Ark GATT 就绪后切主题并播放一次过场。名称后续回包不会重播；断连立即取消。明确选择 ForFilm 时保持该外观。
- 设置可离线预览明日方舟；主题偏好目前只在当前应用会话内保留。
- Android bridge 接入扫描、权限、连接、断开、0x42 屏参、0x23 电量与 0x54 名字读取。命令同时等待写入完成和同通道回包；超时断开以丢弃旧连接的迟到回包。
- 设置支持读取/保存自动休眠、定时唤醒与 10–2880 分钟间隔、修改设备名称后缀及同步手机时间/时区；保存后核对设备回读，广播名在重启后生效。传输、下载及清理期间禁用设置。
- 通行证支持头像、代号、编号、所属与签名，自动保存本机草稿；读取 SD 的 profile.json、发送 profile.bin + profile.json 并回读资料核对。预览与输出共用 440×608 黑白像素，复用原网页档案格式。设置页同时提供设备按键遥控和页面切换。
- Film 页支持系统文件选择器导入已有 `.film`、格式与 720×480 尺寸校验、Wi-Fi Direct 直传、设备实际字节进度、取消、清理状态与重试。单文件上限 128 MiB；非法文件或取消选择保留上次导入。合法 ASCII 文件名最长 50 字符（包含 `.film`），其他名称映射为本次导入的稳定短名，以预留固件保存后缀。
- 传输通过 BLE 协商手机创建的临时 2.4GHz 直连组，不需要路由器或用户手动开热点。重试从头传输，不是断点续传；只有设备确认保存且连接清理完成才显示成功。
- 导入副本保存在应用缓存；替换、清除与会话结束时释放，进程启动清理上次遗留副本。活动传输和未完成的清理期间不允许替换文件。
- Frame 恢复原网页的拾光、定影、一言、批量上传入口；Film 恢复上传与算法、预览、输出。已有 film 文件导入和 WiFi 直传作为 Film 附加区保留。动画编辑尚未接入。

## 结构与复用

- `lib/app.dart`：原生页面、主题与连接过场。
- `lib/device_gateway.dart`：平台通道、设备快照与页面控制器。
- `android/app/src/main/kotlin/org/framefilm/framefilm_ark/ArkBridge.kt`：唯一 ArkSession、权限、通知读取与系统文件导入。
- Android Gradle 从仓库现有 `../android/app/src/main/java` 引入通信源码，排除旧 Activity/UI，复用相机 Provider；没有复制第二套 BLE/P2P 状态机。
- 桥接在 Flutter 宿主生命周期内持有会话，页面切换不关闭连接；关闭 engine 时释放。暂不承诺后台常驻或进程终止后自动恢复；进程结束后需重新导入文件。
- `assets/rhodes_logo.png` 来自仓库 `tools/ui-assets/boot_logo_mono.png`，源图说明见 `tools/ui-assets/boot-logo.md`。

## Android 9 兼容性

最低 Android 9（API 28）。Android 9 使用公开旧版 `createGroup(channel, listener)`，读取系统生成的名称和密码；系统不提供强制指定或查询组频段的公开 API，若当前 5GHz Wi-Fi 使 Ark 无法连接，请先断开该 Wi-Fi 后重试。Android 10 及以上使用可明确指定 2.4GHz 的接口。蓝牙扫描和旧版直连需要定位授权，部分系统还需要开启定位服务。

直传准备阶段不再发送切换图片 app 的命令，避免先重放旧图。固件保存时按帧数归入图片/动画目录；已处在相应内容页时由固件已有保存事件刷新，其他页面保持原画面。Android 9 尚需对应系统的实机验证，当前连接手机报告 Android 17。

参考：[Android Wi-Fi Direct API](https://developer.android.com/reference/android/net/wifi/p2p/WifiP2pManager)、[API 29 起支持指定频段](https://developer.android.com/reference/android/net/wifi/p2p/WifiP2pConfig.Builder)。

## 工具链与命令

本轮使用 Flutter stable 3.47.6 / Dart 3.13.5。Android compile/target SDK 35、minSDK 28（Android 9）、JVM 17；Release 包名 `org.framefilm.ark.flutter`；显式 debug 构建包名 `org.framefilm.ark.flutter.dev`。Release/AOT APK 使用独立发行签名，本机配置在 `D:\FrameFilm-signing\key.properties`，可通过 `FRAMEFILM_SIGNING_PROPERTIES` 覆盖路径。密钥与密码不进入代码仓库，需安全备份；本机和 GitHub Actions 使用同一密钥。旧 debug 签名正式包无法直接覆盖升级，开发包可并行保留。配置方式见 [自动构建说明](../docs/ci-builds.md)。

本机大型依赖与构建目录已经迁到 D 盘，原 C 盘入口保留 Windows 目录联接（junction）。没有更改系统 PATH，也没有迁移整个 `.codex/worktrees`：其他工作树仍有正在运行的程序和未提交内容。

| 内容 | 本机实际位置 |
|---|---|
| Flutter SDK | `D:\dev-tool\FrameFilm-toolchains\flutter` |
| Android SDK / NDK | `D:\dev-tool\Android\Sdk` |
| Gradle 缓存与分发包 | `D:\dev-tool\gradle-home` |
| Dart Pub 缓存 | `D:\dev-tool\pub-cache` |
| 当前 Flutter 构建输出 | `D:\dev-tool\FrameFilm-build\flutter` |
| 本构建入口的临时文件 | `D:\dev-tool\FrameFilm-build\temp` |
| 当前 Ark 固件 build | `D:\dev-tool\FrameFilm-build\firmware-ark` |

从仓库根目录使用入口脚本。它为本次命令设置 SDK、缓存与 TEMP/TMP，命令结束后恢复进程环境；自动维护忽略的 `local.properties`。执行 `clean` 删除 build 联接后，下次使用入口会重建联接，防止输出重新落到 C 盘。

```powershell
$env:JAVA_HOME = 'C:/Program Files/Java/jdk-25' # 本机安装路径；其他电脑使用自己的兼容 JDK
./flutter_app/tool/flutter.ps1 pub get
./flutter_app/tool/flutter.ps1 analyze --no-pub
./flutter_app/tool/flutter.ps1 test --no-pub test/widget_test.dart
./flutter_app/tool/flutter.ps1 build web --debug --no-pub
./flutter_app/tool/flutter.ps1 build apk --release --target-platform android-arm64 --no-pub
```

这组 D 盘路径是当前电脑的开发配置，不是项目运行依赖。其他电脑可以按标准 Flutter 命令构建；也可以用 `FRAMEFILM_DEV_ROOT` 指定同结构存储目录。不同 checkout 应使用独立存储根目录，避免共享构建输出。已有非联接的 build 目录需要先迁移，脚本不会自动删除。Ark 的 build 联接如被 `fullclean` 删除，也需要重新创建或用 `idf.py -B <D盘目录>` 指定输出位置。

浏览器预览从 `build/web` 启动静态服务，本机地址 `http://127.0.0.1:8770/`。默认仅查看界面；点击“进入模拟设备演示”，或直接打开 `http://127.0.0.1:8770/?preview=1`，可使用同一份页面操作内存模拟设备。Android 默认不会进入模拟模式。

```powershell
./flutter_app/tool/flutter.ps1 build web --debug --no-pub
python -m http.server 8770 --bind 127.0.0.1 --directory D:/dev-tool/FrameFilm-build/flutter/web
```

模拟模式顶部始终显示“模拟设备 / 演示数据，不会连接或修改真实设备”。连接页扫描并连接模拟 Ark 后，在 Film 页选择演示文件并直传，或到设置页选择演示固件并检查升级；无需连接手机或 Ark。

- 正常流程：演示进度、提交及 OTA 重启后确认；传输期间可取消并从头重试。
- 传输失败（重试恢复）：首次传输中断，清理后重试成功。
- 升级结果待确认：传输结束仍保留“待确认”，点击确认后演示重连与构建匹配。
- 相同构建（跳过升级）：演示无需升级的结果，不启动传输。
- 切换场景或点击“重置演示”会取消演示计时器，并清空模拟连接、文件及传输状态。页面关闭释放模拟器。

演示文件仅为内存元信息，不读取本地文件，不发送真实 BLE/WiFi/OTA 命令。模拟验证用于页面与状态交互，不代表硬件功能通过；真实文件解析、权限、蓝牙、Wi-Fi Direct 和 Flash 升级仍需 Android 构建及实机验收。动画编辑和高级图片算法仍是后续功能。GitHub 在线固件功能见下节。

## 验证记录（2026-10-08）

- Flutter analyze：通过，无问题。
- 第一阶段 3 项 widget 测试：通过。覆盖六页导航、离线边界、主题选择、表单切页保留、先连接后补名称、过场结束/断连取消及明确 ForFilm 偏好。测试使用 FakeGateway，不是真蓝牙验收。
- Web 构建通过，电脑实际打开并检查两套主题。
- 首次 Android 构建因 C 盘空间不足失败。迁移依赖、构建输出和临时目录后重试成功，NDK 28.2.13676358 安装于 D 盘。
- Android arm64 debug APK 已生成：`build/app/outputs/flutter-apk/app-debug.apk`。此结果确认 Kotlin 桥接和原生依赖编译通过；不能替代蓝牙实机验收。
- 首次实机出现白屏：旧 APK 缺少 `kernel_blob.bin`，Dart VM 没有应用 isolate。清除迁移残留的 `.dart_tool/flutter_build` 并执行 `pub get` 后重新打包，手机已正常显示页面。C/D 盘别名混用的旧输出记录与 Flutter 增量清理机制是本次构建缺文件的相关证据，不是页面或蓝牙逻辑故障。
- 构建入口新增标准 debug APK 检查：kernel、isolate snapshot 必须非空，VM snapshot 必须存在（本 SDK 的 VM 占位文件为 0 字节，允许为空）。本次修复 APK 为 81,936,838 字节。
- 实机 `25042PN24C`（系统报告 Android 17）已更新安装并完成启动、扫描、连接、首次读取和手动刷新验证：设备名 `FRAMEFILMARK-伊卡洛斯sama`，电量 100%，屏幕 720×480。连接后界面切为 PRTS 主题。
- 第一阶段只读测试仅查询屏参、电量与设备名；第二阶段传输测试记录见下。

## 第二阶段：film 直传与 CI

- `flutter analyze --no-pub`：通过。6 项 widget 测试已通过（其中一项滚动定位失败修正后，仅复测该项）。新增覆盖导入、实际进度、取消、清理与重试状态；FakeGateway 不替代真实设备。
- Android arm64 debug APK 编译、运行资源检查与覆盖安装通过；最终包 81,982,381 字节。`aapt dump badging` 确认 `sdkVersion: 28`，支持安装到 Android 9。
- 实机已成功导入 43,232 字节、720×480 的测试 film；初次直传到达设备保存阶段后返回错误 3。定位到旧 HTTP 服务附加时间戳后，加上固件 `.ffupload.part` 后缀超过 FATFS 64 字符限制；已将实际发送名限制为 50 字符以内并取消每次重试的时间戳前缀，UI 与设备保存名一致。
- 修复后真机：导入通过；取消显示“已取消”，临时服务与连接清理确认；取消后重试可启动并接收完整 43,232 字节，仍在设备提交阶段返回状态 5 / 错误 3。100% 没有误报成功，失败后清理确认。所选 `ark_flutter_test.film` 与仓库样例 SHA-256 完全相同，用户确认 SD 卡可打开已有图片。设备最终保存、显示尚未验收通过，需要后续读取固件提交日志定位；未格式化 SD、未修改设备固件。
- Android 9 分支本轮完成源码与 APK 编译检查，尚无 Android 9 真机结果；不能用 Android 17 手机代替该项验收。
- 自动构建配置及产物说明见 [Ark 自动构建](../docs/ci-builds.md)。分别构建 Android debug APK 和 ESP-IDF 5.5.2 Ark 固件，保留构建日志；首次远端 [Ark builds](https://github.com/Rafael-ban/FrameFilm/actions/runs/37736539292) 的两项编译及产物上传已通过。

## 保存故障定位补充（2026-10-08）

当前结论：WiFi 保存错误已在下述 PSRAM 修复版实机复测通过。以下先保留定位过程的历史记录，最终结果见本节末尾。

- 本轮接入过 Android 9 `TYH201H` 和 `25042PN24C` 两台手机，测试结果分开记录。Android 9 扫描无结果的原因尚未确定，不能判定为权限或机型不兼容。
- `25042PN24C` 已覆盖安装当前本地 APK，手机安装包与本地 SHA-256 均为 `174326575dbc265ebe73bbadc9ecd8db0f0fc336aad5e06424134fbd95c570d6`；扫描、连接成功。
- 当前 APK 导入缓存与仓库测试 film SHA-256 相同，发送名为 `ark_flutter_test.film`。WiFi 实测 UART 仍返回 `state=5 bytes=43232/43232 error=3`，不是仅凭手机 100% 进度判断。
- 同一份 film、同一文件名通过 BLE 静默保存成功，UART 确认 `Film transfer saved: 43232 bytes`。此结果支持继续调查 WiFi 保存路径，不能据此宣称 WiFi 保存已修复。
- 保存诊断固件已增量编译并通过 BLE OTA 部署，大小 1,852,064 字节，镜像 SHA-256 `c4b0c586d0a2cb18330817ef50200f9f63729d7bda80faf0729f474ac66e5abf`，启动 ELF 摘要前缀 `da7026223`；设备恢复蓝牙，通行证档案回读与升级前一致。
- 诊断实测明确失败于文件头校验：`Invalid film color count: 0 (format=0)`，路径 `/sdcard/film/ark_flutter_test.film.ffupload.part`。导入源应为 format=1，故目前证据是 WiFi 路径最终读回头部与源不符；尚未确定差异出现在 HTTP 接收还是存储写入/读回，不能宣称根因或修复完成。
- 后续对照已定位首次差异：HTTP 接收与文件任务 `fwrite` 前的 32 字节均与源文件一致；关闭并重新打开后，读回的 32 字节全为 `00`，偏移 0 为 `C0 → 00`。问题已收窄到写入至读回的存储链路，尚不能区分文件系统、驱动或介质根因；校验继续拒绝异常文件。结构化实机证据见 文件头对照（本地开发记录）。当前无扫描逻辑修改。
- 后续三阶段诊断记录 `http`（接收缓冲）、`prewrite`（文件任务写前）、`readback`（关闭后重新打开读回）的前 32 字节；镜像 SHA-256 `e0e6ae8b8126325dac5676faaccc0fe4aa2f45501525e119814599c9666a1d6b`，大小 1,852,944 字节，已 OTA 部署，启动 ELF 摘要前缀 `3b055b23f`，通行证档案保持一致。

- 对照实测后，源码进一步限定只记录 film 的写前头部，避免记录非 film 显式路径文件；增量编译通过，此日志范围收窄未再次部署，film 路径与已测诊断版一致。

### 最终修复：WiFi 接收缓冲避开 RTC FAST

- 加细诊断发现首个 4096 字节缓冲位于 `0x600fe1e0`（RTC FAST heap），`dma=0`。源缓冲在 `fwrite` 后仍正确，但 `fflush/fsync` 成功后、关闭写句柄之前，另一句柄读回已全零；关闭后 POSIX 512 字节读取也全零，排除仅由 `fclose` 或小块读缓冲造成。
- ESP-IDF 5.5.2 当前 SDMMC 路径对齐检查没有排除 RTC FAST。Ark 启用了 RTC FAST heap，普通 `pvPortMalloc` 因而可能给出 CPU 可读写、SDMMC DMA 不可访问的缓冲。此前成功的 WiFi 测试未记录缓冲地址与实际分块，不能断言当时的具体触发差异。
- 直接申请 4096 字节内部 DMA 内存实测资源不足（`state=5/error=6`）；最终改为 `heap_caps_malloc(want, MALLOC_CAP_SPIRAM)`，由 S3 SDMMC 既有的单 sector DMA 缓冲中转。保留 4096 网络分块上限、文件校验、取消及提交语义；额外同步和重复读取探针未纳入最终修复。
- 最终固件 3.2.5 已 OTA 启动：1,853,200 字节，SHA-256 `c3d4ddda362449dccc7a70fc0c031339abf4b7e1aa47e0b2ab0cf6ea17edb102`，ELF 前缀 `59982a465`。首次电脑重连核验时手机已占用 BLE，后续单独只读核验确认版本及通行证档案保持一致。
- 同一台手机、现有 APK 与 `ark_flutter_test.film` 实测：缓冲位于 PSRAM `0x3c1f04f4`；HTTP、写前、落盘读回的 32 字节完全一致；日志确认正式提交文件及 `state=4 bytes=43232/43232 error=0`。App 显示传输完成、原 WiFi 配置未变、临时服务与连接清理确认。此项为保存链路验收，不代表整文件回读 hash 或实体屏幕显示验收；本轮未扩大为全量回归。
- 结构化证据见 DMA 缓冲修复对照（本地开发记录）。此前取消、清理测试仍以先前记录为准，本轮修复后的验证集中于失败的保存路径。

### 实体显示验收（2026-10-08）

- 通过现有 `0x4B/0x06/0x07/0x08` 精确选择刚保存的 `ark_flutter_test.film`，日志确认完整读取 43,232 字节。用户照片显示图案完整，但以竖屏排线朝下为基准，图标相对参考图逆时针转了 90°；初次“正常”反馈已据此修正，不作为方向通过结论。
- `.film` 导入是原样复制，播放器直接使用面板像素；该测试样例是横向排布。新样例按设备 UI 的 480×720 逻辑画布与 `dx=719-y, dy=x` 映射生成物理 720×480 数据，未修改固件或 App 的全局旋转规则。
- 竖屏样例（本地开发记录） 经同一 App WiFi 传输返回 `state=4/error=0`，设备正式提交并完整加载 43,232 字节，当前索引为 5。用户最终确认“方向和图案都正确，圆点在左上角”。播放模式原本为手动，本轮未更改；App 确认原 WiFi 配置未变、临时连接清理完成。
- 本轮完成实体显示路径验收；固件没有普通 film 文件的内容/hash 回读命令，因此仍不声称完成整文件回读 SHA-256。证据与样例摘要见 显示验收记录（本地开发记录）。
- 保存后旧图重复刷新已在 `9cf19eb` 修复并实机验收：只排队新图，提交到显示请求约70ms，取消旧图额外28.95s彩刷；用户确认显示正常且方向正确。证据保留在本地开发记录中。

## WiFi 固件 OTA（本地开发，未推送）

- 设置页支持导入 Ark 原始应用 `.bin`、检查并升级、实际接收进度、取消、从头重试与重连确认。固件直接写备用 Flash 分区，不经过 SD。
- **版本字符串是彩蛋，仅供显示**，不比较版本大小，不用于判断是否更新。设备 `0x56` 的实际 ELF SHA256 与导入镜像中的 ELF SHA256 相同时提示无需升级；项目与指纹匹配才确认升级成功。完整 `.bin` SHA256 另外用于传输完整性校验。
- 导入检查 ESP32-S3、`frame_film_ark` 项目、镜像分段、长度、checksum 与附加 SHA256；不接收 merged flash 镜像。导入上限4MiB，发送前按设备实际 OTA 分区容量检查；不会改变最低 Android 9 支持。
- BLE控制复用原有直传状态/取消；新增0x56构建信息、0x57暂存固件、0x58应用升级。设备完成校验并恢复原WiFi后进入READY，App清理手机HTTP/P2P后才提交启动分区。提交之前可取消；提交后断连是重启阶段，App仅重连同一设备确认。超时保留“结果待确认”，按钮仅重新核对，不自动重刷。
- 当前没有启用bootloader自动回滚；后台常驻、进程被终止后的升级确认恢复仍不在本阶段承诺范围。旧BLE OTA保留，可用于首次给旧设备接入WiFi OTA能力。
- 验证已完成：4项直接相关Flutter测试、Flutter analyze、Kotlin镜像解析及真实固件解析、同版本不同指纹/空版本定向用例、OTA暂存函数host测试、Ark固件和Android arm64 debug APK构建。`aapt`确认minSdk=28；host OTA摘要接口为stub，不代表真实SHA库或物理Flash验证。
- 已完成首次BLE部署WiFi OTA能力，重启回读实际ELF指纹匹配，通行证档案保持一致。手机 WiFi OTA 已完成 1,854,672 字节升级，重启后 App 确认实际 ELF 指纹 df9b5eabd7cd44ec… 匹配，UART 同时确认新构建启动。新 OTA 中途取消/重试、同构建跳过及本轮 film 冒烟仍待实测；不能用编译结果替代这些验收。

## 下一步

1. 继续完成功能后，再按现有网页统一手机 UI。
2. 完成通行证头像、代号、编号、所属、签名与 SD 读写，增加草稿持久化。
3. 迁移图片转换与高级动画功能；后台保持、进程恢复与长时间稳定性另行验证。

## 可交互 Web 演示验证（2026-10-08）

- 新增 PreviewDeviceGateway 定向测试4项通过；受影响文件 analyze 无问题，Web 构建通过。
- 浏览器检查发现 MaterialApp.builder 外置演示栏不在 Navigator 内，已移到 Scaffold.body 顶部；实际点击场景下拉的 widget 测试复测通过。
- Edge 浏览器已验证模拟扫描/连接、Ark主题切换、film选择、取消后重试并完成；390×844窄屏场景选择与布局已检查。页面无浏览器脚本异常。
- 浏览器探针最初使用文本/分组查找完成区域导致等待超时；实际区域为 progressbar，可访问性状态明确记录完成及完整字节数。修正探针后确认，同样修正了弹出菜单的 menuitem 定位。此问题属于测试定位，不是传输失败。
- 本轮未重装 APK、未连接实体 Ark；OTA异常分支覆盖来自模拟器定向测试，不替代手机OTA验收。GitHub在线下载在后续阶段实现，见下节。

## GitHub 在线固件（2026-10-08，本地实现）

设置 → 固件升级 → 检查 GitHub 发布。客户端无登录访问固定公开仓库 `kirmisaki/FrameFilm`（下载地址兼容旧 `Rafael-Ikaros/FrameFilm`、`Rafael-ban/FrameFilm`），在最近20个正式发布中选择包含精确 `frame_film_ark.bin` 的最新发布时间；忽略草稿和预发布，不比较版本字符串大小。没有适用发布、403/429、网络失败都显示可重试状态，不伪造“有更新”。

Android 点击“下载并校验”后流式保存到临时缓存，显示进度并可取消。限制4MiB及声明长度，若资产提供 SHA256则核对，再复用 `ArkFirmware.inspect` 检查原始ESP32-S3应用镜像和Ark项目身份。成功才替换导入文件；失败、取消和关闭会清理临时副本，保留原导入。下载完成不会自动刷机，仍需连接Ark并点击“检查并升级”，由原流程比对ELF指纹。下载需手机能够访问GitHub，后续手机至Ark仍使用WiFi直传，无需设备接入路由器。

Web真实查询可用，普通Web不提供原生下载与刷机。模拟设备模式独立提供“演示在线固件”，演示下载、取消、失败后重新下载及完成导入；持续模拟标识仍保留。真实查询返回空列表不会自动替换成演示发布。

- Flutter 定向7项测试及受影响文件analyze通过，Android下载helper host probe覆盖正常、截断、超长、空响应、SHA不匹配、HTTP失败和取消。
- Android arm64 debug APK 构建及Dart运行资源检查通过，最低Android9保持不变；本轮未安装手机，不将host与模拟测试当作实际网络下载/生命周期验收。
- 本次公开GitHub API返回HTTP200/空列表。可选Release发布工作流已准备，默认关闭，尚未推送/发布/运行远端验证。详见[构建与发布说明](../docs/ci-builds.md)。

- Web 构建通过，Edge 已实际查询 GitHub（HTTP200/空列表）并完成模拟下载取消、重新下载及导入；未自动执行 OTA。

## 设备设置与页面状态隔离（2026-10-09）

- 新增设备设置的 Flutter 表单、Android BLE 桥接及 Web 模拟操作。未读到实际值时禁止保存；断连清空设备值；普通状态更新保留编辑草稿。休眠开关和间隔 SET 没有 ACK，因此发送成功后 GET 回读核对；改名与时间同步核对对应回包。
- Film 和固件升级各自显示对应类型的进度、取消和重试，共享传输互斥保留。另一类传输活动时只显示去对应页面处理的提示，不把 OTA 面板塞进 Film 的等待文件区域。
- 本机入口默认 Web debug / APK Release，CI 同步改为 `ark-flutter-release-apk`。暂用 debug 签名；Release 包不含 `.dev` 后缀，因此与原 debug 包是两个安装实例。
- 验证：设备设置 5 项、页面隔离 9 项定向测试通过，受影响文件 analyze 无问题；Web debug 与 Android arm64 Release 构建通过。首次 Release 依赖下载出现 TLS 握手失败，Flutter 自动重试后成功，未关闭 Lint。
- Edge 模拟操作已检查间隔、中文改名、时间同步，以及升级期间 Film 页面隔离。输入探针需等待 Flutter 焦点/事件处理，修正探针后通过。
- APK：`D:/dev-tool/FrameFilm-build/flutter/app/outputs/flutter-apk/app-release.apk`，17,967,222 字节，已检查 AOT 应用和 Flutter 引擎存在。尚未安装手机；BLE 保存、断连重连和重启广播名仍需真机验收。没有修改固件或设备数据，未推送远端。
- 下一步仍以功能为先：设备遥控、通行证实际读写与草稿、Frame 图片转换和动画编辑，再统一 UI。
## 通行证与设备遥控（2026-10-09）

- 通行证保留现有网页 v1 JSON 格式；头像中心裁剪为 128×160 PNG。Dart 渲染沿用网页的 440×608 布局和亮度160阈值，FFUI 位图为33,456字节。必填字段允许明确勾选未设置，按绘制宽度校验，签名最多三行。
- 本机草稿：Android SharedPreferences，Web localStorage；切页、断连保留编辑内容。读取设备前提示会覆盖草稿，仅完整校验成功后替换；取消、解析失败不会覆盖。
- Android 通过0x53顺序读取资料；写入沿用既有 BLE 显式路径上传，先 `app/pass/profile.bin`，再 `app/pass/profile.json`。发送后回读JSON逐字核对。两个文件不是原子事务，协议不提供位图保存ACK或位图回读，因此状态明确标注部分完成和实体显示待确认。
- 取消时断连使旧任务失效，草稿仍保留；重新连接后使用“从头发送资料”重试。该操作与设置、Film/WiFi传输、固件下载和OTA互斥；不会修改固件协议。
- 遥控上/下/确认/返回/休眠复用0x4E，方向与原ForFilm遥控一致；回显只表示收到。快捷打开主菜单、通行证、图片、系统设置使用0x4B后0x4C查询确认。休眠后需实体确认键唤醒。
- Web演示支持内存资料读写、发送取消、连接中断后重试；模拟重置清空设备资料，不清除本机草稿。Web普通模式不连接真实Ark。
- 定向schema/位图/草稿/导航/取消互斥测试通过，受影响源码analyze通过；Web debug和Android arm64 Release构建通过，APK为18,180,426字节，临时debug签名、minSdk28不变。本轮未安装手机或写入真实Ark；BLE保存、SD双文件结果与实体显示仍需实机验收。
- 下一步补齐Frame图片转换与动画编辑，再统一UI细节。保持本地提交/合入main，不推送。
- Edge浏览器已完成头像选择、四字段编辑、模拟发送、确认覆盖后回读、遥控点击及跨页草稿保留。最初探针对未滚入视口的按钮和Canvas语义文本定位不当，改用滚动与可访问性group后通过；浏览器未出现脚本异常。

## C5 早期简化转换记录（已由下方 C6 迁移实现替代）

Frame → 选择图片 → 调整构图与色彩 → 生成 film。转换使用 Flutter/Dart 的同一份实现，Web 下载的文件与 Android 导入传输的文件使用相同编码。

- 默认竖屏480×720，沿用此前实体屏幕验收的方向。可旋转90°、缩放、平移，选择填满裁剪或完整显示白底；提供亮度、对比度、饱和度和Floyd–Steinberg抖动。
- 基础六色v1输出172,832字节；黑白MonoFast输出43,232字节。最终预览来自写入film的量化像素。文件头保持设备的720×480物理规格，逻辑竖屏映射为 `dx=719-y, dy=x`。
- 生成支持进度和取消；改变图片或参数后须重新生成，不能把旧结果当作当前画面发送。切页保留本次编辑，清除图片释放内存；图片会话不承诺关闭应用后恢复，通行证草稿的持久化不受影响。
- Web提供真实film下载；在模拟设备模式可转入Film页演示已有进度/取消/重试。生成和下载真实文件不等于向真实设备传输。
- Android桥接新增 `importGeneratedFilm`，校验临时文件后才替换已有导入，失败保留原文件；后续复用现有WiFi直传。按用户要求，本阶段不编译/安装APK，原生新增桥接与实机链路留待功能集中完成后验证。
- 暂不包含ColorFast/ColorQual扩展色板、SZ增强或动画编辑，后续分功能补齐。UI精修仍放在功能完成之后。
- 本阶段定向测试7项通过，Web预览已实际下载并校验六色film头部与大小，并完成Film页模拟发送。浏览器解码使用两端共用尺寸回调接口，避免Web不支持的ImageDescriptor属性。Android新增桥接仍待集中构建及实机验收。

## 以 ForFilm 为基准的迁移状态（2026-10-09，C7）

Flutter 按 tools/ForFilm 的页面职责迁移，原网页不变。Frame 保留拾光、定影、一言、批量四入口；Film 保留图片处理、预览与输出。WiFi 直传、取消重试、OTA 与构建指纹、GitHub 固件获取、通行证和设备设置继续保留。

| 功能 | 当前实现 | 验证边界与差异 |
|---|---|---|
| Frame | 四入口、自动增强、相机、在线/自定义一言、批量转换发送 | 批量仍经现有 WiFi 逐张显示；不是旧 BLE 静默整批上传 |
| Film | 原图构图、拖动缩放、四方向旋转、输出命名、下载/发送；桌面左参数/输出、右预览 | 保留 MonoFast、亮度与铺满扩展选项 |
| 高级算法 | 自适应、55 色 ColorFast、46 色 ColorQual、SZ 增强、Atkinson SZ 校色 | 使用原版算法与色板；自适应缩略图采用双线性，浏览器 medium 采样边界可能导致候选差异 |
| 动画工坊 | 图片/GIF/相机、帧复制删除排序、逐帧构图与同步、画笔/橡皮/填充/取色/撤销、镜像与洋葱皮、设备色预览、多帧 film 导出发送、播放参数 | 48 帧；发送上限 2.5 MiB。原生相机逐张拍摄、自由画布不同于原 Web 常驻相机流与低分辨率像素格 |
| 设备文件 | 读取当前目录、图片/动画目录切换、显示、删除确认、错误与空态 | 目录切换通过 app 切页实现，有明确提示；协议不支持任意目录；ID 回读不表示屏幕已刷新 |
| 主题 | 原 ForFilm 奶油底/硬边/彩色卡片，Ark 黑灰方形面板/黑白按钮；跨页面统一控件、导航及连接入场 | Flutter 字体与原生控件不保证逐像素相同；连接后自动主题规则保持 |
| 保存 | Web 下载；Android ACTION_CREATE_DOCUMENT 导出到用户选择位置 | Android 源码接入，未重新构建 APK 或执行本轮真机验证 |

算法工具：node flutter_app/tool/sync_forfilm_lut.cjs 生成增强 LUT；sync_forfilm_palettes.cjs 生成实测色板；sync_forfilm_sz.cjs 从原 JS AST 生成纯 Dart SZ 数值流程；forfilm_algorithm_fixtures.cjs 生成原 JS 对照测试样例。SZ 不依赖 WebView/JS 引擎。生成器与生成文件一起提交。

46/55 色输出 Film 2.0 8bpp（Format 3/2），预览与文件共享颜色索引；所有格式保留已经实机确认的竖屏逻辑坐标到 720×480 物理排布。动画复用同一转换器，仅拼接主体与更新总字节数/帧数。关闭抖动时，46/55 色仍使用对应色板。

SZ 使用计算时间片控制让出，避免每 32 像素强制 timer 导致 Web 调度延迟。完整 480×720、dart2js O0 独立浏览器验证约 5.9 秒，最大事件循环间隔约 80ms；不是 Android 性能承诺。

本阶段只构建 Web debug。算法源 JS 对照、GIF/多帧编码、绘图填充和设备文件状态使用最小定向测试；Web 模拟连接与真实文件生成分开验收。Android 相机/导出、BLE 文件命令与动画参数下发留到集中 APK 构建和真机验收；不能把 Web 模拟完成称为设备验收。

## 设置、关于与 App 更新（2026-10-09，C8）

- 连接信息显示在连接页；设置页依次容纳设备文件、设备设置、遥控、固件升级、界面主题、App 更新与关于；关于位于最底。关于页署名 App 开发维护 · Rafael-ban，提供 [作者 GitHub 主页](https://github.com/Rafael-ban)、[项目源码](https://github.com/Rafael-ban/FrameFilm)、反馈入口和 GPL-3.0 许可展示。
- 设置 → 应用更新从公开 GitHub Releases 最近 20 个正式发布中查找精确的 `framefilm-ark-flutter.apk` 与 `framefilm-ark-flutter.json`。发布源固定为 `kirmisaki/FrameFilm`（下载地址兼容旧 `Rafael-Ikaros`、`Rafael-ban`）。元数据记录 APK 包名、versionName、versionCode、最低系统版本、大小和 SHA-256；用 Android 当前安装包的 `versionCode` 判断是否有新版本，固件升级仍按实际 ELF 指纹判断。Web 仅查询，演示设备模式只模拟下载和安装。更换发行签名后，旧签名安装包需卸载后重新安装一次。
- Android 更新流程显示下载字节进度，支持取消和失败后重新下载；下载后核对大小、SHA-256、APK 包名、系统版本、versionCode 与现有 App 签名，再由用户打开 Android 系统安装器确认安装。需要安装未知应用权限时跳转系统设置，返回后可继续。下载包暂存在 App 缓存，清除未完成临时文件；已准备好的兼容包可在下次启动恢复。安装是否完成仍以系统结果为准。
- CI 手动发布须先配置 `ARK_DEBUG_KEYSTORE_BASE64`，使用与现有 App 相同的临时 debug 签名；缺少该 Secret 时发布构建失败。提交 `4fa8e77` 已推送，[Actions #37910525382](https://github.com/Rafael-ban/FrameFilm/actions/runs/37910525382) 的 APK 与 Ark 固件构建均通过；APK 为 0.4.0+4，最低 Android 9，59,475,012 字节。此次未发布 Release，App 内覆盖安装尚未验收。此前 C7 的算法、动画、设备文件与主题检查仍按上方矩阵的验证边界理解，不代表全部 UI 逐像素一致。

## 应用内下载入口（2026-10-16）

设置 → 应用更新 → 检查更新 → 应用内下载更新 → 打开系统安装器。Android 复用已有 AppUpdateManager 下载 APK，无需跳转 GitHub 网页；下载显示百分比与 MiB，支持取消及从头重试。下载/校验期间暂停检查更新，防止界面上的发布信息与当前下载任务混淆。

Web `?preview=1` 的“体验下载流程”提供明确标注的本地演示安装包，可在没有正式 Release 时检查进度、取消、重试、准备安装界面；不会进行真实 APK 下载或安装。真实更新仍须仓库 Release 提供 APK 与 JSON 元数据，且安装包签名与当前 App 兼容。Android 系统安装确认不能由网页模拟验收代替。
