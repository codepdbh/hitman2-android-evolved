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

#include "funcs_all.h"      /* every lifted module, ascending by guest address (scripts/lift_image.py) */

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

/* Diagnostic: report registers and the first stack words every time one of these addresses is entered. */
static uint32_t g_watch[8];
static unsigned g_nwatch;
void h2_dispatch_watch(uint32_t va) { if (g_nwatch < 8) g_watch[g_nwatch++] = va; }
/* ... or every time something is entered whose return address (top of stack) is `ret`. */
static uint32_t g_watch_ret[8];
static unsigned g_nwatch_ret;
void h2_dispatch_watch_ret(uint32_t ret) { if (g_nwatch_ret < 8) g_watch_ret[g_nwatch_ret++] = ret; }

/* Memory watch: checked at every dispatch, so the report names the dispatch AFTER which the value changed. */
static const char *(*g_import_name)(uint32_t);
void h2_dispatch_set_name_resolver(const char *(*fn)(uint32_t)) { g_import_name = fn; }
static uint32_t g_wmem_addr[4], g_wmem_val[4];
static unsigned g_nwmem;
static uint32_t g_wmem_prev_va;
void h2_dispatch_watch_mem(uint32_t addr) { if (g_nwmem < 4) { g_wmem_addr[g_nwmem] = addr; g_wmem_val[g_nwmem++] = 0xDEADBEEFu; } }

void dispatch(CPU *c, uint32_t va)
{
    size_t lo = 0, hi = N_FUNCS;
    g_trail[g_trail_pos] = va; g_trail_pos = (g_trail_pos + 1) % H2_TRAIL_LEN;
    for (unsigned i = 0; i < g_nwmem; i++) {
        uint32_t now = rd32(g_wmem_addr[i]);
        if (now != g_wmem_val[i]) {
            const char *nm = g_import_name ? g_import_name(g_wmem_prev_va) : 0;
            h2_log(H2_LOG_MEMORY, H2_LVL_INFO, "MEMWATCH [%08X] %08X -> %08X, changed since the dispatch of %08X%s%s%s (now entering %08X, #%llu, esp=%08X)",
                   g_wmem_addr[i], g_wmem_val[i], now, g_wmem_prev_va, nm ? " (" : "", nm ? nm : "", nm ? ")" : "", va, (unsigned long long)g_count, c->esp);
            g_wmem_val[i] = now;
        }
    }
    g_wmem_prev_va = va;
    for (unsigned i = 0; i < g_nwatch_ret; i++)
        if (rd32(c->esp) == g_watch_ret[i])
            h2_log(H2_LOG_CPU, H2_LVL_INFO, "WATCH-RET %08X entered %08X #%llu esi=%08X ebx=%08X edi=%08X esp=%08X", g_watch_ret[i], va,
                   (unsigned long long)g_count, c->esi, c->ebx, c->edi, c->esp);
    for (unsigned i = 0; i < g_nwatch; i++)
        if (g_watch[i] == va)
            h2_log(H2_LOG_CPU, H2_LVL_INFO, "WATCH %08X #%llu eax=%08X ecx=%08X edx=%08X ebx=%08X esi=%08X edi=%08X ebp=%08X esp=%08X [esp]=%08X %08X %08X %08X",
                   va, (unsigned long long)g_count, c->eax, c->ecx, c->edx, c->ebx, c->esi, c->edi, c->ebp, c->esp,
                   rd32(c->esp), rd32(c->esp + 4), rd32(c->esp + 8), rd32(c->esp + 12));
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
