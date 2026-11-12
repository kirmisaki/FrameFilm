set(srcs "src/ui_core.c"
         "src/ui_display.c"
         "src/ui_assets.c"
         "src/ui_defaults.c"      # 由 tools/ui-assets/gen_ui_assets.py 生成
         "src/ui_fonts_body.c"    # 由 tools/ark-font/gen_fonts.py 生成（14/18px）
         "src/ui_fonts_title.c"   # 由 tools/ark-font/gen_fonts.py 生成（24/36px）
         "src/ui_boot_logo.c"     # 由 tools/ui-assets/gen_boot_logo.cjs 生成
         "src/ui_boot_typography.c" # 固定标题图形，不嵌入字体文件
         )

if(CONFIG_ARK_HAS_NFC)
    list(APPEND srcs "src/ui_fonts_wallet.c") # 由 tools/ark-font/gen_fonts.py 生成
endif()

set(public_include "inc")

set(requires lvgl film_hal film_sys esp_timer)

idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS ${public_include}
                       REQUIRES ${requires}
                      )
