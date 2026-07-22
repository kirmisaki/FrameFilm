# BLE 命令速查表

> AI 开发快速参考 — 完整命令列表、数据格式、使用示例

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
| 0x20 | CTRL_MODE | ↓ | uint8 | 0:手动 1:本地轮播 2:WiFi轮播 |
| 0x21 | CTRL_MODE_GET | ↑ | uint8 | 查询模式 |
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

## 播放模式枚举

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
