#!/usr/bin/env python3
"""Lift the icon subject off its presentation canvas into a transparent master.

The commissioned art (icons/NewDetailed.png) renders the emerald-on-books subject
centred on a light "App Store" canvas (grey border + white rounded card + soft drop
shadow). For an app icon we want just the subject, free-floating on transparency and
filling the square frame. This script flood-fills the light, neutral background
(grey + white + shadow) from the image border to alpha, leaving the subject — then
trims, centres, and scales it to a square master. Interior near-white highlights on
the crystal survive because they are not connected to the border.

Needs numpy + Pillow (image tooling only); the resulting PNG is committed so ordinary
builds need none. Run via packaging/make_icons.sh, or directly:

    python3 extract_subject.py [src.png] [out.png] [--size 1024] [--fill 0.90]
"""
import sys
from collections import deque
from pathlib import Path

import numpy as np
from PIL import Image, ImageFilter


def lift(src: Path, out: Path, size: int, fill: float) -> None:
    arr = np.asarray(Image.open(src).convert("RGB")).astype(np.int16)
    h, w, _ = arr.shape
    mx, mn = arr.max(2), arr.min(2)
    # Background = light AND neutral: the grey card, white canvas, and the soft
    # drop shadow. The subject's dark rock and saturated green fall outside this.
    candidate = (mx - mn <= 22) & (mn >= 130)

    bg = np.zeros((h, w), bool)
    dq: deque = deque()

    def seed(y: int, x: int) -> None:
        if candidate[y, x] and not bg[y, x]:
            bg[y, x] = True
            dq.append((y, x))

    for x in range(w):
        seed(0, x)
        seed(h - 1, x)
    for y in range(h):
        seed(y, 0)
        seed(y, w - 1)
    while dq:  # 4-connected flood, confined to the border-touching background
        y, x = dq.popleft()
        if y > 0:
            seed(y - 1, x)
        if y < h - 1:
            seed(y + 1, x)
        if x > 0:
            seed(y, x - 1)
        if x < w - 1:
            seed(y, x + 1)

    alpha = np.where(bg, 0, 255).astype(np.uint8)
    img = Image.fromarray(np.dstack([arr.astype(np.uint8), alpha]), "RGBA")
    img.putalpha(img.split()[3].filter(ImageFilter.GaussianBlur(0.8)))  # soften the cut

    img = img.crop(img.getbbox())  # trim to the subject
    target = round(size * fill)
    scale = target / max(img.size)
    img = img.resize((round(img.width * scale), round(img.height * scale)), Image.LANCZOS)

    master = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    master.alpha_composite(img, ((size - img.width) // 2, (size - img.height) // 2))
    master.save(out)
    print(f"wrote {out} ({size}x{size}, subject {img.width}x{img.height})")


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    opts = dict(a[2:].split("=", 1) for a in sys.argv[1:] if a.startswith("--") and "=" in a)
    src = Path(args[0]) if args else root / "icons" / "NewDetailed.png"
    out = Path(args[1]) if len(args) > 1 else root / "icons" / "EmeraldClean.png"
    lift(src, out, int(opts.get("size", 1024)), float(opts.get("fill", 0.90)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
