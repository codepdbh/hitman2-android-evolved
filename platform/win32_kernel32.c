/*
 * win32_kernel32.c - the KERNEL32 functions implemented so far.
 *
 * Each one does what the documented function does, within the stated model:
 *   - one guest process, one guest thread (CreateThread is not implemented;
 *     the critical-section functions below are only correct until it is);
 *   - the reported system is Windows XP (5.1.2600), ANSI code page 1252;
 *   - no console: standard handles are NULL, as for a GUI process.
 * A function that is not here is not implemented: calling it stops the guest
 * and names it. Add it properly; do not add a stub.
 */
#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif
#include <stdio.h>
#include <string.h>

#include "h2_platform.h"
#include "h2_handles.h"

#define ARG(i) (args[(i)])
#define UNUSED (void)c
#define ERROR_INVALID_PARAMETER 87u
#define ERROR_INSUFFICIENT_BUFFER 122u
#define ERROR_INVALID_HANDLE 6u
#define ERROR_NOT_ENOUGH_MEMORY 8u
#define ERROR_INVALID_ADDRESS 487u
#define ERROR_MOD_NOT_FOUND 126u

#define HEAP_HANDLE 0x00110000u

static const H2Image *g_img;
static char g_module_path[] = "C:\\Hitman2\\hitman2.exe";
static uint32_t g_cmdline, g_env_a, g_env_w, g_unhandled_filter, g_error_mode;

static const char *gstr(uint32_t a) { return (const char *)guest_ptr(a); }

/* ---- code page 1252 <-> UTF-16 ---- */
static const uint16_t k_1252_80[32] = {
    0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
    0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178
};
static uint16_t to_wide(uint8_t ch) { return ch >= 0x80 && ch < 0xA0 ? k_1252_80[ch - 0x80] : ch; }
static uint8_t to_ansi(uint16_t w)
{
    if (w < 0x80 || (w >= 0xA0 && w <= 0xFF)) return (uint8_t)w;
    for (int i = 0; i < 32; i++) if (k_1252_80[i] == w) return (uint8_t)(0x80 + i);
    return '?';
}
static int known_cp(uint32_t cp) { return cp == 0 || cp == 1 || cp == 3 || cp == 1252 || cp == 850; }

/* ---- version / process ---- */
static uint32_t k_GetVersion(CPU *c, const uint32_t *args) { UNUSED; (void)args; return 0x0A280105u; }  /* 5.1, build 2600 */

static uint32_t k_GetVersionExA(CPU *c, const uint32_t *args)
{
    UNUSED;
    uint32_t p = ARG(0), cb = rd32(p);
    if (cb != 148 && cb != 156) { g_h2_last_error = ERROR_INSUFFICIENT_BUFFER; return 0; }
    memset(guest_ptr(p + 4), 0, cb - 4);
    wr32(p + 4, 5); wr32(p + 8, 1); wr32(p + 12, 2600); wr32(p + 16, 2 /* VER_PLATFORM_WIN32_NT */);
    memcpy(guest_ptr(p + 20), "Service Pack 3", 15);
    return 1;
}

static uint32_t k_GetCommandLineA(CPU *c, const uint32_t *args) { UNUSED; (void)args; return g_cmdline; }
static uint32_t k_GetCurrentProcess(CPU *c, const uint32_t *args) { UNUSED; (void)args; return 0xFFFFFFFFu; }
static uint32_t k_GetCurrentThread(CPU *c, const uint32_t *args) { UNUSED; (void)args; return 0xFFFFFFFEu; }
static uint32_t k_GetLastError(CPU *c, const uint32_t *args) { UNUSED; (void)args; return g_h2_last_error; }

static uint32_t k_GetStartupInfoA(CPU *c, const uint32_t *args)
{
    UNUSED;
    memset(guest_ptr(ARG(0)), 0, 68);
    wr32(ARG(0), 68);
    return 0;
}

static uint32_t k_GetModuleHandleA(CPU *c, const uint32_t *args)
{
    UNUSED;
    if (!ARG(0)) return g_img->base;
    const char *want = gstr(ARG(0)), *base = strrchr(g_module_path, '\\') + 1;
    const char *w = strrchr(want, '\\'); w = w ? w + 1 : want;
    size_t i;
    for (i = 0; w[i] && base[i]; i++) if ((w[i] | 32) != (base[i] | 32)) break;
    if (!w[i] && (!base[i] || base[i] == '.')) return g_img->base;
    H2_WARN(H2_LOG_WIN32, "GetModuleHandleA(\"%s\"): module not loaded", want);
    g_h2_last_error = ERROR_MOD_NOT_FOUND;
    return 0;
}

static uint32_t k_GetModuleFileNameA(CPU *c, const uint32_t *args)
{
    UNUSED;
    if (ARG(0) && ARG(0) != g_img->base) { g_h2_last_error = ERROR_INVALID_HANDLE; return 0; }
    uint32_t n = (uint32_t)strlen(g_module_path), cap = ARG(2);
    if (!cap) return 0;
    if (n >= cap) {                                   /* truncated, still NUL-terminated on NT 5.1+ */
        memcpy(guest_ptr(ARG(1)), g_module_path, cap - 1); wr8(ARG(1) + cap - 1, 0);
        g_h2_last_error = ERROR_INSUFFICIENT_BUFFER;
        return cap;
    }
    memcpy(guest_ptr(ARG(1)), g_module_path, n + 1);
    return n;
}

static uint32_t k_ExitProcess(CPU *c, const uint32_t *args)
{
    UNUSED;
    h2_guest_stop(H2_STOP_EXIT, "ExitProcess(%u)", ARG(0));
}

static uint32_t k_TerminateProcess(CPU *c, const uint32_t *args)
{
    UNUSED;
    if (ARG(0) != 0xFFFFFFFFu) { g_h2_last_error = ERROR_INVALID_HANDLE; return 0; }
    h2_guest_stop(H2_STOP_EXIT, "TerminateProcess(self, %u)", ARG(1));
}

static uint32_t k_SetUnhandledExceptionFilter(CPU *c, const uint32_t *args)
{
    UNUSED;
    uint32_t prev = g_unhandled_filter; g_unhandled_filter = ARG(0);
    return prev;
}

static uint32_t k_SetErrorMode(CPU *c, const uint32_t *args)
{
    UNUSED;
    uint32_t prev = g_error_mode; g_error_mode = ARG(0);
    return prev;
}

static uint32_t k_OutputDebugStringA(CPU *c, const uint32_t *args)
{
    UNUSED;
    if (ARG(0)) H2_INFO(H2_LOG_GAME, "OutputDebugString: %.900s", gstr(ARG(0)));
    return 0;
}

/* ---- environment ---- */
static uint32_t k_GetEnvironmentStrings(CPU *c, const uint32_t *args) { UNUSED; (void)args; return g_env_a; }
static uint32_t k_GetEnvironmentStringsW(CPU *c, const uint32_t *args) { UNUSED; (void)args; return g_env_w; }
static uint32_t k_FreeEnvironmentStrings(CPU *c, const uint32_t *args)
{
    UNUSED;
    return ARG(0) == g_env_a || ARG(0) == g_env_w;
}

/* ---- standard handles: a GUI process has none ---- */
static uint32_t k_GetStdHandle(CPU *c, const uint32_t *args) { UNUSED; (void)args; return 0; }
static uint32_t k_GetFileType(CPU *c, const uint32_t *args)
{
    UNUSED;
    if (h2_handle_get(ARG(0), H2_OBJ_FILE)) return 1;                  /* FILE_TYPE_DISK */
    g_h2_last_error = ERROR_INVALID_HANDLE;
    return 0;                                                          /* FILE_TYPE_UNKNOWN */
}
static uint32_t k_SetHandleCount(CPU *c, const uint32_t *args) { UNUSED; return ARG(0); }

/* ---- heap ---- */
static uint32_t k_HeapCreate(CPU *c, const uint32_t *args) { UNUSED; (void)args; return HEAP_HANDLE; }
static uint32_t k_HeapDestroy(CPU *c, const uint32_t *args) { UNUSED; return ARG(0) == HEAP_HANDLE; }
static uint32_t k_GetProcessHeap(CPU *c, const uint32_t *args) { UNUSED; (void)args; return HEAP_HANDLE; }
static uint32_t k_HeapAlloc(CPU *c, const uint32_t *args)
{
    UNUSED;
    uint32_t p = h2_heap_alloc(ARG(2), (ARG(1) & 8u) != 0);
    if (!p) g_h2_last_error = ERROR_NOT_ENOUGH_MEMORY;
    return p;
}
static uint32_t k_HeapFree(CPU *c, const uint32_t *args)
{
    UNUSED;
    if (!ARG(2)) return 1;
    if (h2_heap_free(ARG(2)) != 0) { g_h2_last_error = ERROR_INVALID_PARAMETER; return 0; }
    return 1;
}
static uint32_t k_HeapReAlloc(CPU *c, const uint32_t *args)
{
    UNUSED;
    uint32_t p = h2_heap_realloc(ARG(2), ARG(3), (ARG(1) & 8u) != 0);
    if (!p) g_h2_last_error = ERROR_NOT_ENOUGH_MEMORY;
    return p;
}
static uint32_t k_HeapSize(CPU *c, const uint32_t *args) { UNUSED; return h2_heap_size(ARG(2)); }

/* ---- virtual memory ----
 * Model: a reservation is committed (zero-filled) at once. A later MEM_COMMIT
 * inside it is therefore already satisfied; MEM_DECOMMIT zero-fills, which is
 * what the guest would observe after decommit + recommit. */
static uint32_t k_VirtualAlloc(CPU *c, const uint32_t *args)
{
    UNUSED;
    uint32_t addr = ARG(0), size = ARG(1), type = ARG(2);
    if (!(type & 0x3000u) || !size) { g_h2_last_error = ERROR_INVALID_PARAMETER; return 0; }
    if (!addr) {
        uint32_t p = h2_virtual_alloc(size);
        if (!p) g_h2_last_error = ERROR_NOT_ENOUGH_MEMORY;
        return p;
    }
    uint32_t rsize, base = h2_virtual_find(addr, &rsize);
    if (!base || (uint64_t)(addr - base) + size > ((rsize + 0xFFFFu) & ~0xFFFFu)) {
        H2_WARN(H2_LOG_MEMORY, "VirtualAlloc at fixed address %08X (+%X) outside any reservation", addr, size);
        g_h2_last_error = ERROR_INVALID_ADDRESS;
        return 0;
    }
    return addr & ~0xFFFu;
}
static uint32_t k_VirtualFree(CPU *c, const uint32_t *args)
{
    UNUSED;
    uint32_t addr = ARG(0), size = ARG(1), type = ARG(2);
    if (type == 0x8000u) {                                             /* MEM_RELEASE */
        if (size || h2_virtual_free(addr) != 0) { g_h2_last_error = ERROR_INVALID_PARAMETER; return 0; }
        return 1;
    }
    if (type == 0x4000u) {                                             /* MEM_DECOMMIT */
        uint32_t rsize, base = h2_virtual_find(addr, &rsize);
        if (!base) { g_h2_last_error = ERROR_INVALID_ADDRESS; return 0; }
        uint32_t lo = addr & ~0xFFFu, end = size ? addr + size : base + rsize;
        if (end > base + rsize) end = base + rsize;
        if (end > lo) memset(guest_ptr(lo), 0, end - lo);
        return 1;
    }
    g_h2_last_error = ERROR_INVALID_PARAMETER;
    return 0;
}

/* ---- critical sections (see the single-thread note at the top) ---- */
static uint32_t k_InitializeCriticalSection(CPU *c, const uint32_t *args)
{
    UNUSED;
    memset(guest_ptr(ARG(0)), 0, 24);
    wr32(ARG(0) + 4, 0xFFFFFFFFu);                                     /* LockCount = -1: free */
    return 0;
}
static uint32_t k_EnterCriticalSection(CPU *c, const uint32_t *args)
{
    UNUSED;
    wr32(ARG(0) + 4, rd32(ARG(0) + 4) + 1); wr32(ARG(0) + 8, rd32(ARG(0) + 8) + 1);
    wr32(ARG(0) + 12, rd32(H2_TIB_ADDR + 0x24));                       /* OwningThread */
    return 0;
}
static uint32_t k_LeaveCriticalSection(CPU *c, const uint32_t *args)
{
    UNUSED;
    uint32_t rec = rd32(ARG(0) + 8);
    if (!rec) { H2_ERROR(H2_LOG_WIN32, "LeaveCriticalSection(%08X) without a matching Enter", ARG(0)); return 0; }
    wr32(ARG(0) + 8, rec - 1); wr32(ARG(0) + 4, rd32(ARG(0) + 4) - 1);
    if (rec == 1) wr32(ARG(0) + 12, 0);
    return 0;
}
static uint32_t k_DeleteCriticalSection(CPU *c, const uint32_t *args) { UNUSED; memset(guest_ptr(ARG(0)), 0, 24); return 0; }

/* ---- code pages and strings ---- */
static uint32_t k_GetACP(CPU *c, const uint32_t *args) { UNUSED; (void)args; return 1252; }
static uint32_t k_GetOEMCP(CPU *c, const uint32_t *args) { UNUSED; (void)args; return 850; }
static uint32_t k_GetCPInfo(CPU *c, const uint32_t *args)
{
    UNUSED;
    if (!known_cp(ARG(0))) { g_h2_last_error = ERROR_INVALID_PARAMETER; return 0; }
    memset(guest_ptr(ARG(1)), 0, 20);
    wr32(ARG(1), 1); wr8(ARG(1) + 4, '?');                              /* single-byte, default char '?' */
    return 1;
}

static uint32_t k_MultiByteToWideChar(CPU *c, const uint32_t *args)
{
    UNUSED;
    uint32_t cp = ARG(0), src = ARG(2), dst = ARG(4);
    int32_t cb = (int32_t)ARG(3), cch = (int32_t)ARG(5);
    if (!known_cp(cp) || !src || cb == 0 || cch < 0) { g_h2_last_error = ERROR_INVALID_PARAMETER; return 0; }
    uint32_t n = cb < 0 ? (uint32_t)strlen(gstr(src)) + 1 : (uint32_t)cb;
    if (cch == 0) return n;
    if ((uint32_t)cch < n) { g_h2_last_error = ERROR_INSUFFICIENT_BUFFER; return 0; }
    for (uint32_t i = 0; i < n; i++) wr16(dst + 2 * i, to_wide(rd8(src + i)));
    return n;
}

static uint32_t wide_len(uint32_t p) { uint32_t n = 0; while (rd16(p + 2 * n)) n++; return n; }

static uint32_t k_WideCharToMultiByte(CPU *c, const uint32_t *args)
{
    UNUSED;
    uint32_t cp = ARG(0), src = ARG(2), dst = ARG(4), used_default = ARG(7);
    int32_t cch = (int32_t)ARG(3), cb = (int32_t)ARG(5);
    if (!known_cp(cp) || !src || cch == 0 || cb < 0) { g_h2_last_error = ERROR_INVALID_PARAMETER; return 0; }
    uint32_t n = cch < 0 ? wide_len(src) + 1 : (uint32_t)cch;
    if (used_default) wr32(used_default, 0);
    if (cb == 0) return n;
    if ((uint32_t)cb < n) { g_h2_last_error = ERROR_INSUFFICIENT_BUFFER; return 0; }
    for (uint32_t i = 0; i < n; i++) {
        uint16_t w = rd16(src + 2 * i);
        uint8_t a = to_ansi(w);
        if (a == '?' && w != '?' && used_default) wr32(used_default, 1);
        wr8(dst + i, a);
    }
    return n;
}

/* CT_CTYPE1 for Latin-1 (what code page 1252 text maps to, bar 27 symbols). */
static uint16_t ctype1(uint16_t w)
{
    enum { UP = 1, LO = 2, DIG = 4, SP = 8, PUN = 16, CTL = 32, BLK = 64, HEX = 128, ALPHA = 256 };
    if (w < 0x20 || w == 0x7F || (w >= 0x80 && w < 0xA0))
        return (uint16_t)(CTL | ((w >= 9 && w <= 13) ? SP : 0) | (w == 9 ? BLK : 0));
    if (w == ' ') return SP | BLK;
    if (w >= '0' && w <= '9') return DIG | HEX;
    if (w >= 'A' && w <= 'Z') return (uint16_t)(UP | ALPHA | (w <= 'F' ? HEX : 0));
    if (w >= 'a' && w <= 'z') return (uint16_t)(LO | ALPHA | (w <= 'f' ? HEX : 0));
    if (w < 0x7F) return PUN;
    if (w == 0xA0) return SP | BLK;
    if (w == 0xAA || w == 0xB5 || w == 0xBA) return LO | ALPHA;
    if (w >= 0xC0 && w <= 0xDE && w != 0xD7) return UP | ALPHA;
    if (w >= 0xDF && w <= 0xFF && w != 0xF7) return LO | ALPHA;
    if (w <= 0xFF) return PUN;
    return ALPHA;                                                     /* outside Latin-1: not classified further */
}

static uint32_t k_GetStringTypeW(CPU *c, const uint32_t *args)
{
    UNUSED;
    int32_t cch = (int32_t)ARG(2);
    if (ARG(0) != 1 /* CT_CTYPE1 */) h2_guest_stop(H2_STOP_UNIMPLEMENTED_IMPORT, "GetStringTypeW info type %u", ARG(0));
    if (!ARG(1) || !ARG(3) || cch == 0) { g_h2_last_error = ERROR_INVALID_PARAMETER; return 0; }
    uint32_t n = cch < 0 ? wide_len(ARG(1)) + 1 : (uint32_t)cch;
    for (uint32_t i = 0; i < n; i++) wr16(ARG(3) + 2 * i, ctype1(rd16(ARG(1) + 2 * i)));
    return 1;
}

static uint32_t k_GetStringTypeA(CPU *c, const uint32_t *args)
{
    UNUSED;
    int32_t cb = (int32_t)ARG(3);
    if (ARG(1) != 1) h2_guest_stop(H2_STOP_UNIMPLEMENTED_IMPORT, "GetStringTypeA info type %u", ARG(1));
    if (!ARG(2) || !ARG(4) || cb == 0) { g_h2_last_error = ERROR_INVALID_PARAMETER; return 0; }
    uint32_t n = cb < 0 ? (uint32_t)strlen(gstr(ARG(2))) + 1 : (uint32_t)cb;
    for (uint32_t i = 0; i < n; i++) wr16(ARG(4) + 2 * i, ctype1(to_wide(rd8(ARG(2) + i))));
    return 1;
}

static uint16_t map_case(uint16_t w, int upper)
{
    if (upper) {
        if ((w >= 'a' && w <= 'z') || (w >= 0xE0 && w <= 0xFE && w != 0xF7)) return (uint16_t)(w - 32);
        if (w == 0xFF) return 0x0178;
    } else {
        if ((w >= 'A' && w <= 'Z') || (w >= 0xC0 && w <= 0xDE && w != 0xD7)) return (uint16_t)(w + 32);
        if (w == 0x0178) return 0xFF;
    }
    return w;
}

/* LCMapString: only the case-mapping flags. Sort keys are not implemented. */
static uint32_t lcmap(const uint32_t *args, int wide)
{
    uint32_t flags = ARG(1), src = ARG(2), dst = ARG(4);
    int32_t cs = (int32_t)ARG(3), cd = (int32_t)ARG(5);
    if (flags & ~0x300u || !(flags & 0x300u) || (flags & 0x300u) == 0x300u)
        h2_guest_stop(H2_STOP_UNIMPLEMENTED_IMPORT, "LCMapString%c flags %08X (only LCMAP_LOWERCASE/UPPERCASE)", wide ? 'W' : 'A', flags);
    if (!src || cs == 0 || cd < 0) { g_h2_last_error = ERROR_INVALID_PARAMETER; return 0; }
    uint32_t n = cs < 0 ? (wide ? wide_len(src) : (uint32_t)strlen(gstr(src))) + 1 : (uint32_t)cs;
    if (cd == 0) return n;
    if ((uint32_t)cd < n) { g_h2_last_error = ERROR_INSUFFICIENT_BUFFER; return 0; }
    for (uint32_t i = 0; i < n; i++) {
        if (wide) wr16(dst + 2 * i, map_case(rd16(src + 2 * i), flags == 0x200u));
        else      wr8(dst + i, to_ansi(map_case(to_wide(rd8(src + i)), flags == 0x200u)));
    }
    return n;
}
static uint32_t k_LCMapStringA(CPU *c, const uint32_t *args) { UNUSED; return lcmap(args, 0); }
static uint32_t k_LCMapStringW(CPU *c, const uint32_t *args) { UNUSED; return lcmap(args, 1); }

/* ---- time ---- */
static uint32_t k_QueryPerformanceFrequency(CPU *c, const uint32_t *args)
{
    UNUSED;
    wr64(ARG(0), 1000000ull);                                          /* we report microseconds */
    return 1;
}
static uint32_t k_QueryPerformanceCounter(CPU *c, const uint32_t *args)
{
    UNUSED;
    wr64(ARG(0), h2_host_monotonic_ns() / 1000ull);
    return 1;
}

/* The guest's clock is UTC: there is no time zone database behind this layer,
 * so local time == system time and the zone reports a zero bias. */
#include <time.h>
#define UNIX_TO_FILETIME 116444736000000000ull

static void write_systemtime(uint32_t p, time_t t)
{
    struct tm tmv;
#if defined(_WIN32)
    gmtime_s(&tmv, &t);
#else
    gmtime_r(&t, &tmv);
#endif
    wr16(p, (uint16_t)(tmv.tm_year + 1900)); wr16(p + 2, (uint16_t)(tmv.tm_mon + 1));
    wr16(p + 4, (uint16_t)tmv.tm_wday);      wr16(p + 6, (uint16_t)tmv.tm_mday);
    wr16(p + 8, (uint16_t)tmv.tm_hour);      wr16(p + 10, (uint16_t)tmv.tm_min);
    wr16(p + 12, (uint16_t)tmv.tm_sec);      wr16(p + 14, 0);
}
static uint32_t k_GetSystemTimeAsFileTime(CPU *c, const uint32_t *args)
{
    UNUSED;
    wr64(ARG(0), (uint64_t)time(0) * 10000000ull + UNIX_TO_FILETIME);
    return 0;
}
static uint32_t k_GetSystemTime(CPU *c, const uint32_t *args) { UNUSED; write_systemtime(ARG(0), time(0)); return 0; }
static uint32_t k_GetTimeZoneInformation(CPU *c, const uint32_t *args)
{
    UNUSED;
    memset(guest_ptr(ARG(0)), 0, 172);
    return 0;                                                          /* TIME_ZONE_ID_UNKNOWN: no daylight saving */
}
static uint32_t k_FileTimeToSystemTime(CPU *c, const uint32_t *args)
{
    UNUSED;
    uint64_t ft = rd64(ARG(0));
    if (ft < UNIX_TO_FILETIME || ft >> 63) { g_h2_last_error = ERROR_INVALID_PARAMETER; return 0; }
    write_systemtime(ARG(1), (time_t)((ft - UNIX_TO_FILETIME) / 10000000ull));
    wr16(ARG(1) + 14, (uint16_t)((ft / 10000ull) % 1000ull));
    return 1;
}
static uint32_t k_FileTimeToLocalFileTime(CPU *c, const uint32_t *args) { UNUSED; wr64(ARG(1), rd64(ARG(0))); return 1; }

/* ---- modules ----
 * System DLLs are not images in guest memory; each gets a fixed pseudo-handle
 * so GetModuleHandle/LoadLibrary/GetProcAddress work for them. A function
 * obtained through GetProcAddress is bound like a static import: if it is not
 * implemented, calling it stops the guest and names it. */
static const struct { const char *name; uint32_t handle; } k_sysmods[] = {
    { "kernel32", 0x7C800000u }, { "user32", 0x7E410000u }, { "gdi32", 0x77F10000u }, { "advapi32", 0x77DD0000u },
    { "ole32", 0x774E0000u }, { "oleaut32", 0x77120000u }, { "shell32", 0x7C9C0000u }, { "shlwapi", 0x77F60000u },
    { "version", 0x77C00000u }, { "imagehlp", 0x76C90000u }, { "dinput8", 0x6CE10000u }, { "dsound", 0x73F10000u },
};
static int module_base_eq(const char *path, const char *base)
{
    const char *p = strrchr(path, '\\'); p = p ? p + 1 : path;
    size_t i;
    for (i = 0; base[i]; i++) if ((p[i] | 32) != base[i]) return 0;
    return !p[i] || ((p[i] == '.') && (p[i + 1] | 32) == 'd' && (p[i + 2] | 32) == 'l' && (p[i + 3] | 32) == 'l' && !p[i + 4]);
}
static uint32_t sysmod_by_name(const char *name)
{
    for (size_t i = 0; i < sizeof k_sysmods / sizeof k_sysmods[0]; i++)
        if (module_base_eq(name, k_sysmods[i].name)) return k_sysmods[i].handle;
    return 0;
}
static const char *sysmod_by_handle(uint32_t h)
{
    for (size_t i = 0; i < sizeof k_sysmods / sizeof k_sysmods[0]; i++)
        if (k_sysmods[i].handle == h) return k_sysmods[i].name;
    return 0;
}

static uint32_t k_GetModuleHandleA2(CPU *c, const uint32_t *args)
{
    if (ARG(0)) { uint32_t h = sysmod_by_name(gstr(ARG(0))); if (h) return h; }
    return k_GetModuleHandleA(c, args);
}
static uint32_t k_LoadLibraryA(CPU *c, const uint32_t *args)
{
    UNUSED;
    uint32_t h = sysmod_by_name(gstr(ARG(0)));
    if (h) return h;
    /* A DLL that ships with the game (the renderer, EAX, ...) has to be lifted
     * and mapped as a second guest image. That is not built yet. */
    h2_guest_stop(H2_STOP_UNIMPLEMENTED_IMPORT, "LoadLibraryA(\"%.200s\"): loading game DLLs is not implemented", gstr(ARG(0)));
}
static uint32_t k_FreeLibrary(CPU *c, const uint32_t *args)
{
    UNUSED;
    if (sysmod_by_handle(ARG(0))) return 1;
    g_h2_last_error = ERROR_INVALID_HANDLE;
    return 0;
}
static uint32_t k_GetProcAddress(CPU *c, const uint32_t *args)
{
    UNUSED;
    const char *mod = sysmod_by_handle(ARG(0));
    char dll[40];
    if (!mod || ARG(1) < 0x10000u) {                                   /* unknown module, or lookup by ordinal */
        H2_WARN(H2_LOG_WIN32, "GetProcAddress(%08X, %08X): module or ordinal lookup not supported", ARG(0), ARG(1));
        g_h2_last_error = 127;                                         /* ERROR_PROC_NOT_FOUND */
        return 0;
    }
    snprintf(dll, sizeof dll, "%s.dll", mod);
    H2_INFO(H2_LOG_WIN32, "GetProcAddress(%s, \"%s\")", dll, gstr(ARG(1)));
    return h2_import_bind(dll, gstr(ARG(1)));
}

/* ---- pointer validity: answered from what is actually committed for the guest ---- */
static int guest_range_ok(uint32_t addr, uint32_t size)
{
    uint64_t end = (uint64_t)addr + size;
    if (!size) return 1;
    if (addr >= g_img->base && end <= (uint64_t)g_img->base + g_img->size) return 1;
    if (addr >= H2_STACK_TOP - H2_STACK_SIZE && end <= H2_STACK_TOP) return 1;
    if (addr >= H2_OSDATA_BASE && end <= H2_OSDATA_BASE + H2_OSDATA_SIZE) return 1;
    uint32_t rsize, base = h2_virtual_find(addr, &rsize);
    return base && end <= (uint64_t)base + rsize;
}
static uint32_t k_IsBadReadPtr(CPU *c, const uint32_t *args) { UNUSED; return !guest_range_ok(ARG(0), ARG(1)); }
static uint32_t k_IsBadCodePtr(CPU *c, const uint32_t *args)
{
    UNUSED;
    return !(ARG(0) >= g_img->base && ARG(0) - g_img->base < g_img->size);
}

/* Answers describe the CPU the lifter models, not the host: no FDIV erratum,
 * MMX/SSE/SSE2 are translated, rdtsc is not (so it is not advertised). */
static uint32_t k_IsProcessorFeaturePresent(CPU *c, const uint32_t *args)
{
    UNUSED;
    switch (ARG(0)) {
    case 3: case 6: case 10: return 1;      /* PF_MMX, PF_XMMI, PF_XMMI64 */
    default: return 0;                      /* incl. PF_FLOATING_POINT_PRECISION_ERRATA (0), PF_RDTSC (8) */
    }
}

static uint32_t k_Sleep(CPU *c, const uint32_t *args)
{
    UNUSED;
    if (ARG(0)) {                                                      /* Sleep(0) only yields; one guest thread, nothing to yield to */
        struct timespec ts = { (time_t)(ARG(0) / 1000u), (long)(ARG(0) % 1000u) * 1000000L };
#if defined(_WIN32)
        extern void __stdcall Sleep(unsigned long);
        (void)ts; Sleep(ARG(0));
#else
        nanosleep(&ts, 0);
#endif
    }
    return 0;
}

static const H2ImportDef k_defs[] = {
    { "KERNEL32.dll", "GetModuleHandleA", 1, k_GetModuleHandleA2 },   /* system DLLs first, then the image itself */
    { "KERNEL32.dll", "GetLocalTime", 1, k_GetSystemTime },
    { "KERNEL32.dll", "IsBadWritePtr", 2, k_IsBadReadPtr },            /* every committed guest page is read-write */
#define K(name, n) { "KERNEL32.dll", #name, n, k_##name }
    K(GetSystemTimeAsFileTime, 1), K(GetSystemTime, 1), K(GetTimeZoneInformation, 1), K(FileTimeToSystemTime, 2),
    K(FileTimeToLocalFileTime, 2), K(LoadLibraryA, 1), K(FreeLibrary, 1), K(GetProcAddress, 2),
    K(IsBadReadPtr, 2), K(IsBadCodePtr, 1), K(Sleep, 1),
    { "kernel32.dll", "IsProcessorFeaturePresent", 1, k_IsProcessorFeaturePresent },
    K(GetVersion, 0), K(GetVersionExA, 1), K(GetCommandLineA, 0), K(GetCurrentProcess, 0), K(GetCurrentThread, 0),
    K(GetLastError, 0), K(GetStartupInfoA, 1), K(GetModuleFileNameA, 3),
    K(ExitProcess, 1), K(TerminateProcess, 2), K(SetUnhandledExceptionFilter, 1), K(SetErrorMode, 1),
    K(OutputDebugStringA, 1),
    K(GetEnvironmentStrings, 0), K(GetEnvironmentStringsW, 0),
    { "KERNEL32.dll", "FreeEnvironmentStringsA", 1, k_FreeEnvironmentStrings },
    { "KERNEL32.dll", "FreeEnvironmentStringsW", 1, k_FreeEnvironmentStrings },
    K(GetStdHandle, 1), K(GetFileType, 1), K(SetHandleCount, 1),
    K(HeapCreate, 3), K(HeapDestroy, 1), K(GetProcessHeap, 0), K(HeapAlloc, 3), K(HeapFree, 3), K(HeapReAlloc, 4), K(HeapSize, 3),
    K(VirtualAlloc, 4), K(VirtualFree, 3),
    K(InitializeCriticalSection, 1), K(EnterCriticalSection, 1), K(LeaveCriticalSection, 1), K(DeleteCriticalSection, 1),
    K(GetACP, 0), K(GetOEMCP, 0), K(GetCPInfo, 2), K(MultiByteToWideChar, 6), K(WideCharToMultiByte, 8),
    K(GetStringTypeW, 4), K(GetStringTypeA, 5), K(LCMapStringA, 6), K(LCMapStringW, 6),
    K(QueryPerformanceFrequency, 1), K(QueryPerformanceCounter, 1),
#undef K
};

void h2_register_kernel32(void)
{
    h2_imports_register(k_defs, sizeof k_defs / sizeof k_defs[0]);
}

/* Called once the image is loaded and the OS data area exists. */
void h2_kernel32_attach(const H2Image *img)
{
    g_img = img;
    g_cmdline = h2_os_strdup("\"C:\\Hitman2\\hitman2.exe\"");
    g_env_a = h2_os_alloc(4);                                          /* empty block: two NULs */
    g_env_w = h2_os_alloc(4);
}
