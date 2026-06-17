#!/usr/bin/env python3
"""Build a multi-resolution macOS .icns from a single square PNG.

Produces the same icon-type layout that Apple's `iconutil -c icns` emits
(ic07-ic14 plus icp4/icp5), so macOS picks a crisp icon at every size and on
Retina displays. Needs ImageMagick (`magick`) on PATH for the resizing only;
the resulting .icns is committed to the repo so normal builds need no tools.

Usage:
    python3 make_icns.py <source.png> <out.icns>
    python3 make_icns.py                 # defaults: icons/EmeraldClean.png -> resources/emerald.icns
"""
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

# (OSType, pixel size) — mirrors an iconutil .iconset. Duplicate sizes are
# intentional: e.g. 32px is both icp5 (32x32@1x) and ic11 (16x16@2x).
ENTRIES = [
    (b"icp4", 16),   # 16x16
    (b"ic11", 32),   # 16x16@2x
    (b"icp5", 32),   # 32x32
    (b"ic12", 64),   # 32x32@2x
    (b"ic07", 128),  # 128x128
    (b"ic13", 256),  # 128x128@2x
    (b"ic08", 256),  # 256x256
    (b"ic14", 512),  # 256x256@2x
    (b"ic09", 512),  # 512x512
    (b"ic10", 1024), # 512x512@2x (1024x1024)
]


def main() -> int:
    args = sys.argv[1:]
    root = Path(__file__).resolve().parents[2]
    src = Path(args[0]) if len(args) > 0 else root / "icons" / "EmeraldClean.png"
    out = Path(args[1]) if len(args) > 1 else root / "resources" / "emerald.icns"

    if not src.exists():
        print(f"source not found: {src}", file=sys.stderr)
        return 1

    chunks = bytearray()
    with tempfile.TemporaryDirectory() as td:
        cache: dict[int, bytes] = {}
        for ostype, size in ENTRIES:
            if size not in cache:
                png = Path(td) / f"{size}.png"
                subprocess.run(
                    ["magick", str(src), "-resize", f"{size}x{size}", str(png)],
                    check=True,
                )
                cache[size] = png.read_bytes()
            data = cache[size]
            chunks += ostype + struct.pack(">I", len(data) + 8) + data

    out.parent.mkdir(parents=True, exist_ok=True)
    body = bytes(chunks)
    out.write_bytes(b"icns" + struct.pack(">I", len(body) + 8) + body)
    print(f"wrote {out} ({len(body) + 8} bytes, {len(ENTRIES)} entries)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
