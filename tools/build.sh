#!/usr/bin/env bash
# build.sh - One-click build for Vacuum Music Player
# Location: tools/build.sh

set -euo pipefail

# --- Config ---
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

BUILD_RELEASE_DIR="${PROJECT_ROOT}/build-release"
BUILD_DEB_DIR="${PROJECT_ROOT}/build-deb"
JOBS="$(nproc)"

# --- Colors ---
RED=$'\033[0;31m'
GREEN=$'\033[0;32m'
YELLOW=$'\033[1;33m'
BLUE=$'\033[0;34m'
NC=$'\033[0m'

log()  { echo "${BLUE}==>${NC} $*"; }
ok()   { echo "${GREEN}==>${NC} $*"; }
warn() { echo "${YELLOW}==>${NC} $*"; }
err()  { echo "${RED}==>${NC} $*" >&2; }

# --- Version & Desktop Processing ---
process_version_and_desktop() {
    log "正在处理版本号与桌面快捷方式..."
    
        # 1. 读取 ver.config
    local ver_file="${PROJECT_ROOT}/src/ver.config"
    local v_major=3 v_month=0 v_build=0
    
    if [ -f "${ver_file}" ]; then
        # 先清掉可能存在的 BOM 乱码
        local clean_ver=$(sed 's/^\xEF\xBB\xBF//' "${ver_file}")
        
        v_major=$(echo "$clean_ver" | grep -oP '^MAJOR=\K[0-9]+' || echo 3)
        v_month=$(echo "$clean_ver" | grep -oP '^LAST_MONTH=\K[0-9]+' || echo 10)
        v_build=$(echo "$clean_ver" | grep -oP '^BUILD_COUNT=\K[0-9]+' || echo 7)
        
        v_build=$((v_build - 1))
        if [ "$v_build" -lt 0 ]; then
            v_build=0
        fi
    fi
    # 2. 获取当前年份后两位
    local v_year=$(date +%y)
    export APP_VERSION_STRING="${v_major}.${v_year}.${v_month}.${v_build}"
    log "最终构建版本号: ${APP_VERSION_STRING}"

    # 3. 自动生成 .desktop 文件到 src 目录
    local desktop_file="${PROJECT_ROOT}/src/vacuum_music_player.desktop"
    cat > "${desktop_file}" << EOF
[Desktop Entry]
Type=Application
Name=Vacuum Music Player
Comment=A modern music player for Linux and Windows
Exec=Vacuum_Music_Player_CMAKE
Icon=vacuum_music_player
Terminal=false
Categories=AudioVideo;Audio;Player;
EOF
    ok "已生成 Desktop 快捷方式: src/vacuum_music_player.desktop"
}

# --- Sanity checks ---
check_env() {
    command -v cmake >/dev/null 2>&1 || { err "cmake not found"; exit 1; }
    command -v cpack >/dev/null 2>&1 || { err "cpack not found"; exit 1; }
    [ -f "${PROJECT_ROOT}/CMakeLists.txt" ] || { err "CMakeLists.txt not found at ${PROJECT_ROOT}"; exit 1; }
}

# --- Release build ---
build_release() {
    log "Building Release into ${BUILD_RELEASE_DIR}"
    rm -rf "${BUILD_RELEASE_DIR}" # 清理旧缓存
    
    process_version_and_desktop

    cmake -S "${PROJECT_ROOT}" -B "${BUILD_RELEASE_DIR}" \
        -DCMAKE_BUILD_TYPE=Release \
        -DAPP_VERSION_STRING="${APP_VERSION_STRING}"
    cmake --build "${BUILD_RELEASE_DIR}" -j"${JOBS}"
    ok "Binary: ${BUILD_RELEASE_DIR}/bin/Vacuum_Music_Player_CMAKE"
}

# --- Deb build ---
build_deb() {
    log "Building .deb into ${BUILD_DEB_DIR}"
    rm -rf "${BUILD_DEB_DIR}" # 强制清理旧缓存
    
    process_version_and_desktop

    cmake -S "${PROJECT_ROOT}" -B "${BUILD_DEB_DIR}" \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_DEB=ON \
        -DAPP_VERSION_STRING="${APP_VERSION_STRING}"
    cmake --build "${BUILD_DEB_DIR}" -j"${JOBS}" --target deb

    local deb
    deb="$(find "${BUILD_DEB_DIR}" -maxdepth 1 -name '*.deb' | head -n1)"
    if [ -n "${deb}" ]; then
        ok "Package: ${deb}"
        echo
        echo "Install with:"
        echo "  sudo dpkg -i ${deb}"
        echo "  sudo apt-get install -f   # fix missing deps"
    else
        err "No .deb produced"
        exit 1
    fi
}

# --- Clean ---
clean_all() {
    warn "Removing build dirs..."
    rm -rf "${BUILD_RELEASE_DIR}" "${BUILD_DEB_DIR}"
    ok "Cleaned"
}

# --- Menu ---
show_menu() {
    echo
    echo "================================"
    echo " Vacuum Music Player - Build"
    echo "================================"
    echo "  1) Build Release"
    echo "  2) Build .deb package"
    echo "  3) Clean build dirs"
    echo "  0) Exit"
    echo "================================"
    read -rp "Select [0-3]: " choice
    case "${choice}" in
        1) build_release ;;
        2) build_deb ;;
        3) clean_all ;;
        0) exit 0 ;;
        *) err "Invalid choice"; exit 1 ;;
    esac
}

# --- Entry ---
check_env

case "${1:-}" in
    release|rel|r) build_release ;;
    deb|d)         build_deb ;;
    clean|c)       clean_all ;;
    ""|menu)       show_menu ;;
    -h|--help)
        echo "Usage: $0 [release|deb|clean]"
        exit 0
        ;;
    *)
        err "Unknown command: $1"
        exit 1
        ;;
esac