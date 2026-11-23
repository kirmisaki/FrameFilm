set(srcs "src/ui_core.c"
         "src/ui_display.c"
         "src/ui_assets.c"
         "src/ui_defaults.c"      # 由 tools/ui-assets/gen_ui_assets.py 生成
         "src/ui_boot_logo.c"     # 由 tools/ui-assets/gen_boot_logo.cjs 生成
         "src/ui_boot_typography.c" # 固定标题图形，不嵌入字体文件
         )

# 正文 14/18px 与标题 24/36px 字库按变体分两份（同符号，二选一）：
# n16r8 含 wallet/tamago 文案，n4r2 只收录框架与常驻 app 用到的字，
# 否则 1900K 的 app 分区装不下。均由 tools/ark-font/gen_fonts.py 生成。
if(CONFIG_ARK_HAS_NFC)
    list(APPEND srcs "src/ui_fonts_body_nfc.c" "src/ui_fonts_title_nfc.c")
else()
    list(APPEND srcs "src/ui_fonts_body.c" "src/ui_fonts_title.c")
endif()

if(CONFIG_ARK_HAS_NFC)
    list(APPEND srcs "src/ui_fonts_wallet.c") # 由 tools/ark-font/gen_fonts.py 生成
endif()

set(public_include "inc")

set(requires lvgl film_hal film_sys esp_timer)

idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS ${public_include}
                       REQUIRES ${requires}
                      )
