# FilmAgent 实施路线与研究方向

> 版本：v0.1
> 状态：设计中
> 原则：先打通基础音频驱动与 dock 框架，再逐步接入 AI 与 filmagent 功能；每阶段以"可运行、可验证"为交付。

## 1. 分阶段路线

### 阶段一：基础音频驱动跑通（当前位置）
**目标**：确认硬件可用，驱动链路可读可写。

- [ ] 硬件接线：ES8311 I2C（SDA/SCL）+ I2S（BCLK/WS/DOUT/DIN）+ NS4150B PA_EN，回填 `hal_audio.h` 引脚宏
- [ ] `hal_audio_init`：I2C 扫描确认 ES8311（地址 0x18）；I2S 标准模式全双工初始化
- [ ] 采集通路：`hal_audio_read_stream` 读到麦克风波形（log 或 VAD 能量可视化验证）
- [ ] 播放通路：`hal_audio_write_stream` 播放测试音/正弦波，功放开关正常、无 POP 音
- [ ] 构建改动：`film_hal/CMakeLists.txt` 加 `esp_driver_i2s`
- [ ] 接入 `hal_init.c`

**验证**：能用喇叭播放测试音，能读到麦克风数据。

### 阶段二：服务层语音链路
**目标**：形成 `service_audio` 状态机，可用按键触发采集与播放。

- [ ] `service_audio_init` 挂载到 `service_init.c`
- [ ] 状态机：IDLE / LISTEN / PROCESS / PLAYBACK / FAIL
- [ ] 唤醒：编码器按键事件触发（初版，复用 `hal_input`）
- [ ] VAD/能量检测：判定"说完了"，截断采集片段
- [ ] 双工调度：同一时刻单一数据流向；空闲关 codec/PA 省电
- [ ] 采集片段的上传与 TTS 音频下载接口（本地 mock，先不真连 AI）

**验证**：按键说话 → 采集 → 上传（mock）→ 下载 TTS → 播放，全链路可通。

### 阶段三：对接后端 AI
**目标**：接入 film-hub 服务端，打通 ASR / LLM / TTS。

- [ ] 服务端 `services/asr.py`、`services/tts.py`、`voice` 路由
- [ ] 复用 `ai_client.py`（`chat_json` / `generate_image`）
- [ ] 语音对话闭环：说话 → 文字 → LLM → TTS 回放
- [ ] 供电/采样率/网络耗时优化（16k→必要时 8k；chunk 上传策略）

**验证**：dock 语音对话，后端返回语音回应。

### 阶段四：FilmAgent 功能落地
**目标**：对话驱动墨水屏显示 + 局域网传输。

- [ ] `display_cmd` 解析：模板 key / 相册 / 原图 → 复用 renderer 生成 film → 设备 film API 下发
- [ ] 功能：天气 / 照片 / 菜谱 / 日历 等模板接入
- [ ] 局域网照片传输：A 设备对话指定目标设备 → 服务端生成 film → 目标设备心跳下发
- [ ] 前端（管理页）补充语音/AI 设置项
- [ ] 跨端一致性确认：BLE 命令、颜色编码三端同步（若语音涉及 BLE 控制）

**验证**：对 dock 说"显示明天天气"，墨水屏刷新为天气画面；说"把这张照片发到客厅"，客厅设备刷新。

## 2. 研究方向（开放问题）

- **唤醒词**：从按键触发演进到离线唤醒词（ESP-SR / OpenWakeWord），评估算力与功耗。
- **ASR / TTS 选型**：本地（whisper / VITS / Piper） vs 云端（OpenAI / 讯飞 / 阿里），考虑离线可用性与延迟。
- **带宽与延迟**：16k mono 16bit = ~32KB/s；chunk 上传 / 流式（websocket）策略研究。
- **回声消除（AEC）**：全双工对话时需考虑麦克风采样喇叭回放，评估是否需要 AEC（NS4150B 无 RST，需软件或结构规避）。
- **显示决策**：LLM 如何可靠输出结构化显示指令（JSON schema 约束 + 回喂重试，参考 film-hub §12.2）。
- **多设备协同**：设备注册、目标解析、屏幕分辨率匹配（`device_type`）的会话级协议。

## 3. 待确认项汇总

- [ ] 音频 GPIO 引脚最终分配（硬件原理图定稿）
- [ ] 采样率：16k（默认） vs 8k
- [ ] 驱动路径：裸 I2S + 手动寄存器 vs `esp_codec_dev` 组件
- [ ] 唤醒方案：按键（初版） vs 离线唤醒词（后续）
- [ ] ASR/TTS 供应商与本地/云端选型
- [ ] 是否需要 AEC
- [ ] 语音相关是否落地到 BLE 命令（新增从 0x3E 起）
