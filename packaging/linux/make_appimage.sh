#!/usr/bin/env bash
# Build a portable Emerald AppImage with linuxdeploy + its Qt plugin.
#
# Produces a single self-contained Emerald-<version>-x86_64.AppImage that runs
# on most Linux distros without installing Qt. The linuxdeploy tools are fetched
# on first run into a cache dir (override with TOOLS_DIR).
#
# Usage:  packaging/linux/make_appimage.sh [build-dir]
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${1:-$ROOT/build-appimage}"
APPDIR="$BUILD_DIR/AppDir"
TOOLS_DIR="${TOOLS_DIR:-$ROOT/build-appimage/.tools}"
ARCH="$(uname -m)"

# Run bundled AppImage tools without needing FUSE (works in containers/CI).
export APPIMAGE_EXTRACT_AND_RUN=1
export ARCH

fetch() { # url dest
  if [ ! -x "$2" ]; then
    echo ">> fetching $(basename "$2")"
    curl -fL --retry 3 -o "$2" "$1"
    chmod +x "$2"
  fi
}

mkdir -p "$TOOLS_DIR"
fetch "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-${ARCH}.AppImage" \
      "$TOOLS_DIR/linuxdeploy-${ARCH}.AppImage"
fetch "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-${ARCH}.AppImage" \
      "$TOOLS_DIR/linuxdeploy-plugin-qt-${ARCH}.AppImage"

echo ">> configuring + building (Release)"
cmake -S "$ROOT" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD_DIR" -j"$(nproc)"

echo ">> installing into AppDir"
rm -rf "$APPDIR"
DESTDIR="$APPDIR" cmake --install "$BUILD_DIR" --prefix /usr

VERSION="$(sed -n 's/^project(Emerald VERSION \([0-9.]*\).*/\1/p' "$ROOT/CMakeLists.txt")"
export VERSION
export OUTPUT="Emerald-${VERSION}-${ARCH}.AppImage"

echo ">> bundling Qt + generating $OUTPUT"
"$TOOLS_DIR/linuxdeploy-${ARCH}.AppImage" \
  --appdir "$APPDIR" \
  --plugin qt \
  --desktop-file "$APPDIR/usr/share/applications/emerald.desktop" \
  --icon-file "$APPDIR/usr/share/icons/hicolor/256x256/apps/emerald.png" \
  --output appimage

echo ">> done: $ROOT/$OUTPUT"
