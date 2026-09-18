#!/usr/bin/env python3
"""把 assets/app/<app>/cover.film 内联为 tools/ForFilm/js/app-cover-data.js。

封面是 MonoFast 720×480 单帧 film（43232 字节）。ForFilm 常以 file:// 直接打开，
此时浏览器不允许 fetch 本地 assets，故以内联 base64 让“App 封面”功能在任何打开
方式下都能用。

用法（仓库根目录）：
    python tools/ForFilm/scripts/gen-app-cover-data.py
"""

import base64
import os
import sys

APPS = ["image", "template", "clock", "animation"]
SRC_DIR = os.path.join("assets", "app")
OUT_FILE = os.path.join("tools", "ForFilm", "js", "app-cover-data.js")


def main() -> int:
    if not os.path.isdir(SRC_DIR):
        print("请在仓库根目录执行（未找到 %s）" % SRC_DIR)
        return 1

    lines = [
        "// 本文件由 tools/ForFilm/scripts/gen-app-cover-data.py 生成，请勿手工编辑。",
        "// 内容：assets/app/<app>/cover.film 的 base64 内联数据。",
        "const APP_COVER_DATA = {",
    ]

    for name in APPS:
        path = os.path.join(SRC_DIR, name, "cover.film")
        with open(path, "rb") as fp:
            raw = fp.read()
        lines.append("    %s: '%s'," % (name, base64.b64encode(raw).decode("ascii")))
        print("%-10s %6d 字节" % (name, len(raw)))

    lines.append("};")

    with open(OUT_FILE, "w", encoding="utf-8", newline="\n") as fp:
        fp.write("\n".join(lines) + "\n")

    print("已生成 %s" % OUT_FILE)
    return 0


if __name__ == "__main__":
    sys.exit(main())
