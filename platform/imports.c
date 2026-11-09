/* imports.c - registry of implemented imports and the thunks IAT slots point at. */
#include <stdio.h>
#include <string.h>

#include "h2_platform.h"

#define MAX_DEFS  1024
#define MAX_BOUND 2048

static const H2ImportDef *g_defs[MAX_DEFS];
static size_t g_ndefs;

typedef struct { char dll[32]; char name[96]; const H2ImportDef *def; uint64_t calls; } Bound;
static Bound g_bound[MAX_BOUND];
static unsigned g_nbound;

int g_h2_trace_imports = 0;

static int ieq(const char *a, const char *b)
{
    for (;; a++, b++) {
        char x = *a, y = *b;
        if (x >= 'A' && x <= 'Z') x = (char)(x + 32);
        if (y >= 'A' && y <= 'Z') y = (char)(y + 32);
        if (x != y) return 0;
        if (!x) return 1;
    }
}

void h2_imports_register(const H2ImportDef *defs, size_t count)
{
    for (size_t i = 0; i < count && g_ndefs < MAX_DEFS; i++) g_defs[g_ndefs++] = &defs[i];
}

uint32_t h2_import_bind(const char *dll, const char *name)
{
    unsigned i;
    for (i = 0; i < g_nbound; i++)
        if (ieq(g_bound[i].dll, dll) && strcmp(g_bound[i].name, name) == 0)
            return H2_THUNK_BASE + i * H2_THUNK_STRIDE;
    if (g_nbound == MAX_BOUND)
        h2_guest_stop(H2_STOP_INTERNAL, "import table full binding %s!%s", dll, name);
    Bound *b = &g_bound[g_nbound];
    snprintf(b->dll, sizeof b->dll, "%s", dll);
    snprintf(b->name, sizeof b->name, "%s", name);
    b->def = 0; b->calls = 0;
    for (size_t k = 0; k < g_ndefs; k++)
        if (ieq(g_defs[k]->dll, dll) && strcmp(g_defs[k]->name, name) == 0) { b->def = g_defs[k]; break; }
    return H2_THUNK_BASE + (g_nbound++) * H2_THUNK_STRIDE;
}

int h2_import_dispatch(CPU *c, uint32_t va)
{
    uint32_t args[16];
    if (va < H2_THUNK_BASE || (va - H2_THUNK_BASE) % H2_THUNK_STRIDE) return 0;
    unsigned idx = (va - H2_THUNK_BASE) / H2_THUNK_STRIDE;
    if (idx >= g_nbound) return 0;
    Bound *b = &g_bound[idx];
    uint32_t ret_addr = rd32(c->esp);
    b->calls++;
    if (!b->def)
        h2_guest_stop(H2_STOP_UNIMPLEMENTED_IMPORT, "%s!%s is not implemented (called from guest %08X)",
                      b->dll, b->name, ret_addr);
    for (int i = 0; i < 16; i++) args[i] = rd32(c->esp + 4u + 4u * (uint32_t)i);
    uint32_t r = b->def->fn(c, args);
    c->eax = r;
    if (g_h2_trace_imports)
        h2_log(H2_LOG_WIN32, H2_LVL_DEBUG, "%s(%08X, %08X, %08X, %08X) -> %08X   [from %08X]",
               b->name, args[0], args[1], args[2], args[3], r, ret_addr);
    /* `ret N`: drop the return address the call site pushed, plus stdcall arguments. */
    c->esp += 4u + (b->def->nargs > 0 ? 4u * (uint32_t)b->def->nargs : 0u);
    return 1;
}

void h2_imports_stats(unsigned *bound, unsigned *implemented)
{
    unsigned n = 0;
    for (unsigned i = 0; i < g_nbound; i++) n += g_bound[i].def != 0;
    if (bound) *bound = g_nbound;
    if (implemented) *implemented = n;
}

size_t h2_imports_report(H2ImportStat *out, size_t max)
{
    size_t n = 0;
    for (unsigned i = 0; i < g_nbound && n < max; i++) {
        out[n].dll = g_bound[i].dll; out[n].name = g_bound[i].name;
        out[n].implemented = g_bound[i].def != 0; out[n].calls = g_bound[i].calls; n++;
    }
    return n;
}
