# FilmAgent 设计与研究方向

> 项目：FrameFilm（彩色电子纸冰箱贴 / 墨水屏相框）
> 平台：FrameFilm Dock（ESP32-S3 + EPD 3.64" 760×568 + ES8311/NS4150B 语音模块）
> 状态：设计中（语音模块驱动与 dock 端框架确定，filmagent 功能为后续方向）
> 日期：2026-09-05

## 目标

让 **dock** 具备语音交互能力，通过对话驱动墨水屏生成内容，最终形成一套 **FilmAgent**：

- **语音对话** → 生成天气 / 照片 / 菜谱 / 日历等墨水屏显示画面
- **语音对话** → 向局域网内其他 FrameFilm 设备传输照片（后续）
- 复用既有 **film-hub** 后端（FastAPI + Pillow 渲染 + OpenAI 兼容 AI 客户端）

## 文档索引

| 文档 | 内容 |
|------|------|
| [audio_driver.md](audio_driver.md) | ES8311+NS4150B 硬件接线 + dock 端音频驱动框架（HAL/Service） |
| [architecture.md](architecture.md) | FilmAgent 整体架构：语音链路、服务端对接、局域网传输方向 |
| [roadmap.md](roadmap.md) | 分阶段实施路线与待确认项 |

## 参考

- 参考项目：xiaozhi-esp32（`github.com/78/xiaozhi-esp32`）—— 取其 ES8311 编解码驱动与语音链路范式
- 模块资料：`docs/datasheet/ES8311+NS4150B CODEC 音频模块 使用说明书.pdf`、`...原理图.pdf`
- 关联后端：`docs/filmhub/design.md`、`server/backend/app/services/ai_client.py`
- 固件架构：`docs/knowledge/architecture.md`（三层分层 `film_service → film_hal → film_sys`）
