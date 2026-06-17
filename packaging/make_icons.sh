#!/usr/bin/env bash
# Regenerate every committed raster icon from the master icons/EmeraldClean.png.
# Run this whenever the master changes. Needs ImageMagick (`magick`) + python3;
# the generated assets are committed so ordinary builds need no image tooling.
#
# Usage:  packaging/make_icons.sh
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$ROOT/icons/EmeraldClean.png"

echo ">> Windows multi-size .ico (16-256)"
magick "$SRC" -background none \
  -define icon:auto-resize=256,128,64,48,32,16 \
  "$ROOT/resources/emerald.ico"

echo ">> Linux hicolor PNGs (128/256/512)"
for size in 128 256 512; do
  magick "$SRC" -background none -resize "${size}x${size}" \
    "$ROOT/packaging/linux/icons/emerald-${size}.png"
done

echo ">> macOS .icns"
python3 "$ROOT/packaging/macos/make_icns.py"

echo ">> done"
