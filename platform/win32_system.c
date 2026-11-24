/*
 * win32_system.c - small KERNEL32 services the game reaches outside the main paths: string comparison, memory
 * queries, global atoms, DOS date/time conversion, message formatting, console handler registration.
 * Each entry is either implemented as documented (with the limits stated) or stops the guest by name.
 */
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "h2_platform.h"

#define ARG(i) (args[(i)])
#define UNUSED (void)c

/* ---- CompareStringA / CompareStringW ----
 * Ordinal comparison; NORM_IGNORECASE (1) folds ASCII letters only (no locale rules, no other flags).
 * Returns CSTR_LESS_THAN 1, CSTR_EQUAL 2, CSTR_GREATER_THAN 3, or 0 on a bad argument. */
static int fold(int ch, int ic) { return ic && ch >= 'A' && ch <= 'Z' ? ch + 32 : ch; }

static uint32_t compare(const uint32_t *args, int wide)
{
    uint32_t flags = ARG(1), s1 = ARG(2), s2 = ARG(4);
    int32_t n1 = (int32_t)ARG(3), n2 = (int32_t)ARG(5);
    if (!s1 || !s2) { g_h2_last_error = 87; return 0; }
    if (flags & ~1u) h2_guest_stop(H2_STOP_UNIMPLEMENTED_IMPORT, "CompareString%c: flags %08X are not implemented (only NORM_IGNORECASE)", wide ? 'W' : 'A', flags);
    int ic = (int)(flags & 1u), w = wide ? 2 : 1;
    for (int32_t i = 0;; i++) {
        int a, b, end1 = n1 >= 0 ? i >= n1 : 0, end2 = n2 >= 0 ? i >= n2 : 0;
        a = end1 ? 0 : (wide ? (int)(rd32(s1 + (uint32_t)(i * w)) & 0xFFFFu) : (int)rd8(s1 + (uint32_t)i));
        b = end2 ? 0 : (wide ? (int)(rd32(s2 + (uint32_t)(i * w)) & 0xFFFFu) : (int)rd8(s2 + (uint32_t)i));
        if (n1 < 0 && a == 0) end1 = 1;
        if (n2 < 0 && b == 0) end2 = 1;
        if (end1 || end2) return end1 && end2 ? 2 : end1 ? 1 : 3;
        a = fold(a, ic); b = fold(b, ic);
        if (a != b) return a < b ? 1 : 3;
    }
}
static uint32_t k_CompareStringA(CPU *c, const uint32_t *args) { UNUSED; return compare(args, 0); }
static uint32_t k_CompareStringW(CPU *c, const uint32_t *args) { UNUSED; return compare(args, 1); }

/* ---- LocalFree: LocalAlloc and the buffers FormatMessage allocates come from the guest heap ---- */
static uint32_t k_LocalFree(CPU *c, const uint32_t *args)
{
    UNUSED;
    if (!ARG(0)) return 0;
    if (h2_heap_free(ARG(0)) != 0) { g_h2_last_error = 6; return ARG(0); }     /* failure returns the handle */
    return 0;
}

/* ---- FormatMessageA ----
 * Only system messages (FORMAT_MESSAGE_FROM_SYSTEM 0x1000) are answered, and with a generic text: Windows' own
 * message tables are not available here. Intended for log lines; the text is NOT Windows' wording. Message
 * strings with inserts (FROM_STRING 0x400 / FROM_HMODULE 0x800) are not implemented. */
static uint32_t k_FormatMessageA(CPU *c, const uint32_t *args)
{
    UNUSED;
    uint32_t flags = ARG(0), id = ARG(2), buf = ARG(4), size = ARG(5);
    char text[64];
    if (!(flags & 0x1000u) || (flags & 0xC00u))
        h2_guest_stop(H2_STOP_UNIMPLEMENTED_IMPORT, "FormatMessageA: flags %08X are not implemented (only FORMAT_MESSAGE_FROM_SYSTEM)", flags);
    int n = snprintf(text, sizeof text, "Windows error %u.\r\n", id);
    if (flags & 0x100u) {                                                      /* ALLOCATE_BUFFER: buf is a LPSTR* */
        uint32_t p = h2_heap_alloc((uint32_t)n + 1, 0);
        if (!p) { g_h2_last_error = 8; return 0; }
        memcpy(guest_ptr(p), text, (size_t)n + 1);
        wr32(buf, p);
        return (uint32_t)n;
    }
    if (!buf || size < (uint32_t)n + 1) { g_h2_last_error = 122; return 0; }
    memcpy(guest_ptr(buf), text, (size_t)n + 1);
    return (uint32_t)n;
}

/* ---- VirtualQuery(addr, MEMORY_BASIC_INFORMATION*, len) ---- */
static uint32_t k_VirtualQuery(CPU *c, const uint32_t *args)
{
    UNUSED;
    uint32_t a = ARG(0), mbi = ARG(1), sz = 0;
    if (ARG(2) < 28 || !mbi) { g_h2_last_error = 24; return 0; }               /* ERROR_BAD_LENGTH */
    uint32_t base = a & ~0xFFFu, size = 0x1000, state = 0x10000 /* MEM_FREE */, prot = 1 /* PAGE_NOACCESS */, type = 0;
    uint32_t vb = h2_virtual_find(a, &sz);
    if (vb) { base = vb; size = sz; state = 0x1000; prot = 4; type = 0x20000; }          /* MEM_COMMIT, PAGE_READWRITE, MEM_PRIVATE */
    else if (a >= H2_STACK_TOP - H2_STACK_SIZE && a < H2_STACK_TOP) { base = H2_STACK_TOP - H2_STACK_SIZE; size = H2_STACK_SIZE; state = 0x1000; prot = 4; type = 0x20000; }
    else if (h2_image_contains(a, 1)) { state = 0x1000; prot = 0x40; type = 0x1000000; }  /* image: executable and writable here */
    wr32(mbi, base); wr32(mbi + 4, state == 0x10000 ? 0 : base); wr32(mbi + 8, state == 0x10000 ? 0 : prot);
    wr32(mbi + 12, size); wr32(mbi + 16, state); wr32(mbi + 20, state == 0x10000 ? 1 : prot); wr32(mbi + 24, type);
    return 28;
}

/* ---- ReadProcessMemory: only the current process (the pseudo handle) ---- */
static uint32_t k_ReadProcessMemory(CPU *c, const uint32_t *args)
{
    UNUSED;
    if (ARG(0) != 0xFFFFFFFFu) { g_h2_last_error = 6; return 0; }
    if (ARG(2) == 0 || ARG(3) == 0) { g_h2_last_error = 87; return 0; }
    memmove(guest_ptr(ARG(2)), guest_ptr(ARG(1)), ARG(3));
    if (ARG(4)) wr32(ARG(4), ARG(3));
    return 1;
}

/* ---- global atoms: a small in-memory table (case-insensitive, reference counted) ---- */
static int strcasecmp_like(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) if (fold((unsigned char)*a, 1) != fold((unsigned char)*b, 1)) return 1;
    return *a != *b;
}

#define MAX_ATOMS 64
static struct { char name[256]; unsigned refs; } g_atoms[MAX_ATOMS];

static uint32_t k_GlobalAddAtomA(CPU *c, const uint32_t *args)
{
    UNUSED;
    const char *n = (const char *)guest_ptr(ARG(0));
    if (ARG(0) < 0x10000u) return ARG(0);                                      /* MAKEINTATOM */
    for (int i = 0; i < MAX_ATOMS; i++)
        if (g_atoms[i].refs && strcasecmp_like(g_atoms[i].name, n) == 0) { g_atoms[i].refs++; return 0xC000u + (uint32_t)i; }
    for (int i = 0; i < MAX_ATOMS; i++)
        if (!g_atoms[i].refs) { snprintf(g_atoms[i].name, sizeof g_atoms[i].name, "%s", n); g_atoms[i].refs = 1; return 0xC000u + (uint32_t)i; }
    g_h2_last_error = 8; return 0;
}
static uint32_t k_GlobalDeleteAtom(CPU *c, const uint32_t *args)
{
    UNUSED;
    uint32_t i = ARG(0) - 0xC000u;
    if (i < MAX_ATOMS && g_atoms[i].refs) { g_atoms[i].refs--; return 0; }
    g_h2_last_error = 6; return ARG(0);
}
static uint32_t k_GlobalGetAtomNameA(CPU *c, const uint32_t *args)
{
    UNUSED;
    uint32_t i = ARG(0) - 0xC000u;
    if (i >= MAX_ATOMS || !g_atoms[i].refs) { g_h2_last_error = 6; return 0; }
    size_t n = strlen(g_atoms[i].name);
    if (!ARG(1) || ARG(2) == 0) { g_h2_last_error = 122; return 0; }
    if (n >= ARG(2)) n = ARG(2) - 1;
    memcpy(guest_ptr(ARG(1)), g_atoms[i].name, n); wr8(ARG(1) + (uint32_t)n, 0);
    return (uint32_t)n;
}

/* ---- DOS date/time <-> FILETIME (the time is treated as UTC; there is no time zone in this layer) ---- */
static int64_t days_from_civil(int64_t y, unsigned m, unsigned d)
{
    y -= m <= 2;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400), doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1, doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int64_t)doe - 719468;
}
static void civil_from_days(int64_t z, int *y, unsigned *m, unsigned *d)
{
    z += 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = (unsigned)(z - era * 146097), yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t yy = (int64_t)yoe + era * 400;
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153;
    *d = doy - (153 * mp + 2) / 5 + 1; *m = mp < 10 ? mp + 3 : mp - 9; *y = (int)(yy + (*m <= 2));
}
/* BOOL DosDateTimeToFileTime(WORD date, WORD time, FILETIME *out) */
static uint32_t k_DosDateTimeToFileTime(CPU *c, const uint32_t *args)
{
    UNUSED;
    unsigned date = ARG(0) & 0xFFFFu, time = ARG(1) & 0xFFFFu;
    unsigned y = 1980 + (date >> 9), mo = (date >> 5) & 15, d = date & 31, h = time >> 11, mi = (time >> 5) & 63, s = (time & 31) * 2;
    if (!mo || mo > 12 || !d || h > 23 || mi > 59 || s > 59 || !ARG(2)) { g_h2_last_error = 87; return 0; }
    uint64_t secs = (uint64_t)((days_from_civil(y, mo, d) * 86400) + h * 3600 + mi * 60 + s);
    uint64_t ft = (secs + 11644473600ull) * 10000000ull;
    wr32(ARG(2), (uint32_t)ft); wr32(ARG(2) + 4, (uint32_t)(ft >> 32));
    return 1;
}
/* BOOL FileTimeToDosDateTime(const FILETIME *, WORD *date, WORD *time) */
static uint32_t k_FileTimeToDosDateTime(CPU *c, const uint32_t *args)
{
    UNUSED;
    if (!ARG(0) || !ARG(1) || !ARG(2)) { g_h2_last_error = 87; return 0; }
    uint64_t ft = (uint64_t)rd32(ARG(0)) | ((uint64_t)rd32(ARG(0) + 4) << 32);
    int64_t secs = (int64_t)(ft / 10000000ull) - 11644473600ll;
    int y; unsigned m, d;
    civil_from_days(secs / 86400 - (secs % 86400 < 0), &y, &m, &d);
    if (y < 1980 || y > 2107) { g_h2_last_error = 87; return 0; }
    unsigned sod = (unsigned)(((secs % 86400) + 86400) % 86400);
    uint32_t date = ((uint32_t)(y - 1980) << 9) | (m << 5) | d, time = ((sod / 3600) << 11) | (((sod / 60) % 60) << 5) | ((sod % 60) / 2);
    wr8(ARG(1), (uint8_t)date); wr8(ARG(1) + 1, (uint8_t)(date >> 8));
    wr8(ARG(2), (uint8_t)time); wr8(ARG(2) + 1, (uint8_t)(time >> 8));
    return 1;
}

/* ---- console and exception services ---- */
/* The handler is accepted and never called: a guest without a console receives no Ctrl-C / close events. */
static uint32_t k_SetConsoleCtrlHandler(CPU *c, const uint32_t *args) { UNUSED; (void)args; return 1; }
static uint32_t k_SetStdHandle(CPU *c, const uint32_t *args) { UNUSED; (void)args; return 1; }   /* the guest has no console to redirect */

static uint32_t k_UnhandledExceptionFilter(CPU *c, const uint32_t *args)
{
    UNUSED;
    h2_guest_stop(H2_STOP_UNIMPLEMENTED_IMPORT, "UnhandledExceptionFilter(%08X): an exception reached the top-level filter (structured exception handling is not implemented)", ARG(0));
}
static uint32_t k_RtlUnwind(CPU *c, const uint32_t *args)
{
    UNUSED;
    h2_guest_stop(H2_STOP_UNIMPLEMENTED_IMPORT, "RtlUnwind: structured exception handling / C++ exception unwinding is not implemented (target frame %08X)", ARG(0));
}

static const H2ImportDef k_defs[] = {
#define K(name, n) { "KERNEL32.dll", #name, n, k_##name }
    K(CompareStringA, 6), K(CompareStringW, 6), K(LocalFree, 1), K(FormatMessageA, 7), K(VirtualQuery, 3), K(ReadProcessMemory, 5),
    K(GlobalAddAtomA, 1), K(GlobalDeleteAtom, 1), K(GlobalGetAtomNameA, 3), K(DosDateTimeToFileTime, 3), K(FileTimeToDosDateTime, 3),
    K(SetConsoleCtrlHandler, 2), K(SetStdHandle, 2), K(UnhandledExceptionFilter, 1), K(RtlUnwind, 4),
#undef K
};

void h2_register_system(void) { h2_imports_register(k_defs, sizeof k_defs / sizeof k_defs[0]); }
