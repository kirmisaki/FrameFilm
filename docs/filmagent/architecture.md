# FilmAgent 整体架构设计

> 版本：v0.1
> 状态：设计中 —— 语音链路与后端对接为方向规划，驱动层见 `audio_driver.md`
> 目标：dock 通过语音对话，驱动墨水屏生成内容（天气/照片/菜谱/日历），并可向局域网设备传输照片。

## 1. 总体链路

```
┌────────────── FrameFilm Dock (ESP32-S3) ──────────────┐
│  麦克风 ──► ES8311 ADC (I2S) ──► service_audio 采集      │
│                                          │ (WiFi 上传) │
│                                          ▼             │
│  喇叭 ◄── NS4150B 功放 ◄─ ES8311 DAC ◄─ service_audio 播放│
│                                   ▲                    │
│                                   │ (TTS 音频流返回)    │
└───────────────────────────────────┼────────────────────┘
                                    │
        ┌───────────────────────────▼──────────────────────────┐
        │                 film-hub 服务端 (FastAPI)              │
        │   ASR(语音转文字) → LLM(理解/对话) → TTS(文字转语音)      │
        │                    ↓ (生成显示指令 / 模板 JSON)          │
        │   Pillow 模板渲染 → 6 色转 film → film-hub 设备 API      │
        │   （复用 ai_client.py：chat_json / generate_image）     │
        └──────────────────────────────────────────────┬────────┘
                                                       │ (HTTP 下载 film)
                    ┌──────────────────────────────────▼────────────────┐
                    │            dock  →  EPD(760×568) 显示                │
                    └────────────────────────────────────────────────────┘
```

### 关键点
- **语音闭环**：`采集(ASR) → 理解(LLM) → 回应(TTS)` 走服务端；`对话结果` 同时映射为"显示指令/模板"，经服务端渲染为 film 后由 dock 下载显示。
- **显示闭环**：复用 film-hub 既有能力（渲染管线、`film_convert`、设备 film API、模板引擎），**FilmAgent 只是新增语音入口**，不改动既有显示链路。
- **AI 复用**：直接复用 `server/backend/app/services/ai_client.py` 的 `chat_json()` 与 `generate_image()`，服务端只增加语音相关的 API 与 ASR/TTS 适配，不重写 AI 客户端。

## 2. 分模块职责

### 2.1 dock 端（固件）
| 模块 | 职责 | 阶段 |
|------|------|------|
| `film_hal/hal_audio` | I2S/I2C/GPIO 底层驱动，采集与播放流 | 阶段一 |
| `film_service/service_audio` | 语音状态机、唤醒、采集/播放调度、上传/下载 | 阶段二 |
| `film_service/service_wifi` | 复用：上传音频、下载 film、心跳 | 已有 |
| `film_service/service_film` | 复用：本地播放/E EPD 显示 | 已有 |
| `film_hal/hal_epd` | 复用：墨水屏渲染刷新 | 已有 |

### 2.2 服务端（film-hub 后端）
| 模块 | 职责 | 状态 |
|------|------|------|
| `ai_client.py` | 复用：`chat_json`（LLM 文本/JSON）、`generate_image`（生图） | 已有 |
| 新增 `voice` API | 接收音频、转 ASR、调 LLM、返回 TTS 音频流 | 待开发 |
| 新增"显示指令"接口 | 将 LLM 结果解析为模板/渲染指令，复用 renderer 生成 film | 待开发 |
| 既有 `device_proto.py` | 复用：心跳下发 `download_film`、设备 token 校验 | 已有 |

> 语音服务适配：ASR / TTS 建议独立封装为 `services/asr.py`、`services/tts.py`，接入 OpenAI 兼容或本地模型（如 whisper / edge-tts、VITS），并作为 `voice` 路由的依赖注入，与 `ai_client` 平级。

## 3. 语音 API 设计（初始草案）

```
POST /api/v1/device/voice/asr          # 上传音频 chunk → 返回文字
POST /api/v1/device/voice/chat         # 文字(或 ASR 结果)+上下文 → 返回 { reply_text, display_cmd }
POST /api/v1/device/voice/tts          # 文字 → 返回音频流 (audio/mpeg)
GET  /api/v1/device/voice/session      # 获取会话上下文/清除
```

- 认证：复用设备 token（同心跳）。
- **对话-显示映射**：`chat` 返回的 `display_cmd` 形如：

```json
{
  "reply_text": "好的，为你生成明天天气",
  "display_cmd": {
    "kind": "template",            // template | album | image | schedule
    "template_key": "weather",     // 内置模板 key
    "params": { "city": "深圳" },
    "fallback_film_url": "http://…/device/film/latest.film"
  }
}
```

- dock 收到后：播放 `reply_text` 的 TTS 声音；若有 `display_cmd`，走既有的 film 下载/渲染链路刷新 EPD。

## 4. FilmAgent 功能映射

| 对话指令 | 服务端处理 | 显示结果 |
|----------|-----------|----------|
| "显示明天天气" | LLM → 天气数据源 → 天气模板渲染 | 天气 film |
| "展示相册的宝宝照片" | LLM → 相册选择 → 相册模板/原图 | 照片 film |
| "做个菜谱：番茄炒蛋" | LLM → 菜谱模板(图文) | 菜谱 film |
| "显示这个月日历" | LLM → 日历模板 | 日历 film |
| "把这照片发到客厅那台" | 目标设备解析 → 局域网转发 | 目标设备 EPD 显示 |

## 5. 局域网照片传输方向（后续）

- **链路**：A 设备对话"把照片发到 B 设备" → 服务端解析目标 → 将目标 film 生成好 → B 设备心跳时下发 `download_film` → B 设备刷新显示。
- **实现依赖**：复用 film-hub 的 `devices` 表与心跳指令体系（`commands: [download_film]`），无需新增长连接。
- **前提**：所有 FrameFilm 设备都已注册到同一 film-hub 并带设备标识与目标屏幕分辨率（`device_type`）。

## 6. 架构约束（必须遵守）

1. **分层单向**：`film_service → film_hal → film_sys`；服务层不直接操作 GPIO/I2S/I2C。
2. **不动 BLE 命令值**：新增命令从 `0x3E` 起（语音相关若需 BLE 控制则新加值）。
3. **跨端一致性**：BLE 命令常量 / film 颜色编码在固件、小程序、Web 三处同步。
4. **sample rate 输入=输出**：ES8311 需一致（默认 16k），避免重采样复杂度。
5. **ASCII + `\0` 传输**：沿用既有约定。
