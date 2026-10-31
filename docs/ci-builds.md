# 自动构建

工作流：

- `.github/workflows/ark-builds.yml`：Flutter Android app 与 `firmware/frame_film_ark`。
- `.github/workflows/frame-film-builds.yml`：经典固件 `firmware/frame_film` 的 Pro 机型，按屏幕逐项构建。

Dock 与 `frame_film_c3` 暂不构建。

## 触发与下载

- 推送到 `main`、向 `main` 提交 PR 时，限定 Flutter、复用的 Android Kotlin 源码、Ark 固件、Ark 更新日志及工作流自身路径。
- main 推送或目标为 main 的 PR 自动构建；工作流进入默认分支后，也可从 GitHub → Actions → Ark builds → Run workflow 手动运行。
- Android 与固件 job 独立运行，完成后从该次运行的 Artifacts 下载。构建失败时保留已生成的日志；SDK 安装或 checkout 阶段失败请查看 Actions 步骤日志。

## Android

固定 Flutter 3.47.6（随附 Dart 3.13.5）、Temurin JDK 21；Gradle 9.3.1、AGP 9.1.0、Kotlin 2.4.0 与 Android SDK 配置由仓库文件控制。JDK 21 用于运行构建，应用仍保持 Java/Kotlin 17 编译目标，最低安装系统 Android 9（API 28）。CI APK 包含 Flutter 默认支持的多个 Android ABI，本机快速验证包为 arm64。

仓库忽略 Gradle wrapper 脚本和 jar。CI 使用同版本 Flutter 在临时目录生成 Android 项目，仅复制 wrapper 脚本/jar，不覆盖仓库中的 Gradle 配置和业务代码。

产物 `ark-flutter-release-apk` 包含构建原件 `app-release.apk`，以及用于 GitHub Release 的精确命名附件 `framefilm-ark-flutter.apk`、`framefilm-ark-flutter.json`。JSON 记录包名、versionName、versionCode、最低 Android 版本、APK 大小和 SHA-256；上传前检查 `libapp.so` 与 `libflutter.so` 存在且没有 debug 的 `kernel_blob.bin`。日志位于 `ark-flutter-build-logs`。

Release/AOT APK 使用独立发行密钥，缺少配置时不会回退到 debug 签名。按 [Component_Manager 的发行工作流](https://github.com/Rafael-ban/Component_Manager/blob/master/.github/workflows/release.yml) 配置四项仓库 Actions Secrets：`ANDROID_KEYSTORE_BASE64`、`ANDROID_KEYSTORE_PASSWORD`、`ANDROID_KEY_ALIAS`、`ANDROID_KEY_PASSWORD`。工作流在 runner 临时目录恢复 keystore，将路径及签名参数交给 Gradle，结束后删除临时密钥。Fork PR 无法访问这些 Secrets，因此不能完成发行签名 job。

未配置上述 Secrets 的普通 push / PR 只构建 debug APK（`ark-flutter-debug-apk`）用于编译验证，不会产出 Release 附件；只有计划发布（`publish == true`）时缺少签名才会以明确错误失败。需要正式发布时请先在仓库配置四项 Secrets。

本机密钥与私有 `key.properties` 保存在 `D:\FrameFilm-signing`，不提交到仓库。`flutter_app/tool/flutter.ps1` 默认引用此配置；其他环境可设置 `FRAMEFILM_SIGNING_PROPERTIES`，或使用 `ANDROID_KEYSTORE_PATH` 与上述三个密码/别名环境变量。请将整个签名目录另行安全备份，后续更新必须继续使用同一密钥。旧 debug 签名的正式包不能被新发行签名直接覆盖；`.dev` 开发包可继续并行保留，迁移前需备份旧 App 本地资料。

2026-10-16 本机 Release 构建和 APK 签名验证通过，最终 APK 为 59,802,980 字节，证书 SHA-256：`e5c1b5305670858656fce38cea82614838992aa3326ef3166546af4f9da7245e`。用户授权后，以上四项 Secrets 已加密上传到 `Rafael-ban/FrameFilm`，并回查各项元数据确认存在。这不代表新工作流已经在云端运行或已完成手机安装验收。

2026-10-31 当前仓库 `kirmisaki/FrameFilm` 已重新配置上述四项 Secrets，并改用全新的发行签名：证书 SHA-256 `9a623e563ef3198ee0b0bd34e3b017af69a20d013bfae093906fdf8b3de79299`，keystore 与口令保存在本机 `D:\FrameFilm-signing`（需离线备份）。因为发行签名已更换，旧签名安装的 App 无法覆盖升级，需要卸载后重新安装一次；应用内更新在下载校验阶段会提示签名不兼容。

## Ark 固件

使用 Espressif 官方 `espressif/idf:v5.5.2` 容器与仓库现有 `sdkconfig`（ESP32-S3、自定义 OTA 分区），不执行 `set-target` 或重置配置。依赖按 `dependencies.lock` 从 Espressif registry 解析；不依赖本机 build/managed_components。

`ark-firmware-ota` 仅包含 `frame_film_ark.bin`，这是现有 OTA 流程需要的应用镜像。`ark-firmware-debug` 包含：

- `frame_film_ark-flash.bin`：按构建生成的 flash_args 合并的串口烧录镜像，从地址 `0x0` 写入；此操作会覆盖镜像范围内的现有数据，应先备份设备数据。
- bootloader、partition-table、OTA 初始化镜像，以及 flash_args/flasher_args.json：按该次构建生成的偏移进行分文件烧录。
- ELF、map、sdkconfig：用于定位问题和核对构建配置。

`ark-firmware-build-logs` 包含构建、merge-bin、`idf.py size` 报告及生成的 IDF 日志。CI 不连接设备、不刷机。普通构建只上传 Artifacts。

## 经典固件 Pro 全屏幕

`frame-film-builds.yml` 只改触发路径内的 `firmware/frame_film/**`。每个矩阵项都从 `sdkconfig_pro` 复制生成 `sdkconfig`，再在 `hal_epd.h` 的 `FRAMEFILM_PRO` 分支内仅把目标 `EPD_SELECT_*` 置 1，随后执行 `idf.py build`、`idf.py size` 与 `idf.py merge-bin`。

- 构建屏幕：E6 3.68" 792×528（默认）、E6 3.70" 720×480、E6 3.60" 600×400、E6 3.64" 760×568、E6 3.98" 768×552、E4 3.98" 768×552（四色）。
- 不构建 1.54"（无驱动）与 7.09"（双 CS/无 DC，仅 Max 接线支持）。
- 每个屏幕的产物 `frame-film-pro-<屏幕标识>` 含 `frame_film.bin`（OTA 应用镜像）、`frame_film_pro_<屏幕标识>-flash.bin`（串口合并镜像，从 `0x0` 写入）与 ELF；日志在同一产物名后加 `-logs`。
- `sdkconfig_pro` 为 4MB Flash / Quad PSRAM 配置；编译成功不代表对应屏幕已实机验证。

## 验证范围

首次远端运行：[Ark builds #37736539292](https://github.com/Rafael-ban/FrameFilm/actions/runs/37736539292)，源码提交 `f9614d3`。已验证 Flutter APK 编译、运行资源检查、APK 上传，以及 ESP-IDF 5.5.2 Ark 固件编译、大小报告、合并镜像与产物上传；两份构建日志也已上传。编译成功和 APK 资源检查通过不代表设备最终文件提交、相机、屏幕刷新或 OTA 已经实测。

参考：[Flutter action](https://github.com/subosito/flutter-action)、[Gradle Java compatibility](https://docs.gradle.org/current/userguide/compatibility.html)、[Espressif 官方 CI 容器](https://github.com/espressif/esp-idf-ci-action)、[IDF 5.5.2 merge-bin 实现](https://github.com/espressif/esp-idf/blob/v5.5.2/tools/idf_py_actions/serial_ext.py)。

## Ark 更新日志发布

编辑 [Ark 更新日志](ARK_CHANGELOG.md) 的 `Unreleased` 内容，再新增一个正式节，例如：

```markdown
## [2026.10.16.1] - 2026-10-16

App: 0.4.1+5

- 本次 Ark 固件或 App 的变更说明。
```

发布 ID 是独立且唯一的 `年.月.日.序号`，日期需与标题一致；`App:` 必须等于本次源码 `flutter_app/pubspec.yaml` 的 `version:`。固件单独更新可沿用同一个 APK 版本与构建号；App 有新版本则先提高 `pubspec.yaml` 的构建号。标题会显示 `Ark 发布 ID · App 版本+构建号`。

推送到 `main` 时，只有**新出现的最新正式节**会触发发布：plan job 先创建指向本次源码的 `ark-发布ID` tag，Android 与固件都从该 tag 构建，两个 job 成功后才发布。没有新增正式节的 push 及 PR 只运行 CI。只改已有正式节的文字不会重新发布。首次发布之前保持 `Unreleased` 即可。

构建或发布失败后，可重跑原失败的 Actions 运行；它保留原始源码，已存在的 tag 会继续用于补发。也可从 Actions → Ark builds → Run workflow 选择 `main` 并勾选 `publish_release`，重试当前最新正式节；若 tag 已存在，标题、版本和构建都以 tag 的源码为准。若 Release 已创建但附件不全，会补传三份 Ark 附件并更新说明；三份已齐全则跳过重复发布。若需要新内容，新增一个 ID。手动运行不勾选时只构建。整个流程不回写版本提交，现有四项 Android 签名 Secrets 保持不变。

- Release 只附原始 `frame_film_ark.bin`、`framefilm-ark-flutter.apk` 和 `framefilm-ark-flutter.json`；合并串口镜像留在 debug Artifact。
- Notes 取对应更新日志节，并附源码 commit 与 OTA 文件 SHA256。Release 保留仓库原有 Latest 标记；固件客户端从最近 20 个正式发布按 `published_at` 选含精确 Ark 镜像的候选，App 客户端优先选最高 `versionCode`（相同构建号再比发布时间）。
- App 按 JSON `versionCode` 判断升级；固件按构建 ELF SHA256 判断同构建并在重启后确认。屏幕显示的 `3.2.5` 不作升级版本号。
- 客户端无登录查询公开 Releases；只下载后校验导入，不自动刷机。CI 构建和发布不代表真实设备验收。

2026-10-09 已推送提交 `4fa8e77`，[Actions #37910525382](https://github.com/Rafael-ban/FrameFilm/actions/runs/37910525382) 的 Android APK 与 Ark 固件 job 均成功，包含新增 Android 原生更新代码、AOT 资源检查和 APK 元数据生成。此次为普通 push，Release job 按预期跳过；公开发布与 App 内覆盖安装仍需单独验收。

参考：[GitHub Releases API](https://docs.github.com/en/rest/releases/releases)、[gh release create](https://cli.github.com/manual/gh_release_create)。

该次 CI APK 为 `0.4.0+4`，59,475,012 字节，最低 API 28，包含 arm64-v8a / armeabi-v7a / x86_64。下载后 SHA256 与 JSON 元数据一致：`1afd4547b5106d436599b7e59a13ffb93f1725f573a0007331062998ece3c13b`。本地路径为 `D:/dev-tool/FrameFilm-build/ci/37910525382/extracted/app-release/framefilm-ark-flutter.apk`。本轮本机构建因工具审批超时未执行，交付的是 CI 产物。
