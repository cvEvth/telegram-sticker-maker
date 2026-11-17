#!/usr/bin/env bash
# Usage: scripts/build-macos.sh [dev|release] [test]
#
# Builds TelegramStickerMaker.app for macOS.
# Required environment:
#   QT_ROOT          Qt 6.8 macOS kit, e.g. ~/Qt/6.8.3/macos
#   TSM_DEPS_PREFIX  prefix containing WebPConfig.cmake and libzip-config.cmake
#                    (vcpkg: $VCPKG_ROOT/installed/arm64-osx, or a local install)
# Optional:
#   FFMPEG_ROOT      directory with bin/ffmpeg and bin/ffprobe (must support libvpx-vp9);
#                    defaults to ffmpeg found on PATH
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MODE="${1:-release}"
WITH_TESTS="${2:-}"

case "$MODE" in
    dev) BUILD_TYPE=RelWithDebInfo ;;
    release) BUILD_TYPE=Release ;;
    *) echo "unknown mode '$MODE' (expected dev or release)" >&2; exit 2 ;;
esac

: "${QT_ROOT:?set QT_ROOT to the Qt 6.8 macOS kit (e.g. ~/Qt/6.8.3/macos)}"
: "${TSM_DEPS_PREFIX:?set TSM_DEPS_PREFIX to the prefix with WebPConfig.cmake and libzip-config.cmake}"

PREFIX_PATH="$QT_ROOT;$TSM_DEPS_PREFIX"
[ -n "${CMAKE_PREFIX_PATH:-}" ] && PREFIX_PATH="$PREFIX_PATH;$CMAKE_PREFIX_PATH"

ARGS=(
    -S "$ROOT" -B "$ROOT/build/macos" -G Ninja
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
    -DCMAKE_PREFIX_PATH="$PREFIX_PATH"
    -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0
)
[ -n "${FFMPEG_ROOT:-}" ] && ARGS+=(-DFFMPEG_ROOT="$FFMPEG_ROOT")

cmake "${ARGS[@]}"
cmake --build "$ROOT/build/macos"

if [ "$WITH_TESTS" = "test" ]; then
    ctest --test-dir "$ROOT/build/macos" --output-on-failure
fi
