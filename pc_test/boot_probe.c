/*
 * boot_probe.c - run the recompiled program from its entry point, headless,
 * and report exactly how far it gets and why it stopped.
 *
 * Console program: no window, no input, no display change. It loads the user's
 * hitman2.exe as DATA (image bytes into guest memory); the code that runs is
 * the lifted C linked into this executable.
 *
 *   boot_probe <path-to-hitman2.exe> [--budget N] [--trace] [--calls]
 *
 * Exit code: 0 guest called ExitProcess / entry returned, 10+reason otherwise.
 * The last line is machine-readable:  RESULT reason=<..> dispatches=<n> ...
 */
#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE 1
#endif
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "h2_platform.h"
#include "h2_dispatch.h"

static jmp_buf g_stop_jmp;
static h2_stop_reason g_reason;
static char g_message[512];

static const char *reason_name(h2_stop_reason r)
{
    static const char *const n[] = { "exit", "untranslated-instruction", "unresolved-dispatch",
                                     "unimplemented-import", "budget", "fault", "internal" };
    return (unsigned)r < sizeof n / sizeof n[0] ? n[r] : "?";
}

static void on_stop(h2_stop_reason why, const char *message)
{
    g_reason = why;
    snprintf(g_message, sizeof g_message, "%s", message);
    longjmp(g_stop_jmp, 1);
}

/* ---- host faults from guest code: reported, never handed to the OS crash UI ---- */
static void fault_message(const char *what, uintptr_t host_addr)
{
    uintptr_t base = (uintptr_t)g_guest_base;
    g_reason = H2_STOP_FAULT;
    if (host_addr >= base && host_addr - base < GUEST_SPACE_SIZE + GUEST_GUARD_SIZE)
        snprintf(g_message, sizeof g_message, "%s at guest address %08X", what, (unsigned)(host_addr - base));
    else
        snprintf(g_message, sizeof g_message, "%s outside guest memory (host fault)", what);
}

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
static int fault_filter(EXCEPTION_POINTERS *ep)
{
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    if (code == EXCEPTION_ACCESS_VIOLATION) fault_message("invalid memory access", (uintptr_t)ep->ExceptionRecord->ExceptionInformation[1]);
    else if (code == EXCEPTION_INT_DIVIDE_BY_ZERO) fault_message("integer divide by zero", 0);
    else if (code == EXCEPTION_STACK_OVERFLOW) { fault_message("host stack overflow (guest recursion too deep)", 0); }
    else { g_reason = H2_STOP_FAULT; snprintf(g_message, sizeof g_message, "host exception %08lX", (unsigned long)code); }
    return EXCEPTION_EXECUTE_HANDLER;
}
static int run_guarded(CPU *c, uint32_t entry)
{
    __try { dispatch(c, entry); return 0; }
    __except (fault_filter(GetExceptionInformation())) { return 1; }
}
static void platform_init(void) { SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX); }
#else
#include <signal.h>
static sigjmp_buf g_fault_jmp;
static void on_signal(int sig, siginfo_t *si, void *ctx)
{
    (void)ctx;
    fault_message(sig == SIGFPE ? "integer divide by zero" : "invalid memory access", sig == SIGFPE ? 0 : (uintptr_t)si->si_addr);
    siglongjmp(g_fault_jmp, 1);
}
static int run_guarded(CPU *c, uint32_t entry)
{
    if (sigsetjmp(g_fault_jmp, 1)) return 1;
    dispatch(c, entry);
    return 0;
}
static void platform_init(void)
{
    static uint8_t alt[65536];
    stack_t ss; struct sigaction sa;
    ss.ss_sp = alt; ss.ss_size = sizeof alt; ss.ss_flags = 0; sigaltstack(&ss, 0);
    memset(&sa, 0, sizeof sa); sa.sa_sigaction = on_signal; sa.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_NODEFER;
    sigaction(SIGSEGV, &sa, 0); sigaction(SIGBUS, &sa, 0); sigaction(SIGFPE, &sa, 0);
}
#endif

int main(int argc, char **argv)
{
    const char *path = 0, *user_root = "h2_user";
    uint64_t budget = 50000000ull;
    int show_calls = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--budget") && i + 1 < argc) budget = strtoull(argv[++i], 0, 0);
        else if (!strcmp(argv[i], "--user-root") && i + 1 < argc) user_root = argv[++i];
        else if (!strcmp(argv[i], "--trace")) g_h2_trace_imports = 1;
        else if (!strcmp(argv[i], "--calls")) show_calls = 1;
        else path = argv[i];
    }
    if (!path) { fprintf(stderr, "usage: boot_probe <hitman2.exe> [--budget N] [--trace] [--calls]\n"); return 2; }

    platform_init();
    static H2Image img;
    static CPU cpu;
    if (guest_mem_init() != 0) { fprintf(stderr, "cannot reserve the guest address space\n"); return 3; }
    h2_register_kernel32();
    h2_register_objects();
    h2_register_files();
    h2_register_user32();
    {   /* game root = the directory holding the exe; user root given or ./h2_user */
        char root[400];
        snprintf(root, sizeof root, "%s", path);
        char *s = strrchr(root, '/'), *b = strrchr(root, '\\');
        if (b > s) s = b;
        if (s) *s = 0; else snprintf(root, sizeof root, ".");
        h2_files_configure(root, user_root);
    }
    if (h2_pe_load(path, &img) != 0 || h2_process_init(&img) != 0) return 3;
    h2_kernel32_attach(&img);
    h2_dispatch_set_import_hook(h2_import_dispatch);
    h2_dispatch_set_budget(budget);
    h2_set_stop_handler(on_stop);

    unsigned bound, implemented;
    h2_imports_stats(&bound, &implemented);
    printf("image %s: base=%08X entry=%08X, %u distinct imports bound, %u implemented\n",
           path, img.base, img.entry, bound, implemented);
    printf("lifted functions linked: %u; entry has a lifted body: %s\n",
           (unsigned)h2_dispatch_function_count(), h2_dispatch_has(img.entry) ? "yes" : "NO");

    h2_cpu_init_main_thread(&cpu);
    push32(&cpu, 0xDEAD0000u);                       /* return address of the entry point (never used: it exits) */

    int returned = 0;
    if (setjmp(g_stop_jmp) == 0) {
        if (run_guarded(&cpu, img.entry) == 0) returned = 1;
    }
    if (returned) { g_reason = H2_STOP_EXIT; snprintf(g_message, sizeof g_message, "entry point returned, eax=%08X", cpu.eax); }

    printf("\nstopped: %s\n  %s\n", reason_name(g_reason), g_message);
    printf("  cpu: eax=%08X ecx=%08X edx=%08X ebx=%08X esp=%08X ebp=%08X esi=%08X edi=%08X\n",
           cpu.eax, cpu.ecx, cpu.edx, cpu.ebx, cpu.esp, cpu.ebp, cpu.esi, cpu.edi);
    uint32_t trail[H2_TRAIL_LEN];
    size_t nt = h2_dispatch_trail(trail, H2_TRAIL_LEN);
    printf("  last dispatch targets (oldest first):");
    for (size_t i = nt > 16 ? nt - 16 : 0; i < nt; i++) printf(" %08X", trail[i]);
    printf("\n");

    static H2ImportStat st[2048];
    size_t ns = h2_imports_report(st, 2048);
    unsigned called = 0; uint64_t calls = 0;
    for (size_t i = 0; i < ns; i++) if (st[i].calls) { called++; calls += st[i].calls; }
    printf("  imports called: %u distinct, %llu calls\n", called, (unsigned long long)calls);
    if (show_calls)
        for (size_t i = 0; i < ns; i++)
            if (st[i].calls) printf("    %8llu  %s!%s%s\n", (unsigned long long)st[i].calls, st[i].dll, st[i].name,
                                    st[i].implemented ? "" : "   <-- NOT IMPLEMENTED");
    printf("RESULT reason=%s dispatches=%llu imports_called=%u import_calls=%llu implemented=%u bound=%u\n",
           reason_name(g_reason), (unsigned long long)h2_dispatch_count(), called, (unsigned long long)calls, implemented, bound);
    return g_reason == H2_STOP_EXIT ? 0 : 10 + (int)g_reason;
}
