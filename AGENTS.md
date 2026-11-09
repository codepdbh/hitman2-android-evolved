# AGENTS.md — permanent rules for any agent working in this repository

Project: **Hitman 2: Android Evolved** — a native ARM64 Android port of
*Hitman 2: Silent Assassin* (PC, 2002, Glacier engine) by static recompilation
with [PCRecomp](https://github.com/sp00nznet/pcrecomp).

## 1. The owner is working on this computer. Do not disturb them.

This rule outranks every technical goal. `WORK_SAFE_MODE` is the default.

Never, on this Windows machine:

- run anything fullscreen, maximised, or in an exclusive display mode;
- change the monitor resolution or any display setting;
- take window focus (`SetForegroundWindow`, activating `ShowWindow` calls);
- capture, grab or synthesise global keyboard/mouse input, or move the cursor;
- open visible console windows or modal dialogs;
- close, minimise or alter the owner's applications;
- reboot, or change global system configuration;
- install anything that needs administrator rights or a reboot;
- saturate the CPU, RAM or GPU.

**Never launch `hitman2.exe`, `config.exe` or the uninstaller.** The original
game sets its own display mode and grabs DirectInput; it cannot be made to obey
these rules. Analyse it statically.

### How to comply

- Start every process through `scripts/worksafe.py` (`worksafe.run(...)`). It
  applies `CREATE_NO_WINDOW`, `SW_HIDE`, below-normal priority at creation, a
  timeout, a log file, and refuses GUI and original-game launches.
  `python scripts/worksafe.py --selftest` proves the flags apply.
- From an interactive PowerShell tool call, first lower the shell's own
  priority so children inherit it:
  `(Get-Process -Id $PID).PriorityClass = 'BelowNormal'`.
- Parallel builds: at most `worksafe.max_jobs()` jobs (25 % of cores, cap 4).
- Prefer headless tests. Ghidra only via `analyzeHeadless`.
- A graphical test needs `H2_INTERACTIVE_TEST_MODE=1` set explicitly by the
  owner. Even then: windowed, ≤ 960×540 (default 800×450), created
  non-activating (`WS_EX_NOACTIVATE` / `SW_SHOWNOACTIVATE` / `SWP_NOACTIVATE`),
  no input capture, placed in a screen corner, time-limited. If a technology
  cannot guarantee that, do not run it; leave it prepared for a manual run.
- Fullscreen is forbidden on Windows only. Normal immersive presentation on the
  Android device is fine.

## 2. Honesty about results

- Compiling is not passing. A lifted function is "tested" only when a
  differential test against the original x86 bytes passes.
- Never make an error disappear by stubbing success, swallowing an exception or
  disabling a check. Unimplemented behaviour must fail loudly and be recorded
  in `docs/KNOWN_ISSUES.md`.
- Every deliverable carries one status: Implementado / Compilado / Probado /
  Parcial / Bloqueado / Pendiente (see `docs/STATUS.md`). Evidence lives in
  `docs/evidence/`.
- Keep verified identifications and hypotheses separate
  (`analysis/` vs `analysis/hypotheses.md`).

## 3. Legal boundaries

- The game directory (`Hitman 2 Silent Assassin/`) is read-only input. Never
  modify the original binaries; work on copies under `build/`.
- Never commit game files, extracted assets, lifted C, catalogs derived from
  the binaries, or anything under `recomp/generated/`, `recomp/catalog/`,
  `analysis/raw/`. Run `python scripts/audit_repo.py` before any commit.
- Do not write code whose purpose is to defeat copy protection.
- Commits: the owner is the only author. No AI attribution trailers.

## 4. Engineering rules

- PCRecomp lives unmodified in `third_party/pcrecomp` (pinned in
  `config/dependencies.json`). Changes to it go in `patches/pcrecomp/` or are
  generated at build time (see `tools/gen_portable_cpu.py`).
- Check a tool's real CLI before using it. Do not invent flags.
- A guest (x86-32) address is **not** a host pointer. All guest memory access
  goes through `runtime/guest_mem.h`. No `(void*)(uintptr_t)guest_addr`.
- Game logic and Android glue stay in separate layers (`runtime/`, `platform/`,
  `renderer/`, `input/`, `audio/` vs `android/`).
- Do not touch the owner's other projects under `Documents/`; read them for
  reference only.
- No unbounded build/retry loops. If a phase is blocked, record the blocker and
  move to independent work.
