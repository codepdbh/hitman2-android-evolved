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

import lift_image          # noqa: E402

GEN = lift_image.GEN_ROOT                 # funcs_all.h lives here; chunks are in <GEN>/<module>/
OUT = ROOT / "build" / "boot"
GAME_DIR = ROOT / "Hitman 2 Silent Assassin"
GAME_EXE = GAME_DIR / "hitman2.exe"
# The files the probe reads from the game install (read-only to the guest).
GAME_FILES = ["hitman2.exe", "Hitman2.ini", "RenderOpenGL.dll", "keyboard.cfg"]


def write_profile_ini() -> Path:
    """The port's configuration, written to the USER root where it overrides the install's Hitman2.ini
    (the install is never modified). Only keys that must differ from the original are changed:
      DrawDll     RenderOpenGL.dll  - the renderer this port implements (docs/RENDERER_DECISION.md)
      Window      1                 - never fullscreen
      Resolution  800x450           - the virtual display size (WORK_SAFE_MODE default)
    Everything else is the original file, byte for byte."""
    import json
    cfg = json.loads((ROOT / "config" / "work_safe.json").read_text(encoding="utf-8"))
    res = f"{cfg['window_width']}x{cfg['window_height']}"
    out, seen = [], set()
    for line in (GAME_DIR / "Hitman2.ini").read_text(encoding="latin-1").splitlines():
        key = line.split()[0] if line.split() else ""
        if key == "DrawDll": line = "DrawDll RenderOpenGL.dll"
        elif key == "Window": line = "Window 1"
        elif key == "Resolution": line = f"Resolution {res}"
        seen.add(key); out.append(line)
    for key, val in (("DrawDll", "RenderOpenGL.dll"), ("Window", "1"), ("Resolution", res)):
        if key not in seen: out.append(f"{key} {val}")
    dst = OUT / "profile" / "Hitman2.ini"
    dst.parent.mkdir(parents=True, exist_ok=True)
    dst.write_text("\r\n".join(out) + "\r\n", encoding="latin-1", newline="")
    return dst


def stale_units(target: str) -> list:
    """Lifted C files with no up-to-date object for `target`, across every module."""
    stale = []
    for m in lift_image.MODULES:
        for c in sorted(lift_image.gen_dir(m).glob("chunk_*.c")):
            o = lift_image.obj_dir(target, m) / (c.stem + ".o")
            if not o.exists() or o.stat().st_mtime < c.stat().st_mtime:
                stale.append(c)
    return stale


def object_list(target: str) -> list:
    return sorted((lift_image.OUT / f"obj_{target}").rglob("*.o"))


def build() -> Path | None:
    if not any(lift_image.gen_dir(m).glob("chunk_*.c") for m in lift_image.MODULES):
        print("no lifted code: run `python scripts/lift_image.py lift` first"); return None
    stale = stale_units("host")
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
    rsp.write_text("\n".join(f'"{p.as_posix()}"' for p in object_list("host")), encoding="utf-8")
    cc = toolchain.host_cc() + ["-O1", "-std=c11", "-fno-strict-aliasing", "-fwrapv", "-Wall", "-Wno-unused-function",
                                "-D_CRT_SECURE_NO_WARNINGS",
                                "-I", ROOT / "build" / "generated" / "include", "-I", rt, "-I", pf, "-I", ROOT / "input", "-I", ROOT / "renderer", "-I", GEN,
                                *src, f"@{rsp}",
                                "-Wl,/STACK:268435456",      # lifted calls nest on the host stack
                                "-o", exe]
    r = worksafe.run(cc, log_name="boot_link", timeout=1800)
    if r.returncode != 0:
        print("[build] link FAILED, see logs/boot_link.log"); return None
    return exe


def sources(android: bool = False):
    rt, pf = ROOT / "runtime", ROOT / "platform"
    return [ROOT / "pc_test" / "boot_probe.c", rt / "guest_mem.c", rt / "h2_runtime.c", rt / "h2_dispatch.c",
            pf / "imports.c", pf / "pe_loader.c", pf / "guest_alloc.c", pf / "process.c", pf / "win32_kernel32.c",
            pf / "win32_objects.c", pf / "win32_threads.c", pf / "win32_files.c", pf / "win32_user32.c", pf / "game_dlls.c", pf / "dinput8.c", pf / "win32_advapi32.c", pf / "dsound.c",
            ROOT / "input" / "input_manager.c", ROOT / "renderer" / "h2_gl.c", ROOT / "renderer" / "gl_null_backend.c"] + ([ROOT / "renderer" / "gl_gles3_backend.c"] if android else [])


DEVICE_GAME_DIR = "/sdcard/h2androidevolved"      # where the user's game data lives on the phone (internal storage)


def ppm_to_png(src: Path, dst: Path) -> None:
    """P6 PPM -> PNG (no third-party imaging library needed)."""
    import struct, zlib
    data = src.read_bytes()
    parts = data.split(b"\n", 3)
    w, h = (int(x) for x in parts[1].split())
    pix = parts[3]
    raw = b"".join(b"\x00" + pix[y * w * 3:(y + 1) * w * 3] for y in range(h))
    def chunk(t, b): return struct.pack(">I", len(b)) + t + b + struct.pack(">I", zlib.crc32(t + b) & 0xFFFFFFFF)
    dst.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                    + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


def run_android(serial: str, budget: int, frames: int = 0, shots: list | None = None) -> str | None:
    shots = shots or []
    """Link the same program for arm64-v8a and run it on a device over adb.
    Headless command-line binary under /data/local/tmp; removed afterwards.
    The user's hitman2.exe is copied there only for the run (it is data: the
    code executed is the lifted C)."""
    if stale_units("arm64"):
        print("[arm64] compiling lifted units ...")
        if worksafe.run([sys.executable, ROOT / "scripts" / "lift_image.py", "compile", "--target", "arm64"],
                        log_name="boot_compile_lifted_arm64", timeout=3600).returncode != 0:
            print("[arm64] lifted code failed to compile"); return None
    rsp = OUT / "objects_arm64.rsp"
    rsp.write_text("\n".join(f'"{p.as_posix()}"' for p in object_list("arm64")), encoding="utf-8")
    exe = OUT / "h2_boot_arm64"
    cc = toolchain.android_cc() + ["-O1", "-std=c11", "-fno-strict-aliasing", "-fwrapv", "-w",
                                   "-I", ROOT / "build" / "generated" / "include", "-I", ROOT / "runtime",
                                   "-I", ROOT / "platform", "-I", ROOT / "input", "-I", ROOT / "renderer", "-I", GEN, *sources(True), f"@{rsp}", "-lm", "-llog", "-lEGL", "-lGLESv3", "-o", exe]
    if worksafe.run(cc, log_name="boot_link_arm64", timeout=1800).returncode != 0:
        print("[arm64] link FAILED, see logs/boot_link_arm64.log"); return None
    adb = [toolchain.find_adb(), "-s", serial]
    d = "/data/local/tmp/h2ae_boot"
    sh = lambda cmd, name, t=180: worksafe.run(adb + ["shell", cmd], log_name=name, timeout=t, capture=True).stdout or ""  # noqa: E731
    try:
        sh(f"rm -rf {d}; mkdir -p {d}/game {d}/user", "adb_boot_mkdir")
        worksafe.run(adb + ["push", exe, f"{d}/h2_boot"], log_name="adb_boot_push1", timeout=600, check=True)
        # The game data lives on the phone's internal storage (the user's own copy, pushed once): read in place.
        game = DEVICE_GAME_DIR if "hitman2.exe" in sh(f"ls {DEVICE_GAME_DIR} 2>/dev/null", "adb_boot_lsgame") else None
        if game is None:                                         # otherwise the game's own files, as data, for this run only
            game = f"{d}/game"
            for name in GAME_FILES:
                worksafe.run(adb + ["push", GAME_DIR / name, f"{d}/game/{name}"], log_name="adb_boot_push2", timeout=600, check=True)
        worksafe.run(adb + ["push", write_profile_ini(), f"{d}/user/Hitman2.ini"], log_name="adb_boot_push3", timeout=60, check=True)
        # 256 MiB host stack, like the PC build: lifted calls nest on it.
        extra = f" --frames {frames}" if frames else ""
        if shots:
            extra += " " + " ".join(f"--shot {n} {d}/shot_{n}.ppm" for n in shots[:1]) + " --gles"
        out = sh(f"cd {d} && chmod 755 h2_boot && ulimit -s 262144 2>/dev/null; "
                 f"./h2_boot {game}/hitman2.exe --budget {budget}{extra} --user-root {d}/user 2>/dev/null; echo rc=$?", "adb_boot_run", 900)
        for n in shots[:1]:
            ppm = OUT / f"shot_{n}.ppm"
            worksafe.run(adb + ["pull", f"{d}/shot_{n}.ppm", ppm], log_name="adb_boot_pull", timeout=120)
            if ppm.exists(): ppm_to_png(ppm, ppm.with_suffix(".png")); print(f"[arm64] frame {n} saved to {ppm.with_suffix('.png')}")
    finally:
        sh(f"rm -rf {d}", "adb_boot_cleanup")
    return "\n".join(l for l in out.splitlines() if not l.startswith("# "))


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--android", metavar="SERIAL", help="also run the arm64-v8a build on this adb device and compare")
    ap.add_argument("--budget", type=int, default=50_000_000)
    ap.add_argument("--frames", type=int, default=0, help="stop after this many presented frames")
    ap.add_argument("--trace", action="store_true", help="log every import call")
    ap.add_argument("--calls", action="store_true", help="list imports called, with counts")
    ap.add_argument("--no-build", action="store_true")
    ap.add_argument("--shot", type=int, action="append", default=[], metavar="FRAME", help="(with --android) render with real GLES 3 and save this frame as a PNG")
    ap.add_argument("--watch-mem", action="append", default=[], metavar="HEXADDR", help="log each change of this guest dword (checked at every dispatch)")
    ap.add_argument("--watch-ret", action="append", default=[], metavar="HEXVA", help="log whenever something is entered with this return address")
    ap.add_argument("--watch", action="append", default=[], metavar="HEXVA", help="log registers each time this guest address is entered")
    args = ap.parse_args()
    if not GAME_EXE.exists():
        print(f"game executable not found: {GAME_EXE}"); return 2
    exe = OUT / "h2_boot.exe" if args.no_build else build()
    if not exe or not exe.exists(): return 2
    # Everything the guest writes goes under build/boot/user; the game directory is opened read-only.
    user = OUT / "user"
    user.mkdir(parents=True, exist_ok=True)
    (user / "Hitman2.ini").write_bytes(write_profile_ini().read_bytes())
    cmd =([exe, GAME_EXE, "--budget", args.budget, "--user-root", OUT / "user"] + (["--frames", args.frames] if args.frames else [])
           + [x for w in args.watch for x in ("--watch", w)] + [x for w in args.watch_ret for x in ("--watch-ret", w)] + [x for w in args.watch_mem for x in ("--watch-mem", w)] + (["--trace"] if args.trace else []) + (["--calls"] if args.calls else []))
    try:
        r = worksafe.run(cmd, log_name="boot_probe", timeout=300, capture=True)
    except worksafe.WorkSafeError as ex:
        print(f"probe did not finish: {ex}"); return 3
    host_out = "\n".join(l for l in (r.stdout or "").splitlines() if not l.startswith("# "))
    print(host_out)
    if args.android:
        try:
            arm = run_android(args.android, args.budget, args.frames, args.shot)
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
