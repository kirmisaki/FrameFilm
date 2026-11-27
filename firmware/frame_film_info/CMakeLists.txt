# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

# FrameFilmInfo：信息屏基础工程（单机型，FrameFilmCommon 主板）
# 硬件固定为 ESP32-S3-WROOM-1-N16R8 + GDEM075T41WT 7.5" 800×480 黑白屏，
# 不再有机型/屏幕选择宏；配置见 sdkconfig（16MB Flash + Octal PSRAM）。

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(frame_film_info)
