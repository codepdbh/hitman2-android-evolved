#!/usr/bin/env python3
"""
sanitize_evidence.py - remove machine-specific text from files meant to be published.

    python tools/sanitize_evidence.py docs/evidence/*.json docs/evidence/*.txt

Rewrites, in place:
  * a Windows home path  C:\\Users\\<name>   (also with the JSON-escaped doubled backslashes)  ->  <HOME>
  * an extra literal given with --redact (e.g. a device serial)                                ->  (omitted)

It reports what it changed and exits 1 if, after rewriting, any home path is still present.
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

HOME = re.compile(r"[A-Za-z]:(?:\\\\|\\|/)+Users(?:\\\\|\\|/)+[^\\/\"'\s]+")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("files", nargs="+")
    ap.add_argument("--redact", action="append", default=[], help="literal text to replace with (omitted)")
    args = ap.parse_args()
    bad = 0
    for name in args.files:
        p = Path(name)
        text = p.read_text(encoding="utf-8")
        new = HOME.sub("<HOME>", text)
        for lit in args.redact:
            new = new.replace(lit, "(omitted)")
        if new != text:
            p.write_text(new, encoding="utf-8", newline="")
            print(f"sanitised {p}")
        if HOME.search(new):
            bad += 1
            print(f"STILL CONTAINS A HOME PATH: {p}")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
