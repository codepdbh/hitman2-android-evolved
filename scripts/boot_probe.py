#!/usr/bin/env python3
"""
boot_probe.py - link the whole lifted hitman2.exe with the runtime and the
platform layer, run it from its entry point, headless, and report where it stops.

    python scripts/boot_probe.py [--budget N] [--trace] [--calls] [--no-build]

Prerequisites: `scripts/lift_image.py lift` (the generated C must exist).
The lifted objects for the host are compiled on demand (bounded parallelism).
The probe is a console program started through worksafe: no window, low
priority, time-limited. It never launches the original game.
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "scripts"))
sys.path.insert(0, str(ROOT / "tools"))

import worksafe            # noqa: E402
import toolchain           # noqa: E402
import gen_portable_cpu    # noqa: E402

GEN = ROOT / "recomp" / "generated" / "hitman2"
OBJ = ROOT / "build" / "lift" / "obj_host"
OUT = ROOT / "build" / "boot"
GAME_EXE = ROOT / "Hitman 2 Silent Assassin" / "hitman2.exe"


def build() -> Path | None:
    chunks = sorted(GEN.glob("chunk_*.c"))
    if not chunks:
        print("no lifted code: run `python scripts/lift_image.py lift` first"); return None
    stale = [c for c in chunks if not (OBJ / (c.stem + ".o")).exists()
             or (OBJ / (c.stem + ".o")).stat().st_mtime < c.stat().st_mtime]
    if stale:
        print(f"[build] compiling {len(stale)} lifted units for the host ...")
        r = worksafe.run([sys.executable, ROOT / "scripts" / "lift_image.py", "compile", "--target", "host"],
                         log_name="boot_compile_lifted", timeout=3600)
        if r.returncode != 0:
            print("[build] lifted code failed to compile, see logs/boot_compile_lifted.log"); return None
    gen_portable_cpu.generate()
    OUT.mkdir(parents=True, exist_ok=True)
    exe = OUT / "h2_boot.exe"
    rt, pf = ROOT / "runtime", ROOT / "platform"
    src = sources()
    rsp = OUT / "objects.rsp"                      # 72 object paths: keep the command line short
    rsp.write_text("\n".join(f'"{p.as_posix()}"' for p in sorted(OBJ.glob("chunk_*.o"))), encoding="utf-8")
    cc = toolchain.host_cc() + ["-O1", "-std=c11", "-fno-strict-aliasing", "-fwrapv", "-Wall", "-Wno-unused-function",
                                "-D_CRT_SECURE_NO_WARNINGS",
                                "-I", ROOT / "build" / "generated" / "include", "-I", rt, "-I", pf, "-I", GEN,
                                *src, f"@{rsp}",
                                "-Wl,/STACK:268435456",      # lifted calls nest on the host stack
                                "-o", exe]
    r = worksafe.run(cc, log_name="boot_link", timeout=1800)
    if r.returncode != 0:
        print("[build] link FAILED, see logs/boot_link.log"); return None
    return exe


def sources():
    rt, pf = ROOT / "runtime", ROOT / "platform"
    return [ROOT / "pc_test" / "boot_probe.c", rt / "guest_mem.c", rt / "h2_runtime.c", rt / "h2_dispatch.c",
            pf / "imports.c", pf / "pe_loader.c", pf / "guest_alloc.c", pf / "process.c", pf / "win32_kernel32.c",
            pf / "win32_objects.c", pf / "win32_files.c", pf / "win32_user32.c"]


def run_android(serial: str, budget: int) -> str | None:
    """Link the same program for arm64-v8a and run it on a device over adb.
    Headless command-line binary under /data/local/tmp; removed afterwards.
    The user's hitman2.exe is copied there only for the run (it is data: the
    code executed is the lifted C)."""
    obj = ROOT / "build" / "lift" / "obj_arm64"
    chunks = sorted(GEN.glob("chunk_*.c"))
    if any(not (obj / (c.stem + ".o")).exists() or (obj / (c.stem + ".o")).stat().st_mtime < c.stat().st_mtime for c in chunks):
        print("[arm64] compiling lifted units ...")
        if worksafe.run([sys.executable, ROOT / "scripts" / "lift_image.py", "compile", "--target", "arm64"],
                        log_name="boot_compile_lifted_arm64", timeout=3600).returncode != 0:
            print("[arm64] lifted code failed to compile"); return None
    rsp = OUT / "objects_arm64.rsp"
    rsp.write_text("\n".join(f'"{p.as_posix()}"' for p in sorted(obj.glob("chunk_*.o"))), encoding="utf-8")
    exe = OUT / "h2_boot_arm64"
    cc = toolchain.android_cc() + ["-O1", "-std=c11", "-fno-strict-aliasing", "-fwrapv", "-w",
                                   "-I", ROOT / "build" / "generated" / "include", "-I", ROOT / "runtime",
                                   "-I", ROOT / "platform", "-I", GEN, *sources(), f"@{rsp}", "-lm", "-llog", "-o", exe]
    if worksafe.run(cc, log_name="boot_link_arm64", timeout=1800).returncode != 0:
        print("[arm64] link FAILED, see logs/boot_link_arm64.log"); return None
    adb = [toolchain.find_adb(), "-s", serial]
    d = "/data/local/tmp/h2ae_boot"
    sh = lambda cmd, name, t=180: worksafe.run(adb + ["shell", cmd], log_name=name, timeout=t, capture=True).stdout or ""  # noqa: E731
    try:
        sh(f"rm -rf {d}; mkdir -p {d}/game", "adb_boot_mkdir")
        worksafe.run(adb + ["push", exe, f"{d}/h2_boot"], log_name="adb_boot_push1", timeout=600, check=True)
        worksafe.run(adb + ["push", GAME_EXE, f"{d}/game/hitman2.exe"], log_name="adb_boot_push2", timeout=600, check=True)
        # 256 MiB host stack, like the PC build: lifted calls nest on it.
        out = sh(f"cd {d} && chmod 755 h2_boot && ulimit -s 262144 2>/dev/null; "
                 f"./h2_boot game/hitman2.exe --budget {budget} --user-root {d}/user 2>/dev/null; echo rc=$?", "adb_boot_run", 600)
    finally:
        sh(f"rm -rf {d}", "adb_boot_cleanup")
    return "\n".join(l for l in out.splitlines() if not l.startswith("# "))


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--android", metavar="SERIAL", help="also run the arm64-v8a build on this adb device and compare")
    ap.add_argument("--budget", type=int, default=50_000_000)
    ap.add_argument("--trace", action="store_true", help="log every import call")
    ap.add_argument("--calls", action="store_true", help="list imports called, with counts")
    ap.add_argument("--no-build", action="store_true")
    args = ap.parse_args()
    if not GAME_EXE.exists():
        print(f"game executable not found: {GAME_EXE}"); return 2
    exe = OUT / "h2_boot.exe" if args.no_build else build()
    if not exe or not exe.exists(): return 2
    # Everything the guest writes goes under build/boot/user; the game directory is opened read-only.
    cmd = ([exe, GAME_EXE, "--budget", args.budget, "--user-root", OUT / "user"]
           + (["--trace"] if args.trace else []) + (["--calls"] if args.calls else []))
    try:
        r = worksafe.run(cmd, log_name="boot_probe", timeout=300, capture=True)
    except worksafe.WorkSafeError as ex:
        print(f"probe did not finish: {ex}"); return 3
    host_out = "\n".join(l for l in (r.stdout or "").splitlines() if not l.startswith("# "))
    print(host_out)
    if args.android:
        try:
            arm = run_android(args.android, args.budget)
        except (worksafe.WorkSafeError, toolchain.ToolchainError) as ex:
            arm = None; print(f"[arm64] NOT executed: {ex}")
        if arm:
            res = lambda t: next((l for l in t.splitlines() if l.startswith("RESULT ")), "")   # noqa: E731
            stop = lambda t: next((l.strip() for l in t.splitlines() if "is not implemented" in l and "stopped" not in l), "")  # noqa: E731
            print("\n[arm64] " + (res(arm) or "no RESULT line; output:\n" + arm[-1500:]))
            same = res(arm) and res(arm) == res(host_out)
            print(f"[arm64] same stop point and counters as the x86-64 host: {'YES' if same else 'NO'}  {stop(arm)}")
    return r.returncode


if __name__ == "__main__":
    sys.exit(main())
