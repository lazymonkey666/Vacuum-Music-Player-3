#!/bin/bash
# archive.sh —— 归档项目（递归包含所有文件，保留 .git / .vscode）
# 排除编译产物、IDE 缓存目录

set -euo pipefail

# 切到脚本所在目录（即项目根目录）
cd "$(dirname "$0")"

TIMESTAMP=$(date +%Y%m%d_%H%M%S)
ARCHIVE_NAME="Vacuum_Music_Player_${TIMESTAMP}.zip"

# ============ 需要排除的目录 / 文件（编译产物 + IDE 缓存）============
EXCLUDES=(
    # 编译输出目录
    "build-release/*"   "build-release"
    "out/*"             "out"
    "x64/*"             "x64"
    "build/*"           "build"
    "Debug/*"           "Debug"
    "Release/*"         "Release"
    "cmake-build-*/*"   "cmake-build-*"

    # Visual Studio / IDE
    ".vs/*"             ".vs"
    ".idea/*"           ".idea"

    # 其它缓存 / 中间目录
    ".cache/*"          ".cache"
    "CMakeFiles/*"      "CMakeFiles"
    "CMakeCache.txt"
    "*.user"

    # 常见编译中间产物 / 可执行文件
    "*.o"    "*.obj"    "*.a"    "*.lib"
    "*.so"   "*.so.*"   "*.dylib"
    "*.dll"  "*.exe"    "*.pdb"  "*.ilk"  "*.exp"
    "*.lo"   "*.la"     "*.d"    "*.dep"

    # 打包出来的归档自身
    "$ARCHIVE_NAME"
)

echo ">>> 正在递归打包所有文件（.git / .vscode 会保留）..."
zip -r -q "$ARCHIVE_NAME" . -x "${EXCLUDES[@]}"

echo ">>> 完成：$(pwd)/$ARCHIVE_NAME"
echo ">>> 大小：$(du -h "$ARCHIVE_NAME" | cut -f1)"