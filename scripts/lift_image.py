#!/usr/bin/env python3
"""
lift_image.py - lift every catalogued function of the game's modules to C, and
optionally compile the result, reporting exactly what did not make it.

    python scripts/lift_image.py lift [--module M]          # catalog -> recomp/generated/<M>/
    python scripts/lift_image.py compile --target arm64|host [--module M]
    python scripts/lift_image.py census [--module M]

Modules (see MODULES): hitman2 (the executable, from the section-normalised copy
in build/work) and renderopengl (RenderOpenGL.dll, lifted straight from the
user's file). Without --module, all of them.

Output (never committed; derived from the game binaries):
    recomp/generated/<M>/chunk_NNNN.c    CHUNK functions each
    recomp/generated/<M>/funcs_list.h    X-macro of every lifted entry of M
    recomp/generated/funcs_all.h         X-macro of every module, sorted by address
                                         (what runtime/h2_dispatch.c includes)
    build/lift/<M>.census.json           lifted, lifter exceptions, RECOMP_TODO by
                                         mnemonic, static dispatch targets with no body

All modules live in ONE guest address space. Each is lifted for its preferred
image base and must be loaded there; the bases of the modules do not overlap
(exe 0x00400000, renderer 0x10000000). "Lifted" and "compiled" are statements
about translation only: correctness is what scripts/difftest_game.py measures.

Runs at low priority with a bounded number of worker processes (worksafe).
"""
from __future__ import annotations

import argparse
import bisect
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

GAME = ROOT / "Hitman 2 Silent Assassin"
MODULES = {
    "hitman2": ROOT / "build" / "work" / "hitman2.norm.exe",
    "renderopengl": GAME / "RenderOpenGL.dll",
}
GEN_ROOT = ROOT / "recomp" / "generated"
CATALOG_DIR = ROOT / "recomp" / "catalog"
OUT = ROOT / "build" / "lift"
CHUNK = 300

TODO_RE = re.compile(r'RECOMP_TODO\(0x([0-9A-F]{8}), "([^"]*)"\)')
DISPATCH_RE = re.compile(r"dispatch\(c, 0x([0-9A-F]{8})u\)")


def gen_dir(m: str) -> Path: return GEN_ROOT / m
def census_path(m: str) -> Path: return OUT / f"{m}.census.json"
def obj_dir(target: str, m: str) -> Path: return OUT / f"obj_{target}" / m


def _lift_chunk(job):
    """Worker: lift one chunk. Runs in a child process."""
    module, idx, funcs = job
    import pefile
    import h2_lifter
    exe = MODULES[module]
    pe = pefile.PE(str(exe), fast_load=True)
    base, size = pe.OPTIONAL_HEADER.ImageBase, pe.OPTIONAL_HEADER.SizeOfImage
    read = lambda va, n: pe.get_data(va - base, n)          # noqa: E731
    L = h2_lifter.make_lifter(exe, base, size, read)
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
    (gen_dir(module) / f"chunk_{idx:04d}.c").write_text(text, encoding="utf-8")
    todos = collections.Counter()
    todo_funcs = 0
    for body in text.split("\nvoid L_")[1:]:
        found = TODO_RE.findall(body)
        if found: todo_funcs += 1
        for _, what in found:
            todos[what.split()[0] if what else "?"] += 1
    targets = {int(x, 16) for x in DISPATCH_RE.findall(text)}
    return idx, ok, errors, dict(todos), todo_funcs, targets, text.count("\n")


def write_funcs_all() -> int:
    """funcs_all.h: every lifted entry of every module, strictly ascending."""
    addrs = []
    for m in MODULES:
        lst = gen_dir(m) / "funcs_list.h"
        if lst.exists():
            addrs += [int(x, 16) for x in re.findall(r"X\(([0-9A-F]{8})\)", lst.read_text(encoding="utf-8"))]
    addrs = sorted(set(addrs))
    GEN_ROOT.mkdir(parents=True, exist_ok=True)
    (GEN_ROOT / "funcs_all.h").write_text(
        "/* GENERATED */\n#define LIFTED_FUNCS(X) \\\n" + " \\\n".join(f"    X({a:08X})" for a in addrs) + "\n",
        encoding="utf-8")
    return len(addrs)


def lift_module(module: str) -> dict:
    exe, catalog = MODULES[module], CATALOG_DIR / f"{module}.functions.json"
    for need in (exe, catalog):
        if not need.exists():
            raise SystemExit(f"missing {need}")
    cat = json.loads(catalog.read_text(encoding="utf-8"))["functions"]
    funcs = sorted((f["address"], f["size"]) for f in cat if f["size"] > 0)
    # Run-time discovered indirect targets (config/lift_extra_entries.json), each sized up to the next catalog entry.
    extra_file = ROOT / "config" / "lift_extra_entries.json"
    if extra_file.exists():
        have = {a for a, _ in funcs}
        starts_ = sorted(have)
        for key in json.loads(extra_file.read_text(encoding="utf-8")).get(module, {}):
            a = int(key, 16)
            if a in have: continue
            i = bisect.bisect_right(starts_, a)
            nxt = starts_[i] if i < len(starts_) else a + 0x400
            funcs.append((a, max(16, min(nxt - a, 0x1000)))); have.add(a)
        funcs.sort()
    gd = gen_dir(module)
    gd.mkdir(parents=True, exist_ok=True)
    for old in gd.glob("chunk_*.c"): old.unlink()
    jobs = [(module, i, funcs[k:k + CHUNK]) for i, k in enumerate(range(0, len(funcs), CHUNK))]
    workers = max(1, worksafe.max_jobs() - 1)
    print(f"[lift:{module}] {len(funcs)} entries in {len(jobs)} chunks, {workers} worker processes")
    t0 = time.time()
    lifted, errors, todos, targets = [], {}, collections.Counter(), set()
    todo_funcs = lines = 0
    with cf.ProcessPoolExecutor(max_workers=workers) as ex:
        for idx, ok, err, td, tf, tg, nl in ex.map(_lift_chunk, jobs):
            lifted += ok; errors.update(err); todos.update(td); todo_funcs += tf; targets |= tg; lines += nl
    # Gap closing: a lifted function may branch (a tail `dispatch`) to an address inside code the catalog did not
    # give its own entry. Lift each such target as an extra entry, sized up to the next catalog entry (at most 4 KiB),
    # and repeat for the targets those introduce. These entries are code reached by real branches of lifted code,
    # not guesses; a target that fails to lift stays missing and is reported in the census.
    gap_entries = []
    starts = sorted(a for a, _ in funcs)
    for _round in range(6):
        missing_now = sorted(t for t in targets - set(lifted) if t not in errors)
        if not missing_now: break
        ents = []
        for a in missing_now:
            i = bisect.bisect_right(starts, a)
            nxt = starts[i] if i < len(starts) else a + 0x400
            ents.append((a, max(16, min(nxt - a, 0x1000))))
        jobs2 = [(module, len(jobs) + j, ents[k:k + CHUNK]) for j, k in enumerate(range(0, len(ents), CHUNK))]
        jobs += jobs2
        gap_entries += [a for a, _ in ents]
        with cf.ProcessPoolExecutor(max_workers=workers) as ex:
            for idx, ok, err, td, tf, tg, nl in ex.map(_lift_chunk, jobs2):
                lifted += ok; errors.update(err); todos.update(td); todo_funcs += tf; targets |= tg; lines += nl
    lifted.sort()
    (gd / "funcs_list.h").write_text(
        "/* GENERATED */\n#define LIFTED_FUNCS(X) \\\n" + " \\\n".join(f"    X({a:08X})" for a in lifted) + "\n",
        encoding="utf-8")
    missing = sorted(targets - set(lifted))
    err_kinds = collections.Counter(re.sub(r"0x[0-9a-fA-F]+", "ADDR", e.split(" (")[0])[:90] for e in errors.values())
    census = {
        "module": module, "date": time.strftime("%Y-%m-%dT%H:%M:%S"), "seconds": round(time.time() - t0, 1),
        "catalog_entries": len(funcs), "lifted": len(lifted), "lifter_exceptions": len(errors),
        "lifter_exception_kinds": dict(err_kinds.most_common(20)),
        "lifter_exception_addresses": [f"{a:#010x}" for a in sorted(errors)][:200],
        "gap_entries_added": len(gap_entries),
        "c_lines": lines, "chunks": len(jobs),
        "functions_containing_RECOMP_TODO": todo_funcs,
        "RECOMP_TODO_total": sum(todos.values()), "RECOMP_TODO_by_mnemonic": dict(todos.most_common()),
        "static_dispatch_targets": len(targets), "static_dispatch_targets_without_body": len(missing),
        "static_dispatch_targets_without_body_sample": [f"{a:#010x}" for a in missing[:60]],
    }
    OUT.mkdir(parents=True, exist_ok=True)
    old = json.loads(census_path(module).read_text(encoding="utf-8")) if census_path(module).exists() else {}
    if "compile" in old: census["compile"] = old["compile"]
    census_path(module).write_text(json.dumps(census, indent=1), encoding="utf-8")
    return census


def print_census(c):
    print(f"[census:{c['module']}] lifted {c['lifted']}/{c['catalog_entries']} entries, {c['c_lines']:,} lines of C, {c['seconds']}s")
    print(f"[census:{c['module']}] lifter exceptions: {c['lifter_exceptions']}  {c['lifter_exception_kinds']}")
    print(f"[census:{c['module']}] RECOMP_TODO: {c['RECOMP_TODO_total']} sites in {c['functions_containing_RECOMP_TODO']} functions")
    for k, v in list(c["RECOMP_TODO_by_mnemonic"].items())[:25]: print(f"           {v:6d}  {k}")
    print(f"[census:{c['module']}] static dispatch targets without a lifted body: "
          f"{c['static_dispatch_targets_without_body']} of {c['static_dispatch_targets']}")
    for t, r in c.get("compile", {}).items():
        print(f"[census:{c['module']}] compile {t}: {r['ok']}/{r['chunks']} chunks OK, {r['seconds']}s")


def modules_arg(args):
    return list(MODULES) if not args.module else [args.module]


def cmd_lift(args) -> int:
    for m in modules_arg(args): print_census(lift_module(m))
    print(f"[lift] funcs_all.h: {write_funcs_all()} entries across modules")
    return 0


def cmd_compile(args) -> int:
    import gen_portable_cpu
    gen_portable_cpu.generate()
    cc = toolchain.android_cc() if args.target == "arm64" else toolchain.host_cc()
    flags = ["-c", "-O1", "-std=c11", "-fno-strict-aliasing", "-fwrapv", "-w", "-ferror-limit=20",
             "-I", ROOT / "build" / "generated" / "include", "-I", ROOT / "runtime"]
    rc = 0
    for m in modules_arg(args):
        chunks = sorted(gen_dir(m).glob("chunk_*.c"))
        if not chunks: raise SystemExit(f"{m}: nothing to compile; run `lift` first")
        objdir = obj_dir(args.target, m); objdir.mkdir(parents=True, exist_ok=True)

        def one(src: Path, objdir=objdir, m=m):
            r = worksafe.run(cc + flags + [src, "-o", objdir / (src.stem + ".o")],
                             log_name=f"lift_cc_{args.target}_{m}_{src.stem}", timeout=1200)
            return src.name, r.returncode

        t0 = time.time(); bad = []
        with cf.ThreadPoolExecutor(max_workers=worksafe.max_jobs()) as ex:
            for name, code in ex.map(one, chunks):
                if code != 0: bad.append(name)
        res = {"chunks": len(chunks), "ok": len(chunks) - len(bad), "failed": bad, "seconds": round(time.time() - t0, 1),
               "object_bytes": sum(p.stat().st_size for p in objdir.glob("*.o")), "compiler": str(cc[0]), "flags": "-O1"}
        census = json.loads(census_path(m).read_text(encoding="utf-8")) if census_path(m).exists() else {"module": m}
        census.setdefault("compile", {})[args.target] = res
        census_path(m).write_text(json.dumps(census, indent=1), encoding="utf-8")
        print(f"[compile {args.target}:{m}] {res['ok']}/{res['chunks']} chunks OK in {res['seconds']}s, "
              f"{res['object_bytes'] / 1e6:.1f} MB of objects")
        for b in bad[:10]: print(f"   FAILED {b}  (logs/lift_cc_{args.target}_{m}_{Path(b).stem}.log)")
        rc = rc or (1 if bad else 0)
    return rc


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    for name in ("lift", "compile", "census"):
        p = sub.add_parser(name)
        p.add_argument("--module", choices=sorted(MODULES))
        if name == "compile": p.add_argument("--target", choices=("arm64", "host"), default="arm64")
    args = ap.parse_args()
    if args.cmd == "lift": return cmd_lift(args)
    if args.cmd == "compile": return cmd_compile(args)
    for m in modules_arg(args): print_census(json.loads(census_path(m).read_text(encoding="utf-8")))
    return 0


if __name__ == "__main__":
    sys.exit(main())
