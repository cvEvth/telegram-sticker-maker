#!/usr/bin/env bash
# Usage: scripts/package-macos.sh [--skip-build]
#
# Builds TelegramStickerMaker.app (unless --skip-build) and wraps it in a .dmg that
# contains the app plus an /Applications shortcut. The app is ad-hoc code signed;
# it is not notarized (no Developer ID certificate here).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SKIP_BUILD=0
[ "${1:-}" = "--skip-build" ] && SKIP_BUILD=1

# ---- version: TSM_VERSION (CI tag), else the default in CMakeLists.txt
if [ -n "${TSM_VERSION:-}" ]; then
    VERSION="$TSM_VERSION"
else
    VERSION="$(sed -n 's/^[[:space:]]*set(TSM_VERSION \([0-9][0-9.]*\))$/\1/p' "$ROOT/CMakeLists.txt" | head -1)"
fi
if ! [[ "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo "invalid version '$VERSION' (expected x.y.z)" >&2
    exit 2
fi
echo "[package] version $VERSION"

if [ "$SKIP_BUILD" = 0 ]; then
    "$ROOT/scripts/build-macos.sh" release
fi

APP="$ROOT/build/macos/bin/TelegramStickerMaker.app"
[ -d "$APP" ] || { echo "app bundle not found: $APP" >&2; exit 1; }

# App is arm64-only (Qt framework and the bundled FFmpeg are arm64 as well)
ARCH="$(lipo -archs "$APP/Contents/MacOS/TelegramStickerMaker" | awk '{print $1}')"
case "$ARCH" in
    arm64) TAG=arm64 ;;
    x86_64) TAG=x86_64 ;;
    *) TAG=universal ;;
esac
NAME="TelegramStickerMaker-$VERSION-macos-$TAG"
DIST="$ROOT/dist"
STAGE="$DIST/$NAME"
rm -rf "$STAGE"
mkdir -p "$STAGE"

cp -R "$APP" "$STAGE/"
ln -s /Applications "$STAGE/Applications"

# 本地没有开发者证书，做 ad-hoc 签名：Apple Silicon 上未签名/签名损坏的 app 无法启动
codesign --force --deep --sign - "$STAGE/TelegramStickerMaker.app"
codesign --verify --deep --strict "$STAGE/TelegramStickerMaker.app"

# 保险：确保 ffmpeg / ffprobe 可执行且带签名
for bin in ffmpeg ffprobe; do
    [ -x "$STAGE/TelegramStickerMaker.app/Contents/MacOS/$bin" ] || {
        echo "missing $bin in app bundle" >&2
        exit 1
    }
done

DMG="$DIST/$NAME.dmg"
rm -f "$DMG"
hdiutil create -volname "Telegram Sticker Maker $VERSION" \
    -srcfolder "$STAGE" -ov -format UDZO -fs HFS+ "$DMG" >/dev/null

rm -rf "$STAGE"
echo "[package] $DMG"
echo "[package] sha256 $(shasum -a 256 "$DMG" | awk '{print $1}')"
