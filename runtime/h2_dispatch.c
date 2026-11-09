/*
 * h2_dispatch.c - guest control transfers: address -> lifted body or import.
 *
 * The table is the X-macro the lift driver writes (funcs_list.h, sorted by
 * address), so this file must be compiled with the generated directory on the
 * include path. It is the only place where a guest code address is looked up.
 */
#include "recomp_prelude.h"
#include "h2_log.h"
#include "h2_dispatch.h"

#include "funcs_list.h"

#define X(a) void L_##a(CPU *c);
LIFTED_FUNCS(X)
#undef X

typedef void (*lifted_fn)(CPU *);
static const struct { uint32_t va; lifted_fn fn; } k_funcs[] = {
#define X(a) { 0x##a##u, L_##a },
LIFTED_FUNCS(X)
#undef X
};
#define N_FUNCS (sizeof k_funcs / sizeof k_funcs[0])

static h2_import_hook g_import_hook;
static uint64_t g_count, g_budget;
static uint32_t g_trail[H2_TRAIL_LEN];
static unsigned g_trail_pos;

void h2_dispatch_set_import_hook(h2_import_hook hook) { g_import_hook = hook; }
void h2_dispatch_set_budget(uint64_t max_dispatches) { g_budget = max_dispatches; }
uint64_t h2_dispatch_count(void) { return g_count; }
size_t h2_dispatch_function_count(void) { return N_FUNCS; }

size_t h2_dispatch_trail(uint32_t *out, size_t max)
{
    size_t n = g_count < H2_TRAIL_LEN ? (size_t)g_count : H2_TRAIL_LEN, i;
    if (n > max) n = max;
    for (i = 0; i < n; i++)                         /* oldest first */
        out[i] = g_trail[(g_trail_pos + H2_TRAIL_LEN - n + i) % H2_TRAIL_LEN];
    return n;
}

int h2_dispatch_has(uint32_t va)
{
    size_t lo = 0, hi = N_FUNCS;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        if (k_funcs[mid].va == va) return 1;
        if (k_funcs[mid].va < va) lo = mid + 1; else hi = mid;
    }
    return 0;
}

void dispatch(CPU *c, uint32_t va)
{
    size_t lo = 0, hi = N_FUNCS;
    g_trail[g_trail_pos] = va; g_trail_pos = (g_trail_pos + 1) % H2_TRAIL_LEN;
    if (++g_count == g_budget)
        h2_guest_stop(H2_STOP_BUDGET, "dispatch budget of %llu reached at guest %08X",
                      (unsigned long long)g_budget, va);
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        if (k_funcs[mid].va == va) { k_funcs[mid].fn(c); return; }
        if (k_funcs[mid].va < va) lo = mid + 1; else hi = mid;
    }
    if (g_import_hook && g_import_hook(c, va)) return;
    /* The lifted call site already pushed its return address: [esp] names the caller. */
    h2_guest_stop(H2_STOP_UNRESOLVED, "no lifted body for guest %08X (return address %08X)", va, rd32(c->esp));
}

void dispatch_jmp(CPU *c, uint32_t va) { dispatch(c, va); }
