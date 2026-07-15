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

# Immutable releases and SHA-256 digests for every architecture built in CI.
# Update the release and its digest together; fetch() verifies both freshly
# downloaded and cached tools before they are ever executed.
LINUXDEPLOY_VERSION="1-alpha-20251107-1"
QT_PLUGIN_VERSION="1-alpha-20250213-1"
case "$ARCH" in
  x86_64)
    LINUXDEPLOY_SHA256="c20cd71e3a4e3b80c3483cef793cda3f4e990aca14014d23c544ca3ce1270b4d"
    QT_PLUGIN_SHA256="15106be885c1c48a021198e7e1e9a48ce9d02a86dd0a1848f00bdbf3c1c92724"
    ;;
  aarch64)
    LINUXDEPLOY_SHA256="620095110d693282b8ebeb244a95b5e911cf8f65f76c88b4b47d16ae6346fcff"
    QT_PLUGIN_SHA256="bf1c24aff6d749b5cf423afad6f15abd4440f81dec1aab95706b25f6667cdcf1"
    ;;
  *)
    echo "unsupported AppImage architecture: $ARCH" >&2
    exit 1
    ;;
esac

# Run bundled AppImage tools without needing FUSE (works in containers/CI).
export APPIMAGE_EXTRACT_AND_RUN=1
export ARCH

verify_sha256() { # file expected
  printf '%s  %s\n' "$2" "$1" | sha256sum --check --status
}

fetch() { # url dest sha256
  if [ -f "$2" ] && verify_sha256 "$2" "$3"; then
    chmod +x "$2"
    return
  fi

  local tmp="${2}.download"
  rm -f "$2" "$tmp"
  echo ">> fetching $(basename "$2")"
  curl -fL --retry 3 -o "$tmp" "$1"
  if ! verify_sha256 "$tmp" "$3"; then
    rm -f "$tmp"
    echo "SHA-256 verification failed for $(basename "$2")" >&2
    exit 1
  fi
  chmod +x "$tmp"
  mv "$tmp" "$2"
}

mkdir -p "$TOOLS_DIR"
fetch "https://github.com/linuxdeploy/linuxdeploy/releases/download/${LINUXDEPLOY_VERSION}/linuxdeploy-${ARCH}.AppImage" \
      "$TOOLS_DIR/linuxdeploy-${ARCH}.AppImage" "$LINUXDEPLOY_SHA256"
fetch "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/${QT_PLUGIN_VERSION}/linuxdeploy-plugin-qt-${ARCH}.AppImage" \
      "$TOOLS_DIR/linuxdeploy-plugin-qt-${ARCH}.AppImage" "$QT_PLUGIN_SHA256"

echo ">> configuring + building (Release)"
cmake -S "$ROOT" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD_DIR" -j"$(nproc)"

echo ">> installing into AppDir"
rm -rf "$APPDIR"
DESTDIR="$APPDIR" cmake --install "$BUILD_DIR" --prefix /usr

# VERSION is embedded in the AppImage's metadata; the filename stays version-less
# so README can link to /releases/latest/download/Emerald-<arch>.AppImage forever.
VERSION="$(sed -n 's/^project(Emerald VERSION \([0-9.]*\).*/\1/p' "$ROOT/CMakeLists.txt")"
export VERSION
export OUTPUT="Emerald-${ARCH}.AppImage"

echo ">> bundling Qt + generating $OUTPUT"
"$TOOLS_DIR/linuxdeploy-${ARCH}.AppImage" \
  --appdir "$APPDIR" \
  --plugin qt \
  --desktop-file "$APPDIR/usr/share/applications/emerald.desktop" \
  --icon-file "$APPDIR/usr/share/icons/hicolor/256x256/apps/emerald.png" \
  --output appimage

echo ">> done: $ROOT/$OUTPUT"
