#!/usr/bin/env python3
"""
difftest_game.py - differential test of REAL game functions:
original x86 bytes under Unicorn  vs  PCRecomp lift32_cpu output compiled to
native code against the portable guest-memory runtime.

PCRecomp's own tools/lift/difftest.py checks single instructions through the
global-register lifter (lift32.py). This checks whole functions of hitman2.exe
through the CPU-struct lifter this project actually uses, on the 64-bit host,
with guest memory behind guest_mem.h - i.e. the configuration that ships.

Pipeline (everything headless, started through scripts/worksafe.py):

  1. pick functions from the disasm32 catalog, stratified by kind
  2. lift each one and its direct-call closure with lift32_cpu.Lifter
  3. compile lifted C + tests/difftest/harness.c with clang
  4. for each function, several pseudo-random machine states:
       run the original bytes in Unicorn      (reference)
       run the lifted C in the harness        (candidate)
  5. compare GPRs, ESP, every byte of memory either side changed, x87 depth
     and ST(0). EFLAGS are compared and reported separately: they are dead at
     a function boundary in every Microsoft calling convention.

Verdicts per vector:
  PASS          every strict field equal
  FAIL          a strict field differs
  FP_PRECISION  only ST(0) differs, by a few ulp (x87 80-bit vs double model)
  INCONCLUSIVE  the reference run itself could not complete (fault, left the
                lifted set, instruction budget) - says nothing about the lift
  LIFT_GAP      the lifted code hit RECOMP_TODO / an unresolved dispatch

A function is PASS only if it has at least one PASS vector and no FAIL.

    python scripts/difftest_game.py [--per-kind 30] [--vectors 6] [--seed 1]
"""
from __future__ import annotations

import argparse
import collections
import datetime
import json
import random
import shutil
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "scripts"))
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "third_party" / "pcrecomp" / "tools" / "lift"))

import pefile                                   # noqa: E402
from capstone import Cs, CS_ARCH_X86, CS_MODE_32  # noqa: E402
from capstone.x86 import X86_OP_MEM, X86_OP_REG, X86_OP_IMM  # noqa: E402
from unicorn import Uc, UcError, UC_ARCH_X86, UC_MODE_32, UC_HOOK_MEM_WRITE, UC_HOOK_BLOCK  # noqa: E402
from unicorn import x86_const as X  # noqa: E402

import worksafe                                 # noqa: E402
import toolchain                                # noqa: E402
import gen_portable_cpu                         # noqa: E402
sys.path.insert(0, str(ROOT / "recomp"))
import h2_lifter                                # noqa: E402

EXE = ROOT / "build" / "work" / "hitman2.norm.exe"
CATALOG = ROOT / "recomp" / "catalog" / "hitman2.functions.json"
GEN = ROOT / "recomp" / "generated" / "difftest"
BUILD = ROOT / "build" / "difftest"

STACK_LO, STACK_SIZE, ESP0 = 0x0FE00000, 0x00200000, 0x0FF00000
SCRATCH, SCRATCH_SIZE = 0x20000000, 0x00040000
SENTINEL = 0x7FFF0000          # return address of the function under test
STUB = 0x7FFE0000              # tiny code page used to read ST(0) out of Unicorn
INSN_BUDGET = 400_000
REG_NAMES = ["eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi"]
UC_REGS = [X.UC_X86_REG_EAX, X.UC_X86_REG_ECX, X.UC_X86_REG_EDX, X.UC_X86_REG_EBX,
           X.UC_X86_REG_ESP, X.UC_X86_REG_EBP, X.UC_X86_REG_ESI, X.UC_X86_REG_EDI]
FLAG_BITS = {"cf": 0, "pf": 2, "af": 4, "zf": 6, "sf": 7, "of": 11}


def page_down(a): return a & ~0xFFF
def page_up(a): return (a + 0xFFF) & ~0xFFF


# --------------------------------------------------------------------------- image
class Image:
    def __init__(self, path: Path):
        self.pe = pefile.PE(str(path), fast_load=True)
        self.base = self.pe.OPTIONAL_HEADER.ImageBase
        self.size = self.pe.OPTIONAL_HEADER.SizeOfImage
        self.regions = []                       # (va, bytes) page-padded, per section
        for s in self.pe.sections:
            va = self.base + s.VirtualAddress
            n = page_up(max(s.Misc_VirtualSize, s.SizeOfRawData))
            data = s.get_data()[:n].ljust(n, b"\0")
            self.regions.append((va, data, s.Name.rstrip(b"\0").decode()))
        text = next(r for r in self.regions if r[2] == ".text")
        self.text_lo, self.text_hi = text[0], text[0] + len(text[1])

    def read(self, va, n):
        return self.pe.get_data(va - self.base, n)


# --------------------------------------------------------------------------- selection
def classify(img: Image, md: Cs, f: dict):
    """Return (kind, reject_reason). Facts from decoding the function's own bytes."""
    code = img.read(f["address"], f["size"])
    has_fpu = has_call = has_rep = has_jt = False
    n = 0
    for ins in md.disasm(code, f["address"]):
        n += 1
        m = ins.mnemonic
        ops = ins.operands
        if m in ("call", "jmp") and ops and ops[0].type in (X86_OP_REG, X86_OP_MEM):
            if m == "jmp" and ops[0].type == X86_OP_MEM and ops[0].mem.index and ops[0].mem.scale == 4:
                has_jt = True
            else:
                return None, "indirect call/jmp"
        if m == "call": has_call = True
        if m in ("int", "int3", "in", "out", "hlt", "sysenter", "iretd", "retf"): return None, "system insn"
        # An explicit fs:/gs: prefix is TIB access (SEH frames); it needs a simulated
        # TIB, which this harness does not provide. (capstone also reports the
        # IMPLICIT es:/ds: of string instructions as a segment - not a reason.)
        if ins.prefix[1] in (0x64, 0x65): return None, "fs/gs (TIB) access"
        if m[0] == "f" and m not in ("fs",): has_fpu = True
        if m.startswith("rep"): has_rep = True
    if n == 0 or sum(i.size for i in md.disasm(code, f["address"])) != f["size"]:
        return None, "does not decode linearly"
    kind = ("switch" if has_jt else "calls" if has_call else "x87" if has_fpu
            else "string" if has_rep else "integer")
    return kind, None


def select(img: Image, catalog: list, per_kind: int, exports: dict):
    md = Cs(CS_ARCH_X86, CS_MODE_32); md.detail = True
    by_addr = {f["address"]: f for f in catalog}
    kinds = collections.defaultdict(list)
    rejects = collections.Counter()
    info = {}
    for f in catalog:
        if f.get("is_thunk") or not (6 <= f["size"] <= 700):
            rejects["size/thunk"] += 1; continue
        if not (img.text_lo <= f["address"] < img.text_hi):
            rejects["outside .text"] += 1; continue
        kind, why = classify(img, md, f)
        if why:
            rejects[why] += 1; continue
        info[f["address"]] = kind
        kinds[kind].append(f["address"])

    def closure(a, limit=10):
        seen, todo = set(), [a]
        while todo:
            x = todo.pop()
            if x in seen: continue
            if x not in info: return None       # callee rejected or unknown
            seen.add(x)
            if len(seen) > limit: return None
            todo += by_addr[x].get("calls_to", [])
        return seen

    picked, closures = [], {}
    # exported (named) functions first, then an even spread over the address range
    for kind in ("integer", "x87", "string", "switch", "calls"):
        pool = sorted(kinds[kind])
        named = [a for a in pool if a in exports]
        rest = [a for a in pool if a not in exports]
        step = max(1, len(rest) // max(1, per_kind))
        order = named[:per_kind // 2] + rest[::step]
        got = 0
        for a in order:
            if got >= per_kind: break
            cl = closure(a)
            if cl is None:
                rejects["call closure leaves testable set"] += 1; continue
            closures[a] = cl; picked.append((a, kind)); got += 1
    return picked, closures, {k: len(v) for k, v in kinds.items()}, rejects


# --------------------------------------------------------------------------- lifting
def lift(img: Image, catalog: list, addrs: set):
    by_addr = {f["address"]: f for f in catalog}
    # the exe has no .reloc; the guest image sits at its preferred base
    L = h2_lifter.make_lifter(EXE, img.base, img.size, img.read)
    chunks = ["/* GENERATED from the game binary - not redistributable, never commit */",
              '#include "harness_hooks.h"   /* RECOMP_TODO + x87 stack accounting, shared with harness.c */',
              '#include "cpu.h"', "void dispatch(CPU *c, uint32_t va);", "void dispatch_jmp(CPU *c, uint32_t va);", ""]
    ok, errors = [], {}
    for a in sorted(addrs):
        f = by_addr[a]
        try:
            chunks.append(L.lift_function(img.read(a, f["size"]), a)); chunks.append("")
            ok.append(a)
        except Exception as ex:                 # a lifter exception is a finding, keep going
            errors[a] = f"{type(ex).__name__}: {ex}"
    GEN.mkdir(parents=True, exist_ok=True)
    (GEN / "lifted.c").write_text("\n".join(chunks), encoding="utf-8")
    (GEN / "lifted_list.h").write_text(
        "/* GENERATED */\n#define LIFTED_FUNCS(X) \\\n" + " \\\n".join(f"    X({a:08X})" for a in ok) + "\n",
        encoding="utf-8")
    return ok, errors


# --------------------------------------------------------------------------- vectors
def make_scratch(rng: random.Random) -> bytes:
    out = bytearray()
    for _ in range(SCRATCH_SIZE // 4):
        r = rng.random()
        if r < 0.35:   v = SCRATCH + 0x8000 + (rng.randrange(0, SCRATCH_SIZE - 0x10000) & ~3)
        elif r < 0.65: v = struct.unpack("<I", struct.pack("<f", rng.uniform(-200.0, 200.0)))[0]
        elif r < 0.90: v = rng.randrange(0, 48)
        else:          v = rng.getrandbits(32)
        out += struct.pack("<I", v)
    return bytes(out)


def rand_value(rng: random.Random) -> int:
    r = rng.random()
    if r < 0.45: return SCRATCH + 0x8000 + (rng.randrange(0, SCRATCH_SIZE - 0x10000) & ~0xF)
    if r < 0.65: return struct.unpack("<I", struct.pack("<f", rng.uniform(-100.0, 100.0)))[0]
    if r < 0.90: return rng.randrange(0, 40)
    return rng.getrandbits(32)


def rand_double(rng: random.Random) -> float:
    r = rng.random()
    if r < 0.30: return rng.uniform(-1000.0, 1000.0)
    if r < 0.55: return rng.randrange(-50, 50) + 0.5          # ties: rounding-mode sensitive
    if r < 0.75: return float(rng.randrange(-100, 100))
    if r < 0.90: return rng.uniform(-1.0, 1.0)
    return rng.choice([3.0e9, -3.0e9, 1.0e19, 2147483647.75, -2147483648.5, 0.0])   # int-range edges


def make_vector(rng: random.Random, v: int):
    """Vector v of a function. Some vectors enter with values already on the
    x87 stack: CRT helpers such as _ftol take their argument in ST(0)."""
    fp = [rand_double(rng) for _ in range((0, 1, 2, 0, 1, 3, 2, 1)[v % 8])]
    regs = {n: rand_value(rng) for n in REG_NAMES}
    regs["ecx"] = SCRATCH + 0x8000 + (rng.randrange(0, SCRATCH_SIZE - 0x10000) & ~0xF)   # plausible `this`
    regs["esp"] = ESP0
    regs["ebp"] = ESP0 + 0x400
    stack = struct.pack("<I", SENTINEL) + b"".join(struct.pack("<I", rand_value(rng)) for _ in range(63))
    return regs, stack, fp


# --------------------------------------------------------------------------- reference
class Reference:
    def __init__(self, img: Image, scratch: bytes):
        self.mu = mu = Uc(UC_ARCH_X86, UC_MODE_32)
        self.pristine = {}
        for va, data, _ in img.regions:
            mu.mem_map(va, len(data)); mu.mem_write(va, data); self.pristine[va] = data
        mu.mem_map(STACK_LO, STACK_SIZE)
        self.pristine[STACK_LO] = bytes(STACK_SIZE)
        mu.mem_map(SCRATCH, SCRATCH_SIZE); mu.mem_write(SCRATCH, scratch)
        self.pristine[SCRATCH] = scratch
        mu.mem_map(SENTINEL, 0x1000); mu.mem_write(SENTINEL, b"\xf4" * 16)
        mu.mem_map(STUB, 0x1000)
        # fst qword ptr [STUB+0x100]
        mu.mem_write(STUB, b"\xdd\x15" + struct.pack("<I", STUB + 0x100))
        # fld qword ptr [STUB+0x108]
        mu.mem_write(STUB + 0x10, b"\xdd\x05" + struct.pack("<I", STUB + 0x108))
        self.dirty, self.blocks = set(), []
        mu.hook_add(UC_HOOK_MEM_WRITE, self._on_write)
        mu.hook_add(UC_HOOK_BLOCK, self._on_block)
        self.starts = sorted(self.pristine)

    def _on_write(self, mu, access, addr, size, value, user):
        for k in range(size): self.dirty.add(addr + k)

    def _on_block(self, mu, addr, size, user):
        self.blocks.append(addr)

    def _orig(self, a):
        for s in self.starts:
            d = self.pristine[s]
            if s <= a < s + len(d): return s, d
        return None, None

    def run(self, va, regs, stack, fp, ranges):
        mu = self.mu
        # undo the previous case
        for a in self.dirty:
            s, d = self._orig(a)
            if d is not None: mu.mem_write(a, d[a - s:a - s + 1])
        self.dirty.clear(); self.blocks.clear()
        mu.mem_write(ESP0, stack)
        base = {}                                # memory as it is at function entry
        for k in range(len(stack)): base[ESP0 + k] = stack[k]
        for n, r in zip(REG_NAMES, UC_REGS): mu.reg_write(r, regs[n])
        mu.reg_write(X.UC_X86_REG_EFLAGS, 0x202)
        mu.reg_write(X.UC_X86_REG_FPCW, 0x027F)   # Win32 process default: 53-bit precision, round to nearest
        mu.reg_write(X.UC_X86_REG_FPSW, 0)
        mu.reg_write(X.UC_X86_REG_FPTAG, 0xFFFF)
        for v in fp:                              # last one pushed ends up in ST(0)
            mu.mem_write(STUB + 0x108, struct.pack("<d", v))
            mu.emu_start(STUB + 0x10, STUB + 0x16)
        for n, r in zip(REG_NAMES, UC_REGS): mu.reg_write(r, regs[n])
        self.dirty.clear(); self.blocks.clear()
        try:
            mu.emu_start(va, SENTINEL, count=INSN_BUDGET)
        except UcError as ex:
            return {"status": f"fault: {ex} at eip={mu.reg_read(X.UC_X86_REG_EIP):#x}"}
        if mu.reg_read(X.UC_X86_REG_EIP) != SENTINEL:
            return {"status": "instruction budget exhausted"}
        for b in self.blocks:
            if b != SENTINEL and not any(lo <= b < hi for lo, hi in ranges):
                return {"status": f"executed {b:#x}, outside the lifted set"}
        if (mu.reg_read(X.UC_X86_REG_FPSW) & 0x41) == 0x41:
            # IE+SF: the function popped an empty x87 stack (or overflowed it).
            # It expects FPU inputs this vector did not supply; not a valid run.
            return {"status": "x87 stack fault in reference (function expects other FPU inputs)"}
        out = {"status": "ok", "regs": [mu.reg_read(r) for r in UC_REGS]}
        fl = mu.reg_read(X.UC_X86_REG_EFLAGS)
        out["flags"] = {n: (fl >> b) & 1 for n, b in FLAG_BITS.items()}
        mem = {}
        for a in self.dirty:
            s, d = self._orig(a)
            was = base.get(a, d[a - s] if d is not None else None)
            now = mu.mem_read(a, 1)[0]
            if was is not None and now != was: mem[a] = now
        out["mem"] = mem
        top = (mu.reg_read(X.UC_X86_REG_FPSW) >> 11) & 7
        out["fpu_depth"] = (8 - top) & 7
        if out["fpu_depth"]:
            keep = set(self.dirty)
            mu.emu_start(STUB, STUB + 6)
            out["st0"] = struct.unpack("<Q", bytes(mu.mem_read(STUB + 0x100, 8)))[0]
            self.dirty = keep
        return out


# --------------------------------------------------------------------------- candidate
def write_job(path: Path, img: Image, scratch: bytes, cases):
    with open(path, "wb") as f:
        regions = [(va, data, 1) for va, data, _ in img.regions]
        regions += [(STACK_LO, bytes(STACK_SIZE), 1), (SCRATCH, scratch, 1)]
        f.write(struct.pack("<III", 0x54443248, len(regions), len(cases)))
        for va, data, flags in regions:
            f.write(struct.pack("<III", va, len(data), flags)); f.write(data)
        for va, regs, stack, fp in cases:
            f.write(struct.pack("<I8II", va, *[regs[n] for n in REG_NAMES], 0x202))
            f.write(struct.pack("<I", 1)); f.write(struct.pack("<II", ESP0, len(stack))); f.write(stack)
            f.write(struct.pack("<I", len(fp))); f.write(b"".join(struct.pack("<d", v) for v in fp))


def parse_harness(text: str):
    res, cur = {}, None
    begun = None
    for line in text.splitlines():
        p = line.split()
        if not p: continue
        if p[0] == "BEGIN": begun = int(p[1])
        elif p[0] == "END":
            i = int(p[1])
            cur = {"status": p[2], "regs": [int(x, 16) for x in p[3:11]],
                   "flags": dict(zip(["cf", "pf", "af", "zf", "sf", "of"], map(int, p[11:17]))),
                   "fpu_top": int(p[17]), "st0": int(p[18], 16), "mem": {}}
            res[i] = cur
        elif p[0] == "M" and cur is not None:
            a = int(p[1], 16)
            for k in range(0, len(p[2]), 2): cur["mem"][a + k // 2] = int(p[2][k:k + 2], 16)
        elif p[0] == "DONE":
            res[int(p[1])]["done"] = True; cur = None; begun = None
    return res, begun                           # begun != None -> the harness died inside that case


def run_harness(exe: Path, job: Path, n_cases: int):
    """Run all cases; if the process dies inside one, record it and resume after it."""
    results, crashed, first = {}, {}, 0
    while first < n_cases:
        try:
            r = worksafe.run([exe, job, first, n_cases - 1], log_name="difftest_harness", timeout=300, capture=True)
            text, timed_out = r.stdout or "", False
        except worksafe.WorkSafeError:
            text = (worksafe.LOGS / "difftest_harness.log").read_text(encoding="utf-8", errors="replace")
            timed_out = True
        res, begun = parse_harness(text)
        results.update({i: v for i, v in res.items() if v.get("done")})
        if begun is None and not timed_out: break
        if begun is None: break
        crashed[begun] = "timeout (possible endless loop)" if timed_out else f"host crash (exit {r.returncode})"
        first = begun + 1
    return results, crashed


def run_android(serial: str):
    """Run the arm64-v8a harness on an attached device over adb. Headless: a
    command-line binary in /data/local/tmp, nothing appears on the device
    screen. The directory is removed afterwards. Returns (results, device)."""
    adb = [toolchain.find_adb(), "-s", serial]
    d = "/data/local/tmp/h2ae_difftest"

    def sh(cmd, name, timeout=120):
        return (worksafe.run(adb + ["shell", cmd], log_name=name, timeout=timeout, capture=True).stdout or "")

    def prop(p):
        lines = [l for l in sh(f"getprop {p}", "adb_getprop").splitlines() if l and not l.startswith("#")]
        return lines[-1].strip() if lines else "?"

    device = {"serial": serial, "model": prop("ro.product.model"), "abi": prop("ro.product.cpu.abi"),
              "android": prop("ro.build.version.release")}
    try:
        sh(f"mkdir -p {d}", "adb_mkdir")
        for f in ("harness_arm64", "job.bin"):
            worksafe.run(adb + ["push", BUILD / f, f"{d}/{f}"], log_name="adb_push", timeout=300, check=True)
        sh(f"cd {d} && chmod 755 harness_arm64 && ./harness_arm64 job.bin > out.txt 2>err.txt; echo rc=$? >> err.txt",
           "adb_run", timeout=600)
        worksafe.run(adb + ["pull", f"{d}/out.txt", BUILD / "android_out.txt"], log_name="adb_pull", timeout=300, check=True)
        worksafe.run(adb + ["pull", f"{d}/err.txt", BUILD / "android_err.txt"], log_name="adb_pull2", timeout=120)
    finally:
        sh(f"rm -rf {d}", "adb_cleanup")
    res, _ = parse_harness((BUILD / "android_out.txt").read_text(encoding="utf-8", errors="replace"))
    return {i: v for i, v in res.items() if v.get("done")}, device


def ulp_distance(a: int, b: int) -> int:
    def key(u): return u if u < (1 << 63) else (1 << 63) - u
    return abs(key(a) - key(b))


def compare(ref, cand):
    if cand["status"] == "x87-stack-misuse":
        return "INCONCLUSIVE", ["vector does not supply the x87 inputs this function reads"]
    if cand["status"].startswith("fault"):      # reference completed, lifted code faulted: a real divergence
        return "FAIL", [cand["status"]]
    if cand["status"] != "ok": return "LIFT_GAP", [cand["status"]]
    diffs = []
    for n, a, b in zip(REG_NAMES, ref["regs"], cand["regs"]):
        if a != b: diffs.append(f"{n}: ref={a:08X} lifted={b:08X}")
    if ref["mem"] != cand["mem"]:
        keys = sorted(set(ref["mem"]) | set(cand["mem"]))
        bad = [k for k in keys if ref["mem"].get(k) != cand["mem"].get(k)]
        diffs.append(f"memory: {len(bad)} byte(s) differ, first at {bad[0]:08X} "
                     f"(ref={ref['mem'].get(bad[0])} lifted={cand['mem'].get(bad[0])})")
    cand_depth = (8 - cand["fpu_top"]) & 7
    fp_only = False
    if ref["fpu_depth"] != cand_depth:
        diffs.append(f"x87 depth: ref={ref['fpu_depth']} lifted={cand_depth}")
    elif ref["fpu_depth"] and ref["st0"] != cand["st0"]:
        d = ulp_distance(ref["st0"], cand["st0"])
        if not diffs and d <= 4: fp_only = True
        diffs.append(f"st0: ref={ref['st0']:016X} lifted={cand['st0']:016X} ({d} ulp)")
    if not diffs: return "PASS", []
    return ("FP_PRECISION" if fp_only else "FAIL"), diffs


# --------------------------------------------------------------------------- main
def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--per-kind", type=int, default=30)
    ap.add_argument("--vectors", type=int, default=6)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--only", help="comma-separated hex addresses to test instead of sampling")
    ap.add_argument("--android", metavar="SERIAL",
                    help="also run the arm64-v8a harness on this adb device and compare it to the same reference")
    args = ap.parse_args()

    for need in (EXE, CATALOG):
        if not need.exists():
            raise SystemExit(f"missing {need.relative_to(ROOT)} - run tools/prepare_image.py and disasm32 first")
    img = Image(EXE)
    catalog = json.loads(CATALOG.read_text(encoding="utf-8"))["functions"]
    by_addr = {f["address"]: f for f in catalog}
    exports = {}
    img.pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_EXPORT"]])
    for s in img.pe.DIRECTORY_ENTRY_EXPORT.symbols:
        if s.name: exports[img.base + s.address] = s.name.decode("latin1")

    picked, closures, pool_sizes, rejects = select(img, catalog, args.per_kind, exports)
    if args.only:
        want = {int(x, 16) for x in args.only.split(",")}
        picked = [(a, k) for a, k in picked if a in want] or [(a, "manual") for a in want]
        for a in want: closures.setdefault(a, {a})
    all_addrs = set().union(*[closures[a] for a, _ in picked]) if picked else set()
    print(f"[select] testable pool by kind: {pool_sizes}")
    print(f"[select] picked {len(picked)} functions, {len(all_addrs)} to lift (with call closures)")

    lifted, lift_errors = lift(img, catalog, all_addrs)
    print(f"[lift] {len(lifted)} lifted, {len(lift_errors)} lifter exceptions")
    lifted_set = set(lifted)

    gen_portable_cpu.generate()
    BUILD.mkdir(parents=True, exist_ok=True)
    exe = BUILD / "harness.exe"
    common = ["-O1", "-std=c11", "-fno-strict-aliasing", "-fwrapv", "-w",
              "-I", ROOT / "build" / "generated" / "include", "-I", ROOT / "runtime", "-I", GEN,
              "-I", ROOT / "tests" / "difftest",
              ROOT / "tests" / "difftest" / "harness.c", GEN / "lifted.c", ROOT / "runtime" / "guest_mem.c"]
    cc = toolchain.host_cc() + ["-D_CRT_SECURE_NO_WARNINGS"] + common + ["-o", exe]
    r = worksafe.run(cc, log_name="difftest_build_host", timeout=900)
    if r.returncode != 0:
        print("[build] host build FAILED, see logs/difftest_build_host.log"); return 1
    print(f"[build] {exe.relative_to(ROOT)}")

    # The same sources for the real target. Built here so that "compiles for
    # ARM64" is checked on every run; it is only *executed* when a device is
    # attached (scripts/difftest_android.py), and is not counted as tested here.
    arm64_built = False
    try:
        acc = toolchain.android_cc() + common + ["-lm", "-static-libstdc++", "-o", BUILD / "harness_arm64"]
        arm64_built = worksafe.run(acc, log_name="difftest_build_arm64", timeout=900).returncode == 0
        print(f"[build] arm64-v8a harness: {'OK' if arm64_built else 'FAILED, see logs/difftest_build_arm64.log'}")
    except toolchain.ToolchainError as ex:
        print(f"[build] arm64-v8a harness skipped: {ex}")

    rng = random.Random(args.seed)
    scratch = make_scratch(rng)
    ref = Reference(img, scratch)
    cases, meta = [], []
    for a, kind in picked:
        if not closures[a] <= lifted_set: continue
        ranges = [(x, x + by_addr[x]["size"]) for x in closures[a]]
        for v in range(args.vectors):
            regs, stack, fp = make_vector(rng, v)
            cases.append((a, regs, stack, fp)); meta.append((a, kind, v, ranges))
    print(f"[ref] running {len(cases)} vectors under Unicorn ...")
    refs = [ref.run(a, regs, stack, fp, m[3]) for (a, regs, stack, fp), m in zip(cases, meta)]

    job = BUILD / "job.bin"
    write_job(job, img, scratch, cases)
    print("[cand] running lifted code ...")
    cands, crashed = run_harness(exe, job, len(cases))

    per_func = collections.OrderedDict()
    for a, kind in picked:
        per_func[a] = {"address": f"{a:#010x}", "kind": kind, "name": exports.get(a),
                       "size": by_addr[a]["size"], "closure": len(closures[a]),
                       "vectors": collections.Counter(), "details": []}
        if not closures[a] <= lifted_set:
            bad = sorted(closures[a] - lifted_set)
            per_func[a]["vectors"]["LIFT_GAP"] += 1
            per_func[a]["details"].append("lifter exception: " + "; ".join(lift_errors.get(x, "?") for x in bad))
    flag_only = 0
    for i, (m, rf) in enumerate(zip(meta, refs)):
        a = m[0]; pf = per_func[a]
        if rf["status"] != "ok":
            pf["vectors"]["INCONCLUSIVE"] += 1
            if len(pf["details"]) < 3: pf["details"].append(f"v{m[2]} reference: {rf['status']}")
            continue
        if i in crashed:
            verdict, diffs = "FAIL", [crashed[i]]
        elif i not in cands:
            verdict, diffs = "FAIL", ["no result from harness"]
        else:
            verdict, diffs = compare(rf, cands[i])
            if verdict == "PASS" and rf["flags"] != cands[i]["flags"]: flag_only += 1
        pf["vectors"][verdict] += 1
        if verdict != "PASS" and len(pf["details"]) < 4:
            pf["details"].append(f"v{m[2]} {verdict}: " + " | ".join(diffs[:4]))

    # ---- the same job on real ARM64 hardware ----
    arm = None
    if args.android and arm64_built:
        print(f"[arm64] running on device {args.android} ...")
        try:
            acands, device = run_android(args.android)
        except (worksafe.WorkSafeError, toolchain.ToolchainError, OSError) as ex:
            print(f"[arm64] NOT executed: {ex}")
            acands = None
    if args.android and arm64_built and acands is not None:
        av = collections.Counter(); same_as_host = 0; compared = 0; arm_fail_funcs = set()
        for i, (m, rf) in enumerate(zip(meta, refs)):
            if rf["status"] != "ok": continue
            if i not in acands:
                av["FAIL"] += 1; arm_fail_funcs.add(m[0]); continue
            verdict, _ = compare(rf, acands[i]); av[verdict] += 1
            if verdict == "FAIL": arm_fail_funcs.add(m[0])
            if i in cands:
                compared += 1
                a, b = acands[i], cands[i]
                same_as_host += all(a[k] == b[k] for k in ("status", "regs", "flags", "fpu_top", "st0", "mem"))
        arm = {"device": device, "vector_verdicts": dict(av), "vectors_compared_with_host": compared,
               "vectors_bit_identical_to_host": same_as_host,
               "functions_with_a_failing_vector": sorted(f"{a:#010x}" for a in arm_fail_funcs)}
        print(f"[arm64] {device['model']} ({device['abi']}, Android {device['android']}): {dict(av)}; "
              f"{same_as_host}/{compared} vectors bit-identical to the x86-64 host run")

    known_path = ROOT / "tests" / "difftest" / "known_divergences.json"
    known = {int(k, 16): v for k, v in json.loads(known_path.read_text(encoding="utf-8")).items()
             if not k.startswith("_")} if known_path.exists() else {}

    def func_verdict(pf):
        v = pf["vectors"]
        if v["FAIL"]:
            a = int(pf["address"], 16)
            if a in known:                      # understood and documented; still not a PASS
                pf["known_divergence"] = known[a]; return "KNOWN_DIVERGENCE"
            return "FAIL"
        if v["LIFT_GAP"]: return "LIFT_GAP"
        if v["PASS"]: return "PASS" if not v["FP_PRECISION"] else "PASS_FP_PRECISION"
        if v["FP_PRECISION"]: return "FP_PRECISION"
        return "INCONCLUSIVE"

    summary = collections.Counter(); by_kind = collections.defaultdict(collections.Counter)
    vec_total = collections.Counter()
    for pf in per_func.values():
        pf["verdict"] = func_verdict(pf); summary[pf["verdict"]] += 1
        by_kind[pf["kind"]][pf["verdict"]] += 1
        vec_total.update(pf["vectors"]); pf["vectors"] = dict(pf["vectors"])

    report = {
        "date": datetime.datetime.now().isoformat(timespec="seconds"),
        "binary_sha256_of_original": json.loads((EXE.parent / (EXE.name + ".json")).read_text())["source_sha256"],
        "pcrecomp_lifter": "tools/lift/lift32_cpu.py",
        "host": "x86_64 (clang), guest memory via runtime/guest_mem (4 GiB reservation)",
        "arm64_v8a_harness_compiled": arm64_built,
        "arm64_v8a_harness_executed": arm is not None,
        "arm64_v8a": arm,
        "seed": args.seed, "vectors_per_function": args.vectors,
        "testable_pool_by_kind": pool_sizes, "selection_rejects": dict(rejects),
        "functions_tested": len(per_func), "functions_lifted_incl_callees": len(lifted),
        "function_verdicts": dict(summary), "by_kind": {k: dict(v) for k, v in by_kind.items()},
        "vector_verdicts": dict(vec_total), "pass_vectors_with_flag_differences": flag_only,
        "functions": list(per_func.values()),
    }
    (BUILD / "report.json").write_text(json.dumps(report, indent=1), encoding="utf-8")
    print(f"[result] functions: {dict(summary)}")
    print(f"[result] vectors:   {dict(vec_total)}  (PASS vectors whose dead EFLAGS differ: {flag_only})")
    for k, v in by_kind.items(): print(f"           {k:<8} {dict(v)}")
    print(f"[result] report: {(BUILD / 'report.json').relative_to(ROOT)}")
    return 0 if not summary["FAIL"] else 1


if __name__ == "__main__":
    sys.exit(main())
