#!/usr/bin/env python3
"""
lift_image.py - lift every catalogued function of a game module to C, and
optionally compile the result, reporting exactly what did not make it.

    python scripts/lift_image.py lift                 # catalog -> recomp/generated/<module>/
    python scripts/lift_image.py compile --target arm64   # or: host
    python scripts/lift_image.py census               # print the last census

Output (never committed; derived from the game binary):
    recomp/generated/hitman2/chunk_NNNN.c   CHUNK functions each
    recomp/generated/hitman2/funcs_list.h   X-macro of every lifted entry
    build/lift/hitman2.census.json          counts: lifted, lifter exceptions,
                                            RECOMP_TODO by mnemonic, static
                                            dispatch targets with no body

"Lifted" and "compiled" are statements about translation only. Whether a
function is CORRECT is what scripts/difftest_game.py measures.

Runs at low priority with a bounded number of worker processes (worksafe).
"""
from __future__ import annotations

import argparse
import collections
import concurrent.futures as cf
import json
import re
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "scripts"))
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "recomp"))

import worksafe                                 # noqa: E402
import toolchain                                # noqa: E402

MODULE = "hitman2"
EXE = ROOT / "build" / "work" / f"{MODULE}.norm.exe"
CATALOG = ROOT / "recomp" / "catalog" / f"{MODULE}.functions.json"
GEN = ROOT / "recomp" / "generated" / MODULE
OUT = ROOT / "build" / "lift"
CENSUS = OUT / f"{MODULE}.census.json"
CHUNK = 300

TODO_RE = re.compile(r'RECOMP_TODO\(0x([0-9A-F]{8}), "([^"]*)"\)')
DISPATCH_RE = re.compile(r"dispatch\(c, 0x([0-9A-F]{8})u\)")


def _lift_chunk(job):
    """Worker: lift one chunk. Runs in a child process."""
    idx, funcs = job
    import pefile
    import h2_lifter
    pe = pefile.PE(str(EXE), fast_load=True)
    base, size = pe.OPTIONAL_HEADER.ImageBase, pe.OPTIONAL_HEADER.SizeOfImage
    read = lambda va, n: pe.get_data(va - base, n)          # noqa: E731
    L = h2_lifter.make_lifter(EXE, base, size, read)
    parts = ["/* GENERATED from the game binary - not redistributable, never commit */",
             '#include "recomp_prelude.h"', ""]
    ok, errors = [], {}
    for addr, n in funcs:
        try:
            parts.append(L.lift_function(read(addr, n), addr)); parts.append("")
            ok.append(addr)
        except Exception as ex:
            errors[addr] = f"{type(ex).__name__}: {str(ex)[:160]}"
    text = "\n".join(parts)
    (GEN / f"chunk_{idx:04d}.c").write_text(text, encoding="utf-8")
    todos = collections.Counter()
    todo_funcs = 0
    for body in text.split("\nvoid L_")[1:]:
        found = TODO_RE.findall(body)
        if found: todo_funcs += 1
        for _, what in found:
            todos[what.split()[0] if what else "?"] += 1
    targets = {int(x, 16) for x in DISPATCH_RE.findall(text)}
    return idx, ok, errors, dict(todos), todo_funcs, targets, text.count("\n")


def cmd_lift(args) -> int:
    for need in (EXE, CATALOG):
        if not need.exists():
            raise SystemExit(f"missing {need.relative_to(ROOT)}")
    cat = json.loads(CATALOG.read_text(encoding="utf-8"))["functions"]
    funcs = sorted((f["address"], f["size"]) for f in cat if f["size"] > 0)
    GEN.mkdir(parents=True, exist_ok=True)
    for old in GEN.glob("chunk_*.c"): old.unlink()
    jobs = [(i, funcs[k:k + CHUNK]) for i, k in enumerate(range(0, len(funcs), CHUNK))]
    workers = max(1, worksafe.max_jobs() - 1)
    print(f"[lift] {len(funcs)} entries in {len(jobs)} chunks, {workers} worker processes")
    t0 = time.time()
    lifted, errors, todos, targets = [], {}, collections.Counter(), set()
    todo_funcs = lines = 0
    with cf.ProcessPoolExecutor(max_workers=workers) as ex:
        for idx, ok, err, td, tf, tg, nl in ex.map(_lift_chunk, jobs):
            lifted += ok; errors.update(err); todos.update(td); todo_funcs += tf; targets |= tg; lines += nl
    lifted.sort()
    (GEN / "funcs_list.h").write_text(
        "/* GENERATED */\n#define LIFTED_FUNCS(X) \\\n" + " \\\n".join(f"    X({a:08X})" for a in lifted) + "\n",
        encoding="utf-8")
    have = set(lifted)
    missing = sorted(targets - have)
    err_kinds = collections.Counter(re.sub(r"0x[0-9a-fA-F]+", "ADDR", e.split(" (")[0])[:90] for e in errors.values())
    census = {
        "module": MODULE, "date": time.strftime("%Y-%m-%dT%H:%M:%S"), "seconds": round(time.time() - t0, 1),
        "catalog_entries": len(funcs), "lifted": len(lifted), "lifter_exceptions": len(errors),
        "lifter_exception_kinds": dict(err_kinds.most_common(20)),
        "lifter_exception_addresses": [f"{a:#010x}" for a in sorted(errors)][:200],
        "c_lines": lines, "chunks": len(jobs),
        "functions_containing_RECOMP_TODO": todo_funcs,
        "RECOMP_TODO_total": sum(todos.values()), "RECOMP_TODO_by_mnemonic": dict(todos.most_common()),
        "static_dispatch_targets": len(targets), "static_dispatch_targets_without_body": len(missing),
        "static_dispatch_targets_without_body_sample": [f"{a:#010x}" for a in missing[:60]],
    }
    OUT.mkdir(parents=True, exist_ok=True)
    CENSUS.write_text(json.dumps(census, indent=1), encoding="utf-8")
    print_census(census)
    return 0


def print_census(c):
    print(f"[census] lifted {c['lifted']}/{c['catalog_entries']} entries, {c['c_lines']:,} lines of C, {c['seconds']}s")
    print(f"[census] lifter exceptions: {c['lifter_exceptions']}  {c['lifter_exception_kinds']}")
    print(f"[census] RECOMP_TODO: {c['RECOMP_TODO_total']} sites in {c['functions_containing_RECOMP_TODO']} functions")
    for k, v in list(c["RECOMP_TODO_by_mnemonic"].items())[:25]: print(f"           {v:6d}  {k}")
    print(f"[census] static dispatch targets without a lifted body: "
          f"{c['static_dispatch_targets_without_body']} of {c['static_dispatch_targets']}")
    if "compile" in c:
        for t, r in c["compile"].items(): print(f"[census] compile {t}: {r['ok']}/{r['chunks']} chunks OK, {r['seconds']}s")


def cmd_compile(args) -> int:
    import gen_portable_cpu
    gen_portable_cpu.generate()
    chunks = sorted(GEN.glob("chunk_*.c"))
    if not chunks: raise SystemExit("nothing to compile; run `lift` first")
    cc = toolchain.android_cc() if args.target == "arm64" else toolchain.host_cc()
    objdir = OUT / f"obj_{args.target}"; objdir.mkdir(parents=True, exist_ok=True)
    flags = ["-c", "-O1", "-std=c11", "-fno-strict-aliasing", "-fwrapv", "-w", "-ferror-limit=20",
             "-I", ROOT / "build" / "generated" / "include", "-I", ROOT / "runtime"]

    def one(src: Path):
        r = worksafe.run(cc + flags + [src, "-o", objdir / (src.stem + ".o")],
                         log_name=f"lift_cc_{args.target}_{src.stem}", timeout=1200)
        return src.name, r.returncode

    t0 = time.time(); bad = []
    with cf.ThreadPoolExecutor(max_workers=worksafe.max_jobs()) as ex:
        for name, rc in ex.map(one, chunks):
            if rc != 0: bad.append(name)
    res = {"chunks": len(chunks), "ok": len(chunks) - len(bad), "failed": bad, "seconds": round(time.time() - t0, 1),
           "object_bytes": sum(p.stat().st_size for p in objdir.glob("*.o")), "compiler": str(cc[0]), "flags": "-O1"}
    census = json.loads(CENSUS.read_text(encoding="utf-8")) if CENSUS.exists() else {}
    census.setdefault("compile", {})[args.target] = res
    CENSUS.write_text(json.dumps(census, indent=1), encoding="utf-8")
    print(f"[compile {args.target}] {res['ok']}/{res['chunks']} chunks OK in {res['seconds']}s, "
          f"{res['object_bytes'] / 1e6:.1f} MB of objects")
    for b in bad[:10]: print(f"   FAILED {b}  (logs/lift_cc_{args.target}_{Path(b).stem}.log)")
    return 0 if not bad else 1


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("lift")
    c = sub.add_parser("compile"); c.add_argument("--target", choices=("arm64", "host"), default="arm64")
    sub.add_parser("census")
    args = ap.parse_args()
    if args.cmd == "lift": return cmd_lift(args)
    if args.cmd == "compile": return cmd_compile(args)
    print_census(json.loads(CENSUS.read_text(encoding="utf-8"))); return 0


if __name__ == "__main__":
    sys.exit(main())
