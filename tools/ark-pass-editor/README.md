# Ark 通行证编辑器

独立本地静态网页，提供默认的竖版「干员档案」和横版「罗德岛学院学生证」。两种模板分别保存本地草稿；切换模板不会清空另一张卡的字段或头像。资料默认留空，不预设任何人物身份。

也可从 ForFilm 的通行证页面嵌入打开（`index.html?embedded=1`）。嵌入时复用 ForFilm 已连接的 Ark 和统一任务通道；连接、设备改名均在 ForFilm 页面完成，不会再次弹出蓝牙选择器。读取与发送可以取消；上传中断时 ForFilm 会断开连接并清理未完成的传输。返回连接页可重新连接。独立打开时仍使用本页的蓝牙连接和设备名称功能。

## 使用

在仓库根目录运行 `python -m http.server 8000 --bind 127.0.0.1 --directory tools`，用支持 Web Bluetooth 的浏览器打开 `http://localhost:8000/ark-pass-editor/`。填写资料后可下载 `profile.bin`、PNG 预览和可继续编辑的 `profile.json`。页面自动尝试存一份本地草稿；换浏览器或清理浏览器数据前请导出 JSON。蓝牙由用户点击「连接 Ark」后手动选择以 `FRAMEFILMARK` 开头的设备。连接后可点击「读取设备资料（替换当前草稿）」；页面会先确认，读取完整并校验成功后才覆盖草稿，因此请先导出需要留存的资料。

Ark 固件 3.2.5 起支持下方「设备名称」区域：连接后自动读取，亦可点击「读取名称」重试。用户编辑固定 `FRAMEFILMARK-` 前缀后的 1–16 个 UTF-8 字节，收到设备成功回包后提示已保存；重启设备使新广播名生效。默认后缀为完整 BLE MAC，自定义名称不做全局查重。旧固件无响应时仅提示升级，不影响通行证功能。

头像按最终槽尺寸采样：学生证 **239×299**，干员档案 **128×160**。使用白底合成、high-quality 平滑与分阶段缩图，可拖动、缩放、旋转和调整亮度/对比度/饱和度。JSON 只包含已调整、未量化的槽图 PNG；本地草稿另存有界彩色源（最长边至多 2048、PNG data URL 至多 6 MB）与裁切参数。两种卡面都只提供 **I006 / 512 色**和真正的 **ColorFast / 8bpp 双相**，切模式不改彩色源。弹窗左侧为彩色构图，右侧为所选编码的模型预览；学生右图从整卡编码结果取槽，取消保留旧头像。代号、编号需填写或明确勾选「尚未设置」；所有文字按实际绘制宽度校验，签名最多三行。字段有误时不能导出位图或发送。

学生证有九款 PSD 配色，默认「阿米娅」。切换配色只换底图，保留头像和资料。720×480 的屏幕专用 PSD 将顶部花条拆为独立图层，按实拍接缝逐段对位。花条高 10 px，头像旁色带保持 x=185，标题、字段标签、代号条、校印和水印沿用现有卡面。

阿米娅配色的 I006 顶部红色渐变使用 W 色码。

网页使用随编辑器分发的 `assets/student-passport/` 底图，以 JSON 的 `palette` 值命名（例如 `amiya.png`）：

| 配色 | JSON `palette` |
|---|---|
| 阿米娅（默认） | `amiya` |
| 狙击 | `sniper` |
| 医疗 | `medic` |
| 先锋 | `vanguard` |
| 近卫 | `guard` |
| 重装 | `defender` |
| 术士 | `caster` |
| 辅助 | `supporter` |
| 特种 | `specialist` |

更新底图时，从分层 PSD 按配色导出 PNG，再将同一套文件放入 `assets/student-passport/` 和 Flutter 的 [底图目录](../../app/flutter_app/assets/student_passport/)。浏览器测试会逐一比对两套文件。所选配色缺图、损坏或尺寸不符时，仍可编辑和导入、导出 JSON，PNG/BIN 发送与导出暂停至底图可用。

网页只叠加可编辑的头像、代号、学号和种族，不重画静态元素。学号按 `YYYYMMDD`（国服实装日期）加 `MMDD`（角色生日）填写，也可明确选择「尚未设置」。预览和 PNG 为正向 **720×480** 横版。默认采用 I006 连续色调和 A 档灰水印；灰水印只在保留的原稿浅灰水印区用白 75 与中性灰 407 扩散混色，满覆盖时目标灰态占比为 35%。也可选择 ColorFast：复用 ForFilm `colorFast55` 的候选色板和 Atkinson 量化，模板灰水印正常参与整卡编码，不应用 ABC9 水印掩膜。发送后逆时针横拿 Ark 阅读。全屏卡面不显示状态栏和底部提示，确认键单击刷新、双击退出、长按休眠；其他页面方向和显示不变。

## 定向测试

在仓库根运行 `node --test tools/ark-pass-editor/*.test.cjs`，覆盖编码、槽尺寸、迁移、能力门槛、白底缩图和过期预览丢弃。`node tools/ForFilm/tests/source-fit.test.cjs` 验证共享调色实现。可选浏览器测试为 `tools/ForFilm/tests/ark-student-passport.test.cjs`，需既有 Playwright / Edge 与 tools 静态服务器（端口 8768）；它仅使用模拟 BLE，不代表实体颜色、BIN 落盘或 Wi-Fi 验收。实体 Web 验收应直接打开独立页或 ForFilm 通行证页，由用户选设备、按模式发送并核对回读及屏幕；页面不会自动连接或刷机。

## 档案数据

`profile.json` 是 UTF-8 JSON，整个文件上限 **350,000 B**，头像 data URL 上限 **300,000 字符**。两种卡面都使用 v3，干员档案为：

```json
{
  "version": 3,
  "kind": "operator",
  "output": "i006",
  "codename": "",
  "codenameUnset": false,
  "number": "",
  "numberUnset": false,
  "affiliation": "",
  "signature": "",
  "avatar": null
}
```

学生证为：

```json
{
  "version": 3,
  "kind": "student",
  "output": "i006",
  "template": "rhodes-academy-student",
  "palette": "amiya",
  "codename": "",
  "codenameUnset": false,
  "studentNumber": "",
  "studentNumberUnset": false,
  "race": "",
  "avatar": null
}
```

`output` 必须是 `i006` 或 `colorfast`，`kind` 必须是 `student` 或 `operator`。`avatar` 为 `null` 或对应槽尺寸的彩色 PNG data URL，`Unset` 字段为布尔值。v3 不再导出 `avatarMode` / `studentOutput`，拒绝混入旧黑白/灰阶声明。仍可导入干员 v1 / 学生 v2；旧学生 128×160 彩色裁切会适配 239×299，但不能恢复细节，页面会提示重选原图。有匹配的较大本地源时按原裁切重新生成。已知旧黑白/灰阶头像保留预览和本地稿，必须重选原图或移除后才能导出新 v3 / 发送；旧 `ffui` 需明确重选输出，不解释为 ColorFast。恢复失败不覆盖另一模板或当前有效草稿。设备 JSON 无法恢复原景和完整裁切范围。旧客户端不保证编辑 v3，请先升级并备份；Ark 仍仅保存一份活动卡。

## 主体图与设备文件

干员档案保持 **440×608** 主体图，状态栏和提示行仍由固件绘制。学生证 PNG 为正向 **720×480**；两种 BIN 均为面板原生 **720×480**（相对阅读方向为 180°）。实体颜色以实机为准。

| 新输出 | 容器 / 主体 | 总大小 |
|---|---|---:|
| 学生 I006 | 32 B Film 头，format `0x7f` / ABC9，单帧 1；LE16 ABC9 | 691,232 B |
| 学生 ColorFast | 32 B 通用 Film 头，format `0x03`，单帧 1；8bpp 索引 ≤63 | 345,632 B |
| 干员 I006（有头像） | FFUI v3 + I1 + 128×160 LE16 ABC9 ≤511 | 74,416 B |
| 干员 ColorFast（有头像） | FFUI v3 + I1 + 128×160 8bpp 索引 ≤63 | 53,936 B |
| 干员两模式（无头像） | FFUI v3 + I1，无头像平面 | 33,456 B |

紧凑 FFUI v3 的 16 B 头为：`FFUI`、byte4=3、byte5=`0x7f` 或 `0x03`，offset6/8 为 LE16 的 440/608，byte10 为 hasAvatar 0/1，bytes11..15 为 0。随后 **33,440 B** I1，逻辑行优先、MSB first、1=黑；如有头像，再跟逻辑行优先的 128×160 索引平面，固定覆盖 `(20,143)`，无二次缩放。I006 外壳黑/白使用 387/75，ColorFast 为 0/21；预览使用相同色码。既有 FFUI v1/v2 由固件保留支持，新网页不再生成。

每次发送都重新查询 `0x59`：学生 I006 **bit11**、学生 ColorFast **bit14**、两种紧凑档案均需 **bit15**。通用 ABC9 bit5、旧 FFUI bit0/10 均不可代替。能力不足在任何文件写入前阻止发送并提示升级，不降级；仍能离线编辑和下载所选模式。

蓝牙使用 ForFilm 的 `0x2000/0x2001` UUID 和 `0x55 CH LEN DATA SUM` 帧。每个文件按 START(0x03) → NAME(0x00，ASCII 路径含末尾 `\0`) → LEN(0x01，4 B 大端) → DATA(0x02，192 B 分片) → STOP(0x04) 发送；控制帧间 50 ms，数据帧间 4 ms（与现有 ForFilm 的 2+2 ms 相同）。现有 ForFilm 上传函数发送文件名时未显式附加 `\0`；Ark 固件接收端按 LEN 复制并补零，因此两种都能解析，此工具按项目字符串规范显式发送零结尾。先发送 `app/pass/profile.bin`，再发送 `app/pass/profile.json`。发送期间锁定资料编辑，确保文件对应点击发送时的画面。两个文件发送后通过 0x53 回读 profile.json 并与发送内容比对；一致才报告“回读一致”，否则明确提示回读未确认。取消或中断时断开连接，并保留已发送 bin / 未确认 JSON 的状态；重新连接后从头发送。设备协议没有文件保存 ACK，最终仍需在设备上按确认加载。

读取需要支持 **0x53** 的新版 Ark 固件。连接后同一 BLE characteristic 启用通知，一次只请求一块：请求 `offset`（uint32 BE）+ `count`（1..128）；响应为 `status`（1 B）+ `offset`（4 B）+ `total`（4 B）+ 数据（0..128 B）。`status` 依次为 0 成功、1 未配置、2 读取失败、3 忙、4 参数错误；忙时自动等待 500 ms 重试（最多 12 次）。页面验证帧头、通道、长度、校验和、offset 与 350000 B 上限，按 offset 拼齐 UTF-8 JSON，再校验 schema 与头像；全部成功后才替换表单和本地草稿。每块等待 7 秒，超时会提示可能需要新版固件。取消、断连、解析失败都保留当前编辑内容；读取和上传不能同时执行。
