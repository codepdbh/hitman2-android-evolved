"""Headless regression checks for the renderer's real EGL configuration policy."""
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "scripts"))
from toolchain import host_cc
from worksafe import run

def main():
    output = ROOT / "build" / "tests" / "graphics_config_test.exe"
    output.parent.mkdir(parents=True, exist_ok=True)
    result = run(host_cc() + ["-std=c11", "-O0", "-I", ROOT / "renderer",
        ROOT / "tests" / "graphics_config_test.c", "-o", output],
        cwd=ROOT, log_name="graphics_config_build", timeout=60, capture=True)
    if result.returncode:
        print(result.stdout)
        return result.returncode
    result = run([output], cwd=ROOT, log_name="graphics_config_test", timeout=10, capture=True)
    print(result.stdout)
    return result.returncode

if __name__ == "__main__":
    raise SystemExit(main())
