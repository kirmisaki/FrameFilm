# film-hub 技术方案设计文档

> 版本：v0.2（后端方案已确认，前端与固件配套待细化）
> 日期：2026-08-07
> 关联文档：`docs/filmhub/requirements.md`（v1.1 已确认）
> 状态：后端方案已确认，可进入开发

## 1. 设计目标与范围

基于已确认需求，设计 film-hub 的整体技术方案，覆盖：后端架构、前端架构、数据库、设备通信协议（心跳 + film 获取）、模板引擎、轮播流调度、AI 接入、单用户登录，以及固件侧配套改动清单。

设计原则：
- 局域网单机部署，单用户使用，简单优先
- 设备协议充分考虑 ESP32 资源约束（HTTP GET 为主、JSON 轻量、无 TLS）
- 与固件现有实现兼容（参数结构、下载流程、BLE 命令体系）

## 2. 总体架构

```
┌─────────────┐   HTTP/JSON    ┌──────────────────────────┐
│ 管理前端      │ ─────────────► │          FastAPI 后端      │
│ Vue3 + Vite  │ ◄───────────── │                          │
└─────────────┘                │  ├─ 认证 / 管理 API        │
                               │  ├─ 设备 API（心跳/film）    │
┌─────────────┐   HTTP/JSON    │  ├─ 模板渲染引擎 (Pillow)   │
│ 墨水屏设备    │ ─────────────► │  ├─ 轮播调度器 (APScheduler)│
│ (ESP32-S3)  │ ◄───────────── │  └─ AI 客户端 (httpx)      │
└─────────────┘   film 二进制   │           │                │
                               │      ┌────┴────┐           │
                               │      │ SQLite  │ data/ 文件 │
                               │      └─────────┘           │
                               └──────────────────────────┘
```

- **单服务单体架构**：FastAPI 同时提供管理 API、设备 API、静态资源（托管前端 `dist/`）
- **存储**：SQLite（元数据）+ 磁盘文件（原图 / film / 预览图）
- **调度**：APScheduler 承担服务端推送与定时渲染

## 3. 技术选型与项目结构

### 3.1 技术选型确认

| 层 | 选型 | 说明 |
|----|------|------|
| 后端 | Python 3.11+ / FastAPI / Uvicorn | ASGI，自带 OpenAPI 文档 |
| ORM | SQLAlchemy 2.x | 兼容 SQLite，后续可迁移 |
| 图像 | Pillow | 转换 + 模板渲染 |
| 调度 | APScheduler | 轮播定时任务 |
| AI | httpx + OpenAI 兼容接口 | DeepSeek / Kimi / 通义等可配置 |
| 认证 | 用户 JWT + 设备 token 双体系 | 见 §6 |
| 前端 | Vue 3 + Vite + Pinia + Vue Router | SPA |
| UI 库 | Naive UI + ECharts | 现代风格 + 炫酷图表 |
| 拖拽 | vuedraggable | 轮播流编排 |
| 部署 | 开发：uvicorn 直接跑；生产：uvicorn 单进程（暂不 Docker） | 局域网单用户 |

### 3.2 项目结构

```
server/
├── backend/                      # FastAPI 后端
│   ├── pyproject.toml            # 依赖（uv/pip 均可）
│   ├── app/
│   │   ├── main.py               # 入口，挂载 API 与静态资源
│   │   ├── config.py             # 配置（端口、路径、密钥）
│   │   ├── db.py                 # SQLAlchemy 引擎/会话
│   │   ├── models/               # ORM 模型
│   │   │   ├── user.py device.py album.py photo.py
│   │   │   ├── template.py stream.py
│   │   ├── schemas/              # Pydantic 模型（请求/响应）
│   │   ├── api/
│   │   │   ├── auth.py           # 登录/修改密码
│   │   │   ├── device.py         # 设备管理 API
│   │   │   ├── album.py          # 相册/照片 API
│   │   │   ├── template.py       # 模板库 API
│   │   │   ├── stream.py         # 轮播流 API
│   │   │   └── ai.py             # AI API
│   │   │   └── device_proto.py   # 设备心跳 + film 获取 API
│   │   ├── services/
│   │   │   ├── film_convert.py   # RGB → film 转换（调色板/抖动）
│   │   │   ├── renderer.py       # 模板渲染引擎
│   │   │   ├── scheduler.py      # 轮播调度器
│   │   │   ├── data_sources.py   # 数据源（天气/运势/日历等）
│   │   │   └── ai_client.py      # AI 客户端
│   │   └── utils/
│   ├── scripts/                  # 初始化脚本（建库/默认账号）
│   └── tests/
├── web/                          # Vue3 前端
│   ├── package.json
│   └── src/
│       ├── api/  views/  components/  stores/  router/
├── data/                         # 运行时数据（gitignore）
│   ├── filmhub.db                # SQLite
│   ├── originals/  films/  previews/  thumbs/
└── README.md
```

## 4. 数据库设计

### 4.1 表结构

**users**（单用户）
| 字段 | 类型 | 说明 |
|------|------|------|
| id | int PK | |
| username | str unique | |
| password_hash | str | bcrypt |
| created_at | datetime | |

**devices**（设备）
| 字段 | 类型 | 说明 |
|------|------|------|
| id | int PK | |
| device_id | str unique | 设备标识，取 ESP32 MAC |
| name | str | 设备名（前端可改） |
| device_type | str | `basic`（画布 400×600，film 头 600×400） / `pro`（画布 528×792，film 头 792×528） |
| token | str | 设备访问令牌（注册响应下发） |
| is_claimed | bool | 是否已被用户认领（首次心跳自动注册后待认领） |
| wifi_enable | bool | 最近上报的配置（设备已有设置参数） |
| play_mode | int | 0 手动 / 1 本地轮播 / 2 网络（设备已有设置参数） |
| sleep_mode | bool | 休眠开关（设备已有设置参数） |
| sleep_auto | bool | 定时唤醒开关（设备已有设置参数） |
| sleep_time | int | 定时唤醒时间（分钟，设备已有设置参数） |
| ble_enable | bool | BLE 开关（设备已有设置参数） |
| current_file_id | int | 当前显示文件 ID（设备已有设置参数） |
| heartbeat_interval | int | 秒（5–180，服务端可调） |
| battery_percent | int | 实时电量（心跳上报） |
| voltage_mv | int | 电池电压（心跳上报） |
| last_heartbeat_at | datetime | 在线状态依据（>3×interval 视为离线） |
| last_ip | str | 最近心跳来源 IP |
| created_at | datetime | |

**albums**（相册）
| 字段 | 类型 | 说明 |
|------|------|------|
| id | int PK | |
| name | str | |
| description | str | |
| cover_photo_id | int FK→photos | 封面 |
| created_at | datetime | |

**photos**（照片）
| 字段 | 类型 | 说明 |
|------|------|------|
| id | int PK | |
| album_id | int FK | 所属相册 |
| filename | str | 原始文件名 |
| original_path | str | 原图路径 |
| film_path | str | 转换后的 film 路径（可缓存） |
| preview_path | str | 预览 PNG（e-ink 效果） |
| width / height | int | 原图尺寸 |
| sort | int | 排序 |
| created_at | datetime | |

**templates**（模板）
| 字段 | 类型 | 说明 |
|------|------|------|
| id | int PK | |
| name | str | |
| kind | str | `album/calendar/memo/weather/fridge/fortune/countdown/quote/custom` |
| is_builtin | bool | 内置模板不可编辑删除 |
| definition | text(JSON) | 模板描述（§9） |
| render_config | text(JSON) | 渲染算法参数：`dither_type / dither_strength / contrast / brightness / font` |
| thumb_path | str | 缩略图 |
| created_at | datetime | |

**streams**（轮播流）
| 字段 | 类型 | 说明 |
|------|------|------|
| id | int PK | |
| name | str | |
| mode | str | `server_push` / `device_pull`（§11） |
| enabled | bool | 是否启用 |
| created_at | datetime | |

**stream_items**（轮播流条目）
| 字段 | 类型 | 说明 |
|------|------|------|
| id | int PK | |
| stream_id | int FK | |
| template_id | int FK | 该条目使用的模板 |
| position | int | 拖动顺序 |
| schedule_type | str | `relative`(相对) / `absolute`(绝对) |
| duration_sec | int | 相对：显示时长（秒） |
| start_at | datetime | 绝对：定点开始时刻 |
| enabled | bool | 条目是否启用 |

**push_records**（推送记录，可选增强）
| 字段 | 类型 | 说明 |
|------|------|------|
| id | int PK | |
| device_id | int FK | |
| stream_item_id | int FK | |
| film_path | str | 生成的 film |
| method | str | `push` / `pull` |
| pushed_at | datetime | |

### 4.2 文件目录

```
data/
├── filmhub.db
├── originals/{photo_id}.{ext}      # 原图
├── films/{photo_id|template_key}.film  # film 二进制
├── previews/{photo_id|template_key}.png # 渲染预览（e-ink 效果）
└── thumbs/{id}.png                 # 缩略图
```

## 5. API 设计总览

| 分组 | 前缀 | 认证 |
|------|------|------|
| 管理 API | `/api/v1/admin/*` | 用户 JWT |
| 设备 API | `/api/v1/device/*` | 设备 token |
| 静态 | `/`（前端 dist）、`/files/*` | 管理 API 下鉴权或公开 |

管理 API 一览（详细路径随开发确定）：

```
POST   /api/v1/admin/auth/login        # 登录，返回 JWT
PUT    /api/v1/admin/auth/password     # 修改密码

GET    /api/v1/admin/devices           # 设备列表（含在线状态）
POST   /api/v1/admin/devices/{id}/claim    # 认领设备
PUT    /api/v1/admin/devices/{id}      # 改名称/类型
DELETE /api/v1/admin/devices/{id}
GET    /api/v1/admin/devices/{id}/files    # 设备内文件列表（下发查询指令获取）

POST   /api/v1/admin/albums            # 创建相册
GET    /api/v1/admin/albums
POST   /api/v1/admin/albums/{id}/photos/batch   # 批量上传
DELETE /api/v1/admin/albums/{id}/photos/{pid}
PUT    /api/v1/admin/albums/{id}/photos/{pid}/sort

GET    /api/v1/admin/templates         # 模板库（内置+自定义）
POST   /api/v1/admin/templates         # 创建自定义模板
PUT    /api/v1/admin/templates/{id}
DELETE /api/v1/admin/templates/{id}
GET    /api/v1/admin/templates/{id}/preview    # 服务端渲染预览 PNG

GET/POST/PUT /api/v1/admin/streams     # 轮播流 CRUD
POST   /api/v1/admin/streams/{id}/items/sort   # 拖动排序
PUT    /api/v1/admin/streams/{id}/items/{iid}  # 时间配置

GET    /api/v1/admin/devices/{id}/status       # 实时状态（电量/文件/配置）
GET    /api/v1/admin/stats/dashboard           # 仪表盘统计

POST   /api/v1/admin/ai/template        # AI 创建自定义模板
POST   /api/v1/admin/ai/image           # AI 生图（保存入相册）
GET    /api/v1/admin/settings/ai        # AI 配置
PUT    /api/v1/admin/settings/ai
```

## 6. 认证与安全

### 6.1 用户侧（单用户 JWT）
- 首次启动自动创建默认账号 `admin`（初始密码 `filmhub`），登录后强制引导修改
- 登录成功签发 JWT（24h 有效期），前端存 localStorage，请求头 `Authorization: Bearer <jwt>`
- 修改密码后旧 token 失效（JWT `ver` 版本号校验）

### 6.2 设备侧（设备 token）
- 设备注册流程（§7.3）中，后端签发随机设备 token（32 位 hex），设备存 NVS
- 设备请求心跳/film 时携带 `device_id` + `token`，后端校验
- 用户可在管理页"重置设备 token"（重置后设备需重新注册）

### 6.3 局域网安全假设
- HTTP 明文传输（局域网内），不引入 TLS，降低固件实现成本
- 管理 API 仅监听局域网地址，若后续暴露公网需加 TLS（HTTPS）与强密码

## 7. 设备心跳协议（核心设计）

### 7.1 协议形态
- 设备定时 `HTTP GET` 心跳地址，间隔 `heartbeat_interval`（5–180s）
- 请求用 **GET + query 参数**（esp_http_client 实现最简，无需 body）
- 响应为 **JSON**（固件用 cJSON 解析）

### 7.2 请求

```
GET {heartbeat_url}?device_id={mac}&token={token}
   &battery={0-100}&voltage_mv={2800-4300}
   &play_mode={0|1|2}&wifi_enable={0|1}
   &sleep_mode={0|1}&sleep_auto={0|1}&sleep_time={10-2880}
   &ble_enable={0|1}&current_file_id={n}
   &state={idle|displaying|downloading|sleep}
```

| 参数 | 必填 | 说明 |
|------|------|------|
| device_id | ✓ | 设备标识（默认 ESP32 MAC，`esp_read_mac`） |
| token | ✓ | 注册时下发，存 NVS |
| battery | 选 | 电量百分比 |
| voltage_mv | 选 | 电压（mV） |
| play_mode / wifi_enable | 选 | 设备已有设置参数（`ServiceFilm_Def_t` / `ServiceNetwork_Def_t`） |
| sleep_mode / sleep_auto / sleep_time | 选 | 设备已有设置参数（`ServiceSleep_Def_t`） |
| ble_enable | 选 | 设备已有设置参数（`ServiceBle_Def_t`） |
| current_file_id | 选 | 当前显示文件 ID（`ServiceFilm_Def_t`） |
| state | 选 | 设备工作状态 |

> 说明：心跳上报字段**以设备已有的设置参数为准**（即固件 `ServiceParam_Def_t` 中定义的参数），不额外引入未定义字段。

### 7.3 注册流程（首次心跳自动注册）

```
设备首次心跳（无 token / token 无效）
  → 后端按 device_id 查找
    ├─ 未找到：自动创建设备记录（is_claimed=0），签发 token，
    │   响应体携带 token 与 device_type 建议值
    └─ 已存在未认领：视为重复注册，沿用原记录
前端管理页「设备管理」→ 看到"待认领"设备 → 确认认领、命名、设置设备类型
```

- 设备类型（basic/pro）由**用户认领时在前端设置**（满足需求 F1：工具通过设置设备类型获得屏幕分辨率版本），后端转换按此执行
- 设备收到响应后把 `token` 写入 NVS

### 7.4 响应（指令下发机制）

```json
{
  "code": 0,
  "msg": "ok",
  "data": {
    "server_time": 1786113076,
    "heartbeat_interval": 60,
    "commands": [
      {
        "cmd": "download_film",
        "params": { "url": "http://192.168.1.10:8000/api/v1/device/film/latest.film?device_id=xx&token=yy" }
      },
      {
        "cmd": "set_config",
        "params": { "play_mode": 2, "wifi_enable": 1 }
      }
    ]
  }
}
```

**指令表（cmd）**：

| cmd | params | 说明 |
|-----|--------|------|
| `download_film` | `url`（完整绝对地址） | 发起 film WiFi 下载 |
| `set_config` | `play_mode` / `wifi_enable` / `sleep_*` 等 | 设备功能配置 |
| `set_heartbeat` | `interval`（秒） | 调整心跳间隔 |
| `sync_time` | `timestamp` | 时间同步（设备 NTP 不可用时的兜底） |
| `reboot` | — | 重启设备（可选） |
| `clear_files` | — | 清理设备内文件（可选） |

- `commands` 为空数组表示"无指令"，设备仅维持心跳
- 一次可下发多条，设备按序执行
- `heartbeat_interval` 字段可随时调整设备心跳频率

### 7.5 设备状态展示

- 后端记录每次心跳上报的字段（电量、电压、play_mode、wifi_enable、sleep、ble、current_file_id、state 等，均为设备已有设置参数）
- 在线判定：`now - last_heartbeat_at <= 3 × interval` 视为在线，否则离线
- 前端「设备状态」面板展示：实时电量、电压、设备设置参数（播放模式/WiFi/休眠/BLE/当前文件）、工作状态、最近心跳时间（满足需求 F3 设备状态显示）
- 设备内文件列表：通过心跳响应下发查询指令，设备返回 SD 卡文件清单（不在心跳上报字段内）

## 8. 设备 film 获取协议

```
GET /api/v1/device/film/latest.film?device_id={mac}&token={token}
```

- 响应：`Content-Type: application/octet-stream`，直接返回 film 二进制（30s 内完成，可重定向）
- **文件名设计**：固件用 URL 最后一段作为 SD 文件名，故 URL 固定以 `.film` 结尾（`latest.film`），保证设备端文件命名正确
- **分辨率来源**：后端按 `devices.device_type` 查画布尺寸（basic=400×600，pro=528×792，均为竖版）渲染，再按设备采样方式转 film（basic=rotated 列优先翻转 / pro=row-major 行优先），文件头写固件横屏分辨率（600×400 / 792×528）（满足需求 F1）
- **内容来源**：后端从轮播调度器（§11）计算"当前应显示内容"→ 渲染 → 转 film → 返回；无内容时返回 204
- **推进语义**：设备成功拉取一次 = 当前内容已消费，`device_pull` 模式下调度器自动推进到下一项

## 9. 模板引擎设计

### 9.1 模板 = 定义（JSON）+ 渲染管线

所有模板（内置/自定义）统一为一份 `definition` JSON，由同一渲染管线执行：

```json
{
  "kind": "custom",
  "background": { "color": "#ffffff", "image": null },
  "layers": [
    { "type": "text",  "x": 20, "y": 16, "w": 360, "h": 48,
      "font": "NotoSansSC", "size": 32, "color": "#000000",
      "align": "left", "weight": "bold",
      "value": { "source": "memo", "key": "text" } },
    { "type": "image", "x": 0, "y": 0, "w": 400, "h": 600,
      "source": { "album_id": 3 }, "fit": "cover" },
    { "type": "rect",  "x": 10, "y": 10, "w": 40, "h": 40, "fill": "#000000" }
  ],
  "data": { "memo": { "text": "今晚买牛奶" } }
}
```

- **图层类型**：`text`（文字）、`image`（图片/相册/封面）、`rect`（矩形）、`line`、`circle`、`table`（表格）
- **数据绑定**：`value.source` 引用数据源（§9.3），`data` 存放模板静态数据（如备忘录内容）
- **内置模板** = 预置 definition + 预置数据源接入（日历/天气等），与自定义模板同管线渲染，前端可见不可改
- **自定义模板实现方法（满足需求 F3）**：前端可视化编辑器生成 definition JSON → 后端校验 schema → 存库 → 渲染管线执行。即"所见即所得"画布式编辑，后端零代码扩展

### 9.2 渲染算法选择（满足需求「每个模板选择渲染算法」）

每个模板的 `render_config` 定义生成 film 时的转换参数，**与 `convert.js` 参数对齐**：

```json
{
  "dither_type": "floyd_steinberg",
  "dither_strength": 80,
  "contrast": 100,
  "brightness": 0,
  "palette": "6color"
}
```

| 参数 | 取值 | 说明 |
|------|------|------|
| dither_type | `none` / `floyd_steinberg` / `adaptive` | 抖动算法 |
| dither_strength | 0–100 | 抖动强度 |
| contrast | 0–200 | 对比度（默认 100） |
| brightness | -100–100 | 亮度 |
| palette | `6color` / `bw` | 调色板（对应 EPD 6 色 / 黑白） |

- 转换管线：渲染出 RGB 图 → 按 render_config 预处理（对比度/亮度）→ 6 色调色板量化 + 抖动 → 打包 4bit 像素 → 32B 文件头 → film 文件
- 调色板固定为项目 6 色编码（黑 0x00 / 白 0x11 / 绿 0x66 / 蓝 0x55 / 红 0x33 / 黄 0x22），与固件 `hal_epd.c` 一致

### 9.3 数据源

| 数据源 | 说明 | 来源 |
|--------|------|------|
| `album` | 相册照片 | 本地相册库 |
| `memo` | 备忘录 | 模板静态 data / 管理页维护 |
| `calendar` | 日历（当月） | 本地生成 |
| `weather` | 天气 | 外部 API（和风天气等，需配置 key） |
| `fortune` | 今日运势 | 本地规则生成 / 可选 AI |
| `fridge` | 冰箱食材 | 管理页维护（食材/保质期） |
| `countdown` | 倒计时 | 管理页维护（目标日期） |
| `quote` | 名言 | 内置词库 / 可选 AI 每日一句 |

## 10. 内置模板清单

| kind | 名称 | 说明 |
|------|------|------|
| album | 相册 | 指定相册 + 多图轮流刷新（内部间隔可配） |
| calendar | 日历 | 当月日历 + 当日高亮 |
| memo | 备忘录 | 文本列表 |
| weather | 天气 | 今日/未来天气（外部 API） |
| fridge | 冰箱食材 | 食材与保质期提醒（过期高亮） |
| fortune | 今日运势 | 每日随机/固定运势文案 |
| countdown | 倒计时 | 目标日期倒计时（天） |
| quote | 名言 | 每日一句 |

（补充建议模板已列入需求文档，实现顺序按里程碑推进）

## 11. 轮播流设计（核心设计）

### 11.1 轮播流结构

轮播流 = 有序模板条目序列，每个条目独立配置时间：

```
streams: { name: "客厅轮播", mode: "device_pull", enabled: true }
stream_items (按 position 排序):
  1. 相册模板（相册A）      schedule_type=relative  duration=30s
  2. 日历模板               schedule_type=absolute  start_at=08:00
  3. 天气模板               schedule_type=absolute  start_at=18:00
  4. 名言模板               schedule_type=relative  duration=2min
```

- **相对时间**：条目显示时长 `duration_sec`（从开始显示计时，到期切下一项）
- **绝对时间**：条目在指定时刻 `start_at`（HH:MM）生效，显示至下一绝对条目生效
- 相对与绝对混合：绝对条目作为"锚点"插在时间轴中，相对条目在其间顺延

### 11.2 时间轴计算

调度器维护"当前项游标"，规则：
1. 按 position 顺序读取启用条目，构建当日时间轴
2. 绝对条目绑定具体时刻；相对条目挂在最近绝对锚点之后累计时长
3. 每日 00:00 重置游标，按当天时间轴重新计算

### 11.3 两种轮播方式（物理实现说明）

> **重要说明**：ESP32 设备没有常驻网络连接（无长连接/推送通道），因此"服务端主动推送"在物理上无法直接推送到设备，实际落地为"后端调度 + 心跳响应即时下发指令"组合。两种模式的设备侧差异仅在于**心跳频率**。

| 模式 | 后端行为 | 设备侧行为 |
|------|----------|-----------|
| **服务端主动推送** `server_push` | APScheduler 到点渲染 → 标记设备有"待推送内容" → 设备心跳时在 `commands` 下发 `download_film` | 心跳间隔调短（如 5–15s），收到指令立即下载显示 |
| **设备主动获取** `device_pull` | 设备拉取 film 时，调度器计算当前项并返回；设备无需频繁心跳（间隔 60–180s） | 定时（可结合休眠唤醒）调用 film 下载 API，拉完一次后等待后端填充下一张 |

- 两种模式可全局配置（streams.mode），前端创建轮播流时选择
- `server_push` 对时效性要求高（准点推送），代价是设备频繁心跳耗电；`device_pull` 省电但更新延迟取决于拉取周期
- 设备休眠唤醒场景：设备"定时休眠唤醒来调 film 下载 API"→ 唤醒即拉取 → 显示 → 继续休眠（对应需求 F3 设备主动获取描述）

## 12. AI 接入设计

### 12.1 抽象层

- `ai_client.py` 抽象统一接口：`chat()（文本）` 与 `generate_image()（生图）`
- 配置（管理页可维护）：`provider`（OpenAI 兼容）/ `base_url` / `api_key` / `model` / 生图接口
- 默认支持 OpenAI 兼容协议（DeepSeek / Kimi / 通义千问 / 本地 Ollama 均可接入）

### 12.2 AI 创建自定义模板（满足需求 F5）

```
用户对话 → 后端组装 Prompt（含模板 JSON schema 与示例）→ LLM 输出 definition JSON
→ 后端校验 schema → 存入 templates → 前端可继续可视化微调
```

- Prompt 工程：给定模板 schema + 约束（尺寸、图层类型、数据源、渲染算法参数）
- 校验失败时把错误回喂 LLM 重试（最多 2 次）

### 12.3 AI 生图模板

- `POST /api/v1/admin/ai/image`：提示词 → 生图 API → 保存到指定相册 → 应用相册模板
- 生图服务为可选项：未配置时该功能隐藏

## 13. 前端设计

### 13.1 页面结构

| 路由 | 页面 | 说明 |
|------|------|------|
| `/login` | 登录页 | 炫酷动效（粒子/渐变），单用户 |
| `/` | 仪表盘 | 设备在线状态、电量、最近推送统计（ECharts） |
| `/devices` | 设备管理 | 列表、认领、配置、实时状态、文件列表 |
| `/albums` | 相册 | 批量上传（拖拽+多选）、预览、管理 |
| `/templates` | 模板库 | 内置/自定义卡片、搜索 |
| `/templates/editor/:id` | 模板编辑器 | 画布拖拽可视化编辑、实时预览（服务端渲染 PNG） |
| `/streams` | 轮播流 | 拖拽编排（vuedraggable）、时间配置、两种模式切换 |
| `/ai` | AI 工具 | 对话建模板、生图 |
| `/settings` | 设置 | 账号密码、AI 配置、服务器信息 |

### 13.2 视觉与体验（满足需求 F3「精致好看炫酷好用」）

- 主题：深色主调 + 渐变强调色，玻璃拟态卡片，微动效（hover/加载）
- 登录页与仪表盘使用 canvas 粒子/光效背景
- 模板预览直接展示"墨水屏模拟效果"（黑白灰/6 色渲染预览，所见即所得）
- 响应式适配桌面/平板
- ECharts 展示电量趋势、推送统计

## 14. 固件侧配套改动清单（后续实施，本次仅设计）

| # | 改动 | 涉及文件 |
|---|------|---------|
| 1 | 新增 BLE 命令：0x3E 心跳URL设置 / 0x3F 心跳URL查询 / 0x40 心跳间隔设置 / 0x41 心跳间隔查询 / 0x42 设备注册（请求 token） | `service_ble.h/c`、`ble-utils.js`、`frame.js` |
| 2 | 实现心跳任务：定时 GET heartbeat_url → cJSON 解析 → 执行 commands（download_film / set_config / set_heartbeat） | `service_wifi.c` |
| 3 | 下载请求附加 `device_id` + `token` query 参数；支持心跳下发的完整 URL | `service_wifi.c` |
| 4 | token / device_id 持久化（NVS） | `service_param.c` |
| 5 | 状态上报采集：电量、电压及设备已有设置参数（play_mode/wifi/sleep/ble/current_file_id 等） | 复用 `hal_bat.c` / `service_param.c` |
| 6 | 休眠唤醒按心跳周期拉取（device_pull 模式） | `service_film.c` |

## 15. 实施里程碑

| 里程碑 | 内容 | 交付物 |
|--------|------|--------|
| M1 | 后端骨架：登录、设备注册/心跳、film API（空内容）、前端骨架+登录页 | 可登录、设备可心跳、可下载空内容 |
| M2 | 转换引擎（convert.js 移植）+ 相册 + 内置模板（相册/日历/备忘录） | 上传→转换→预览→设备显示 |
| M3 | 模板编辑器 + 自定义模板 + 渲染算法选择 | 可视化建模板 |
| M4 | 轮播流 + 调度器 + 两种模式 + 设备状态面板 | 完整轮播 |
| M5 | AI 接入（建模板/生图） | AI 功能 |
| M6 | 前端打磨 + 固件配套改动 + 联调 | 整体可用 |

## 16. 风险与待确认项

- [ ] **"服务端主动推送"机制**：按 §11.3 采用"心跳响应即时下发"实现，确认可接受
- [ ] **设备标识**：默认用 ESP32 MAC 作为 device_id，确认
- [ ] **天气数据源**：需要外部 API key（和风等免费额度），确认是否接入或先做本地占位
- [ ] **今日运势**：本地规则生成还是接 AI，确认
- [ ] **AI 供应商**：默认 OpenAI 兼容协议，具体用哪家待定
- [ ] **设备 token 安全**：局域网明文 HTTP，确认可接受
- [ ] 轮播流相对/绝对混合规则细节（§11.2）后续按实际体验调整
