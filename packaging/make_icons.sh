#!/usr/bin/env bash
# Regenerate every committed icon asset from the commissioned art
# icons/NewDetailed.png. The art is first lifted onto a transparent square master
# (icons/EmeraldClean.png) by extract_subject.py, then that master is fanned out to
# the per-platform formats below. Run this whenever the art changes. Needs
# ImageMagick (`magick`), python3, numpy + Pillow; the generated assets are committed
# so ordinary builds need no image tooling.
#
# Usage:  packaging/make_icons.sh
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$ROOT/icons/EmeraldClean.png"

echo ">> Transparent square master from commissioned art"
python3 "$ROOT/packaging/extract_subject.py"

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
