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

# --- Paths ---
set(PROJ_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(PROJ_APP  ${PROJ_ROOT}/Vacuum_Music_Player)

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
    ${PROJ_APP}/platform/platform_linux.cpp
)

# --- ImGui ---
set(IMGUI_SOURCES
    ${PROJ_ROOT}/imgui/imgui.cpp
    ${PROJ_ROOT}/imgui/imgui_draw.cpp
    ${PROJ_ROOT}/imgui/imgui_tables.cpp
    ${PROJ_ROOT}/imgui/imgui_widgets.cpp
    ${PROJ_ROOT}/imgui/backends/imgui_impl_glfw.cpp
    ${PROJ_ROOT}/imgui/backends/imgui_impl_opengl2.cpp
)

# --- ixwebsocket (auto-collect, exclude platform-specific) ---
file(GLOB IXWS_SOURCES ${PROJ_ROOT}/ixwebsocket/IX*.cpp)
list(FILTER IXWS_SOURCES EXCLUDE REGEX "IXSocketAppleSSL\\.cpp$")
list(FILTER IXWS_SOURCES EXCLUDE REGEX "IXSocketMbedTLS\\.cpp$")
list(FILTER IXWS_SOURCES EXCLUDE REGEX "IXSelectInterruptEvent\\.cpp$")

set(RESOURCES_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../Vacuum_Music_Player/resources)
set(GENERATED_CPP ${CMAKE_CURRENT_BINARY_DIR}/embedded_resources.cpp)

# --- Embedded resources ---
add_custom_command(
    OUTPUT  ${GENERATED_CPP}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/../tools/embed_resources.py
            ${RESOURCES_DIR} ${GENERATED_CPP}
    DEPENDS ${RESOURCES_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/../tools/embed_resources.py
    COMMENT "Embedding SVG resources..."
    VERBATIM
)

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
    ${PROJ_ROOT}/imgui
    ${PROJ_ROOT}/imgui/backends
    ${PROJ_ROOT}/ixwebsocket
    ${PROJ_ROOT}/nlohmann
    /usr/local/include/noson
)

# --- Compile definitions ---
target_compile_definitions(Vacuum_Music_Player_CMAKE PRIVATE
    PLATFORM_LINUX=1
    IXWEBSOCKET_USE_OPEN_SSL
)

# --- Link libraries ---
target_link_libraries(Vacuum_Music_Player_CMAKE PRIVATE
    PkgConfig::TAGLIB
    PkgConfig::GLFW3
    PkgConfig::NOSON
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

# --- CPack / .deb (Release + BUILD_DEB=ON only) ---
if(BUILD_DEB)
    if(NOT CMAKE_BUILD_TYPE STREQUAL "Release")
        message(FATAL_ERROR
            "BUILD_DEB=ON requires CMAKE_BUILD_TYPE=Release, "
            "current is '${CMAKE_BUILD_TYPE}'.\n"
            "Usage: cmake -S . -B <build> -DCMAKE_BUILD_TYPE=Release -DBUILD_DEB=ON")
    endif()

    message(STATUS "DEB packaging enabled")

    # Install rules
    install(TARGETS Vacuum_Music_Player_CMAKE
            RUNTIME DESTINATION bin)

    # CPack metadata
    set(CPACK_GENERATOR "DEB")
    set(CPACK_PACKAGE_NAME "vacuum-music-player")
    set(CPACK_PACKAGE_VERSION "1.0.0")
    set(CPACK_PACKAGE_CONTACT "lazymonkey666")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
        "Vacuum Music Player - a music player for Raspberry Pi / Linux")
    set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "arm64")
    set(CPACK_DEBIAN_PACKAGE_SECTION "sound")
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