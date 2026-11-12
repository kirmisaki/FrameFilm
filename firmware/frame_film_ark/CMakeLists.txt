# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

set(PROJECT_VER "3.2.6")

# 第三方库统一放在 components/film_libs/ 下。IDF 只扫描 components/ 的一级子目录，
# 因此把 film_libs 注册为额外组件目录（其下每个子目录仍是一个独立组件）。
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/components/film_libs")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(frame_film_ark)
