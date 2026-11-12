# BLE 命令速查表

> AI 开发快速参考 — 完整命令列表、数据格式、使用示例
>
> 协议在冰箱贴固件（`frame_film` 三机型 / `frame_film_ark` 单机型）**同源**（命令值与回包格式一致）；但 **app 通道（`0x45~0x4F`、含 App 控制/参数/时间同步/远程按键/动图按名播放）只有 `frame_film_ark` 实现**，`frame_film` 会直接忽略（其命令表止于 `0x00~0x42`）；底座 `frame_film_dock` 另有专属通道。

## 数据包格式

```
+--------+--------+--------+----------+--------+
|  HEAD  |   CH   |  LEN   |  DATA    |  SUM   |
+--------+--------+--------+----------+--------+
|  0x55  |  1 B   |  1 B   |  N B     |  1 B   |
+--------+--------+--------+----------+--------+
```

校验和: `SUM = (HEAD + CH + LEN + 所有DATA字节) & 0xFF`

## FILM 文件传输 (0x00-0x08)

| CH | 名称 | 方向 | 数据 | 说明 |
|----|------|------|------|------|
| 0x00 | FILE_NAME | ↓ | ASCII string | 文件名 |
| 0x01 | FILE_LEN | ↓ | uint32 BE | 文件大小 |
| 0x02 | FILE_DATA | ↓ | raw bytes | 数据块 |
| 0x03 | FILE_START | ↓ | 无 | 开始传输 |
| 0x04 | FILE_STOP | ↓ | 无 | 结束传输 |
| 0x05 | FILE_DELETE | ↓ | uint8 | 删除文件 ID |
| 0x06 | FILE_LIST | ↑ | [[ID, NameLen, Name]] | 查询文件列表 |
| 0x07 | FILE_DISPLAY | ↓ | uint8 | 显示文件 ID |
| 0x08 | FILE_DISPLAY_GET | ↑ | uint8 | 查询当前显示 |

## OTA 升级 (0x10-0x13)

| CH | 名称 | 方向 | 数据 | 说明 |
|----|------|------|------|------|
| 0x10 | OTA_LEN | ↓ | uint32 BE | 固件大小 |
| 0x11 | OTA_DATA | ↓ | raw bytes | 固件数据 |
| 0x12 | OTA_START | ↓ | 无 | 开始升级 |
| 0x13 | OTA_STOP | ↓ | 无 | 完成升级 |

## 设备控制 (0x20-0x2B) v0.2

| CH | 名称 | 方向 | 数据 | 说明 |
|----|------|------|------|------|
| 0x20 | CTRL_MODE | ↓ | uint8 | **[已废弃]** 收到忽略；播放模式改用图片 app 参数通道 0x45 |
| 0x21 | CTRL_MODE_GET | ↑ | uint8 | **[已废弃]** 固定回 0xFF；改用 0x46 |
| 0x22 | CTRL_RESET | ↓ | 无 | 恢复出厂设置 |
| 0x23 | CTRL_PWRREAD | ↑ | uint8 | 电量 0-100 |
| 0x24 | CTRL_REBOOT | ↓ | 无 | 重启设备 |
| 0x25 | CTRL_SLEEPONOFF | ↓ | uint8 | 休眠开关 0/1 |
| 0x26 | CTRL_SLEEPONOFF_GET | ↑ | uint8 | 查询休眠 |
| 0x27 | CTRL_SLEEPMODE | ↓ | uint8 | 定时唤醒开关 0/1 |
| 0x28 | CTRL_SLEEPMODE_GET | ↑ | uint8 | 查询定时唤醒 |
| 0x29 | CTRL_SLEEPMODE_TIME | ↓ | uint32 BE | 唤醒间隔(分钟) |
| 0x2A | CTRL_SLEEPMODE_TIME_GET | ↑ | uint32 BE | 查询唤醒间隔 |
| 0x2B | CTRL_SDRESET | ↓ | 无 | SD卡格式化 |

## WiFi 网络配置 (0x30-0x3D) v0.2 (Pro版)

| CH | 名称 | 方向 | 数据 | 说明 |
|----|------|------|------|------|
| 0x30 | WIFI_ENABLE | ↓ | uint8 | WiFi开关 0/1 |
| 0x31 | WIFI_ENABLE_GET | ↑ | uint8 | 查询开关 |
| 0x32 | WIFI_SSID | ↓ | ASCII ≤63B | 设置SSID |
| 0x33 | WIFI_SSID_GET | ↑ | ASCII ≤63B | 查询SSID |
| 0x34 | WIFI_PASSWORD | ↓ | ASCII ≤63B | 设置密码 |
| 0x35 | WIFI_PASSWORD_GET | ↑ | ASCII ≤63B | 查询密码 |
| 0x36 | FILM_API_URL | ↓ | ASCII ≤127B | 下载API地址 |
| 0x37 | FILM_API_URL_GET | ↑ | ASCII ≤127B | 查询API地址 |
| 0x38 | WIFI_CONNECT | ↓ | 无 | 连接WiFi |
| 0x39 | WIFI_DISCONNECT | ↓ | 无 | 断开WiFi |
| 0x3A | WIFI_CONNECT_GET | ↑ | uint8 | 连接状态 0/1 |
| 0x3B | WIFI_CLEAR | ↓ | 无 | 清除网络配置 |
| 0x3C | FILM_DOWNLOAD | ↓ | 无 | 触发下载 |
| 0x3D | FILM_DOWNLOAD_STATE | ↑ | uint8 | 下载状态 0-3 |

## App 控制与参数 (0x42-0x4C) v1.8

| CH | 名称 | 方向 | 数据 | 说明 |
|----|------|------|------|------|
| 0x42 | SCREEN_RESOLUTION_GET | ↑ | ID(1B)+W(2B)+H(2B) BE | 查询屏幕面板与分辨率 |
| 0x45 | APP_IMAGE_PARAM | ↓ | TLV | 图片 app 参数设置 |
| 0x46 | APP_IMAGE_PARAM_GET | ↑ | TLV | 图片 app 参数查询 |
| 0x47 | APP_TEMPLATE_PARAM | ↓ | TLV | 模板 app 参数设置 |
| 0x48 | APP_TEMPLATE_PARAM_GET | ↑ | TLV | 模板 app 参数查询 |
| 0x49 | APP_ANIM_PARAM | ↓ | TLV | 动图 app 参数设置 |
| 0x4A | APP_ANIM_PARAM_GET | ↑ | TLV | 动图 app 参数查询 |
| 0x4B | APP_SWITCH | ↓ | uint8 | 切换 app（app_id 见下），不回包 |
| 0x4C | APP_CURRENT_GET | ↑ | uint8 | 查询当前 app（0xFF=未知） |
| 0x4F | ANIM_PLAY | ↓ + ↑ | 文件名 UTF-8 + NUL | 按文件名播放动图并切到动图 app，回 1B status |

> **适用固件**：`0x42` 三套固件均有；`0x45` ~ `0x4F` **仅通行证版 `frame_film_ark` 实现**，`frame_film`（三机型）会直接忽略。
>
> `0x43` / `0x44` 为 **Dock 底座专属**通道（键盘键值），冰箱贴固件不使用，故 app 控制通道跳号到 `0x4B` / `0x4C`。

app_id：`0x00` 图片 / `0x01` 模板 / `0x02` 时钟 / `0x03` 动图 / `0x04` 系统设置 / `0x05` 主菜单 / `0x06` 通行证（时钟无参数，不占通道；后三者为 UI 层，仅 `FULL` 模式注册）

TLV = `[TAG(1B)][LEN(1B)][VALUE(LEN B)]...`；查询通道 = 设置通道 + 1；SET 不回包；多字节大端。
各 app 的 TAG 定义见 `blecmd_protocol.md` §4.8。

### Dock 底座专属 (0x43-0x44)

| CH | 名称 | 方向 | 数据 | 说明 |
|----|------|------|------|------|
| 0x43 | KEYBOARD_KEY_SET | ↓ | 事件(1)+修饰键(1)+键数(1)+键码(n) | 设置单击/双击/长按 HID 键值，不回包 |
| 0x44 | KEYBOARD_KEY_GET | ↑ | [修饰键(1)+键数(1)+键码(6)]×N | 查询键值（空请求回三组，LEN=8×组数） |

详情见 `blecmd_protocol.md` §3.8 / §4.9。

## 时间同步 (0x4D) v1.9

> **适用固件**：仅通行证版 `frame_film_ark` 实现，`frame_film`（三机型）会直接忽略。

| CH | 名称 | 方向 | 数据 | 说明 |
|----|------|------|------|------|
| 0x4D | TIME_SYNC | ↓ + ↑ | Unix秒(uint32 BE) + 时区(int16 BE, 分钟, 东为正) | 设置时间与时区，设备回显同样 6 字节 |

- 时间戳须在 2020-01-01 ~ 2100-01-01 之间、时区须在 ±14 小时以内，否则不回包（只打日志）
- 时间不落盘（deep sleep 保持）；时区随 `tz_min` 落盘（`service_param`），仅变化时写 NVS
- 时区落成 POSIX TZ 串交给 libc（`UTC-8:00` = 东八区，**符号与"东为正"相反**）

详情见 `blecmd_protocol.md` §3.9。

## 远程按键注入 (0x4E) v1.10

> **适用固件**：仅通行证版 `frame_film_ark` 实现，`frame_film`（三机型）会直接忽略。

| CH | 名称 | 方向 | 数据 | 说明 |
|----|------|------|------|------|
| 0x4E | KEY_INJECT | ↓ + ↑ | 键值(1B) | 模拟本机按键，设备回显该字节表示已收到 |

键值：`0x00` 确认单击 / `0x01` 确认长按（手动休眠）/ `0x02` 上 / `0x03` 下 / `0x04` 确认双击（退回主菜单）。

- 仅通行证版 `frame_film_ark` 支持（Dock 按键是 PC 键盘 HID，不做本机导航）
- 固件映射为与 HAL 同构的输入事件，菜单导航/确认/退回/休眠语义自动一致
- 回显只表示已收到，不保证被消费（开机卡/休眠卡占屏期间按键照本机语义丢弃）

详情见 `blecmd_protocol.md` §3.10 / §4.10。

## 文件管理与截图 (0x5B 目录3 / 0x60 / 0x61) v1.20

> **适用固件**：仅 `frame_film_ark`；客户端按 0x59 bit8（文件管理）/ bit9（截图）判断。

| CH | 名称 | 方向 | 数据 | 说明 |
|----|------|------|------|------|
| 0x5B | STORAGE_CATALOG | ↑ | dir(1)+index(u32 BE) | dir 1=film / 2=animation / 3=screenshot |
| 0x60 | FILE_PATH_DELETE | ↓ + ↑ | dir(1)+文件名 UTF-8+NUL | 按名删除，回 1B status |
| 0x61 | FILE_PATH_READ | ↓ + ↑ | dir(1)+offset(u32 BE)+count(1)+文件名 UTF-8+NUL | 分块读取，回 status+offset+total+bytes |

- 上键双击截图：保存单帧 MonoFast `.film`（720×480，43200B）到 `/sdcard/screenshot`
- 仅 UI 层页面可截图；图片/模板/动图为直绘层，不生成截图

## 播放模式枚举
> **[已废弃]** 下列 CTRL_MODE (0x20) 枚举已下线，播放模式语义下移到图片 app 参数通道（`0x45` 的 `play_mode` / `auto_interval`）。

| 值 | 名称 | 说明 |
|----|------|------|
| 0 | 手动模式 | 用户手动切换文件 |
| 1 | 本地轮播 | 从 SD 卡自动轮播 |
| 2 | WiFi 轮播 | 定时从 API 下载并显示 (Pro版) |

## 下载状态枚举

| 值 | 名称 | 说明 |
|----|------|------|
| 0 | IDLE | 空闲 |
| 1 | DOWNLOADING | 下载中 |
| 2 | DONE | 完成 |
| 3 | ERROR | 失败 |

## 传输协议常量 (ble-utils.js)

```javascript
const BLE_CHUNK_SIZE  = 192;   // 数据传输包大小
const BLE_CTRL_DELAY  = 50;    // 控制命令间隔(ms)
const BLE_DATA_DELAY  = 2;     // 数据包间隔(ms)
const BLE_CMD_HEAD    = 0x55;  // 帧头
```

## 常用操作流程

### 文件传输
```
FILE_START → FILE_NAME → FILE_LEN → FILE_DATA×N → FILE_STOP
```

### WiFi 配网
```
WIFI_ENABLE(1) → WIFI_SSID → WIFI_PASSWORD → WIFI_CONNECT → WIFI_CONNECT_GET
```

### OTA 升级
```
OTA_LEN → OTA_DATA×N → OTA_STOP
(OTA_START 在 OTA_LEN 后自动触发)
```

## 跨端代码引用

| 端 | 文件 | 用途 |
|----|------|------|
| C 固件 | `firmware/*/components/film_service/inc/service_ble.h` | 命令常量定义 |
| 小程序 | `tools/wechart/miniprogram/utils/ble-utils.js` | BLE 命令封装 |
| Web 工具 | `tools/ForFilm/js/frame.js` | Web BLE 命令封装 |
| 文档 | `docs/blecmd/blecmd_protocol.md` | 完整协议规范 |
