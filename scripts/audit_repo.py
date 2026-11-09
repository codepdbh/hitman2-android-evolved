#!/usr/bin/env python3
"""
audit_repo.py - refuse to let game material or derived output into git.

Checks what git would commit (tracked + staged + untracked-not-ignored), not
the working tree: the game directory is expected to exist locally, ignored.

Fails (exit 1) on:
  * any file under the game / generated / catalog / raw-analysis directories
  * PE images, the game's archive and asset extensions
  * files that look like lifted C (`void L_XXXXXXXX(CPU *c)`)
  * any file over 4 MB

Then runs PCRecomp's own tools/audit_repo.py as a second opinion when present.

    python scripts/audit_repo.py
"""
from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
FORBIDDEN_DIRS = ("Hitman 2 Silent Assassin/", "game_data/", "recomp/generated/", "recomp/catalog/",
                  "analysis/raw/", "analysis/private/", "build/", "logs/")
FORBIDDEN_EXT = {".exe", ".dll", ".zip", ".whd", ".wav", ".gms", ".tex", ".prm", ".anm", ".loc", ".snd",
                 ".oct", ".sgp", ".rmc", ".rmi", ".zgf", ".buf", ".sup", ".apk", ".aab", ".iso", ".bin"}
LIFTED = re.compile(rb"void L_[0-9A-F]{8}\(CPU \*c\)\s*\{")
MAX_BYTES = 4 * 1024 * 1024
NO_WINDOW = 0x08000000 if sys.platform == "win32" else 0


def git(*args) -> list[str]:
    r = subprocess.run(["git", "-C", str(ROOT), *args], capture_output=True, text=True,
                       encoding="utf-8", errors="replace", creationflags=NO_WINDOW)
    if r.returncode != 0:
        raise SystemExit(f"git {' '.join(args)} failed: {r.stderr.strip()}")
    return [l for l in r.stdout.split("\0") if l]


def main() -> int:
    files = sorted(set(git("ls-files", "-z") + git("ls-files", "-z", "--others", "--exclude-standard")))
    problems = []
    for rel in files:
        p = ROOT / rel
        low = rel.lower()
        if any(low.startswith(d.lower()) for d in FORBIDDEN_DIRS):
            problems.append((rel, "inside a directory that must never be committed")); continue
        if p.suffix.lower() in FORBIDDEN_EXT:
            problems.append((rel, f"forbidden extension {p.suffix}")); continue
        if not p.is_file():
            continue
        size = p.stat().st_size
        if size > MAX_BYTES:
            problems.append((rel, f"{size:,} bytes (> 4 MB)")); continue
        if p.suffix.lower() in (".c", ".h", ".cpp", ".inc") and LIFTED.search(p.read_bytes()):
            problems.append((rel, "contains lifted code (derived from the game binary)"))
    print(f"audit: {len(files)} files git would track")
    for rel, why in problems:
        print(f"  BLOCK  {rel}: {why}")
    upstream = ROOT / "third_party" / "pcrecomp" / "tools" / "audit_repo.py"
    rc2 = 0
    if upstream.exists() and (ROOT / ".git").exists() and git("rev-list", "-z", "--all", "--max-count=1"):
        r = subprocess.run([sys.executable, str(upstream), str(ROOT)], capture_output=True, text=True,
                           encoding="utf-8", errors="replace", creationflags=NO_WINDOW)
        print("--- pcrecomp audit_repo.py ---")
        print((r.stdout + r.stderr).strip()[-1500:])
        rc2 = r.returncode
    print("audit: CLEAN" if not problems and rc2 == 0 else "audit: PROBLEMS FOUND")
    return 0 if not problems and rc2 == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
