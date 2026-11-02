# Ark Android 客户端

当前开发阶段是 M2 原生 Android 客户端：按 `tools/ForFilm` 的五页结构与 Ark 黑白黄风格重新实现 Android Views，**不使用 WebView**。BLE 保持设备控制连接，传图时临时建立 2.4GHz WiFi Direct GO，由 Ark 下载手机提供的 `.film`。

当前开发优先级（2026-10-08）：网页主题统一与状态收敛后，开始研究 **Flutter 原生界面重构**。现有 Android Views 工程保留为通信实现基线；下述 M1/M2 是已有工程的验证记录，不是 Flutter 的完成记录。取消与重试的实机验收仍为待完成，不因切换框架而视为通过。

## Flutter 重构方案（第一阶段已建立）

新工程位于 [`flutter_app`](../flutter_app/README.md)，已有六页原生框架、双主题、通行证内存预览与 Android BLE 桥接。Dart 静态检查、3 项 widget 测试和 Web 预览构建通过；Android APK 首次构建被 C 盘空间不足阻塞，尚未完成 Android 编译或实机连接验收。旧工程继续作为通信内核来源和既有验证记录。

- 视觉：提供 **明日方舟 / PRTS** 与 **原 ForFilm** 两套完整主题，共享功能、页面状态与草稿；Arknights 采用当前网页已确认的炭灰、黑白、冷蓝体系，不复用旧 Android 黑白黄外观。连接 Ark 成功后进入主题加载界面，同一会话内切页不重复播放。
- 页面：连接、Frame、Film、动画、通行证、设置。全部使用 Flutter widgets，不使用 WebView；手机使用适合触控的导航布局，保留网页功能分组和操作语义。
- 平台内核：复用 Kotlin `ArkSession`、`ArkBleController`、`ArkP2pController`、`FilmHttpServer`、`DirectTransferCoordinator`。Flutter 不再通过另一 BLE 插件建立第二条设备连接。WiFi Direct 群组密码留在原生层。
- 桥接：MethodChannel 发起操作，EventChannel 推送发现设备、连接和 `TransferSnapshot`。发起操作、GATT 写完成、设备回包与最终完成是不同状态；设备改名必须保留现有同通道回包确认。
- 生命周期：会话由 App 级服务对象持有，不随页面切换销毁；终止传输先完成固件取消和临时网络清理。首版不承诺后台常驻、杀进程续传或分片断点续传。
- 图像：首阶段保留 Kotlin `FilmConverter` 的六色/黑白 v1 转换，跨通道传受管理的文件路径和状态；预览变化采用只处理最新请求的调度，避免滑杆积压。高级算法、GIF 编辑与批量队列后续单独迁移。

实施顺序：

1. 配置 Flutter SDK，建立工程和双主题，完成六页导航、连接状态及主题加载预览；接入真实扫描、连接、屏幕参数和设备名称读取。
2. 接入现成 `.film` 文件传输：开始、进度、取消、清理中、失败、重试。使用系统蓝牙稳定的手机完成 START 后取消与同文件重试验收。
3. 接入通行证编辑和 SD 读写、图片转换、设备设置；个人资料沿用网页与固件的格式，读取失败不覆盖草稿，部分文件发送成功明确提示。
4. 再补齐高级图像算法、动画编辑和批量队列；OTA 单独处理已有擦除/超时问题后再验收。

开发环境：Flutter stable 3.47.6 / Dart 3.13.5 已安装到仓库忽略目录 `.output/toolchains/flutter`，新宿主由官方模板生成。Android SDK 目录与 platform-tools 已存在；APK 构建前须解决 C 盘空间，并将大型构建依赖安排到空间充足的磁盘。现有 Android 项目和新宿主均使用 compile/target SDK 35、min SDK 29、JVM 17。

技术依据：[Flutter platform channels](https://docs.flutter.dev/platform-integration/platform-channels)、[Android WiFi Direct](https://developer.android.com/develop/connectivity/wifi/wifip2p)。Android 13+ 的附近 WiFi 权限、旧系统的定位要求及 BLE 权限仍由平台层按系统版本处理。

## M2 实施范围与进度

- 原生页面：连接、Frame、Film、动画、设置。Frame 接入相册、系统相机与本地一言排版；Film 支持预览、旋转、缩放、位置、亮度、对比度、饱和度、抖动和导出。
- Kotlin 转换器生成 Ark 720×480 标准六色或黑白 v1 4bpp 文件，头部、颜色表、180° 像素排列遵循网页与 `.film` 规范。当前量化为 RGB 最近色 + 可选 Floyd–Steinberg，不声称与网页全部高级算法逐像素一致。
- 原生动画页本阶段支持导入有效的单帧/多帧 `.film`；GIF 编辑、逐帧画板、批量队列和 46/55 色、SZ 增强尚未迁移。界面明确显示当前支持范围。
- `ArkSession` 统一持有 BLE；传输期间串行隔离普通设备命令，结束后保留连接。单个本地文件支持取消与重试；重试前先确认旧固件会话和手机直连组已经清理，清理未确认时显示“重试清理”。
- 缓存只保留当前输入；新图片、清除及下次启动清理已放弃的缓存。当前不提供进程被系统终止后的任务续传，也不进行分片断点续传。
- 原生界面、转换和取消/重试代码已完成，APK 构建通过。五页导航和六色编码探针检查通过；发现设备列表被扫描状态清空后已修复并重新构建。START 后取消、同文件重试的实机验收尚未通过，见下方记录。

## M1 基础能力（M2 继续复用）

- 扫描并连接 `FRAMEFILMARK` 或 `FRAMEFILMARK-<后缀>`，列表显示扫描包中的实际设备名；订阅 GATT 通知，通过 `0x42` 读取屏幕参数。
- 设置页通过 `0x54` 读取、`0x55` 保存 Ark 蓝牙设备名。用户只编辑后缀（1–16 个 UTF-8 字节），完整名称最多 29 字节；收到设备同通道通知才视为成功。保存后需重启设备或重新初始化蓝牙才会更新广播名，App 不自动重启。旧固件不支持时约 7 秒超时，可继续使用其他功能。
- 手机创建一个临时的 2.4GHz WiFi Direct 群组，检查群组拥有者身份和实际频率。
- 开发版自动探针在建组前后各读取一次 Ark 屏幕参数，然后释放自己创建的连接和群组。
- `DirectTransferProbe` 会读取原有 WiFi 配置、建立临时群组、通过 Ark 专用的 `0x50/0x51/0x52` 命令发送和查询测试文件，最后比较配置并清理临时资源。M1 的测试文件按钮已由 M2 原生文件上传入口替代。密码只在内存中传给 Ark，不显示在日志。
- PASS 表示 Ark 报告 DONE，接收及总字节数等于打包 asset 的长度，原有 WiFi 配置查询未变，手机临时组清理完成。屏幕显示效果仍需目视确认。

## 构建与安装

需要 JDK 17 或 21、Android SDK Platform 35 和 Build Tools 35.0.0。设置 `ANDROID_HOME`，或在不入库的 `local.properties` 中设置 `sdk.dir`。

```powershell
.\gradlew.bat :app:assembleDebug
adb -s <手机序列号> install -r -g app/build/outputs/apk/debug/app-debug.apk
adb -s <手机序列号> shell am start -n org.framefilm.ark.dev/org.framefilm.ark.MainActivity
```

开发版包名为 `org.framefilm.ark.dev`，与未来正式版分开。应用仅在用户操作后扫描、连接或建组。Android 12+ 需要附近蓝牙权限，Android 13+ 需要附近 WiFi 权限；较旧系统的扫描还需要定位权限和系统定位开关。

## 最小实机探针

先断开电脑网页与 Ark 的 BLE 连接，手机保持蓝牙和 WiFi 开启。不要在已有其他 WiFi Direct 业务运行时测试。探针拒绝接管已有群组。

```powershell
adb -s <手机序列号> shell am instrument -w org.framefilm.ark.dev/org.framefilm.ark.ConnectivityProbe
```

探针必须同时报告 `result=PASS` 和 `cleanupCompleted=true` 才算通过。它调用与界面相同的控制器，不模拟屏幕点击。探针仅编入 debug APK；输出不包含群组密码。

M1 真文件传输探针（同样仅在 debug APK 中）：

```powershell
adb -s <手机序列号> shell am instrument -w org.framefilm.ark.dev/org.framefilm.ark.DirectTransferProbe
```

请先给应用授予附近蓝牙和附近 WiFi 权限，并确保手机没有其他 WiFi Direct 组。异常时探针请求取消，等待 Ark 报告恢复完成后再释放手机 HTTP 服务及直连组；最长可能等待数分钟。

## 原生客户端最小验收

```powershell
adb -s <手机序列号> shell am instrument -w org.framefilm.ark.dev/org.framefilm.ark.NativeAppProbe
```

探针启动实际 MainActivity，确认五页原生控件导航、六色文件头与像素排列，再用生成的测试图执行 START 后取消及同文件重试。只写独立时间戳文件名。该探针不操作系统相册/相机/导出选择器，也不替代全部人工验收。

固件基础功能与设备端通行证界面完成后，再按实际使用继续迁移 App 高级图片算法与动画编辑；本协议不与小米互传私有协议互通。

第三方 ROM 声明支持 WiFi Direct 仅表示具备接口，兼容性以具体手机的实测结果为准。

## 当前验证记录（2026-10-07）

### M2 原生版：构建通过，取消与重试实机验收待完成

- `0.2.0-dev` 调试 APK 编译通过，不包含运行时 WebView 页面。初次原生探针通过五页导航和六色编码检查（文件头、172,832 字节、颜色码与 180° 排列），随后停在设备发现阶段。
- 已修复扫描状态更新清空附近设备列表的问题，探针改为读取实际发现结果；修复版构建日志为本地 `.output/ark/logs/android-m2-native-scan-fix-build.log`。
- 本次重新连接的是小米 9。新版 APK 安装成功，直接启动返回 `Status: ok`；解锁后重新执行探针返回 `Process crashed`。联调期间系统 `com.android.bluetooth` 在 07:03:05 与 07:03:31 再次因 `LE_EXTENDED_CREATE_CONNECTION(0x2043) was not expecting complete event` 中止，与 M0 的系统蓝牙故障一致。尚未完成 START 后取消或重试，不能将此记录为 App 传输通过，也不将系统崩溃直接归因于 App 代码。
- 本地证据：`android-m2-native-probe.log`、`android-m2-native-probe-mi9-unlocked.log`、`android-m2-mi9-bluetooth-crash.log`，均位于 `.output/ark/logs/`。用户暂时无法更换手机，已停止实机测试并保留当前成果；下次使用 M1 已通过的新手机完成剩余验收。本轮未修改或烧录固件。

### 新手机：M1 真文件直传与屏幕显示通过

- Ark 使用 ESP-IDF 5.5.2 构建通过；应用大小 1,884,336 字节，应用分区剩余 61,264 字节。已向 COM15 的 Ark 写入应用区并完成 esptool 校验，保留 NVS 与 SD。烧录前应用备份为本地 `.output/ark/backups/pre-direct-m1-app.bin`。
- Android 调试 APK 构建、安装成功。修复了控制器声明的 Kotlin 递归类型推断错误。
- 首次联调发现 GO 已建立但 IP 地址尚未就绪：`owner=true, groupFormed=true, addressPresent=false, passphrasePresent=true`。客户端改为有限等待真实地址，并在失败时确认本次组是否释放。为调试失败恢复增加 `DirectTransferProbe -e cleanupGroup <精确组名>` 分支；只清理匹配且属于 GO 的组，不修改其他组。
- 实机还暴露出 BLE 通知早于 GATT 写完成的竞态。客户端现在同时等待写成功与设备回包，之后才开始下一条命令。
- 修复后同一 M1 探针返回 `result=PASS`、`cleanupCompleted=true`。Ark 经 WiFi 保存 43,232 字节已知有效的 720×480 单帧 `.film`，状态从 RESTORING 进入 DONE，接收字节数和总长均为 43,232；原有 WiFi 开关、SSID、密码和 API 地址查询结果前后一致。本机原配置为 WiFi 关闭、网络信息为空，尚未验证恢复已有路由器连接。
- 手机系统另确认回到 `InactiveState`，`groupFormed=false`，临时组已释放。测试文件使用独立时间戳名称，未覆盖用户原有图片。
- 本地证据：`.output/ark/logs/build-direct-m1-final.log`、`flash-direct-m1.log`、`android-direct-m1-gatt-fix-build.log`、`android-direct-m1-probe-gatt-fixed.log`。最终传输依据设备 BLE 状态回包；串口捕获在最终传输前已结束，不能声称最终串口日志验证了保存或屏幕刷新。
- 用户已目视确认“已显示新的测试图，正常”，文件传输、保存与实际屏幕显示闭环通过。当前验收限于本次单文件成功路径；取消、断电、满卡、长时间稳定性及吞吐量未做完整回归。

### 新手机：M0 通过

- 型号 `25042PN24C`、设备代号 `dijun`，系统自报 Android 17 / API 37。使用现有 APK 和已授予的附近设备权限执行探针。
- 成功扫描、连接 Ark，发现 GATT 服务并订阅通知，读取到面板 ID `0x02`、`720×480`；手机建立临时 2.4GHz GO 组后，BLE 再次查询仍得到相同屏参。
- 初测 GO 频率为 2462MHz，释放后系统状态回到 `InactiveState`，但客户端收到关闭通道的迟到回调，误报“清理未确认”。已修复：关闭前作废通道身份，忽略主动关闭及旧通道的迟到回调。
- 修复版构建、安装成功；仅对上述流程复测一次，GO 频率为 2412MHz，返回 `result=PASS`、`cleanupCompleted=true`，没有清理误报。修复版已安装到新手机。
- 复测日志：本地 `.output/ark/logs/android-m0-probe-dijun-fixed.log`。本轮只验证手机建组与 BLE 控制通信并存；Ark 尚未通过 WiFi 加入该组，没有进行 WiFi 文件传输或吞吐测试。

### 旧测试手机：小米 9 的安装恢复与 BLE 阻塞

- 空工程和 M0 业务代码的 `:app:assembleDebug` 均构建通过；使用 JDK 21、Gradle 8.11.1、AGP 8.10.1、SDK 35。
- 小米 9（第三方 HyperOS 2 / Android 15、API 35）的 USB 调试已授权，ADB 连接正常。
- 初次流式、非流式 ADB 安装以及手机本地安装都卡在已提交阶段。两个 ADB 会话虽接受取消请求，仍保留占用标记；此前记录的“已清理”不代表系统实际完成释放。
- APK 的 v2 签名校验通过，手机 Download 中的文件 SHA-256 与本地构建产物一致，手机剩余存储约 90GB。没有发现文件损坏、签名或存储不足问题。
- 重启手机并解锁后，**原 APK 未经修改即安装成功**，ADB 命令约 2 秒完成。安装阻塞随系统运行状态恢复而解除，具体卡住的系统组件尚未确定；没有为此修改应用代码或降低 targetSdk。
- 客户端主界面启动命令返回 `Status: ok`，冷启动约 696ms，应用进程存在。这是启动验证，不代表界面视觉验收。
- 首次通信探针因蓝牙运行时权限未就绪而退出，连接资源清理成功。随后确认 BLE 权限已授予，并补齐附近 WiFi 设备权限。
- 补齐权限后的探针已扫描到 Ark 并发起连接，随后返回 `Process crashed`；同一阶段日志记录系统进程 `com.android.bluetooth` 在 `MiuiBluetooth/system/gd/hci/hci_layer.cc:256` 因 `LE_EXTENDED_CREATE_CONNECTION(0x2043) was not expecting complete event` 中止。尚无成功的屏参回包，未进入 WiFi 建组步骤；不能把它记为 BLE 或双通道通过，也尚不能仅凭此断言探针退出的完整因果链。
- 本次安装问题已恢复；第三方 ROM 的 BLE 连接兼容性仍待单独定位。调试证据保存在本地 `.output/ark/logs/android-m0-probe.log` 和 `android-m0-probe-crash.log`，不将手机原始系统日志入库。
