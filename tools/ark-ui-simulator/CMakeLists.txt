cmake_minimum_required(VERSION 3.16)
project(framefilm_ark_ui_simulator C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(ARK_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../firmware/frame_film_ark")
set(LVGL_ROOT "${ARK_ROOT}/managed_components/lvgl__lvgl")
if(NOT EXISTS "${LVGL_ROOT}/CMakeLists.txt")
    message(FATAL_ERROR "Ark managed LVGL missing. Run the Ark ESP-IDF component install first.")
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
set(LV_BUILD_CONF_PATH "${CMAKE_CURRENT_LIST_DIR}/lv_conf.h" CACHE PATH "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG OFF CACHE BOOL "" FORCE)
set(LV_FETCH_DEPENDENCIES OFF CACHE BOOL "" FORCE)
add_subdirectory("${LVGL_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}/lvgl")

add_executable(ark-ui-simulator
    src/main.c
    src/simulator_platform.c
    "${ARK_ROOT}/components/film_app/pages/app_boot.c"
    "${ARK_ROOT}/components/film_app/pages/app_menu.c"
    "${ARK_ROOT}/components/film_app/pages/app_settings.c"
    "${ARK_ROOT}/components/film_app/pages/app_sleep.c"
    "${ARK_ROOT}/components/film_app/apps/pass/app_pass.c"
    "${ARK_ROOT}/components/film_app/apps/clock/app_clock.c"
    "${ARK_ROOT}/components/film_app/apps/clock/font_clock_hero.c"
    "${ARK_ROOT}/components/film_app/core/app_shell.c"
    "${ARK_ROOT}/components/film_app/core/app_typography.c"
    "${ARK_ROOT}/components/film_ui/src/ui_assets.c"
    "${ARK_ROOT}/components/film_ui/src/ui_defaults.c"
    "${ARK_ROOT}/components/film_ui/src/ui_fonts_body.c"
    "${ARK_ROOT}/components/film_ui/src/ui_fonts_title.c"
    "${ARK_ROOT}/components/film_ui/src/ui_boot_logo.c"
    "${ARK_ROOT}/components/film_ui/src/ui_boot_typography.c"
)
target_include_directories(ark-ui-simulator PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/stubs"
    "${ARK_ROOT}/components/film_app/inc"
    "${ARK_ROOT}/components/film_app/core"
    "${ARK_ROOT}/components/film_app/pages"
    "${ARK_ROOT}/components/film_app/apps/pass"
    "${ARK_ROOT}/components/film_app/apps/clock"
    "${ARK_ROOT}/components/film_app/apps/image"
    "${ARK_ROOT}/components/film_app/apps/animation"
    "${ARK_ROOT}/components/film_ui/inc"
    "${ARK_ROOT}/components/film_hal/inc"
    "${ARK_ROOT}/components/film_service/inc"
)
target_link_libraries(ark-ui-simulator PRIVATE lvgl PkgConfig::SDL2)
target_compile_definitions(ark-ui-simulator PRIVATE ARK_UI_SIMULATOR=1)
