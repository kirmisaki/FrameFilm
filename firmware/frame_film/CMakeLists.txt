# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

# 机型选择：std / pro / max。留空时使用 sys_cfg.h 内的手改默认值（Pro）。
# CMakePresets.json 与 build.ps1 会显式传入 std / pro / max。
set(FRAMEFILM_MODEL "" CACHE STRING "FrameFilm 机型：std / pro / max；留空则用 sys_cfg.h 的默认值")
if(FRAMEFILM_MODEL)
    string(TOLOWER "${FRAMEFILM_MODEL}" _FRAMEFILM_MODEL)
    if(NOT _FRAMEFILM_MODEL MATCHES "^std$|^pro$|^max$")
        message(FATAL_ERROR "FRAMEFILM_MODEL 必须是 std / pro / max，当前为 '${FRAMEFILM_MODEL}'")
    endif()
    string(TOUPPER "${_FRAMEFILM_MODEL}" _FRAMEFILM_MODEL_UPPER)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DFRAMEFILM_MODEL_${_FRAMEFILM_MODEL_UPPER}=1")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DFRAMEFILM_MODEL_${_FRAMEFILM_MODEL_UPPER}=1")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(frame_film)
