#!/usr/bin/env python3
"""
make_icons.py - generate the Android launcher icons from android/branding/icono.png.

Writes, under android/app/src/main/res/:
    mipmap-<dpi>/ic_launcher.png             legacy icon (the artwork as is)
    mipmap-<dpi>/ic_launcher_foreground.png  adaptive foreground: the artwork
                                             scaled into the 66/108 safe zone
    mipmap-anydpi-v26/ic_launcher.xml        adaptive icon definition
    values/ic_launcher_background.xml        background colour

    python tools/make_icons.py
"""
from __future__ import annotations

import sys
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "android" / "branding" / "icono.png"
RES = ROOT / "android" / "app" / "src" / "main" / "res"
DPI = {"mdpi": 1.0, "hdpi": 1.5, "xhdpi": 2.0, "xxhdpi": 3.0, "xxxhdpi": 4.0}
BACKGROUND = "#0B0B0C"
SAFE = 66 / 108 * 1.08      # the round artwork may slightly exceed the strict safe zone; masks crop to a circle anyway


def main() -> int:
    if not SRC.exists():
        print(f"missing {SRC.relative_to(ROOT)}", file=sys.stderr); return 2
    img = Image.open(SRC).convert("RGBA")
    side = max(img.size)
    square = Image.new("RGBA", (side, side), (0, 0, 0, 0))
    square.paste(img, ((side - img.width) // 2, (side - img.height) // 2))
    for name, scale in DPI.items():
        d = RES / f"mipmap-{name}"; d.mkdir(parents=True, exist_ok=True)
        legacy = int(48 * scale)
        square.resize((legacy, legacy), Image.LANCZOS).save(d / "ic_launcher.png", optimize=True)
        full = int(108 * scale); inner = int(full * SAFE)
        fg = Image.new("RGBA", (full, full), (0, 0, 0, 0))
        fg.paste(square.resize((inner, inner), Image.LANCZOS), ((full - inner) // 2, (full - inner) // 2))
        fg.save(d / "ic_launcher_foreground.png", optimize=True)
    any26 = RES / "mipmap-anydpi-v26"; any26.mkdir(parents=True, exist_ok=True)
    (any26 / "ic_launcher.xml").write_text(
        '<?xml version="1.0" encoding="utf-8"?>\n'
        '<adaptive-icon xmlns:android="http://schemas.android.com/apk/res/android">\n'
        '    <background android:drawable="@color/ic_launcher_background"/>\n'
        '    <foreground android:drawable="@mipmap/ic_launcher_foreground"/>\n'
        '</adaptive-icon>\n', encoding="utf-8")
    (RES / "values").mkdir(parents=True, exist_ok=True)
    (RES / "values" / "ic_launcher_background.xml").write_text(
        '<?xml version="1.0" encoding="utf-8"?>\n<resources>\n'
        f'    <color name="ic_launcher_background">{BACKGROUND}</color>\n</resources>\n', encoding="utf-8")
    print(f"icons written under {RES.relative_to(ROOT)} from {SRC.relative_to(ROOT)} ({img.width}x{img.height})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
