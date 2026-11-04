cmake_minimum_required(VERSION 3.16)

# The library directory itself is a valid IDF component: point
# EXTRA_COMPONENT_DIRS at it directly (IDF accepts a listed directory that
# contains CMakeLists.txt as a component and does not scan its subfolders).
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../..")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(st25r_picc_m1)
