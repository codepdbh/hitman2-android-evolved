/*
 * h2_selftest.c - checks of the portable runtime itself, runnable anywhere.
 *
 * Contains NO game code: it exercises the guest address space and the CPU
 * model with hand-written cases whose x86 results are known. It is what the
 * Android diagnostics screen runs, and what tests/selftest_main.c runs on the
 * PC. It says nothing about whether the game runs.
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "recomp_prelude.h"
#include "h2_log.h"
#include "h2_selftest.h"

typedef struct { char *buf; size_t cap, len; int pass, fail; } Ctx;

static void out(Ctx *x, const char *fmt, ...)
{
    va_list ap;
    if (x->len + 1 >= x->cap) return;
    va_start(ap, fmt);
    int n = vsnprintf(x->buf + x->len, x->cap - x->len, fmt, ap);
    va_end(ap);
    if (n > 0) x->len += (size_t)n < x->cap - x->len ? (size_t)n : x->cap - x->len - 1;
}

static void check(Ctx *x, int ok, const char *name)
{
    if (ok) x->pass++; else x->fail++;
    out(x, "%s  %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) H2_ERROR(H2_LOG_ERROR, "selftest failed: %s", name);
}

static uint32_t fist32_with(uint32_t cw, double v)
{
    CPU c; memset(&c, 0, sizeof c);
    c.fpu_cw = cw;
    h2_fist32(&c, 0x00500000u, v);
    return rd32(0x00500000u);
}

int h2_selftest(char *report, size_t report_size)
{
    Ctx x = { report, report_size, 0, 0, 0 };
    CPU c;
    if (report_size) report[0] = 0;

    out(&x, "host pointer width: %u bits\n", (unsigned)(sizeof(void *) * 8));

    /* ---- guest address space ---- */
    check(&x, guest_mem_init() == 0, "reserve 4 GiB guest address space");
    if (!g_guest_base) { out(&x, "cannot continue without guest memory\n"); return x.fail ? x.fail : 1; }
    check(&x, guest_mem_commit(0x00400000u, 0x00200000u, GUEST_PROT_RW) == 0, "commit image range at guest 0x00400000");
    check(&x, guest_mem_commit(0xFFFFF000u, 0x1000u, GUEST_PROT_RW) == 0, "commit the last guest page (0xFFFFF000)");
    check(&x, (uint8_t *)guest_ptr(0x00401234u) == g_guest_base + 0x00401234u, "guest address maps to base + address");
    wr32(0x00401000u, 0xCAFEF00Du);
    check(&x, rd32(0x00401000u) == 0xCAFEF00Du && rd8(0x00401000u) == 0x0D && rd16(0x00401002u) == 0xCAFE,
          "little-endian guest reads/writes");
    wr32(0xFFFFFFFCu, 0x12345678u);
    check(&x, rd32(0xFFFFFFFCu) == 0x12345678u, "access at the very top of the guest space");
    check(&x, rd32(0x00500000u) == 0, "freshly committed guest memory is zero");

    /* ---- stack ---- */
    memset(&c, 0, sizeof c);
    c.esp = 0x00480000u;
    push32(&c, 0x11111111u); push32(&c, 0x22222222u);
    check(&x, c.esp == 0x0047FFF8u && pop32(&c) == 0x22222222u && pop32(&c) == 0x11111111u && c.esp == 0x00480000u,
          "push32/pop32 on the guest stack");

    /* ---- integer flags (values from the x86 definition) ---- */
    memset(&c, 0, sizeof c);
    check(&x, flags_add32(&c, 0x7FFFFFFFu, 1u) == 0x80000000u && c.of == 1 && c.sf == 1 && c.cf == 0 && c.zf == 0,
          "add 0x7FFFFFFF+1: OF=1 SF=1 CF=0");
    check(&x, flags_add32(&c, 0xFFFFFFFFu, 1u) == 0 && c.cf == 1 && c.zf == 1 && c.of == 0,
          "add 0xFFFFFFFF+1: CF=1 ZF=1 OF=0");
    check(&x, flags_sub32(&c, 0u, 1u) == 0xFFFFFFFFu && c.cf == 1 && c.sf == 1 && c.of == 0,
          "sub 0-1: CF=1 SF=1 OF=0");
    check(&x, flags_sub32(&c, 0x80000000u, 1u) == 0x7FFFFFFFu && c.of == 1 && c.cf == 0,
          "sub 0x80000000-1: OF=1");
    c.eax = 0xAABBCCDDu; SET8H(c.eax, 0x11); SET8L(c.eax, 0x22);
    check(&x, c.eax == 0xAABB1122u && R16(c.eax) == 0x1122, "AH/AL writes preserve the rest of EAX");

    /* ---- x87 ---- */
    memset(&c, 0, sizeof c); h2_cpu_reset_fpu(&c);
    fpush(&c, 1.5); fpush(&c, 2.25);
    check(&x, *fst(&c, 0) == 2.25 && *fst(&c, 1) == 1.5 && fpop(&c) == 2.25 && fpop(&c) == 1.5 && c.fpu_top == 0,
          "x87 register stack order");
    fpush(&c, 0.0);
    check(&x, (h2_fpu_sw(&c) & 0x3800u) == (7u << 11), "fnstsw reports TOP");
    check(&x, fist32_with(0x027Fu, 2.5) == 2u && fist32_with(0x027Fu, 3.5) == 4u, "fistp, round to nearest even");
    check(&x, fist32_with(0x0E7Fu, 2.7) == 2u && fist32_with(0x0E7Fu, -2.7) == (uint32_t)-2,
          "fistp under truncation (what MSVC _ftol selects)");
    check(&x, fist32_with(0x067Fu, -2.1) == (uint32_t)-3 && fist32_with(0x0A7Fu, 2.1) == 3u, "fistp round down / up");
    check(&x, fist32_with(0x027Fu, 3.0e9) == 0x80000000u && fist32_with(0x027Fu, -1.0e19) == 0x80000000u,
          "fistp out of range stores the integer indefinite (same on every host)");

    /* ---- direction flag ---- */
    memset(&c, 0, sizeof c);
    check(&x, h2_dstep(&c, 4) == 4u, "string step forward (DF=0)");
    c.df = 1;
    check(&x, h2_dstep(&c, 1) == 0xFFFFFFFFu && h2_dstep(&c, 4) == 0xFFFFFFFCu, "string step backward (DF=1)");

    /* ---- release ---- */
    check(&x, guest_mem_decommit(0x00400000u, 0x00200000u) == 0 && guest_mem_decommit(0xFFFFF000u, 0x1000u) == 0,
          "decommit guest ranges");

    out(&x, "\n%d passed, %d failed\n", x.pass, x.fail);
    H2_INFO(H2_LOG_RECOMP, "SELFTEST pass=%d fail=%d pointer_bits=%u", x.pass, x.fail, (unsigned)(sizeof(void *) * 8));
    return x.fail;
}
