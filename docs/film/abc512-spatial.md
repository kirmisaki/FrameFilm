# EPD037A02 联合三相空间混色

2026-10-30，HINK-T037A02-A1 / EPD037A02、JD7601、720×480。用户明确目标为三个物理刷新相位分别使用抖动数据，共同形成最终图。设备显示版本保持3.2.5。实验不推送、不作为正式App功能发布。

## 与前轮目标的区别

前轮验证的是512组A/B/C序列是否对应512种稳定可辨的单像素终态，未达成该验收；这不是空间混色失败，也不是物理色数上限的证明。完整证据见 [前轮结果](abc512-result.md)。本轮在现有三相最终响应上做空间面积混合，目标是改善照片、渐变与逻辑颜色表达。

每个物理相位的像素仍选择0..7号LUT编码，三个相位合计8³种输入序列。三个相位不是独立RGB通道。不同像素按比例使用不同序列，能近似更多凸包内颜色；不能由此称为512³种实物颜色，也不能把输入RGB的8³网格称为512种已经测得的面板颜色。

## 参考项目与采用方式

- [epd-dither 的分解算法](https://github.com/Frans-Willem/epd-dither/blob/main/docs/decomposition.md)：先求实测颜色的非负混合权重，再用Bayer、蓝噪等采样。任意色板的NaiveDecomposer枚举四顶点组合，O(N⁴)不适合512项；规则六色八面体算法也不适用本色板。借鉴数学处理结构，不复制AGPL源码或噪声资产。
- [OpenDisplay/epaper-dithering](https://github.com/OpenDisplay/epaper-dithering)：误差扩散、实测色板和按观看距离的DBS可供参考；核对提交`778a9638c377894ec417523db2e96b526adbf08b`，其[Palette限制](https://github.com/OpenDisplay/epaper-dithering/blob/778a9638c377894ec417523db2e96b526adbf08b/packages/rust/core/src/palettes.rs#L19-L25)最多256项，核心/DBS为u8索引，Python输出P模式，不能直接承载完整512联合态。
- [epdoptimize](https://github.com/paperlesspaper/epdoptimize)：参考实测外观与设备输出身份分离、自定义palette和空间抖动。没有借用其它型号的调色板或波形。
- 几何计算直接复用现有SciPy的[ConvexHull](https://docs.scipy.org/doc/scipy/reference/generated/scipy.spatial.ConvexHull.html)和[Delaunay](https://docs.scipy.org/doc/scipy/reference/generated/scipy.spatial.Delaunay.html)，不枚举所有四元组，不增加跨语言依赖。

上述项目均未提供本屏经验证的512态三相波形，不能作为本屏物理成色的证明。原同屏`mad462/epd-color-lab`局部归档仍只作为原Qual路径参考。

## 实现

`tools/epd-panel-lab/abc_spatial_mix.py`：

1. 以实测最终响应`F(A,B,C)`为色板；在共享白点适配后的近似线性cameraRGB里构建凸包，精确重复点仅在几何内部去重，保留原entry身份。
2. 凸包外目标从色板内部均值点径向压缩至边界。该映射有明确几何定义，但不是保色相算法，也不是精确色度校正。
3. 用Delaunay四面体求最多四组完整ABC序列的重心权重；权重非负且和为1。
4. 标准Bayer8×8或16×16按面积比例选完整tuple，再拆为A/B/C三张图。三相使用同一次联合选择；没有逐相使用独立种子或阈值。Bayer16有更细的面积权重分辨率，不增加每相命令位数。

必须保持相关性。例如只选择两组`(1,2,3)`与`(6,5,4)`混合时，不能独立抖动各相而产生`(1,5,3)`等非预期序列。某一相在局部恒定也是合理结果，不强制三相都增加纹理。

`abc_spatial_render.py`复用现有LANCZOS、共享白点/暗端映射和ABC9打包。输出三组文件：联合nearest、联合FS、联合重心+Bayer；三组共用同一凸包内目标，使比较只改变采样算法。`--fs-error-limit`默认0.08，沿用上一轮限幅；另用1.0做较宽误差范围对照，避免仅凭较弱FS基线断言新算法更优。

每份输出包含私有ABC9 film、uint16等价I模式编码、三相精确0..7 PNG、三相放大灰度预览、相对cameraRGB模拟图与manifest。相位灰度图表示RAM/LUT选择码，不是该相位实际呈现的光学颜色。拆相重组逐像素核验；私有格式不受P模式256索引限制。

没有改固件、LUT、REF次数、电压或刷新等待；实机仍需已归档的ABC9实验固件，正常固件不应打开这些私有文件。

## 本轮验证范围

新增纯算法合同检查5项通过：四面体重构与外点有界、平坦区域Bayer比例、全部512编码拆拼、无效输入与重复几何、两组tuple跨相相关性，以及Bayer16更细权重的表达。检查数量按测试函数计，不等于实物色数。

同一键盘源图与512个nominal sRGB目标色块已生成三种模拟结果。模拟低通后的线性RGB误差只用于验证面积平均模型及比较采样，不是实际色差、人眼阈值或已测面板颜色数量。点对点误差可能增大，换来区域均值更接近目标；仍有有序纹理与细节取舍。

512目标卡采用16×32色块，每块24×16，包含六个完整Bayer8周期。Bayer8实得510种不同编码图案，目标4/76、360/361各合并；两对映射目标在近似线性cameraRGB中的距离仍为0.00419、0.00629，说明本次合并发生在有限空间采样环节。Bayer16权重更细，同一张卡实得512种不同编码图案、没有相同图案。这仍不证明512种实际可辨颜色；也不强行加入随机像素来制造图案差别。Bayer16下24×16不是完整二维周期，区域平均应按实际像素统计。

测试卡用于检验输入覆盖与空间配方，不用于通过640×480失焦摄像头确认512种可辨颜色。色域外目标可能被压缩，不能以色块数量宣称色域增加。

本地证据目录：`.output/ark/abc-spatial-20261030/`。已核验正常ELF，完成实验固件OTA并读回`57c915fc58e01ba132a80b1a2f0200979595d5a7f9847bf3654ae08448f3f1d9`。两份同源照片`SP30F001.film`（较宽FS误差范围）/`SP30M001.film`（联合Bayer8）及512目标卡`SP30G001.film`均691232B，确认保存、按名加载、完整三相结束与关屏。

Gsou取景重新对准后，曝光/白平衡等九项重新设为固定manual，三页每页九帧并保存中位图。画面完整、方向正常，无缺块或错位；当前软焦UVC照片下两份照片差异较小，不能声称模拟优势已经变成明显的实体改善。`actual-comparison.png`只做共享透视校正，不逐张调色。Bayer16新卡的实屏记录追加在下方。

Bayer16新卡`SP30G016.film`（691232B、SHA256 `48066aa89511597830ee3784d9570b16abe54a063c95f022218221777154549c`）也已确认保存、完成原三相与关屏，并用同样九帧取中位图。卡片完整、方向正常，没有缺块或错位；相机不能分辨全部细小色块是否存在稳定可辨差异，因此只验收文件/三相显示链路和空间配方，不验收512种光学颜色。

四份本轮临时film均按上传收据和当前ID清理，逐次校验其它文件集合未改变，最后恢复16张图片、1个动画，与上传前快照一致。随后恢复原正常固件，以免诊断Format2标记影响日常图片；恢复核验见本地日志。

正常固件恢复后，BLE实读ELF为`b7120217633972e13facafba93bf7140f12ed214b4826fce590516be1126d41c`、显示版本3.2.5。原`N2A900A1.film`按名打开，172832B加载、HAL完成与关屏已确认；新UVC照片确认原图恢复完整。串口与摄像头释放。没有推送或发布。

## 运行

```powershell
python tools/epd-panel-lab/abc_spatial_render.py --palette path/to/experimental-palette.json --image photo.jpg --crop 0 0 480 720 --output .output/abc-spatial/photo
python tools/epd-panel-lab/abc_spatial_render.py --palette path/to/experimental-palette.json --chart --output .output/abc-spatial/chart
python tools/epd-panel-lab/abc_spatial_render.py --palette path/to/experimental-palette.json --chart --bayer-size 16 --output .output/abc-spatial/chart16
python -m pytest tools/epd-panel-lab/test_abc_spatial_mix.py -q
```

当前相机RGB没有独立表征，反射光谱、邻域相互影响、位置与复刷差异均可能使真实区域均值偏离模拟。必须保留最终实屏对照，不能只根据仿真或采用的tuple数量宣布完成512色。

## 用户补充的图像处理注意事项

群友提出的明暗范围拟合、暖色彩度裁剪、蛇形扫描分属不同处理层，不应当混成一个“512色开关”：

| 项目 | 本项目现状与本轮处理 |
|---|---|
| 明暗范围拟合 | 当前ABC有共享最低观察亮度到白点的简单线性拟合，尚非完整的Lab L自适应拟合。本屏端点必须来自本屏测量；不能沿用SZ的固定fit128端点。后续应比较暗部/高光阶梯，不能只看平均误差。 |
| 暖色选择性彩度收缩 | 未作为已实现功能。本轮不默认给照片整体去色，也不把扫描纸张去黄当肤色算法；只在人脸明显发黄时，才考虑独立、默认关闭的暖色/中高光强度选项，记录它也会影响红花、木材等暖色。 |
| 蛇形扫描 | 当前联合FS已经逐行反向并镜像扩散邻居。它减少方向性，但不保证消除所有纹理；Bayer是二维有序阈值，不因“蛇形”而获得相同作用。 |

核对[epdoptimize处理源码](https://github.com/paperlesspaper/epdoptimize/blob/main/src/dither/processing.ts)：其dynamicRangeCompression在Lab中映射L，auto采用源图分位区间，另有彩度保护；warmPaper用于扫描纸张中和/红墨保护。当前检查的代码没有独立“照片暖肤色/高光彩度裁剪”开关，不应把同名相近的选项当作已解决该问题。

将来增加的源图拟合和可选暖色处理均应在联合ABC量化前完成，测量色板本身不减黑、不裁剪。先保留同条件三相采样对照，再单变量添加预处理，避免把范围拟合的收益误归因于新抖动算法。
