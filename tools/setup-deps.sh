#!/usr/bin/env bash
# setup_deps.sh - 一键安装 Vacuum Music Player 的编译依赖
# 适用于 Debian / Ubuntu / Linux Mint / 树莓派 OS

set -euo pipefail

# 颜色定义
GREEN=$'\033[0;32m'
RED=$'\033[0;31m'
BLUE=$'\033[0;34m'
NC=$'\033[0m'

log()  { echo "${BLUE}==>${NC} $*"; }
ok()   { echo "${GREEN}==>${NC} $*"; }
err()  { echo "${RED}==>${NC} $*" >&2; }

# 1. 检查是否为 root 权限
if [ "$EUID" -ne 0 ]; then
  err "请使用 root 权限运行此脚本 (sudo ./setup_deps.sh)"
  exit 1
fi

# 2. 检查系统是否支持 apt
if ! command -v apt-get >/dev/null 2>&1; then
    err "未检测到 apt-get，此脚本仅支持 Debian/Ubuntu 系系统。"
    exit 1
fi

log "当前系统架构: $(uname -m)"

# 3. 更新软件源
log "正在更新 apt 软件源..."
apt-get update

# 4. 安装基础编译工具和系统依赖
log "正在安装编译工具链及基础库..."
apt-get install -y \
    build-essential \
    cmake \
    ninja-build \
    pkg-config \
    git \
    python3 \
    wget \
    unzip \
    libgl1-mesa-dev \
    libssl-dev \
    libx11-dev \
    libcurl4-openssl-dev \
    libtag1-dev \
    libglfw3-dev \
    libasound2-dev \
    zlib1g-dev \
    libflac-dev \
    libpulse-dev \
    libjsoncpp-dev

ok "基础系统依赖安装完成！"

# 5. 编译安装 noson
# 因为大部分 Linux 发行版的 apt 源中没有 noson，所以需要从源码编译
NOSON_DIR="/tmp/noson-build"

if [ ! -d "${NOSON_DIR}" ]; then
    log "正在克隆 noson 源码..."
    git clone https://github.com/zjor/noson.git "${NOSON_DIR}"
fi

log "正在编译并安装 noson..."
cd "${NOSON_DIR}"
mkdir -p build && cd build
cmake ..
make -j"$(nproc)"
make install
ldconfig

ok "noson 安装完成！"

# 6. 完成提示
echo
echo "======================================================"
ok "所有依赖安装完毕！"
echo "======================================================"
echo "接下来你可以这样编译项目："
echo "  ./build.sh"
echo "======================================================"