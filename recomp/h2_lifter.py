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
from capstone.x86 import X86_OP_IMM, X86_OP_MEM, X86_OP_REG     # noqa: E402


_STEP = re.compile(r"c->(esi|edi) \+= ([124]);")

_TERMINATORS = {"ret", "retn", "retf", "iret", "iretd", "hlt", "ud2", "int3"}


class _ControlFlowDecoder:
    """Stands in for the Capstone object inside Lifter so that a function is decoded by following
    control flow instead of linearly.

    Why: hand-written CRT assembly (memcpy/memmove/strlen...) embeds jump tables and constant data
    between instructions. A linear sweep decodes those bytes as instructions, loses alignment, and the
    real instructions after the table are no longer at the addresses the table points to - the stock
    lift then turns every table target into a `dispatch` to an address with no lifted body
    (hitman2.exe memmove at 0x0059E830, found by the boot probe).

    Decoding starts at the function start and follows fall-through, direct jump targets and the
    targets of `jmp [reg*4 + table]`, staying inside the function's extent. Bytes never reached are
    not decoded. Everything else about lifting is the stock behaviour: this object delegates to the real
    Capstone for any other attribute."""

    def __init__(self, cs, lifter):
        self._cs = cs
        self._lifter = lifter
        self.func_start = None

    def __getattr__(self, name):
        return getattr(self._cs, name)

    def disasm(self, code, addr):
        if self.func_start is None or addr != self.func_start:
            return iter(())                      # the stock "finish a cut instruction" re-decode: not needed here
        lo, hi = addr, addr + len(code)
        read_va = self._lifter.read_va
        seen = {}
        work = [addr]
        while work:
            a = work.pop()
            while lo <= a < hi and a not in seen:
                fetch = code[a - lo:a - lo + 15]
                if len(fetch) < 15 and read_va:  # an instruction cut by the extent: read the rest from the image
                    try:
                        fetch = read_va(a, 15)
                    except Exception:
                        pass
                ins = next(self._cs.disasm(fetch, a, 1), None)
                if ins is None:
                    break
                seen[a] = ins
                last = ins.mnemonic.split()[-1]
                nxt = a + ins.size
                ops = ins.operands
                if last in _TERMINATORS:
                    break
                is_jump = last.startswith("j") or last in ("loop", "loope", "loopne", "loopz", "loopnz")
                if not is_jump:
                    a = nxt
                    continue
                if ops and ops[0].type == X86_OP_IMM:
                    work.append(ops[0].imm)
                elif last == "jmp" and ops and ops[0].type == X86_OP_MEM:
                    mem = ops[0].mem
                    table = mem.disp & 0xffffffff
                    if mem.index and mem.scale == 4 and self._lifter.in_image(table):
                        work.extend(t for t in self._lifter.resolve_jumptable(table) if lo <= t < hi)
                if last == "jmp":
                    break                        # unconditional: no fall-through
                a = nxt
        return iter([seen[k] for k in sorted(seen)])


class H2Lifter(lift32_cpu.Lifter):
    def __init__(self, *a, **kw):
        super().__init__(*a, **kw)
        self.md = _ControlFlowDecoder(self.md, self)
        self._extent = (0, 0)

    def lift_function(self, code, start):
        self.md.func_start = start
        self._extent = (start, start + len(code))
        try:
            return super().lift_function(code, start)
        finally:
            self.md.func_start = None

    def resolve_jumptable(self, table_va):
        """Jump-table arms for `jmp [reg*4 + table_va]`.

        Stock: the run of in-image dwords starting AT table_va. That is wrong when the index is
        negative: MSVC's memmove ends with `sub ecx,4; jb L; ...; L: jmp [ecx*4 + 0x59E988]` where
        ecx is -4..-1, so the real table lies in the four dwords BEFORE the base. Reading forward
        gives bytes of code, no arms, and every arm becomes a `dispatch` to an address inside this very
        function (no lifted body: 'unresolved dispatch' at 0x0059E9B0).
        Now: if the forward run is empty, take the run of dwords just before table_va that point
        inside this function's extent."""
        fwd = super().resolve_jumptable(table_va)
        if fwd:
            return fwd
        # Not a table at the base. Accept, in this order, a run of in-function code pointers that
        # starts one slot after the base (index 0 never used: memmove's `and eax,3` after
        # `test edi,3 / jne` is 1..3), or ends just before it (negative indices).
        return self._run(table_va + 4, 4) or self._run(table_va - 4, -4)

    def _run(self, va, step, limit=64):
        lo, hi = self._extent
        out = []
        for _ in range(limit):
            try:
                v = int.from_bytes(self.read_va(va, 4), "little")
            except Exception:
                break
            if not (lo <= v < hi):
                break
            out.append(v)
            va += step
        return out

    def translate(self, insn, labels):
        m = insn.mnemonic
        if m == "std": return ["c->df = 1;"]
        if m == "cld": return ["c->df = 0;"]
        # Instructions the stock lifter leaves as RECOMP_TODO although they are plain (found by the
        # whole-image census, scripts/lift_image.py: pushal 114, popal 22, xlatb 33, clc 18, stc 7, cmc 4,
        # rdtsc 9 sites in hitman2.exe).
        if m in ("pushal", "pushad"):
            # EAX ECX EDX EBX, the ESP value from BEFORE the instruction, EBP ESI EDI.
            return ["{ uint32_t _sp = c->esp; push32(c, c->eax); push32(c, c->ecx); push32(c, c->edx);"
                    " push32(c, c->ebx); push32(c, _sp); push32(c, c->ebp); push32(c, c->esi); push32(c, c->edi); }"]
        if m in ("popal", "popad"):
            # The saved ESP slot is read and discarded.
            return ["c->edi = pop32(c); c->esi = pop32(c); c->ebp = pop32(c); (void)pop32(c);"
                    " c->ebx = pop32(c); c->edx = pop32(c); c->ecx = pop32(c); c->eax = pop32(c);"]
        if m == "clc": return ["c->cf = 0;"]
        if m == "stc": return ["c->cf = 1;"]
        if m == "cmc": return ["c->cf = !c->cf;"]
        if m == "xlatb": return ["SET8L(c->eax, rd8(c->ebx + R8L(c->eax)));"]
        if m == "rdtsc": return ["{ uint64_t _t = h2_rdtsc(); c->eax = (uint32_t)_t; c->edx = (uint32_t)(_t >> 32); }"]
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
