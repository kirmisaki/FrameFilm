# JD79665AA 四色屏与独立 C3 机型

面板 0x08：原生 768×552、竖屏编辑 552×768、黑白黄红四色。0x07 保持六色定义。
屏幕能力由 0x42 参数决定；FRAMEFILMC3 是独立 BLE 机型身份，不是 Pro 的别名。

照片/模板/文件编码共享四色量化与 4bpp 颜色索引 0～3，文件头 ColorCount=4。
已有六色面板维持原算法和文件字节。四色面板不调用只针对六色的 LUT/SZ 校色，旧设置会安全回退。

可选四色照片、人像、黑白、文字模式移植自现有帧扣客户端的增量 JS 算法，基于 InkSight 的 MIT 实现；
许可证见 INKSIGHT_LICENSE.txt，参考：https://github.com/datascale-ai/inksight 。
算法内部的参考色用于误差扩散，输出始终为标准黑白黄红四种 RGB；不代表面板实测色彩标定。
分块引擎保留三行误差缓冲，避免额外分配整帧浮点缓冲；页面的异步调度还需要手机性能验证。

本地检查（Node.js，需 @napi-rs/canvas）：

```
node tools/wechart/tests/e4-panel.test.cjs
node tools/wechart/tests/e6-regression.test.cjs
```

覆盖 14 条算法路径、模板分区、透明文字、文件大小/颜色索引、四角方向、
四六色设备切换，以及六个旧面板与上游未修改版本的逐字节算法/编码回归。
未执行手机微信、BLE 及实体屏验证；上游作者合并代码并重新发布小程序后才影响线上版本。
