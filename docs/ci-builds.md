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

本机密钥与私有 `key.properties` 保存在 `D:\FrameFilm-signing`，不提交到仓库。`app/flutter_app/tool/flutter.ps1` 默认引用此配置；其他环境可设置 `FRAMEFILM_SIGNING_PROPERTIES`，或使用 `ANDROID_KEYSTORE_PATH` 与上述三个密码/别名环境变量。请将整个签名目录另行安全备份，后续更新必须继续使用同一密钥。旧 debug 签名的正式包不能被新发行签名直接覆盖；`.dev` 开发包可继续并行保留，迁移前需备份旧 App 本地资料。

2026-10-16 本机 Release 构建和 APK 签名验证通过，最终 APK 为 59,802,980 字节，证书 SHA-256：`e5c1b5305670858656fce38cea82614838992aa3326ef3166546af4f9da7245e`。用户授权后，以上四项 Secrets 已加密上传到 `Rafael-ban/FrameFilm`，并回查各项元数据确认存在。这不代表新工作流已经在云端运行或已完成手机安装验收。

2026-10-31 当前仓库 `kirmisaki/FrameFilm` 已重新配置上述四项 Secrets，并改用全新的发行签名：证书 SHA-256 `9a623e563ef3198ee0b0bd34e3b017af69a20d013bfae093906fdf8b3de79299`，keystore 与口令保存在本机 `D:\FrameFilm-signing`（需离线备份）。因为发行签名已更换，旧签名安装的 App 无法覆盖升级，需要卸载后重新安装一次；应用内更新在下载校验阶段会提示签名不兼容。

## Ark 固件

使用 Espressif 官方 `espressif/idf:v5.5.4` 容器，按矩阵构建两个硬件变体，不执行 `set-target` 或重置配置：

- `n4r2`：仓库默认 `sdkconfig`（4MB Flash / Quad PSRAM，`partitions.csv`），构建目录 `build/`，产物名保持原样。
- `n16r8`：`sdkconfig_n16r8`（16MB Flash / Octal PSRAM，`partitions_16mb.csv`），构建目录 `build_n16r8/`，产物名加 `-n16r8` 后缀。

依赖按 `dependencies.lock` 从 Espressif registry 解析；不依赖本机 build/managed_components。

| 硬件 | OTA Artifact | 完整镜像 Artifact | Release 完整镜像 |
| --- | --- | --- | --- |
| N4R2 | `ark-firmware-ota` | `ark-firmware-flash-n4r2` | `frame_film_ark-n4r2-flash.bin` |
| N16R8 | `ark-firmware-ota-n16r8` | `ark-firmware-flash-n16r8` | `frame_film_ark-n16r8-flash.bin` |

OTA Artifact 各含一份 `frame_film_ark.bin` 应用镜像。完整镜像含 bootloader、分区表、OTA 初始化数据和应用，从 `0x0` 串口写入。完整烧录会覆盖镜像范围内的现有数据，先备份设备设置。

`ark-firmware-debug` / `ark-firmware-debug-n16r8` 提供分文件烧录所需的 bootloader、分区表、OTA 初始化镜像和 flash_args，以及 ELF、map、sdkconfig。`ark-firmware-build-logs` / `ark-firmware-build-logs-n16r8` 提供构建、merge-bin、大小报告及 IDF 日志。

两个变体的固件不能互刷：4MB 设备不要使用 n16r8 产物，反之亦然。

## 经典固件 Pro 全屏幕 × 双硬件规格

`frame-film-builds.yml` 只改触发路径内的 `firmware/frame_film/**`。矩阵为 **硬件规格 × 屏幕**：每个组合都通过 `-D FRAMEFILM_MODEL=pro` 指定机型、`-D SDKCONFIG=<绝对路径>` 指定该规格的配置，再在 `hal_epd.h` 的 `FRAMEFILM_PRO` 分支内仅把目标 `EPD_SELECT_*` 置 1，随后执行 `idf.py build`、`idf.py size` 与 `idf.py merge-bin`。不再需要把 `sdkconfig_pro` 复制成 `sdkconfig`。

- 硬件规格：`n4r2`（4MB Flash / Quad PSRAM，`sdkconfig_pro` + `partitions.csv`，双 OTA 各 1536K）与 `n16r8`（16MB Flash / Octal PSRAM，`sdkconfig_pro_n16r8` + `partitions_16mb.csv`，双 OTA 各 4096K）。
- 构建屏幕：E6 3.68" 792×528（默认）、E6 3.70" 720×480、E6 3.60" 600×400、E6 3.64" 760×568、E6 3.98" 768×552、E4 3.98" 768×552（四色）。
- 不构建 1.54"（无驱动）与 7.09"（双 CS/无 DC，仅 Max 接线支持）。
- 每个组合的产物 `frame-film-pro-<规格>-<屏幕标识>` 含 `frame_film.bin`（OTA 应用镜像）、`frame_film_pro_<规格>_<屏幕标识>-flash.bin`（串口合并镜像，从 `0x0` 写入）与 ELF；日志在同一产物名后加 `-logs`。
- 两个规格的固件不能互刷；编译成功不代表对应屏幕已实机验证。
- STD / Max 机型当前只做本机构建预设（见 `firmware/frame_film/CMakePresets.json` 与 `build.ps1`），不进入 CI 矩阵。

### 经典固件正式发布

普通 push / PR 只构建，不发 Release。正式发布有两种触发方式：

- 推送 tag `frame-film-v<版本>`（例如 `frame-film-v2026.11.07.1`），从 tag 源码构建。
- Actions → FrameFilm builds → Run workflow，选择 `main` 并填写 `release_version`（同样的版本格式）。

矩阵全部成功后，`release` job 会先校验 12 个组合齐全、应用镜像头（ESP32-S3 / `frame_film` 工程 / ELF 指纹 / OTA 槽容量）以及完整镜像在 `0x30000` 与 OTA 应用逐字节一致，再生成 `framefilm-classic-firmware.json`（记录硬件规格、屏幕面板 ID、尺寸、大小与 SHA-256）。Release 附件为每个组合的 `*-ota.bin`、`*-flash.bin` 和清单；Notes 内嵌同一份清单，ForFilm 可直接从 Release body 读取，无需额外请求。Release 使用 `--latest=false`，不占用仓库 Latest 标记，也不会被 Ark 客户端按发布时间误选。

## 验证范围

首次远端运行：[Ark builds #37736539292](https://github.com/Rafael-ban/FrameFilm/actions/runs/37736539292)，源码提交 `f9614d3`。已验证 Flutter APK 编译、运行资源检查、APK 上传，以及 ESP-IDF 5.5.2 Ark 固件编译、大小报告、合并镜像与产物上传；两份构建日志也已上传。编译成功和 APK 资源检查通过不代表设备最终文件提交、相机、屏幕刷新或 OTA 已经实测。

参考：[Flutter action](https://github.com/subosito/flutter-action)、[Gradle Java compatibility](https://docs.gradle.org/current/userguide/compatibility.html)、[Espressif 官方 CI 容器](https://github.com/espressif/esp-idf-ci-action)、[IDF 5.5.4 merge-bin 实现](https://github.com/espressif/esp-idf/blob/v5.5.4/tools/idf_py_actions/serial_ext.py)。

## Ark 更新日志发布

编辑 [Ark 更新日志](ARK_CHANGELOG.md) 的 `Unreleased` 内容，再新增一个正式节，例如：

```markdown
## [2026.10.16.1] - 2026-10-16

App: 0.4.1+5

- 本次 Ark 固件或 App 的变更说明。
```

发布 ID 是独立且唯一的 `年.月.日.序号`，日期需与标题一致；`App:` 必须等于本次源码 `app/flutter_app/pubspec.yaml` 的 `version:`。固件单独更新可沿用同一个 APK 版本与构建号；App 有新版本则先提高 `pubspec.yaml` 的构建号。标题会显示 `Ark 发布 ID · App 版本+构建号`。

推送到 `main` 时，只有**新出现的最新正式节**会触发发布：plan job 先创建指向本次源码的 `ark-发布ID` tag，Android 与固件都从该 tag 构建，两个 job 成功后才发布。没有新增正式节的 push 及 PR 只运行 CI。只改已有正式节的文字不会重新发布。首次发布之前保持 `Unreleased` 即可。

构建或发布失败后，可重跑原失败的 Actions 运行。也可从 Actions → Ark builds → Run workflow 选择 `main` 并勾选 `publish_release`，重试当前最新正式节。若 tag 已存在，标题、版本和构建都以 tag 的源码为准。七份附件齐全时跳过发布。若只缺完整镜像，重建的 OTA 必须与已发布文件逐字节一致，才补传缺失镜像。若重建文件不同，新增发布 ID。手动运行不勾选时只构建。

- Release 提供两份 OTA 镜像 `frame_film_ark-n4r2.bin` / `frame_film_ark-n16r8.bin`、两份完整串口镜像、固件 OTA 清单、APK 和 App 更新清单。App 自动更新使用清单中的 OTA 镜像。
- Notes 取对应更新日志节，并附源码 commit 和四份固件镜像的 SHA256。Release 保留仓库原有 Latest 标记。固件客户端从最近 20 个正式发布中按 `published_at` 选含精确 Ark 镜像的候选，App 客户端优先选最高 `versionCode`，相同构建号再比发布时间。
- App 按 JSON `versionCode` 判断升级；固件按构建 ELF SHA256 判断同构建并在重启后确认。屏幕显示的 `3.2.6` 不作升级版本号。
- 客户端无登录查询公开 Releases；只下载后校验导入，不自动刷机。CI 构建和发布不代表真实设备验收。

2026-10-09 已推送提交 `4fa8e77`，[Actions #37910525382](https://github.com/Rafael-ban/FrameFilm/actions/runs/37910525382) 的 Android APK 与 Ark 固件 job 均成功，包含新增 Android 原生更新代码、AOT 资源检查和 APK 元数据生成。此次为普通 push，Release job 按预期跳过；公开发布与 App 内覆盖安装仍需单独验收。

参考：[GitHub Releases API](https://docs.github.com/en/rest/releases/releases)、[gh release create](https://cli.github.com/manual/gh_release_create)。

该次 CI APK 为 `0.4.0+4`，59,475,012 字节，最低 API 28，包含 arm64-v8a / armeabi-v7a / x86_64。下载后 SHA256 与 JSON 元数据一致：`1afd4547b5106d436599b7e59a13ffb93f1725f573a0007331062998ece3c13b`。本地路径为 `D:/dev-tool/FrameFilm-build/ci/37910525382/extracted/app-release/framefilm-ark-flutter.apk`。本轮本机构建因工具审批超时未执行，交付的是 CI 产物。


### App 的签名发行渠道

App 更新源与设备固件源分别处理。`FRAMEFILM_APP_RELEASE_REPO` 是 Flutter 的编译参数，默认 `kirmisaki/FrameFilm`；`ark-builds.yml` 将当前 `GITHUB_REPOSITORY` 传入，保证各仓库产出的 APK 继续查询同一签名渠道的应用发布。设备 OTA 更新仍查询官方仓库。

本机使用 Rafael-Ikaros 的原发行密钥时，先设置 `$env:FRAMEFILM_APP_RELEASE_REPO='Rafael-Ikaros/FrameFilm'`，再运行 `app/flutter_app/tool/flutter.ps1 build apk --release`；脚本会传入相应 dart-define。直接调用 Flutter 时，显式增加 `--dart-define=FRAMEFILM_APP_RELEASE_REPO=Rafael-Ikaros/FrameFilm`。构建仓库配置不会更换密钥；发布者仍需继续使用原签名，避免旧用户安装失败。
