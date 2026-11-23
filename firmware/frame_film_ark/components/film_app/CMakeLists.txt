# film_app 目录分层：
#   inc/       对外公共头（其他组件只应包含这里的东西：app_init.h / app_interface.h）
#   core/      框架核心：初始化、调度器、渲染抽象、状态栏外壳、开机行为参数
#   pages/     框架自带页面（不是内容 app）：开机画面 / 主菜单 / 系统设置 / 休眠卡
#   apps/<name>/ 内容 app：每个 app 一个文件夹，头文件与源文件同目录
set(srcs "core/app_init.c"
         "core/app_manager.c"
         "core/app_render.c"
         "core/app_boot_cfg.c"
         "core/app_shell.c"
         "core/app_typography.c"
         "pages/app_boot.c"
         "pages/app_menu.c"
         "pages/app_settings.c"
         "pages/app_sleep.c"
         "apps/image/app_image.c"
         "apps/template/app_template.c"
         "apps/clock/app_clock.c"
         "apps/clock/font_clock_hero.c"
         "apps/animation/app_animation.c"
         "apps/pass/app_pass.c"
         )

set(public_include "inc")

# 各分组目录也加入包含路径：组件内部一律按**文件名**包含（如 "app_image.h"），
# 移动文件时无需逐个改 #include。
set(private_include "core"
                    "pages"
                    "apps/image"
                    "apps/template"
                    "apps/clock"
                    "apps/animation"
                    "apps/pass"
                    )

set(requires film_service film_hal film_sys film_ui)

# 需要 EMBED 进 flash 的二进制资源（当前只有宠物帧序列）
set(embed_files "")

if(CONFIG_ARK_HAS_NFC)
    list(APPEND srcs "apps/wallet/app_wallet.c" "apps/wallet/app_wallet_store.c")
    list(APPEND private_include "apps/wallet")
    # 电子宠物（n16r8）：状态动画用视频抽帧的 1-bit 序列，蛋/墓碑用程序化精灵兜底
    list(APPEND srcs "apps/tamago/app_tamago.c"
                     "apps/tamago/tamago_sprite.c"
                     "apps/tamago/tamago_frames.c")
    list(APPEND private_include "apps/tamago")
    list(APPEND embed_files
         "apps/tamago/frames/frames_idle.bin"
         "apps/tamago/frames/frames_happy.bin"
         "apps/tamago/frames/frames_eat.bin"
         "apps/tamago/frames/frames_sick.bin"
         "apps/tamago/frames/frames_sleep.bin"
         "apps/tamago/frames/frames_evolve.bin")
endif()

if(CONFIG_ARK_SCREENSHOT)
    list(APPEND srcs "core/app_screenshot.c")
endif()

idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS ${public_include} ${private_include}
                       REQUIRES ${requires}
                       EMBED_FILES ${embed_files}
                      )
