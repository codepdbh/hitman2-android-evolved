#!/usr/bin/env python3
"""
toolchain.py - locate compilers without assuming paths or touching the system.

Host builds (Windows x86-64 test executables):
    clang from PATH or C:\\Program Files\\LLVM, pointed explicitly at an
    installed MSVC + Windows SDK (clang does not find Visual Studio 18 by
    itself outside a developer prompt). Linked with lld-link.

Android builds (arm64-v8a):
    the NDK's own clang, from ANDROID_NDK_HOME / ANDROID_HOME /
    %LOCALAPPDATA%\\Android\\Sdk\\ndk\\<highest version>.

Nothing is installed or modified; a missing piece raises ToolchainError with
what was looked for.

    python scripts/toolchain.py        # print what was found
"""
from __future__ import annotations

import os
import shutil
from pathlib import Path


class ToolchainError(RuntimeError):
    pass


def _ver_key(p: Path):
    return tuple(int(x) if x.isdigit() else 0 for x in p.name.split("."))


def find_clang() -> Path:
    for c in (shutil.which("clang"), r"C:\Program Files\LLVM\bin\clang.exe"):
        if c and Path(c).exists():
            return Path(c)
    raise ToolchainError("clang not found in PATH or C:\\Program Files\\LLVM\\bin")


def find_msvc() -> Path:
    roots = [Path(os.environ.get("ProgramFiles", r"C:\Program Files")) / "Microsoft Visual Studio",
             Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / "Microsoft Visual Studio"]
    found = []
    for r in roots:
        if r.is_dir():
            found += [p for p in r.glob("*/*/VC/Tools/MSVC/*")
                      if (p / "include" / "vcruntime.h").exists() and (p / "lib" / "x64").is_dir()]
    if not found:
        raise ToolchainError("no MSVC toolset with x64 libraries under Program Files\\Microsoft Visual Studio")
    return max(found, key=_ver_key)


def find_winsdk() -> tuple[Path, str]:
    kit = Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / "Windows Kits" / "10"
    vers = [p for p in (kit / "Include").glob("10.*")
            if (p / "ucrt" / "stdio.h").exists() and (kit / "Lib" / p.name / "um" / "x64" / "kernel32.lib").exists()]
    if not vers:
        raise ToolchainError(f"no complete Windows 10/11 SDK under {kit}")
    return kit, max(vers, key=_ver_key).name


def host_cc() -> list:
    """Command prefix for compiling+linking a 64-bit Windows console program."""
    clang, msvc = find_clang(), find_msvc()
    kit, ver = find_winsdk()
    inc, lib = kit / "Include" / ver, kit / "Lib" / ver
    args = [clang, "--target=x86_64-pc-windows-msvc", "-fuse-ld=lld"]
    for d in (msvc / "include", inc / "ucrt", inc / "um", inc / "shared"):
        args += ["-isystem", d]
    for d in (msvc / "lib" / "x64", lib / "ucrt" / "x64", lib / "um" / "x64"):
        args += ["-L", d]
    return args


def find_ndk() -> Path:
    cands = []
    for env in ("ANDROID_NDK_HOME", "ANDROID_NDK_ROOT"):
        if os.environ.get(env):
            cands.append(Path(os.environ[env]))
    for env in ("ANDROID_HOME", "ANDROID_SDK_ROOT"):
        if os.environ.get(env):
            cands += sorted((Path(os.environ[env]) / "ndk").glob("*"), key=_ver_key, reverse=True)
    local = Path(os.environ.get("LOCALAPPDATA", "")) / "Android" / "Sdk" / "ndk"
    if local.is_dir():
        cands += sorted(local.glob("*"), key=_ver_key, reverse=True)
    for c in cands:
        if (c / "toolchains" / "llvm" / "prebuilt").is_dir():
            return c
    raise ToolchainError("Android NDK not found (ANDROID_NDK_HOME, ANDROID_HOME, %LOCALAPPDATA%\\Android\\Sdk\\ndk)")


def find_adb() -> Path:
    name = "adb.exe" if os.name == "nt" else "adb"
    cands = [shutil.which("adb")]
    for env in ("ANDROID_HOME", "ANDROID_SDK_ROOT"):
        if os.environ.get(env):
            cands.append(str(Path(os.environ[env]) / "platform-tools" / name))
    cands.append(str(Path(os.environ.get("LOCALAPPDATA", "")) / "Android" / "Sdk" / "platform-tools" / name))
    for c in cands:
        if c and Path(c).exists():
            return Path(c)
    raise ToolchainError("adb not found (PATH, ANDROID_HOME, %LOCALAPPDATA%\\Android\\Sdk\\platform-tools)")


def android_cc(api: int = 26, abi_triple: str = "aarch64-linux-android") -> list:
    """Command prefix for compiling+linking an arm64-v8a Android executable/library."""
    ndk = find_ndk()
    pre = next((ndk / "toolchains" / "llvm" / "prebuilt").glob("*"))
    exe = pre / "bin" / ("clang.exe" if os.name == "nt" else "clang")
    if not exe.exists():
        raise ToolchainError(f"NDK clang missing: {exe}")
    return [exe, f"--target={abi_triple}{api}"]


if __name__ == "__main__":
    for label, fn in (("clang", find_clang), ("msvc", find_msvc), ("winsdk", find_winsdk), ("ndk", find_ndk)):
        try:
            print(f"{label:<7} {fn()}")
        except ToolchainError as ex:
            print(f"{label:<7} MISSING: {ex}")
