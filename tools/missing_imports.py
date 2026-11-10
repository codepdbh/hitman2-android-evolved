#!/usr/bin/env python3
"""
missing_imports.py - which imports of a game module the platform layer does not implement yet.

    python tools/missing_imports.py [Module.dll|hitman2.exe ...]     (default: every module of the install)

Reads analysis/pe_inventory.json (tools/pe_inventory.py) for what each module imports, and
platform/*.c for what is registered: H2ImportDef rows `{ "DLL", "Name", n, fn }` and the
per-file macros `#define K(name, n) { "KERNEL32.dll", #name, n, k_##name }` with their uses.
Imports provided by another loaded module (the renderer's imports from hitman2.exe) are not listed.
"""
from __future__ import annotations

import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def implemented() -> set[tuple[str, str]]:
    have: set[tuple[str, str]] = set()
    row = re.compile(r'\{\s*"([\w.#]+)"\s*,\s*"([^"]+)"\s*,')
    macro = re.compile(r'#define\s+(\w+)\(name,\s*n\)\s*\{\s*"([\w.#]+)"\s*,\s*(?:"(\w*)"\s*)?#name')
    for f in sorted([*(ROOT / "platform").glob("*.c"), *(ROOT / "renderer").glob("*.c")]):
        s = f.read_text(encoding="utf-8")
        for m in row.finditer(s):
            have.add((m.group(1).lower(), m.group(2)))
        for m in macro.finditer(s):
            pre, dll = m.group(1), m.group(2).lower()
            for u in re.finditer(r"\b" + re.escape(pre) + r"\((\w+),\s*(?:\d+|H2_CDECL)\)", s):
                have.add((dll, (m.group(3) or "") + u.group(1)))
    return have


def main() -> int:
    want = {a.lower() for a in sys.argv[1:]}
    inv = json.loads((ROOT / "analysis" / "pe_inventory.json").read_text(encoding="utf-8"))["modules"]
    local = {m["file"].lower() for m in inv if "error" not in m}
    have = implemented()
    total_missing = 0
    for mod in inv:
        if "error" in mod or (want and mod["file"].lower() not in want):
            continue
        if not want and mod["file"].lower() in ("unins000.exe", "config.exe"):
            continue
        print(f"== {mod['file']}")
        for dll, names in mod["imports"].items():
            if dll.lower() in local:
                continue                                     # satisfied by another guest image
            names = [n.replace("ordinal#", "#") for n in names]          # the inventory says ordinal#N, the registry #N
            miss = [n for n in names if (dll.lower(), n) not in have]
            total_missing += len(miss)
            print(f"   {dll:<14} {len(names) - len(miss):>3}/{len(names):<3} implemented"
                  + (f"   missing: {', '.join(miss)}" if miss and len(miss) <= 70 else f"   missing: {len(miss)}" if miss else ""))
    print(f"\n{total_missing} imports not implemented")
    return 0


if __name__ == "__main__":
    sys.exit(main())
