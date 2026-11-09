/*
 * harness.c - runs lifted game functions on prepared machine states and prints
 * the resulting state, for comparison against a reference x86 (Unicorn).
 *
 * Headless console program. Portable C: the same file builds for the x86-64
 * test host and for ARM64 Android. Guest memory goes through guest_mem.h only.
 *
 * Job file (little-endian, written by scripts/difftest_game.py):
 *   u32 magic 'H2DT', u32 n_regions, u32 n_cases
 *   regions: u32 addr, u32 size, u32 flags(bit0 = tracked), u8 data[size]
 *   cases:   u32 func_va, u32 regs[8] (eax ecx edx ebx esp ebp esi edi),
 *            u32 eflags, u32 n_pokes, pokes: u32 addr, u32 len, u8 data[len],
 *            u32 n_fp, f64 fp[n_fp]  (pushed on the x87 stack in order)
 *
 * Usage: harness <job> [first_case [last_case]]
 * Output, one block per case (stdout):
 *   BEGIN <i> <func_va>
 *   END <i> <status> eax ecx edx ebx esp ebp esi edi  cf pf af zf sf of  top  st0bits
 *   M <addr> <hexbytes>         (one per run of bytes that differ from the pre-run state)
 *   DONE <i>
 * status: ok | unresolved:<va> | todo:<va>:<text> | fault:<kind>:guest=<addr>
 *         | x87-stack-misuse (the vector did not supply the FPU inputs read)
 */
#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE 1
#endif
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static jmp_buf g_bail;
static char g_status[256];

#include "harness_hooks.h"
int g_fp_depth = 0, g_fp_bad = 0;

#include "cpu.h"

uint32_t g_image_delta = 0;

void h2_todo(unsigned va, const char *text)
{
    snprintf(g_status, sizeof g_status, "todo:%08X:%s", va, text);
    for (char *p = g_status; *p; p++) if (*p == ' ') *p = '_';
    longjmp(g_bail, 1);
}

/* ---- the lifted functions under test ---- */
#include "lifted_list.h"
#define X(a) void L_##a(CPU *c);
LIFTED_FUNCS(X)
#undef X
typedef void (*lifted_fn)(CPU *);
static const struct { uint32_t va; lifted_fn fn; } g_funcs[] = {
#define X(a) { 0x##a##u, L_##a },
LIFTED_FUNCS(X)
#undef X
};
#define N_FUNCS (sizeof g_funcs / sizeof g_funcs[0])

static lifted_fn lookup(uint32_t va)
{
    size_t lo = 0, hi = N_FUNCS;            /* table is emitted sorted by address */
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        if (g_funcs[mid].va == va) return g_funcs[mid].fn;
        if (g_funcs[mid].va < va) lo = mid + 1; else hi = mid;
    }
    return 0;
}

/* Calls and tail-jumps the lifter could not resolve statically land here. An
 * address with no lifted body is reported, never papered over. */
void dispatch(CPU *c, uint32_t va)
{
    lifted_fn f = lookup(va);
    if (!f) {
        snprintf(g_status, sizeof g_status, "unresolved:%08X", va);
        longjmp(g_bail, 1);
    }
    f(c);
}
void dispatch_jmp(CPU *c, uint32_t va) { dispatch(c, va); }

/* ---- host faults raised by lifted code ----
 *
 * A wild guest pointer lands on an uncommitted page of the guest reservation
 * and the host raises an access violation. That is a test RESULT (the lifted
 * code touched memory the original did not), so it is caught here, reported
 * with the guest address, and the run continues with the next case. Nothing
 * reaches the OS crash reporter: no dialog, no dump, no noise on the machine.
 */
static void describe_fault(const char *what, uintptr_t host_addr)
{
    uintptr_t base = (uintptr_t)g_guest_base;
    if (host_addr >= base && host_addr - base < GUEST_SPACE_SIZE + GUEST_GUARD_SIZE)
        snprintf(g_status, sizeof g_status, "fault:%s:guest=%08X", what, (unsigned)(host_addr - base));
    else
        snprintf(g_status, sizeof g_status, "fault:%s:host", what);
}

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
static int fault_filter(EXCEPTION_POINTERS *ep)
{
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    if (code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_IN_PAGE_ERROR)
        describe_fault("access", (uintptr_t)ep->ExceptionRecord->ExceptionInformation[1]);
    else if (code == EXCEPTION_INT_DIVIDE_BY_ZERO || code == EXCEPTION_INT_OVERFLOW)
        describe_fault("divide", 0);
    else if (code == EXCEPTION_STACK_OVERFLOW) {
        /* the guard page is gone; this process cannot safely take another one */
        printf("END -1 fault:host-stack-overflow\n"); fflush(stdout);
        TerminateProcess(GetCurrentProcess(), 78);
    } else
        snprintf(g_status, sizeof g_status, "fault:exception=%08lX", (unsigned long)code);
    return EXCEPTION_EXECUTE_HANDLER;
}
static void run_guarded(lifted_fn fn, CPU *c)
{
    __try { fn(c); }
    __except (fault_filter(GetExceptionInformation())) { }
}
static void platform_init(void)
{
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
}
#else
#include <signal.h>
static sigjmp_buf g_fault_jmp;
static void on_signal(int sig, siginfo_t *si, void *ctx)
{
    (void)ctx;
    describe_fault(sig == SIGFPE ? "divide" : "access", sig == SIGFPE ? 0 : (uintptr_t)si->si_addr);
    siglongjmp(g_fault_jmp, 1);
}
static void run_guarded(lifted_fn fn, CPU *c)
{
    if (sigsetjmp(g_fault_jmp, 1) == 0) fn(c);
}
static void platform_init(void)
{
    static uint8_t altstack[65536];
    stack_t ss; struct sigaction sa;
    ss.ss_sp = altstack; ss.ss_size = sizeof altstack; ss.ss_flags = 0;
    sigaltstack(&ss, 0);
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = on_signal; sa.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_NODEFER;
    sigaction(SIGSEGV, &sa, 0); sigaction(SIGBUS, &sa, 0); sigaction(SIGFPE, &sa, 0);
}
#endif

/* ---- job ---- */
typedef struct { uint32_t addr, size, flags; uint8_t *pristine; } Region;

static uint32_t rd_u32(FILE *f)
{
    uint8_t b[4];
    if (fread(b, 1, 4, f) != 4) { fprintf(stderr, "job: short read\n"); exit(2); }
    return b[0] | (b[1] << 8) | (b[2] << 16) | ((uint32_t)b[3] << 24);
}

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: harness <job> [first [last]]\n"); return 2; }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 2; }
    if (rd_u32(f) != 0x54443248u) { fprintf(stderr, "job: bad magic\n"); return 2; }
    uint32_t n_regions = rd_u32(f), n_cases = rd_u32(f);
    uint32_t first = argc > 2 ? (uint32_t)strtoul(argv[2], 0, 0) : 0;
    uint32_t last  = argc > 3 ? (uint32_t)strtoul(argv[3], 0, 0) : n_cases - 1;

    platform_init();
    if (guest_mem_init() != 0) { fprintf(stderr, "guest_mem_init failed\n"); return 3; }
    Region *regions = (Region *)calloc(n_regions, sizeof *regions);
    for (uint32_t i = 0; i < n_regions; i++) {
        Region *r = &regions[i];
        r->addr = rd_u32(f); r->size = rd_u32(f); r->flags = rd_u32(f);
        r->pristine = (uint8_t *)malloc(r->size);
        if (fread(r->pristine, 1, r->size, f) != r->size) { fprintf(stderr, "job: short region\n"); return 2; }
        if (guest_mem_commit(r->addr, r->size, GUEST_PROT_RW) != 0) {
            fprintf(stderr, "commit %08X+%X failed\n", r->addr, r->size); return 3;
        }
    }
    uint8_t *before = 0; size_t before_cap = 0;

    for (uint32_t i = 0; i < n_cases; i++) {
        uint32_t va = rd_u32(f), regs[8];
        for (int k = 0; k < 8; k++) regs[k] = rd_u32(f);
        uint32_t eflags = rd_u32(f), n_pokes = rd_u32(f);
        int run = (i >= first && i <= last);

        if (run)   /* every case starts from the same memory */
            for (uint32_t r = 0; r < n_regions; r++)
                memcpy(guest_ptr(regions[r].addr), regions[r].pristine, regions[r].size);
        for (uint32_t p = 0; p < n_pokes; p++) {
            uint32_t a = rd_u32(f), len = rd_u32(f);
            uint8_t *tmp = (uint8_t *)malloc(len ? len : 1);
            if (fread(tmp, 1, len, f) != len) { fprintf(stderr, "job: short poke\n"); return 2; }
            if (run) memcpy(guest_ptr(a), tmp, len);
            free(tmp);
        }
        uint32_t n_fp = rd_u32(f);
        double fp[8];
        for (uint32_t p = 0; p < n_fp; p++) {
            double d;
            if (fread(&d, 1, 8, f) != 8) { fprintf(stderr, "job: short fp\n"); return 2; }
            if (p < 8) fp[p] = d;
        }
        if (!run) continue;

        size_t tracked = 0;
        for (uint32_t r = 0; r < n_regions; r++) if (regions[r].flags & 1) tracked += regions[r].size;
        if (tracked > before_cap) { before = (uint8_t *)realloc(before, tracked); before_cap = tracked; }
        size_t off = 0;
        for (uint32_t r = 0; r < n_regions; r++) if (regions[r].flags & 1) {
            memcpy(before + off, guest_ptr(regions[r].addr), regions[r].size); off += regions[r].size;
        }

        CPU c; memset(&c, 0, sizeof c);
        c.eax = regs[0]; c.ecx = regs[1]; c.edx = regs[2]; c.ebx = regs[3];
        c.esp = regs[4]; c.ebp = regs[5]; c.esi = regs[6]; c.edi = regs[7];
        eflags_unpack(&c, eflags);
        h2_cpu_reset_fpu(&c);
        g_fp_depth = 0; g_fp_bad = 0;
        for (uint32_t p = 0; p < n_fp && p < 8; p++) fpush(&c, fp[p]);   /* last pushed is ST(0) */

        printf("BEGIN %u %08X\n", i, va); fflush(stdout);
        strcpy(g_status, "ok");
        if (setjmp(g_bail) == 0) {
            lifted_fn fn = lookup(va);
            if (!fn) { snprintf(g_status, sizeof g_status, "unresolved:%08X", va); }
            else run_guarded(fn, &c);
        }
        if (g_fp_bad && strcmp(g_status, "ok") == 0) strcpy(g_status, "x87-stack-misuse");
        uint64_t st0; memcpy(&st0, &c.st[c.fpu_top & 7], 8);
        printf("END %u %s %08X %08X %08X %08X %08X %08X %08X %08X  %u %u %u %u %u %u  %d  %016llX\n",
               i, g_status, c.eax, c.ecx, c.edx, c.ebx, c.esp, c.ebp, c.esi, c.edi,
               c.cf, c.pf, c.af, c.zf, c.sf, c.of, c.fpu_top & 7, (unsigned long long)st0);
        off = 0;
        for (uint32_t r = 0; r < n_regions; r++) if (regions[r].flags & 1) {
            const uint8_t *now = (const uint8_t *)guest_ptr(regions[r].addr);
            const uint8_t *was = before + off;
            for (uint32_t k = 0; k < regions[r].size; ) {
                if (now[k] == was[k]) { k++; continue; }
                uint32_t s = k;
                while (k < regions[r].size && now[k] != was[k]) k++;
                printf("M %08X ", regions[r].addr + s);
                for (uint32_t j = s; j < k; j++) printf("%02X", now[j]);
                printf("\n");
            }
            off += regions[r].size;
        }
        printf("DONE %u\n", i); fflush(stdout);
    }
    fclose(f);
    return 0;
}
