#!/usr/bin/env python3
"""
run_selftest.py - build and run the runtime self-test on the PC (headless).

    python scripts/run_selftest.py

Exit code 0 only if every check passes. The same C file is what the Android
app's diagnostics screen runs through JNI.
"""
from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "scripts"))
sys.path.insert(0, str(ROOT / "tools"))

import worksafe            # noqa: E402
import toolchain           # noqa: E402
import gen_portable_cpu    # noqa: E402


def main() -> int:
    gen_portable_cpu.generate()
    out = ROOT / "build" / "selftest"
    out.mkdir(parents=True, exist_ok=True)
    exe = out / "h2_selftest.exe"
    rt = ROOT / "runtime"
    cc = toolchain.host_cc() + ["-O1", "-std=c11", "-Wall", "-Wno-unused-function", "-D_CRT_SECURE_NO_WARNINGS",
                                "-I", ROOT / "build" / "generated" / "include", "-I", rt,
                                ROOT / "tests" / "selftest_main.c", rt / "h2_selftest.c", rt / "h2_runtime.c",
                                rt / "guest_mem.c", "-o", exe]
    if worksafe.run(cc, log_name="selftest_build", timeout=300).returncode != 0:
        print("build FAILED, see logs/selftest_build.log"); return 2
    r = worksafe.run([exe], log_name="selftest_run", timeout=60, capture=True)
    print("\n".join(l for l in (r.stdout or "").splitlines() if not l.startswith("#")))
    return r.returncode


if __name__ == "__main__":
    sys.exit(main())
