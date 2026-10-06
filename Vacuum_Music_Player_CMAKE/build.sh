#!/usr/bin/env bash
# build.sh - One-click build for Vacuum Music Player
# Usage:
#   ./build.sh           # interactive menu
#   ./build.sh release   # build Release binary
#   ./build.sh deb       # build Release + .deb package
#   ./build.sh clean     # remove build dirs

set -euo pipefail

# --- Config ---
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_RELEASE_DIR="${SCRIPT_DIR}/build-release"
BUILD_DEB_DIR="${SCRIPT_DIR}/build-deb"
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

# --- Sanity checks ---
check_env() {
    command -v cmake >/dev/null 2>&1 || { err "cmake not found"; exit 1; }
    command -v cpack >/dev/null 2>&1 || { err "cpack not found"; exit 1; }
    [ -f "${SCRIPT_DIR}/CMakeLists.txt" ] || { err "CMakeLists.txt not found"; exit 1; }
}

# --- Release build ---
build_release() {
    log "Building Release into ${BUILD_RELEASE_DIR}"
    cmake -S "${SCRIPT_DIR}" -B "${BUILD_RELEASE_DIR}" \
        -DCMAKE_BUILD_TYPE=Release
    cmake --build "${BUILD_RELEASE_DIR}" -j"${JOBS}"
    ok "Binary: ${BUILD_RELEASE_DIR}/Vacuum_Music_Player_CMAKE"
}

# --- Deb build ---
build_deb() {
    log "Building .deb into ${BUILD_DEB_DIR}"
    cmake -S "${SCRIPT_DIR}" -B "${BUILD_DEB_DIR}" \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_DEB=ON
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
        echo "  (no args)  interactive menu"
        exit 0
        ;;
    *)
        err "Unknown command: $1"
        echo "Try: $0 --help"
        exit 1
        ;;
esac