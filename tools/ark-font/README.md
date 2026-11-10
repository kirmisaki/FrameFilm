# Ark UI fonts

The firmware uses four generated, compact 1-bpp fonts in `firmware/frame_film_ark/components/film_ui/src/ui_fonts.c`. The generator rasterizes the official Adobe Source Han Sans CN subset at its native target size, with threshold 128 and no grayscale or scaling at runtime.

| LVGL symbol | Source face | Raster size | Line height | Baseline |
| --- | --- | ---: | ---: | ---: |
| `ui_font_14` | Bold | 16 px | 18 px | 4 px |
| `ui_font_18` | Bold | 18 px | 22 px | 4 px |
| `ui_font_24` | Medium | 24 px | 28 px | 4 px |
| `ui_font_36` | Medium | 36 px | 40 px | 4 px |

The 14 and 18 fonts collect printable ASCII and non-ASCII string literals from Ark app pages, shell, core, and content apps, including settings options and clock labels. The 24 and 36 fonts retain explicit title subsets to keep firmware size small. Regenerate after changing displayed text; the generated C file is committed so offline builds need no OTF files or Pillow.

`ui_font_wallet_name` (`src/ui_fonts_wallet.c`) is the exception for user-supplied text: it adds all GB2312 level-1 characters (3755 common hanzi) at 18 px so wallet card names renamed over BLE can contain common Chinese. It is only compiled into NFC builds (`CONFIG_ARK_HAS_NFC`, i.e. n16r8); n4r2 keeps its 1900K app partition free of the extra ~210 KB.

Download the official OTFs to `.output/ark/` (ignored by Git), or pass custom local paths:

- [SourceHanSansCN-Bold.otf](https://raw.githubusercontent.com/adobe-fonts/source-han-sans/release/SubsetOTF/CN/SourceHanSansCN-Bold.otf)
- [SourceHanSansCN-Medium.otf](https://raw.githubusercontent.com/adobe-fonts/source-han-sans/release/SubsetOTF/CN/SourceHanSansCN-Medium.otf)

```sh
py -3.12 tools/ark-font/gen_fonts.py
py -3.12 tools/ark-font/gen_fonts.py --check
py -3.12 tools/ark-font/gen_fonts.py --body-font /path/to/SourceHanSansCN-Bold.otf --title-font /path/to/SourceHanSansCN-Medium.otf
```

The font license is [OFL.txt](OFL.txt). The OTF files are local generation inputs and are not committed.