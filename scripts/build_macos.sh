#!/usr/bin/env zsh
# macOS用ビルドスクリプト (zsh)
set -euo pipefail

BUILD_TYPE="${1:-Release}"
BUILD_DIR="build"
NCPU=$(sysctl -n hw.ncpu)

echo "=== BubiC-8801MA macOS Build ==="
echo "  Build type : ${BUILD_TYPE}"
echo "  Parallelism: ${NCPU}"
echo ""

# Configure (SDLは常にFetchContentで取得するため追加引数なし)
cmake -S . -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}"

cmake --build "${BUILD_DIR}" --config "${BUILD_TYPE}" -j "${NCPU}"

echo ""
echo "=== Build complete ==="
echo "  App: ${BUILD_DIR}/BubiC-8801MA.app"