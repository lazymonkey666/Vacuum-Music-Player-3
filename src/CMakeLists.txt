cmake_minimum_required(VERSION 3.16)

project(Vacuum_Music_Player_CMAKE LANGUAGES CXX) 

# --- Build type: default Debug ---
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
    message(STATUS "CMAKE_BUILD_TYPE not set, defaulting to Debug")
endif()

set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
    Debug Release RelWithDebInfo MinSizeRel)

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

# --- Packaging switch: OFF by default ---
option(BUILD_DEB "Generate a .deb package with CPack (requires Release)" OFF)

# --- Dependencies ---
find_package(PkgConfig REQUIRED)
find_package(OpenGL REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)
find_package(X11 REQUIRED)
find_package(CURL REQUIRED)

pkg_check_modules(TAGLIB REQUIRED IMPORTED_TARGET taglib)
pkg_check_modules(GLFW3  REQUIRED IMPORTED_TARGET glfw3)
pkg_check_modules(NOSON  REQUIRED IMPORTED_TARGET noson)
pkg_check_modules(DBUS REQUIRED IMPORTED_TARGET dbus-1)

# --- Paths ---
set(PROJ_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(PROJ_APP  ${PROJ_ROOT}/src)
set(PROJ_THIRD_PARTY ${PROJ_ROOT}/third-party)

# --- App sources ---
# NOTE: platform_linux.cpp is in platform/, not platform/linux/
set(APP_SOURCES
    ${PROJ_APP}/main.cpp
    ${PROJ_APP}/albumimgtagreader.cpp
    ${PROJ_APP}/manageconfig.cpp
    ${PROJ_APP}/SettingItem.cpp
    ${PROJ_APP}/AMLLWebSocketV2.cpp
    ${PROJ_APP}/SonosController.cpp
    ${PROJ_APP}/IconManager.cpp
    ${PROJ_APP}/NeteaseAPIManager.cpp
    ${PROJ_APP}/ThemePack.cpp
    ${PROJ_APP}/platform/platform_linux.cpp
    ${PROJ_APP}/platform/linux/LinuxThemeDetector.cpp)

# --- ImGui ---
set(IMGUI_SOURCES
    ${PROJ_THIRD_PARTY}/imgui/imgui.cpp
    ${PROJ_THIRD_PARTY}/imgui/imgui_draw.cpp
    ${PROJ_THIRD_PARTY}/imgui/imgui_tables.cpp
    ${PROJ_THIRD_PARTY}/imgui/imgui_widgets.cpp
    ${PROJ_THIRD_PARTY}/imgui/backends/imgui_impl_glfw.cpp
    ${PROJ_THIRD_PARTY}/imgui/backends/imgui_impl_opengl2.cpp
)

# --- ixwebsocket (auto-collect, exclude platform-specific) ---
file(GLOB IXWS_SOURCES ${PROJ_THIRD_PARTY}/ixwebsocket/IX*.cpp)
list(FILTER IXWS_SOURCES EXCLUDE REGEX "IXSocketAppleSSL\\.cpp$")
list(FILTER IXWS_SOURCES EXCLUDE REGEX "IXSocketMbedTLS\\.cpp$")
list(FILTER IXWS_SOURCES EXCLUDE REGEX "IXSelectInterruptEvent\\.cpp$")

set(RESOURCES_DIR ${PROJ_ROOT}/src/resources)
set(GENERATED_CPP ${CMAKE_CURRENT_BINARY_DIR}/embedded_resources.cpp)

# --- Embedded resources ---
add_custom_command(
    OUTPUT  ${GENERATED_CPP}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}
    COMMAND python3 ${PROJ_ROOT}/tools/embed_resources.py
            ${RESOURCES_DIR} ${GENERATED_CPP}
    DEPENDS ${RESOURCES_DIR} ${PROJ_ROOT}/tools/embed_resources.py
    COMMENT "Embedding SVG resources..."
    VERBATIM
)

add_compile_options(-w)

# --- Target ---
add_executable(Vacuum_Music_Player_CMAKE
    ${APP_SOURCES} ${IMGUI_SOURCES} ${IXWS_SOURCES} ${GENERATED_CPP})

set_property(TARGET Vacuum_Music_Player_CMAKE PROPERTY CXX_STANDARD 20)
set_property(TARGET Vacuum_Music_Player_CMAKE PROPERTY CXX_STANDARD_REQUIRED ON)

# --- Include paths ---

target_include_directories(Vacuum_Music_Player_CMAKE PRIVATE
    ${PROJ_ROOT}
    ${PROJ_APP}
    ${PROJ_APP}/platform
    ${PROJ_THIRD_PARTY}
    ${PROJ_THIRD_PARTY}/imgui
    ${PROJ_THIRD_PARTY}/imgui/backends
    ${PROJ_THIRD_PARTY}/ixwebsocket
    ${PROJ_THIRD_PARTY}/nlohmann
    /usr/local/include/noson
)

# --- Compile definitions ---
target_compile_definitions(Vacuum_Music_Player_CMAKE PRIVATE
    PLATFORM_LINUX=1
    IXWEBSOCKET_USE_OPEN_SSL
)

# --- Version config ---

# 如果外部（比如 build.sh）没有传入 APP_VERSION_STRING
if(NOT DEFINED APP_VERSION_STRING)
    set(VER_CONFIG_FILE "${CMAKE_CURRENT_SOURCE_DIR}/ver.config")
    set(VERSION_MAJOR "0")
    set(VERSION_MONTH "0")
    set(VERSION_BUILD "0")

    if(EXISTS "${VER_CONFIG_FILE}")
        file(STRINGS "${VER_CONFIG_FILE}" VER_LINES)
        foreach(LINE IN LISTS VER_LINES)
            if(LINE MATCHES "^MAJOR=([0-9]+)")
                set(VERSION_MAJOR ${CMAKE_MATCH_1})
            elseif(LINE MATCHES "^LAST_MONTH=([0-9]+)")
                set(VERSION_MONTH ${CMAKE_MATCH_1})
            elseif(LINE MATCHES "^BUILD_COUNT=([0-9]+)")
                set(VERSION_BUILD ${CMAKE_MATCH_1})
            endif()
        endforeach()
    endif()

    # ★ 核心修复：无论是不是通过 build.sh 编译，只要是 Linux 环境，小版本号统统减 1
    if(VERSION_BUILD GREATER 0)
        math(EXPR VERSION_BUILD "${VERSION_BUILD} - 1")
    endif()

    # 自动获取编译时的年份（后两位）
    string(TIMESTAMP VERSION_YEAR "%y")
    
    # 拼接版本号
    set(APP_VERSION_STRING "${VERSION_MAJOR}.${VERSION_YEAR}.${VERSION_MONTH}.${VERSION_BUILD}")
endif()

message(STATUS "最终构建版本号: ${APP_VERSION_STRING}")

# 注入到代码宏中
target_compile_definitions(Vacuum_Music_Player_CMAKE PRIVATE
    APP_VERSION_STRING="${APP_VERSION_STRING}"
)

# --- Link libraries ---
target_link_libraries(Vacuum_Music_Player_CMAKE PRIVATE
    PkgConfig::TAGLIB
    PkgConfig::GLFW3
    PkgConfig::NOSON
    PkgConfig::DBUS
    OpenGL::GL
    Threads::Threads
    dl
    asound
    OpenSSL::SSL
    OpenSSL::Crypto
    X11::X11
    z
    CURL::libcurl
)

target_compile_options(Vacuum_Music_Player_CMAKE PRIVATE -Wall -pthread)
target_link_options(Vacuum_Music_Player_CMAKE PRIVATE
    -pthread
    -Wl,--no-as-needed
)

if(BUILD_DEB)
    if(NOT CMAKE_BUILD_TYPE STREQUAL "Release")
        message(FATAL_ERROR
            "BUILD_DEB=ON requires CMAKE_BUILD_TYPE=Release, "
            "current is '${CMAKE_BUILD_TYPE}'.\n"
            "Usage: cmake -S . -B <build> -DCMAKE_BUILD_TYPE=Release -DBUILD_DEB=ON")
    endif()

    message(STATUS "DEB packaging enabled")

    # 如果传递了版本号宏，使用它；否则兜底
    if(NOT DEFINED APP_VERSION_STRING)
        set(APP_VERSION_STRING "1.0.0")
    endif()

    # 将版本号写入编译宏
    target_compile_definitions(Vacuum_Music_Player_CMAKE PRIVATE
        APP_VERSION_STRING="${APP_VERSION_STRING}"
    )

    # 1. 安装可执行文件（只写这一次即可）
    install(TARGETS Vacuum_Music_Player_CMAKE RUNTIME DESTINATION bin)

    # 2. 安装 desktop 快捷方式
    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/vacuum_music_player.desktop
            DESTINATION share/applications)

    # 3. 安装图标文件
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/vacuum_music_player.png")
        install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/vacuum_music_player.png
                DESTINATION share/icons/hicolor/256x256/apps)
    endif()

    # --- CPack metadata ---
    set(CPACK_GENERATOR "DEB")
    set(CPACK_PACKAGE_NAME "vacuum-music-player")
    
    # ★ 修复 1：使用动态传进来的版本号
    set(CPACK_PACKAGE_VERSION "${APP_VERSION_STRING}") 
    
    set(CPACK_PACKAGE_CONTACT "lazymonkey666")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Vacuum Music Player - a simple music player")
    
    # ★ 修复 2：自动识别当前系统的架构（在 WSL 下会是 amd64，在树莓派上会是 arm64）
    execute_process(COMMAND dpkg --print-architecture OUTPUT_VARIABLE DEB_ARCH OUTPUT_STRIP_TRAILING_WHITESPACE)
    set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "${DEB_ARCH}") 
    
    set(CPACK_DEBIAN_PACKAGE_SECTION "video") # 让它尽量归类到多媒体
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)

    include(CPack)

    # Convenience target: cmake --build <dir> --target deb
    add_custom_target(deb
        COMMAND ${CMAKE_CPACK_COMMAND} -G DEB
                --config ${CMAKE_BINARY_DIR}/CPackConfig.cmake
        DEPENDS Vacuum_Music_Player_CMAKE 
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Building .deb package"
        VERBATIM
    )
endif()