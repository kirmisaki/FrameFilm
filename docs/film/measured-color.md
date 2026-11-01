# Film 实测校色（第一阶段）

Film 编辑器新增两个可选算法，保留现有自适应、六色、ColorFast 和 ColorQual：

| 选项 | 色板来源 | 文件及刷新方式 |
| --- | --- | --- |
| 实测校色 · 原值 | `48-color-qual.act` | Film v2，Format `0x02`，ColorQual 三相 |
| 实测校色 · 校正2.5% | `48-color-qual-c025.act` | 同上 |

色板来自 [mad462/epd-color-lab](https://github.com/mad462/epd-color-lab)，遵循该项目的 [GPL-3.0 许可](https://github.com/mad462/epd-color-lab/blob/main/LICENSE)。数据对应其 [ACT 色板及候选列表](https://github.com/mad462/epd-color-lab/tree/main/host/act)，算法参考其 [host/device.html](https://github.com/mad462/epd-color-lab/blob/main/host/device.html) 的量化部分。接入日期：2026-10-16。

## 编码与兼容

这两张色板均有 64 个物理索引槽位、52 个候选索引，候选内有 46 个不同 RGB 值。参考项目名称中的“48 色”不作为新增硬件颜色数量的承诺。

保留原始索引顺序与重复 RGB 条目，不把候选重新编号。屏幕按 `index >> 3` 和 `index & 7` 拆分平面，继续使用 Ark 已有的三相波形，不修改固件或 BLE/WiFi 协议。720×480 单帧为 32 字节头加 345600 字节主体。

文件只包含物理索引，不包含色板配置名称。App 生成时记录算法名称用于本地展示，但重新导入独立 `.film` 无法准确反推出源图使用的是哪张测量色板；通用查看器的 RGB 预览可能不同，设备的物理索引含义不变。

## 第一阶段的处理步骤

1. 沿用 Film 的裁剪、旋转、亮度、对比度和饱和度处理。
2. 使用对应实测 RGB 的 D65 Lab 最近色匹配：亮度差乘 1.2 后平方，再与 a/b 色差平方相加。
3. 开启抖动时采用蛇形 Floyd–Steinberg；每次扩散将累积值限制在该像素原始通道值 ±64 内。抖动强度沿用编辑器滑块，0 等同关闭扩散。
4. 输出颜色索引及对应实测 RGB 预览，打包为现有 ColorQual 格式。

Web 与 Dart 使用双精度工作缓冲，并将当前像素裁剪到 0～255、四舍五入后进行选色及残差计算，以便两端结果对照。这里没有照搬参考仓库完整默认预设：不自动叠加亮度 -11、色偏 -10、高光 100 等作者屏幕专用参数，也不新增动态范围压缩或高光/阴影控制。

Web 色板为 `tools/ForFilm/js/convert.js` 中的 `CF_PALETTE_QUAL_MEASURED` 和 `CF_PALETTE_QUAL_CALIBRATED`。运行 `node app/flutter_app/tool/sync_forfilm_palettes.cjs` 同步 Dart 常量，避免维护两份手工数据。

## 通行证与传输边界

通行证继续使用原有六色 FFUI 与 ColorFast 快刷，不提供这两个 ColorQual 选项。它继续共享适用的构图、亮度、对比度、饱和度和原有六色抖动步骤，不套用三相专用实测色板。

App 上传沿用现有 WiFi 传输，Web 沿用既有上传路径。该功能只更换图像转换配置，不额外开启设备连接或自动上传。Flutter 演示网关支持生成的 Format `0x02`/`0x03` 单帧文件，演示发送不代表硬件实测。

## 验证范围

针对两种配置验证 Web/Dart 的候选索引与输出一致性、抖动关闭/开启/强度边界、ColorQual 编码及方向、取消转换与演示导入。保留原有算法的固定样例校验，避免改变旧图输出。

真实色彩与刷新耗时仍需相同图片的实屏对照。测量色板来源于另一块屏幕，不保证不同面板、环境与刷新起始状态下得到相同观感。


## 同型号校色基准

当前目标为实物排线 `HINK-T037A02-A1 / EPD037A02`，控制器JD7601。GDEH037E01只作交叉参考，未经验证不混用校色参数。本地epd-color-lab快照的9份波形与当前Ark一致；增量是测量色板和选色处理。

`tools/epd-panel-lab/panel_lab.py` 生成同屏0..63全编码色卡和短Mono基准，默认预览复用原有ColorQual/ColorFast，不套用measured/calibrated变体。编码覆盖不承诺有效色数。流程见 [EPD037A02](epd037a02.md)。
