#!/usr/bin/env python3
"""
worksafe.py - the only sanctioned way for project scripts to start a process.

Enforces config/work_safe.json (WORK_SAFE_MODE) instead of merely describing it:

  * children are created with CREATE_NO_WINDOW and STARTF_USESHOWWINDOW/SW_HIDE,
    so no console ever appears and nothing can take focus at creation time;
  * children run at BELOW_NORMAL (or IDLE) priority from their first instruction
    (creation flag, not a later SetPriorityClass);
  * every run has a timeout, after which the whole child tree is terminated;
  * stdout/stderr go to a log file under logs/;
  * parallel job counts are capped to a fraction of the cores;
  * launching a GUI program is refused unless INTERACTIVE_TEST_MODE was enabled
    explicitly (H2_INTERACTIVE_TEST_MODE=1), and the original game executable is
    refused always: it sets its own display mode and grabs DirectInput.

Only processes started through this module are ever terminated by it.

    python scripts/worksafe.py --check          # print the effective profile
    python scripts/worksafe.py --selftest       # prove the flags really apply
"""
from __future__ import annotations

import json
import os
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CONFIG = ROOT / "config" / "work_safe.json"
LOGS = ROOT / "logs"

IS_WINDOWS = os.name == "nt"

# Win32 process-creation constants (subprocess exposes most, not all).
CREATE_NO_WINDOW = 0x08000000
BELOW_NORMAL_PRIORITY_CLASS = 0x00004000
IDLE_PRIORITY_CLASS = 0x00000040
CREATE_NEW_PROCESS_GROUP = 0x00000200
STARTF_USESHOWWINDOW = 0x00000001
SW_HIDE = 0
SW_SHOWNOACTIVATE = 4

# Never started by automation, whatever the mode.
FORBIDDEN_EXECUTABLES = {"hitman2.exe", "config.exe", "unins000.exe"}


class WorkSafeError(RuntimeError):
    pass


def _suppress_error_dialogs() -> None:
    """The error mode is inherited by child processes. With these bits set a
    crashing child terminates instead of raising a fault dialog."""
    if IS_WINDOWS:
        import ctypes
        SEM_FAILCRITICALERRORS, SEM_NOGPFAULTERRORBOX, SEM_NOOPENFILEERRORBOX = 0x1, 0x2, 0x8000
        ctypes.windll.kernel32.SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX)


_suppress_error_dialogs()


def load_config() -> dict:
    cfg = json.loads(CONFIG.read_text(encoding="utf-8"))
    if not cfg.get("enabled", True):
        # The profile cannot be switched off from the file; only relaxed, and
        # only through the explicit interactive mode below.
        cfg["enabled"] = True
    if interactive_mode():
        cfg = {**cfg, **{k: v for k, v in cfg.get("interactive_test_mode", {}).items()
                         if not k.startswith("_") and k != "enabled"}}
        cfg["profile"] = "INTERACTIVE_TEST_MODE"
    # Hard invariants on a PC, in every mode.
    cfg["allow_fullscreen_on_pc"] = False
    cfg["allow_window_focus_stealing"] = False
    cfg["allow_display_mode_changes"] = False
    cfg["allow_global_input_capture"] = False
    cfg["allow_automatic_reboot"] = False
    cfg["window_width"] = min(cfg["window_width"], cfg["max_window_width"], 960)
    cfg["window_height"] = min(cfg["window_height"], cfg["max_window_height"], 540)
    return cfg


def interactive_mode() -> bool:
    return os.environ.get("H2_INTERACTIVE_TEST_MODE") == "1"


def max_jobs(cfg: dict | None = None) -> int:
    cfg = cfg or load_config()
    cores = os.cpu_count() or 2
    if not cfg.get("limit_parallel_jobs", True):
        return cores
    by_fraction = max(1, int(cores * float(cfg.get("max_parallel_jobs_fraction", 0.25))))
    return max(1, min(by_fraction, int(cfg.get("max_parallel_jobs", 4))))


def window_size(cfg: dict | None = None) -> tuple[int, int]:
    cfg = cfg or load_config()
    return cfg["window_width"], cfg["window_height"]


def _creation(cfg: dict, gui: bool):
    if not IS_WINDOWS:
        return {}, None
    prio = IDLE_PRIORITY_CLASS if cfg.get("process_priority") == "idle" else BELOW_NORMAL_PRIORITY_CLASS
    flags = prio | CREATE_NEW_PROCESS_GROUP
    si = subprocess.STARTUPINFO()
    si.dwFlags |= STARTF_USESHOWWINDOW
    if gui:
        # A test window may exist, but its first ShowWindow must not activate it.
        si.wShowWindow = SW_SHOWNOACTIVATE
    else:
        flags |= CREATE_NO_WINDOW
        si.wShowWindow = SW_HIDE
    return {"creationflags": flags, "startupinfo": si}, prio


def _kill_tree(proc: subprocess.Popen) -> None:
    """Terminate only the tree rooted at a process this module created."""
    if proc.poll() is not None:
        return
    if IS_WINDOWS:
        subprocess.run(["taskkill", "/PID", str(proc.pid), "/T", "/F"],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                       creationflags=CREATE_NO_WINDOW)
    else:
        proc.kill()


def run(cmd, *, log_name: str | None = None, timeout: float | None = None,
        cwd: str | os.PathLike | None = None, env: dict | None = None,
        gui: bool = False, check: bool = False, capture: bool = False) -> subprocess.CompletedProcess:
    """Run `cmd` (a list) under the active profile. Returns CompletedProcess.

    stdout+stderr are written to logs/<log_name>.log (and returned in .stdout
    when capture=True). A timeout kills the child tree and raises WorkSafeError.
    """
    cfg = load_config()
    cmd = [str(c) for c in cmd]
    exe = Path(cmd[0]).name.lower()
    if exe in FORBIDDEN_EXECUTABLES:
        raise WorkSafeError(f"refusing to launch {exe}: the original game binaries are never run by automation")
    if gui and cfg.get("headless_default", True):
        raise WorkSafeError("graphical run requested in WORK_SAFE_MODE; set H2_INTERACTIVE_TEST_MODE=1 explicitly")
    if timeout is None:
        timeout = cfg["max_graphical_test_seconds"] if gui else cfg["default_timeout_seconds"]

    extra, _ = _creation(cfg, gui)
    child_env = dict(os.environ if env is None else env)
    child_env.setdefault("PYTHONDONTWRITEBYTECODE", "1")
    child_env.setdefault("PYTHONIOENCODING", "utf-8")
    child_env["H2_WORK_SAFE_MODE"] = "1"
    w, h = window_size(cfg)
    child_env["H2_WINDOW_WIDTH"], child_env["H2_WINDOW_HEIGHT"] = str(w), str(h)

    LOGS.mkdir(exist_ok=True)
    log_path = LOGS / f"{log_name or Path(cmd[0]).stem}.log"
    started = time.time()
    with open(log_path, "w", encoding="utf-8", errors="replace") as log:
        log.write(f"# cmd: {cmd}\n# profile: {cfg['profile']} cwd: {cwd or os.getcwd()}\n")
        log.flush()
        proc = subprocess.Popen(cmd, stdout=log, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL,
                                cwd=cwd, env=child_env, **extra)
        try:
            rc = proc.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            _kill_tree(proc)
            raise WorkSafeError(f"timeout after {timeout}s: {cmd[0]} (see {log_path})")
        except KeyboardInterrupt:
            _kill_tree(proc)
            raise
        log.write(f"# exit: {rc} elapsed: {time.time() - started:.1f}s\n")
    out = log_path.read_text(encoding="utf-8", errors="replace") if capture else None
    if check and rc != 0:
        raise WorkSafeError(f"{cmd[0]} exited {rc} (see {log_path})")
    return subprocess.CompletedProcess(cmd, rc, stdout=out, stderr=None)


def _selftest() -> int:
    """Start a child through run() and have it report its own priority class and
    whether it owns a console window. Evidence, not assumption."""
    probe = (
        "import ctypes,sys\n"
        "k=ctypes.windll.kernel32\n"
        "k.GetCurrentProcess.restype=ctypes.c_void_p\n"
        "k.GetPriorityClass.argtypes=[ctypes.c_void_p]\n"
        "k.GetConsoleWindow.restype=ctypes.c_void_p\n"
        "print('priority=0x%X'%k.GetPriorityClass(k.GetCurrentProcess()))\n"
        "print('console_window=%s'%bool(k.GetConsoleWindow()))\n"
    )
    if not IS_WINDOWS:
        print("selftest: not Windows, nothing to verify"); return 0
    r = run([sys.executable, "-c", probe], log_name="worksafe_selftest", timeout=30, capture=True)
    out = r.stdout or ""
    ok = ("priority=0x4000" in out or "priority=0x40" in out) and "console_window=False" in out
    forbidden = False
    try:
        run(["hitman2.exe"], log_name="worksafe_selftest_forbidden", timeout=5)
    except WorkSafeError:
        forbidden = True
    gui_refused = False
    if not interactive_mode():
        try:
            run([sys.executable, "-c", "pass"], gui=True, timeout=5)
        except WorkSafeError:
            gui_refused = True
    else:
        gui_refused = True
    print(out.strip())
    print(f"below-normal priority + no console window: {'PASS' if ok else 'FAIL'}")
    print(f"original game launch refused:              {'PASS' if forbidden else 'FAIL'}")
    print(f"GUI refused in WORK_SAFE_MODE:             {'PASS' if gui_refused else 'FAIL'}")
    print(f"max parallel jobs: {max_jobs()} of {os.cpu_count()} cores; window {window_size()}")
    return 0 if (ok and forbidden and gui_refused) else 1


if __name__ == "__main__":
    if "--selftest" in sys.argv:
        sys.exit(_selftest())
    cfg = load_config()
    print(json.dumps({k: v for k, v in cfg.items() if not k.startswith("_") and k != "interactive_test_mode"}, indent=2))
    print(f"effective max jobs: {max_jobs(cfg)}")
