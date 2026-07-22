# FrameFilm 编码规范与约定

> 供 AI 开发工具自动遵循的编码风格、命名约定和项目规范。

## 一、通用规范

### 语言与平台
- **固件**: C (ESP-IDF v5.5.2), 文件编码 UTF-8
- **小程序**: JavaScript (ES5 兼容微信小程序), 文件编码 UTF-8
- **Web 工具**: JavaScript (ES6), HTML/CSS, 文件编码 UTF-8
- **文档**: Markdown (中文为主)

### Git 提交
- Commit 消息使用中文
- 格式: `type(scope): 描述`
- 常用 type: feat, fix, style, refactor, docs, chore

## 二、固件编码规范 (C)

### 文件头注释模板
```c
/*********************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 kiritro
 * FileName : /component_name/src/filename.c
 * Author: Kiritro  Version: v0.1  Date: YYYY/MM/DD
 * Description: 简要描述
 * ChangeLog: 变更记录
 *********************************************************************/
```

### 命名约定
| 类型 | 规则 | 示例 |
|------|------|------|
| 文件名 | `snake_case` | `service_ble.c`, `hal_epd.h` |
| 函数 | `snake_case` | `film_hal_init()`, `service_wifi_connect()` |
| 全局变量 | `g_` 前缀 | `g_service_param`, `g_wifi_connected` |
| 静态变量 | `s_` 或 `g_` (文件级) | `s_ble_initialized` |
| 宏/常量 | `UPPER_CASE` | `BLE_CMD_HEAD`, `GATTS_CHAR_VAL_LEN_MAX` |
| 类型定义 | `PascalCase` + `_t` 后缀 | `ServiceParam_Def_t`, `wifi_download_state_t` |
| 枚举值 | `UPPER_CASE` | `WIFI_DOWNLOAD_IDLE`, `FILM_PLAY_MODE_WIFI` |
| 头文件保护 | `__MODULE_NAME_H__` | `__SERVICE_WIFI_H__` |

### 组件结构
每个组件遵循 ESP-IDF 标准结构:
```
component_name/
├── inc/               # 公开头文件
│   └── component.h
├── src/               # 源文件
│   └── component.c
└── CMakeLists.txt     # idf_component_register(...)
```

### 组件分层规则
```
film_service (服务层)  ── 只能调用 film_hal, film_sys, 第三方库
film_hal (抽象层)      ── 只能调用 film_sys, ESP-IDF driver
film_sys (系统层)      ── 只能调用 ESP-IDF 基础组件
```

**严格禁止**: 上层不能直接调用底层驱动的寄存器操作，必须通过 HAL 接口。

### 日志规范
```c
sys_logi(TAG, "info message %d", val);   // 信息
sys_logw(TAG, "warning message");        // 警告
sys_loge(TAG, "error: %s", str);         // 错误
```

### 错误检查
```c
ESP_ERROR_CHECK(esp_wifi_init(&cfg));    // 严重错误直接 abort
// 或
esp_err_t ret = some_function();
if (ret != ESP_OK) { /* 处理错误 */ }
```

## 三、微信小程序编码规范 (JavaScript ES5)

### 命名约定
| 类型 | 规则 | 示例 |
|------|------|------|
| 常量 | `UPPER_CASE` | `BLE_CHUNK_SIZE`, `FILM_FILE_TOTAL_SIZE` |
| 变量/函数 | `camelCase` | `sendBlePacket`, `fetchQuote` |
| Page 方法 | `onXxx` (生命周期) / `camelCase` (自定义) | `onShow`, `goToUpload` |
| 文件名 | `kebab-case` 或 `camelCase` | `ble-utils.js`, `film-utils.js` |

### 文件结构约定
- 页面目录: `pages/{name}/index.{js,wxml,wxss,json}`
- 子页面: `pages/{name}/{sub}/index.{js,wxml,wxss,json}`
- 工具模块: `utils/{name}.js`
- 组件: `components/{name}/`

### BLE 协议调用模式
```javascript
// 命令发送 (带响应等待)
app.sendBleCmd(bleUtils.BLE_FILM_TRANS_CH_CTRL_PWRREAD, null)
   .then(function() { /* success */ })
   .catch(function(err) { /* error */ });

// 快速发送 (Write Without Response)
app.sendBlePacketFast(packet)
   .then(function() { /* success */ });
```

### 字符串编码
- BLE 传输中所有字符串使用 **ASCII 编码**，以 `\0` 结尾
- JS 中 `str.charCodeAt(i)` 获取 ASCII 码
- 固件端 `String.fromCharCode()` 解析

## 四、ForFilm Web 规范

### 命名约定
- 变量使用 `camelCase` 或 `frame` 前缀 (全局): `frameCurrentQuote`, `frameRenderQuote`
- 工具函数前缀 `frame`: `frameFetchQuote()`, `frameInitBle()`

### BLE API
- 使用 Web Bluetooth API (`navigator.bluetooth`)
- 服务UUID: `00002000-0000-1000-8000-00805f9b34fb`
- 特征UUID: `00002001-0000-1000-8000-00805f9b34fb`
- 与小程序共享完全相同的 BLE 命令常量

### 主题系统
- 4 套 CSS 主题: `style.css` (默认), `style_arcade.css`, `style_bw.css`, `style_stardew.css`
- 通过 `<link>` 标签动态切换

## 五、文档规范

### 技术文档格式
- 章节编号使用 `## N. 标题` 格式
- 表格用 Markdown 标准语法
- 代码块标注语言类型
- 版本历史表格放在文档末尾

### 文档索引
| 文档 | 维护时机 |
|------|----------|
| `blecmd_protocol.md` | BLE 命令新增/变更时 |
| `hardware_spec.md` | 硬件设计变更时 |
| `wifi_doc.md` | WiFi 功能变更时 |
| `knowledge/*.md` | 项目结构/架构变更时 |

## 六、跨端一致性

### BLE 协议常量三端对齐
以下文件必须保持命令常量完全一致：
- `firmware/*/components/film_service/inc/service_ble.h` (C 固件)
- `tools/wechart/miniprogram/utils/ble-utils.js` (小程序)
- `tools/ForFilm/js/frame.js` (Web 工具)

### film 格式一致性
- `firmware/*/components/film_hal/src/hal_epd.c` 颜色编码
- `tools/wechart/miniprogram/utils/film-utils.js` 编码
- `tools/ForFilm/js/convert.js` 编码
- 三端必须使用相同的 6 色编码表: 黑0x00 白0x11 绿0x66 蓝0x55 红0x33 黄0x22
