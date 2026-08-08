"""AI 客户端：OpenAI 兼容接口（DeepSeek / Kimi / 通义等）

配置存 settings 表（key="ai"）：
{"base_url": "...", "api_key": "...", "model": "...", "image_model": ""}
"""
import json

import httpx

DEFAULT_TIMEOUT = 120.0

SYSTEM_TEMPLATE_PROMPT = """你是 FrameFilm 墨水屏设备（6 色电子纸）的模板设计师。
用户会用自然语言描述想要的自定义模板，你负责把它翻译成模板 definition JSON。

模板 JSON 结构（400x600 竖版参考坐标系，整数坐标）：
{
  "kind": "custom",
  "background": {"color": "#ffffff"},
  "layers": [
    {"type": "text", "x": 24, "y": 20, "w": 300, "h": 48, "size": 36,
     "weight": "bold", "color": "#000000", "align": "left",
     "value": "标题文字"},
    {"type": "text", "x": 24, "y": 100, "w": 352, "h": 300, "size": 28,
     "color": "#222222", "value": "正文内容"},
    {"type": "rect", "x": 20, "y": 70, "w": 360, "h": 2, "color": "#000000"},
    {"type": "line", "x": 20, "y": 420, "w": 360, "h": 0, "color": "#888888", "width": 2}
  ],
  "data": {}
}

图层类型说明：
- text: 文字。value 可以直接是字符串。
- image: 图片，source: {"album_id": null}（相册，可空，前端配置）。
- rect: 矩形。fill 填充色、color 边框色、width 边框宽。
- line: 直线（x,y 起点，w,h 终点偏移）。
- circle: 圆形。
设计规则：
1. 只输出 JSON，不要任何解释或 markdown 代码块。
2. 电子纸显示偏灰暗，优先黑白灰，可用少量红/黄/蓝/绿强调。
3. 版式要克制，留白充足，字号层级清晰，适合 400x600 竖版低分辨率阅读。
4. 中文字体由系统渲染，无需指定 font。
"""


def load_ai_settings(db) -> dict:
    from ..models import Setting

    s = db.get(Setting, "ai")
    if s is None:
        return {}
    try:
        return json.loads(s.value or "{}")
    except json.JSONDecodeError:
        return {}


def save_ai_settings(db, settings: dict):
    from ..models import Setting

    s = db.get(Setting, "ai")
    if s is None:
        s = Setting(key="ai", value=json.dumps(settings, ensure_ascii=False))
        db.add(s)
    else:
        s.value = json.dumps(settings, ensure_ascii=False)
    db.commit()


async def chat_json(db, prompt: str, system: str = SYSTEM_TEMPLATE_PROMPT) -> dict:
    """调用对话模型并解析 JSON 返回"""
    settings = load_ai_settings(db)
    base_url = (settings.get("base_url") or "").rstrip("/")
    api_key = settings.get("api_key") or ""
    model = settings.get("model") or ""
    if not base_url or not api_key or not model:
        raise RuntimeError("AI 未配置，请先在设置中填写 API 配置")

    url = f"{base_url}/chat/completions"
    headers = {"Authorization": f"Bearer {api_key}"}
    payload = {
        "model": model,
        "messages": [
            {"role": "system", "content": system},
            {"role": "user", "content": prompt},
        ],
        "temperature": 0.7,
        "response_format": {"type": "json_object"},
    }
    async with httpx.AsyncClient(timeout=DEFAULT_TIMEOUT) as client:
        resp = await client.post(url, json=payload, headers=headers)
        resp.raise_for_status()
        data = resp.json()
    content = data["choices"][0]["message"]["content"]
    return json.loads(content)


async def generate_image(db, prompt: str, size: str = "1024x1024") -> bytes:
    """调用图像生成模型，返回 PNG bytes（images/generations 兼容接口）"""
    settings = load_ai_settings(db)
    base_url = (settings.get("base_url") or "").rstrip("/")
    api_key = settings.get("api_key") or ""
    image_model = settings.get("image_model") or ""
    if not base_url or not api_key or not image_model:
        raise RuntimeError("AI 图像生成未配置（需设置 image_model）")

    url = f"{base_url}/images/generations"
    headers = {"Authorization": f"Bearer {api_key}"}
    payload = {"model": image_model, "prompt": prompt, "size": size, "n": 1}
    async with httpx.AsyncClient(timeout=DEFAULT_TIMEOUT) as client:
        resp = await client.post(url, json=payload, headers=headers)
        resp.raise_for_status()
        data = resp.json()
    item = data["data"][0]
    if item.get("b64_json"):
        import base64

        return base64.b64decode(item["b64_json"])
    url_out = item.get("url")
    if url_out:
        async with httpx.AsyncClient(timeout=DEFAULT_TIMEOUT) as client:
            img_resp = await client.get(url_out)
            img_resp.raise_for_status()
            return img_resp.content
    raise RuntimeError("图像生成接口未返回有效图片")
