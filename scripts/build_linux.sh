#!/usr/bin/env bash
# build_linux.sh — BubiC-8801MA ローカルビルド (Linux)
# 使い方: bash scripts/build_linux.sh [Debug|Release] [x64|arm64]
set -euo pipefail

BUILD_TYPE="${1:-Release}"
TARGET_ARCH="${2:-native}"

# Detect native architecture if not specified
if [ "${TARGET_ARCH}" = "native" ]; then
    UNAME_ARCH="$(uname -m)"
    case "${UNAME_ARCH}" in
        x86_64) TARGET_ARCH="x64" ;;
        aarch64|arm64) TARGET_ARCH="arm64" ;;
        *)
            echo "[ERROR] Unsupported architecture: ${UNAME_ARCH}"
            exit 1
            ;;
    esac
fi
BUILD_DIR="build"
NCPU=$(nproc)

echo "=== BubiC-8801MA Linux Build ==="
echo "  Build type : ${BUILD_TYPE}"
echo "  Parallelism: ${NCPU}"
echo "  Target arch : ${TARGET_ARCH}"
echo ""

# 必要な開発パッケージを確認・案内
check_pkg() {
    if ! dpkg -s "$1" &>/dev/null 2>&1 && ! rpm -q "$1" &>/dev/null 2>&1; then
        echo "[WARN] Package '$1' may not be installed."
        return 1
    fi
    return 0
}

DEPS_APT=(
    ninja-build pkg-config
    libasound2-dev libpulse-dev libdbus-1-dev libudev-dev
    libx11-dev libxcursor-dev libxext-dev libxi-dev
    libxinerama-dev libxrandr-dev libxss-dev libxtst-dev
    libwayland-dev libxkbcommon-dev libdecor-0-dev
    libgbm-dev libdrm-dev
)

# apt が使える環境では一括インストールを提案
if command -v apt-get &>/dev/null; then
    echo "[INFO] Detected apt-based distro."
    echo "[INFO] If build fails due to missing deps, run:"
    echo "  sudo apt-get install -y ${DEPS_APT[*]}"
    echo ""
fi

#
# Configure (Linux: SDL3は常にFetchContentでビルド)
CMAKE_ARCH_ARGS=()

if [ "${TARGET_ARCH}" = "arm64" ]; then
    CMAKE_ARCH_ARGS+=("-DCMAKE_SYSTEM_PROCESSOR=aarch64")
elif [ "${TARGET_ARCH}" = "x64" ]; then
    CMAKE_ARCH_ARGS+=("-DCMAKE_SYSTEM_PROCESSOR=x86_64")
else
    echo "[ERROR] Invalid TARGET_ARCH: ${TARGET_ARCH}"
    exit 1
fi

cmake -S . -B "${BUILD_DIR}" \
    -G Ninja \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
    "${CMAKE_ARCH_ARGS[@]}"

cmake --build "${BUILD_DIR}" --config "${BUILD_TYPE}" -j "${NCPU}"

echo ""
echo "=== Build complete ==="
echo "  Binary: ${BUILD_DIR}/BubiC-8801MA"
