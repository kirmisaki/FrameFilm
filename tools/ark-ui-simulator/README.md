# Ark LVGL 电脑模拟器

这是**真实 Ark 页面 C 源码**的桌面预览。CMake 直接编译固件中的开机、主菜单、设置、休眠卡、通行证、时钟页面，以及 `app_shell`、图标资源；LVGL 使用 Ark 已有的 `managed_components/lvgl__lvgl`，SDL2 负责打开电脑窗口。硬件数据由模拟器给出样例值，不模拟 ESP-IDF、电子纸物理旋转和刷新耗时。

在已配置 WSLg 的 Windows 电脑上，双击 `run.cmd`，或从 PowerShell 运行：

```powershell
./tools/ark-ui-simulator/run.ps1
```

WSL 中需要已有 GCC、CMake、pkg-config 和 SDL2 开发包；`pkg-config --modversion sdl2` 可检查 SDL2。脚本只在本目录的 `build/` 中生成文件，不安装全局依赖。Linux 有 SDL2 时也可直接运行：

```sh
cmake -S tools/ark-ui-simulator -B tools/ark-ui-simulator/build
cmake --build tools/ark-ui-simulator/build -j4
./tools/ark-ui-simulator/build/ark-ui-simulator
```

用 ↑/↓ 切换菜单项或设置行，按 Enter 进入页面或修改设置，双击 Enter 返回菜单并高亮刚离开的应用。按住 Enter 一秒模拟休眠，通行证页会留屏，休眠后轻按 Enter 唤醒。数字 1–6 依次直接预览开机、菜单、设置、休眠卡、通行证、时钟。Esc 关闭窗口。图片、模板、动图走固件直绘路径，尚未接入此模拟器。

检查菜单字体的真实 I1 渲染时，用 `--menu-preview <目录>`：

```sh
./tools/ark-ui-simulator/build/ark-ui-simulator --menu-preview .output/ark/menu-preview
```

该入口无需 SDL 窗口，使用与 Ark 固件相同的 480×720 I1 全屏格式，再逐像素保存为 1:1 黑白 BMP。共输出 12 张：`menu-zh-0.bmp` 至 `menu-zh-5.bmp`、`menu-en-0.bmp` 至 `menu-en-5.bmp`；数字是从 0 开始的选中菜单项索引。可逐张检查中英文六项文字与字形；截图只验证 LVGL 渲染，不代表实体电子纸显示效果。

统一字体的全页预览使用 `--font-preview <目录>`，导出共享 C 页面源码的 480×720 I1 黑白截图：

```sh
./tools/ark-ui-simulator/build/ark-ui-simulator --font-preview .output/ark/font-preview
```

包含中英文主菜单各六项、设置页 18 个导航状态、时钟、通行证空状态，以及开机和休眠的普通画面与各条可用彩蛋。共输出 74 张 `font-<页面>-<状态>-zh.bmp` / `-en.bmp`（时钟省略状态）。此入口无需 SDL 窗口，不连接设备；通行证档案本体是上传的位图，预览只检查其外围提示和空状态。字库生成方法见 [字体说明](../ark-font/README.md)。

只生成 PRTS 主题截图时，用 `--prts-preview <目录>`，无需运行完整的 `--smoke` 导航：

```sh
./tools/ark-ui-simulator/build/ark-ui-simulator --prts-preview .output/ark/prts-preview
```

共输出 24 张：中文的 `boot-normal.bmp`、`sleep-normal.bmp`、`boot-egg-1.bmp` 至 `boot-egg-5.bmp`、`sleep-egg-1.bmp` 至 `sleep-egg-5.bmp`；英文文件在 `.bmp` 前加 `-en`，只有普通状态和第 2–5 句（第 1 句暂无核实的英文）；另有 `menu-zh.bmp` 和 `menu-en.bmp`。该模式将彩蛋逐条强制显示以便检查；普通启动在中英文模式下均以 10% 总概率显示彩蛋。

当前六个页面及状态栏支持简体中文 / English，英文文案参照汉化前的 `515b9b0`；个人档案是后增功能。设置页语言项位于“系统参数”分组，18 行单页保留原版轮播、图标、反白卡片、切角与信息面板。开机和休眠共用用户提供的完整 logo 的 1-bit 转换资源、PRTS 文案和 10% 概率彩蛋，详见 [资源说明](../ui-assets/boot-logo.md)。状态栏统一显示 WIFI / BT，电量百分比位于最右侧。模拟器始终链接最新页面源码。

通行证支持读取网页编辑器导出的 `profile.bin`。在 WSL/Linux 中设置环境变量为文件的绝对路径，再启动模拟器：

```sh
ARK_PASS_PROFILE="$PWD/.output/ark/logs/pass-preview/profile.bin" \
  ./tools/ark-ui-simulator/build/ark-ui-simulator
```

上例指向本机验证用样例文件；使用自己的资料时换成实际导出路径。按数字 5 打开通行证，单击 Enter 重新加载同一路径；没有配置文件时显示空状态，加载失败保留当前通行证。主体图为 440×608，支持原 1bit FFUI v1 与六色 4bpp FFUI v2，格式见 [`ark-pass-editor`](../ark-pass-editor/README.md)。实机把 v2 卡片与黑白外壳合成，映射为 ColorFast 8bpp 索引后两阶段快刷；原文件格式不变。模拟器使用这些索引的标定 RGB 预览，快刷颜色与原生六色存在差异。这里不模拟蓝牙、SD 或实机电子纸刷新耗时。

检查彩色通行证的 LVGL 卡片定位时，运行独立回归入口（无需 `--smoke` 或自己的资料文件）：

```sh
./tools/ark-ui-simulator/build/ark-ui-simulator --pass-layout-check .output/ark/pass-layout
```

它用临时六色资料检查首次显示、空状态后刷新、已有彩色卡刷新时传给 `ui_display_set_color_card` 的坐标，预期三次均为 `(20,57) 440×608`；可选目录保存 `pass-color-layout.bmp`，卡片有黑色边框及彩色四角便于看边界。此项只验证共享 C 页面逻辑与 LVGL 几何，不能代替实机面板的合成和刷新验收。

模拟器把 LVGL 的内存分配接到桌面 C `malloc`，供快速切页预览；固件使用自己的 ESP-IDF heap 配置。两者的可用内存和分配失败行为不能互推。

使用的是 [LVGL 官方 SDL 桌面驱动](https://docs.lvgl.io/master/integration/pc/sdl.html)。逻辑窗口为 480×720，符合 Ark `ui_conf.h`；实际电子纸仍需上机验证。

`--smoke` 覆盖菜单选通行证、双击返回后的菜单位置、通行证休眠留屏和唤醒、设置修改、设置及时钟返回索引，以及菜单休眠卡和唤醒。通行证留屏截图为 `pass-sleep.bmp`。模拟器按当前 app 的 `sleep_face` 决定保留画面或显示休眠卡。实际电子纸留屏和深度休眠由实机验证。

个人通行证版设置 `ARK_PASS_PROFILE` 后的 `--smoke` 也已通过，包含重载失败时保留旧图与恢复文件后重试，截图在 `.output/ark/logs/pass-device-preview/`，新增 `pass-reload-failed.bmp` 和 `pass-reload-retry.bmp`。
