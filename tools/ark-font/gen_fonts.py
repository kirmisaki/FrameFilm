#!/usr/bin/env python3
"""Generate compact 1-bpp Ark UI fonts from official Source Han Sans CN."""
import argparse
import re
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[2]
ARK = ROOT / "firmware/frame_film_ark"
APP = ARK / "components/film_app"
BODY_OUT = ARK / "components/film_ui/src/ui_fonts_body.c"
BODY_NFC_OUT = ARK / "components/film_ui/src/ui_fonts_body_nfc.c"
TITLE_OUT = ARK / "components/film_ui/src/ui_fonts_title.c"
TITLE_NFC_OUT = ARK / "components/film_ui/src/ui_fonts_title_nfc.c"
WALLET_OUT = ARK / "components/film_ui/src/ui_fonts_wallet.c"
BODY = ROOT / "assets/fonts/NotoSansCJKsc-Bold.otf"
TITLE = ROOT / "assets/fonts/NotoSansCJKsc-Bold.otf"
# 24/36px 标题字体只在中文界面使用（英文走 Montserrat），故不保留英文小写字母。
# 只收录实际用到的字：全量收录（约 458 字形）会给固件多加约 107KB，
# 直接撑爆 n4r2 的 1900K app 分区（CI 会报 ota_0/ota_1 overflow）。
# 分两份：框架/常驻 app 用的字两个变体都要，宠物 app 的标题字只进 n16r8。
# 24px：主菜单条目名；n16r8 另有 喂食 / 正餐 / 零食 / 猜大小 / 宠物状态 / 已离开
#       + 游戏页动态文本（第 N 局、大 ▲ / 小 ▼、得分 N / 3）
# 36px：通行证页主字「通行证」、休眠页「已休眠」；n16r8 另有 TAMAGO、游戏结果「本局 猜中/猜错」
# 新增 app_font_title / app_font_hero 文案时，把新字补进对应的表。
LARGE_TEXT = {
    24: " 图片通行证模板时钟动图设置卡包宠物",
    36: " 通行证已休眠",
}
LARGE_TEXT_NFC = {
    24: "喂食正餐零食猜大小状态已离开得分第▲▼0123456789/",
    36: "本局猜中错TAMAGO",
}
TOKEN = re.compile(r'"(?:\\.|[^"\\])*"|//[^\n]*|/\*.*?\*/', re.S)

# 只在带 NFC 的 n16r8 固件里编译的 app：它们的中文文案不该进 n4r2 的正文子集，
# 否则 n4r2 的 1900K app 分区会被挤爆（约 10KB）。
NFC_ONLY_APPS = {"wallet", "tamago"}


def characters(include_nfc: bool = True) -> list[int]:
    """Keep the original page, shell and app sources; include core UI strings."""
    chars = set(range(32, 127))
    chars.update(map(ord, "—℃"))
    files = list((APP / "pages").glob("*.c"))
    files += list((APP / "core").glob("*.c"))
    files += list((APP / "core").glob("*.h"))
    files += list((APP / "apps").glob("*/app_*.c"))
    files += list((APP / "apps").glob("*/app_*.h"))
    if not include_nfc:
        files = [p for p in files if p.parts[-2] not in NFC_ONLY_APPS]
    for path in files:
        for match in TOKEN.finditer(path.read_text(encoding="utf-8")):
            literal = match.group()
            if literal.startswith('"'):
                chars.update(ord(c) for c in literal if 127 < ord(c) < 65536)
    return sorted(chars)

def common_chinese() -> set[int]:
    """GB2312 level-1: the 3755 most common hanzi, for user-supplied card names."""
    chars = set()
    for high in range(0xB0, 0xD8):
        for low in range(0xA1, 0xFF):
            try:
                chars.add(ord(bytes((high, low)).decode("gb2312")))
            except UnicodeDecodeError:
                pass
    return chars

def emit_font(name: str, font_path: Path, pixels: int,
              chars: list[int], line: int, cap: int) -> tuple[str, int]:
    font = ImageFont.truetype(str(font_path), pixels)
    bitmap = bytearray()
    descriptors = ["    {0},"]
    for codepoint in chars:
        char = chr(codepoint)
        x0, y0, x1, y1 = font.getbbox(char, anchor="ls")
        width, height = x1 - x0, y1 - y0
        image = Image.new("L", (max(1, width), max(1, height)))
        ImageDraw.Draw(image).text(
            (-x0, -y0), char, font=font, fill=255, anchor="ls"
        )
        packed = bytearray((width * height + 7) // 8)
        for index, value in enumerate(image.tobytes()[:width * height]):
            if value >= 128:
                packed[index // 8] |= 0x80 >> (index % 8)
        descriptors.append(
            "    {.bitmap_index = %d, .adv_w = %d, .box_w = %d, "
            ".box_h = %d, .ofs_x = %d, .ofs_y = %d}, /* U+%04X */"
            % (len(bitmap), round(font.getlength(char) * 16), width,
               height, x0, -y1, codepoint)
        )
        bitmap.extend(packed)
    prefix = f"font_{name}"
    rows = [
        "    " + ", ".join(f"0x{v:02x}" for v in bitmap[i:i + 16]) + ","
        for i in range(0, len(bitmap), 16)
    ]
    offsets = ", ".join(str(codepoint - chars[0]) for codepoint in chars)
    source = f"""
static const uint8_t {prefix}_bitmap[] = {{
{chr(10).join(rows)}
}};
static const lv_font_fmt_txt_glyph_dsc_t {prefix}_glyphs[] = {{
{chr(10).join(descriptors)}
}};
static const uint16_t {prefix}_unicodes[] = {{{offsets}}};
static const lv_font_fmt_txt_cmap_t {prefix}_cmap[] = {{{{
    .range_start = {chars[0]}, .range_length = {chars[-1] - chars[0] + 1},
    .glyph_id_start = 1, .unicode_list = {prefix}_unicodes,
    .list_length = {len(chars)}, .type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY
}}}};
static const lv_font_fmt_txt_dsc_t {prefix}_dsc = {{
    .glyph_bitmap = {prefix}_bitmap, .glyph_dsc = {prefix}_glyphs,
    .cmaps = {prefix}_cmap, .cmap_num = 1, .bpp = 1,
    .bitmap_format = LV_FONT_FMT_TXT_PLAIN, .stride = 0
}};
const lv_font_t ui_font_{name} = {{
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,
    .line_height = {line}, .base_line = 4,
    .cap_height = {cap}, .x_height = {cap * 2 // 3},
    .subpx = LV_FONT_SUBPX_NONE, .kerning = LV_FONT_KERNING_NONE,
    .static_bitmap = 1, .underline_position = -2,
    .underline_thickness = 1, .dsc = &{prefix}_dsc
}};
"""
    return source, len(bitmap)

def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="check generated output")
    parser.add_argument("--body-font", type=Path, default=BODY)
    parser.add_argument("--title-font", type=Path, default=TITLE)
    args = parser.parse_args()
    for path in (args.body_font, args.title_font):
        if not path.is_file():
            parser.error(f"Font not found: {path}")
    all_chars = characters()
    base_chars = characters(include_nfc=False)

    def header(desc: str) -> str:
        return (
            "/* Generated by tools/ark-font/gen_fonts.py; do not edit.\n"
            f" * {desc}\n"
            " * Adobe SIL OFL 1.1; license: tools/ark-font/OFL.txt.\n"
            " * 1-bpp, threshold 128, native-size packed rows. */\n"
            '#include "ui_fonts.h"\n'
        )

    def body_fonts(chars: list[int], desc: str, tag: str) -> str:
        output = header(desc)
        for name, pixels, line, cap in (
            (14, 16, 18, 14),
            (18, 18, 22, 18),
        ):
            source, count = emit_font(name, args.body_font, pixels, chars, line, cap)
            output += source
            print(f"[{tag}] ui_font_{name}: {len(chars)} glyphs, {count} bitmap bytes")
        return output

    # 正文 14/18px 分两份，由 CMake 按 CONFIG_ARK_HAS_NFC 选择：
    # 不带 NFC 的 n4r2 不收录 wallet / tamago 的文案（它们的页面根本不会被编译），
    # 这样 1900K 的 app 分区才装得下。
    body_output = body_fonts(base_chars, "Source Han Sans CN Bold 16/18px body subsets (n4r2).",
                             "n4r2")
    body_nfc_output = body_fonts(all_chars, "Source Han Sans CN Bold 16/18px body subsets (n16r8).",
                                 "n16r8")

    # Title fonts (24/36) are separate generated files so the two variants can
    # be maintained independently without conditional code in the body font.
    #
    # 字符集 = 中文界面实际用到的那批字（见 LARGE_TEXT / LARGE_TEXT_NFC），
    # 不跟随正文全量扫描：全量会让 n4r2 的 1900K 分区溢出（约多 107KB）。
    def title_fonts(extra: dict, desc: str, tag: str) -> str:
        output = header(desc)
        table = {pixels: {ord(c) for c in text} for pixels, text in extra.items()}
        for name, pixels, line, cap in (
            (24, 24, 28, 24),
            (36, 36, 40, 36),
        ):
            chars = sorted(table.get(name, set()))
            source, count = emit_font(name, args.title_font, pixels, chars, line, cap)
            output += source
            print(f"[{tag}] ui_font_{name}: {len(chars)} glyphs, {count} bitmap bytes")
        return output

    nfc_extra = {pixels: LARGE_TEXT.get(pixels, "") + LARGE_TEXT_NFC.get(pixels, "")
                 for pixels in set(LARGE_TEXT) | set(LARGE_TEXT_NFC)}
    title_output = title_fonts(LARGE_TEXT, "Source Han Sans CN Bold 24/36px title subsets (n4r2).",
                              "n4r2")
    title_nfc_output = title_fonts(nfc_extra, "Source Han Sans CN Bold 24/36px title subsets (n16r8).",
                                   "n16r8")

    wallet_chars = sorted(set(all_chars) | common_chinese())
    wallet_output = (
        "/* Generated by tools/ark-font/gen_fonts.py; do not edit.\n"
        " * Wallet card-name font: Source Han Sans CN Bold 18px + GB2312 level-1 hanzi.\n"
        " * Built only when CONFIG_ARK_HAS_NFC is on (card names are user supplied).\n"
        " * Adobe SIL OFL 1.1; license: tools/ark-font/OFL.txt.\n"
        " * 1-bpp, threshold 128, native-size packed rows. */\n"
        '#include "ui_fonts.h"\n'
    )
    source, count = emit_font("wallet_name", args.body_font, 18,
                              wallet_chars, 22, 18)
    wallet_output += source
    print(f"ui_font_wallet_name: {len(wallet_chars)} glyphs, {count} bitmap bytes")

    outputs = {BODY_OUT: body_output, BODY_NFC_OUT: body_nfc_output,
               TITLE_OUT: title_output, TITLE_NFC_OUT: title_nfc_output,
               WALLET_OUT: wallet_output}
    if args.check:
        for path, text in outputs.items():
            if not path.exists() or path.read_text(encoding="utf-8") != text:
                raise SystemExit(f"{path.name} is stale; run gen_fonts.py")
    else:
        for path, text in outputs.items():
            path.write_text(text, encoding="utf-8", newline="\n")

if __name__ == "__main__":
    main()