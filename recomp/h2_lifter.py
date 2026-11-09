#!/usr/bin/env python3
"""
h2_lifter.py - project overrides on top of PCRecomp's lift32_cpu.Lifter.

PCRecomp stays unmodified in third_party/. Where differential testing of real
hitman2.exe functions (scripts/difftest_game.py) showed the stock lifter
diverging from x86, the fix lives here as a subclass override, with the
evidence that motivated it. Each one is a candidate to send upstream.

Overrides
---------
x87 control word (`fldcw` / `fnstcw`)
    Stock: `fldcw` is ignored and `fnstcw` always stores 0x027F; `fist/fistp`
    and `frndint` always round to nearest.
    Why it matters here: hitman2.exe is MSVC 6 code. Every C `(int)x` on a
    float compiles to a call to `_ftol`, which is
        fnstcw [old]; or ah,0Ch; fldcw [new]; fistp qword [..]; fldcw [old]
    i.e. it selects TRUNCATION through the control word. Ignoring it turns
    every float->int cast in the game into round-to-nearest ((int)2.7 == 3).
    Now: the control word is CPU state (`c->fpu_cw`), and the integer stores
    and frndint honour its rounding-control field.

x87 integer store out of range
    Stock: `(int32_t)nearbyint(v)` - undefined behaviour in C when v does not
    fit, and in practice x86 hosts produce 0x80000000 while ARM64 saturates.
    Now: `h2_fist*` return the x87 "integer indefinite" explicitly, the same
    on every host.

`fnstsw`
    Stock: stores only the condition bits. Real hardware also stores TOP in
    bits 11-13, so AX differed from the reference after `fnstsw ax`.
    Now: TOP is merged in (`h2_fpu_sw`).

Direction flag (`std` / `cld`)
    Stock: `std` is a comment ("DF=1 unsupported; string ops assume forward").
    Evidence: the CRT's strrchr at 0x0059D7D0 does `std; repne scasb` to scan
    backwards; the stock lift scanned forwards and returned NULL where the
    original returns the match.
    Now: DF is CPU state (`c->df`) and every string instruction steps ESI/EDI
    through h2_dstep(). Not yet covered: DF is not part of pushfd/popfd.

The C helpers these emit are in runtime/h2_x87.h, appended to the generated
cpu.h by tools/gen_portable_cpu.py (which also adds the `fpu_cw`/`df` fields).
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "third_party" / "pcrecomp" / "tools" / "lift"))

import lift32_cpu                                   # noqa: E402
from capstone.x86 import X86_OP_MEM, X86_OP_REG     # noqa: E402


_STEP = re.compile(r"c->(esi|edi) \+= ([124]);")


class H2Lifter(lift32_cpu.Lifter):
    def translate(self, insn, labels):
        m = insn.mnemonic
        if m == "std": return ["c->df = 1;"]
        if m == "cld": return ["c->df = 0;"]
        out = super().translate(insn, labels)
        parts = m.split()
        if parts[-1] in lift32_cpu.STRING_OPS and (len(parts) == 1 or parts[0] in lift32_cpu.REP_PREFIXES):
            out = [_STEP.sub(r"c->\1 += h2_dstep(c, \2);", line) for line in out]
        return out

    def fpu(self, insn):
        m = insn.mnemonic
        ops = insn.operands
        memop = ops[0] if ops and ops[0].type == X86_OP_MEM else None

        if m in ("fnstsw", "fstsw"):
            if ops and ops[0].type == X86_OP_REG:
                return ["SET16(c->eax, h2_fpu_sw(c));"]
            return [self.wr(insn, ops[0], "h2_fpu_sw(c)")]
        if m in ("fnstcw", "fstcw") and memop is not None:
            return [self.wr(insn, memop, "(uint16_t)c->fpu_cw")]
        if m == "fldcw" and memop is not None:
            return [f"c->fpu_cw = {self.rd(insn, memop)};"]
        if m in ("fninit", "finit"):
            return ["c->fpu_cw = 0x037Fu; c->fpu_sw = 0; c->fpu_top = 0;"]
        if m in ("fist", "fistp") and memop is not None and memop.size in (2, 4, 8):
            fn = {2: "h2_fist16", 4: "h2_fist32", 8: "h2_fist64"}[memop.size]
            pop = " fpop(c);" if m == "fistp" else ""
            return [f"{fn}(c, {self.addr_expr(insn, memop)}, *fst(c, 0));{pop}"]
        if m == "frndint":
            return ["*fst(c, 0) = h2_round_rc(c, *fst(c, 0));"]
        return super().fpu(insn)


def make_lifter(pe_path, image_base: int, image_size: int, read_va, reloc_vas=None) -> H2Lifter:
    """Construct the project lifter. lift32_cpu reads IMAGE_BASE as a module
    global at construction time, so it is set here rather than left to callers."""
    lift32_cpu.IMAGE_BASE = image_base
    L = H2Lifter(str(pe_path), image_size, read_va)
    L.reloc_vas = set(reloc_vas or ())
    return L
