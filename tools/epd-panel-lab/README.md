# Same-panel EPD fixtures

仅生成测试文件，不连接设备、不改固件、不上传 SD、不测量颜色。目标为当前这块 HINK-T037A02-A1 / EPD037A02（JD7601）：驱动原生 **720×480**，观看 **480×720 竖屏、排线下**。历史 testicon 圆点左上是已验收方向；这里的左上圆点和不对称 L 用于继续核对，工具生成通过不代表硬件重新验收通过。

在仓库根目录、现有 Python 3.12 + Pillow 环境运行：

```powershell
python tools/epd-panel-lab/panel_lab.py
python tools/epd-panel-lab/panel_lab.py --codes-per-page 16 --frames 12
python tools/epd-panel-lab/panel_lab.py --codes-per-page 64 --output .output/panel-lab-single-page
```

默认输出到已被仓库忽略的 `.output/panel-lab/`。默认 5 个 `.film`：ColorQual、ColorFast 各两页（每页 32 编码），MonoFast 一个 12 帧文件（518432 字节）。色卡每页有 PNG；MonoFast 每帧有 PNG。只用现有 Format `0x02` / `0x03` / `0x01`，不添加波形。生成相同选项可重跑，单个文件临时写入后替换，manifest 最后写入；中断后重跑即可。换分页选项建议换输出目录，旧生成物不会自动删除；要彻底清理，删除指定输出目录。以本次 manifest 的文件列表为准。

## 编码、预览与取样

- 色卡直接写入全部物理编码 **0..63**，不会用 RGB 量化重新选择索引。64 个编码不承诺 64 个有效颜色，更不承诺它们在这块屏上可分辨。
- 两种模式使用同样的格子位置和编码顺序。每格显示十六进制编码，`X` 表示不在当前 Web 候选列表中。X 也绘制到 `.film`，保留周围平坦编码区域供测量；取样时避开字、边缘和 X 笔画。占位/非候选格不会被替换为白色，也不会隐去。
- PNG 由同一 portrait 索引画布按 Web RGB 色板渲染。RGB **只是预览估计，不是本块屏的实测值**；黑白占位 RGB 也不等于面板实物结果。候选列表只是当前客户端选择范围，不是本工具重新证明的硬件有效性。
- 不复制维护 64 RGB。Python 从 `tools/ForFilm/js/convert.js` 读取 `CF_PALETTE_QUAL` / `CF_PALETTE_FAST` 纯数字数组和各自候选列表，用 `ast.literal_eval` 解析数字，不执行 JavaScript。仅支持当前明确的数组结构；上游改成动态表达式会给出诊断，需相应更新读取器。当前 Web 的 measured/calibrated 变体未用于本工具默认预览。
- `manifest.json` 保存面板身份、尺寸、Format、frameCount、完整预览 RGB、候选范围、色板源文件 SHA-256，以及每个 film/PNG 的大小和 SHA-256；MonoFast 另保存每帧主体 SHA-256。它是生成物说明，不是颜色测量报告。

## 姿态与二进制合同

源码核对入口：`docs/film/film.md` §10、Web `tools/ForFilm/js/animation.js` 的 `animPortraitToGrid` / `animPixelIndex` / `animMonoBit` / `animBuildFilmFile`，以及 Flutter `flutter_app/lib/frame_codec.dart` 的 `packFramePixels` 与 `flutter_app/lib/animation_film.dart` 的 `assembleAnimationFilm`。

portrait `(px,py)` 到 native `(719-py,px)`，native 行优先索引为 `px*720 + 719-py`。Web portrait 先逆时针转到720×480逻辑画布，再按动画打包时180°落位，得到同一映射。两种彩色和 MonoFast 共用 `native_image()`，PNG 直接来自转换前的同一画布。原生头尺寸是720×480，不能因观看竖屏而写成480×720。

32字节头为 little endian：bodySize@0、width@4、height@6、ColorCount@8、Format@9、FrameCount@10，保留位与v2色表填零。单帧 frameCount=1（协议允许0或1），多帧直接拼主体，等同 Web 动画工坊。Flutter 单帧 indexed 写0、Mono写1且ColorCount=2；其动画拼接沿用首帧，均在协议允许范围。工具统一使用 Web 动画工坊的 unused ColorCount=0。Mono 行宽720可整除8，MSB-first、1黑0白。

## MonoFast 基准

12帧移动实心块加反转棋盘，含 WW/WB/BW/BB 四类像素转移。固定方向标记与静止区域保留。文件没有 FPS/时长字段，实际帧率由设备播放逻辑和刷新耗时决定。它用于短小的传输/差分刷新对照，不是物理流体模拟。

**冷启动第一帧与预热后的差分帧是不同基准。** 记录首帧、后续帧、此前画面/刷新模式、温度与设备日志；不要把一次预热循环的结果当成冷启动性能，也不要仅凭 PNG 判断真实颜色或残影。重新上机时先确认圆点确在左上、排线下，再记录各编码的实际表现。

## 最小局部检查

```powershell
python -m py_compile tools/epd-panel-lab/panel_lab.py tools/epd-panel-lab/test_panel_lab.py
python -m unittest discover -s tools/epd-panel-lab -p test_panel_lab.py -v
```

单个集成测试覆盖头/大小/哈希、Qual与Fast逐格64编码覆盖、非对称左上单点映射、Mono位序、帧间差异、四类像素转移。测试生成文件置于临时目录；不运行全仓测试、不替代实屏验收。


## 独立 A/B/C 诊断生成器

先完成开源源码调查后才添加此候选。仅供启用 `CONFIG_ARK_EPD_ABC_DIAG=y` 的实验固件，默认关闭；详细来源与边界见 [同屏文档](../../docs/film/epd037a02.md)。不连接设备、不自动上传、不改 palette/profile。

```powershell
python tools/epd-panel-lab/abc_diag.py --output D:/dev-tool/FrameFilm-build/abc-diag-fixtures
python tools/epd-panel-lab/abc_diag.py --all-b --output D:/dev-tool/FrameFilm-build/abc-diag-all-b
python -m py_compile tools/epd-panel-lab/abc_diag.py tools/epd-panel-lab/test_abc_diag.py
python -m unittest discover -s tools/epd-panel-lab -p test_abc_diag.py -v
```

默认四个单帧 Format2：`abc-reuse-a.film`、`abc-rewrite-a.film`、`abc-constant-b0.film`、`abc-constant-b7.film`。每份345632字节，每页保留64个相同位置的A/C码块、原X标记和方向标记，页头区别mode/B。`abc-manifest.json` 记录参数、SHA256、标记、方向和4次REF合同。`--all-b` 共生成10份（8种B加2个控制），不自动上传。临时文件替换/重跑及输出清理方式与上面的原生成器相同。

实验标记只写native主体第0字节（文件offset32），编码为 `(mode << 3) | B`，对应portrait `(0,719)`，不是左上角圆点。mode0=原A,A,C复用RAM；mode1=第二相前额外重写A；mode2=第二相前写常量B。mode>2在实验HAL中拒绝，但普通Format2首像素仍可能碰巧命中0..2，因此不得把诊断固件作为日常固件使用。结束恢复正常固件，正常配置未启用时文件仍只是普通Format2。

每次实验强制full clear后做原Qual三次REF，总计4次REF；LUT、寄存器和电压设置不改。mode1/2额外172800字节RAM传输会改变上电停留时隙，不能宣称完整时序不变。先比较原序列与重写A控制，再少量比较B0/B7；确认影响后才扩展八B。PNG仅按现有ColorQual A/C色板帮助定位，不是新B的颜色预测。采样避开文字、边框、X、metadata像素；512组合不承诺512可辨色。生成合同测试不替代烧录、真实显示或颜色测量。
