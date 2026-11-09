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
    unexplained = 0
    if upstream.exists() and (ROOT / ".git").exists() and git("rev-list", "-z", "--all", "--max-count=1"):
        unexplained = second_opinion(upstream)
    clean = not problems and unexplained == 0
    print("audit: CLEAN" if clean else "audit: PROBLEMS FOUND - do not commit or push")
    return 0 if clean else 1


def second_opinion(upstream: Path) -> int:
    """Run PCRecomp's heuristic auditor. Each path it flags must be explained in
    config/audit_allow.json (reviewed by hand, with a reason) or it counts as a
    problem. Its history findings are accepted only up to the number of blob
    versions the allow-listed files of that category have in history."""
    import fnmatch
    import json
    allow = {k: v for k, v in json.loads((ROOT / "config" / "audit_allow.json").read_text(encoding="utf-8")).items()
             if not k.startswith("_")}
    r = subprocess.run([sys.executable, str(upstream), str(ROOT), "-v"], capture_output=True, text=True,
                       encoding="utf-8", errors="replace", creationflags=NO_WINDOW)
    category, flagged, history, bad = None, {}, {}, 0
    for line in (r.stdout + r.stderr).splitlines():
        s = line.strip()
        if s.startswith("!! GAME MATERIAL"): category = "game material"; continue
        if s.startswith("!! LIFTED OUTPUT"): category = "lifted output"; continue
        if s.startswith("!! IN HISTORY"): category = "history"; continue
        m = re.match(r"^\d+\s*[KMG]?B\s+(\S.*)$", s)
        if m and category in ("game material", "lifted output"):
            flagged.setdefault(category, []).append(m.group(1)); continue
        m = re.match(r"^(\d+) objects\s+(.+)$", s)
        if m and category == "history":
            history[m.group(2).strip()] = int(m.group(1))
    print("--- pcrecomp audit_repo.py (heuristic second opinion) ---")
    explained = {}
    for cat, paths in flagged.items():
        for p in paths:
            why = next((v for k, v in allow.items() if fnmatch.fnmatch(p, k)), None)
            if why:
                explained.setdefault(cat, []).append(p)
                print(f"  reviewed  [{cat}] {p}")
            else:
                bad += 1
                print(f"  BLOCK     [{cat}] {p}: flagged and not in config/audit_allow.json")
    for cat, count in history.items():
        versions = set()
        for p in explained.get(cat, []):
            for l in git("log", "--all", "--format=", "--raw", "--no-abbrev", "-z", "--", p):
                parts = l.split()
                if len(parts) >= 4 and parts[0].startswith(":"): versions.add(parts[3])
        if count > len(versions):
            bad += 1
            print(f"  BLOCK     [history: {cat}] {count} objects flagged, only {len(versions)} are versions of reviewed files")
        else:
            print(f"  reviewed  [history: {cat}] {count} objects, all versions of reviewed files")
    if not flagged and not history:
        print("  nothing flagged")
    return bad


if __name__ == "__main__":
    sys.exit(main())
