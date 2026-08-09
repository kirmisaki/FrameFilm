# AGENTS.md

film-hub（server/）AI 开发指南。本目录为 FrameFilm 的局域网后端 + 管理前端。

## 项目身份

开源彩色电子纸冰箱贴的服务端：模板编排、服务端渲染、轮播时间轴、OTA 推送。

- 后端 Python + FastAPI（Pillow 渲染），前端纯静态 HTML + 原生 JS（无框架/Vite）
- 兼容双屏幕：basic 400×600（film 120032B）/ pro 528×792（film 209120B）
- Git 中文 commit: `type(scope): 描述`（全仓库适用）

## 目录结构

```
server/
├── backend/                    # FastAPI 后端
│   ├── app/
│   │   ├── main.py             # 入口：seed_admin / seed_builtin_templates
│   │   ├── config.py           # 路径、默认管理员、心跳参数
│   │   ├── db.py               # SQLite + SQLAlchemy
│   │   ├── models/ schemas/    # ORM 模型 / Pydantic
│   │   ├── api/                # auth/device/album/template/stream/ai/settings_api/device_proto
│   │   └── services/           # 核心业务（见下）
│   └── .venv/                  # 虚拟环境（python.exe 直接运行）
└── web/dist/                   # 管理前端（后端 StaticFiles 直接提供，改完刷新即生效）
    ├── js/api.js               # JWT 请求封装（API.* / FH.authImg）
    ├── js/common.js            # guard/toast/confirm/refreshStatus
    └── *.html                  # login/dashboard/devices/templates/streams/albums/ai/settings
```

## 核心服务层（services/）

| 文件 | 职责 |
|---|---|
| `builtin_templates.py` | **内置模板定义**（声明式：schemes+params+layers）+ `BUILTIN_TEMPLATES` 注册表 |
| `data_sources.py` | 数据源：`resolve(kind)` 分发到 calendar/memo/quote/fridge/countdown/fortune/weather/album 各 *_data 函数 |
| `renderer.py` | 渲染引擎：按 layers 逐图层绘制（rect/line/circle/text/checklist/image/table/calendar_grid） |
| `film_convert.py` | RGB → film 二进制 + 预览 PNG（抖动/调色板，双屏参数化） |
| `scheduler.py` | 轮播流调度：build_timeline / current_item / resolve_film_for_device |

## 模板架构（重点）

**设计态** = `schemes`(配色方案) + `params`(参数定义数组) + `layers`(布局图层)
**应用态** = `definition.data.params`（用户配置随模板保存，轮播引用自动生效）

- 图层坐标以 400×600（竖版）为参考系，渲染时按目标分辨率缩放
- 颜色可写 `{"scheme": "key"}` 引用当前方案；数据绑定 `{"source": kind, "key": field}`
- 前端表单由 `params` 声明自动生成，无需改前端
- 内置模板注册/升级：`main.py::seed_builtin_templates()`，`_core_definition` 比较结构（剔除 data），`_merge_old_data` 保留旧应用参数与相册绑定

### 新增内置模板流程
1. `builtin_templates.py` 定义 dict + 追加 `BUILTIN_TEMPLATES`（唯一必需步骤）
2. 新数据源 → `data_sources.py` 写 `xxx_data()` + `resolve()` 加 elif 分支
3. 新绘制能力 → `renderer.py` `render_template` 加 `elif ltype == "xxx"`
4. 新表单控件 → `templates.html` 表单渲染 + `collectParams` 两处各加分支
5. 重启后端，seed 自动建库/升级，用 GET preview 验证

## 关键约定

- **预览接口要鉴权**：`/api/v1/admin/templates/{id}/preview` 不能直接 `<img src>`，必须 `FH.authImg(url)`
- `/files/*` 静态文件（缩略图/原图）无需鉴权，可直链
- 保存模板应用参数后必须调 `GET preview?refresh=true` 强制重渲染，否则卡片图是旧参数
- 前端 HTML 由后端中间件 no-cache（`main.py`），改页面直接刷新即生效
- 渲染算法唯一来源是后端（服务端渲染 + 转换，前端只展示）
- BLE 命令常量、film 颜色编码需与 C 固件 / 小程序 / Web 三端一致（见仓库根 AGENTS.md）

## 常见陷阱（不要做）

1. **不要只在 service 层直调 ESP-IDF driver** —— 那是固件的事，server 只负责渲染与分发
2. **不要改 BLE 命令值**；新增命令从 `0x3E` 起，同步 `blecmd_protocol.md`
3. **不要假设字符串编码** —— BLE 传输一律 ASCII + `\0`
4. **模板结构变更必须走 seed 升级**（保留用户配置），不要删库重灌；改名/删除内置模板要清理 DB 残留记录
5. **字体**：`simsunb.ttf`（宋体粗体）已损坏会渲染成方块，`serif_bold` 用 `simsun.ttc` 兜底
6. **PowerShell 不能内联多行 `python -c`**（& 与换行解析问题）、中文 JSON body 会乱码 —— 验证用临时脚本文件
7. **PIL `Image.rotate` 是逆时针** —— layout.rotate 语义为顺时针，渲染时取负
8. **`convert_image` 返回 `(palette_bytes, png_bytes)` 元组**，不是 Image

## 运行 / 验证命令

```bash
# 启动后端（8000 端口，--reload 热重载）
cd server/backend && .venv\Scripts\python.exe -m uvicorn app.main:app --host 127.0.0.1 --port 8000 --reload

# API 验证（默认管理员 admin / filmhub）
# 登录 POST /api/v1/admin/auth/login 拿 access_token → 请求头 Authorization: Bearer <token>
```

## 文档索引

- `docs/filmhub/requirements.md` — film-hub 需求与技术方案
- `docs/film/film.md` — film 文件格式
- `docs/blecmd/blecmd_protocol.md` — BLE 协议规范
- 仓库根 `AGENTS.md` — 全仓库架构约束（GPIO、跨端一致性、命名约定）
