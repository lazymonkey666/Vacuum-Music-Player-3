cmake_minimum_required(VERSION 3.16)

# ═══════════════════════════════════════════════════════════
# 平台路由
#
#   Windows       → 使用 Vacuum_Music_Player.sln（Visual Studio vcxproj）
#   Linux / macOS → 使用 CMake（即本文件）
# ═══════════════════════════════════════════════════════════
if(WIN32)
    message(FATAL_ERROR
        "检测到 Windows 平台。\n"
        "Windows 构建请使用 Vacuum_Music_Player.sln（vcxproj），\n"
        "不要走 CMake。此 CMakeLists.txt 仅用于 Linux / macOS。")
endif()

# ─── 项目基础设置（供子目录继承）───
project(Vacuum_Music_Player LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 可执行文件统一输出到 build/bin/
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# ─── 引入业务子项目 ───
add_subdirectory(Vacuum_Music_Player_CMAKE)