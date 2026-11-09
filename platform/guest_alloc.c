/*
 * guest_alloc.c - memory the "OS" gives the guest: VirtualAlloc regions, the
 * process heap, and OS-owned data (command line, environment, ...).
 *
 * Bookkeeping lives on the host side; only an 8-byte header (size, tag) sits
 * in front of each heap block in guest memory, so a guest overrun is detected
 * on free instead of corrupting the allocator's own lists.
 *
 * Single-threaded for now: h2_process_init is the only guest thread. This must
 * get a lock before CreateThread is implemented.
 */
#include <stdlib.h>
#include <string.h>

#include "h2_platform.h"

/* ---- regions (VirtualAlloc) ---- */
#define REGION_GRAN 0x10000u
#define MAX_REGIONS 4096
typedef struct { uint32_t addr, size; } Region;
static Region g_regions[MAX_REGIONS];
static unsigned g_nregions;
static uint32_t g_region_next = H2_ARENA_BASE;

uint32_t h2_virtual_alloc(uint32_t size)
{
    if (!size || size > H2_ARENA_END - H2_ARENA_BASE) return 0;
    uint32_t span = (size + REGION_GRAN - 1) & ~(REGION_GRAN - 1);
    if (g_nregions == MAX_REGIONS || span > H2_ARENA_END - g_region_next) return 0;   /* no reuse of freed space yet */
    uint32_t addr = g_region_next;
    if (guest_mem_commit(addr, size, GUEST_PROT_RW) != 0) return 0;
    g_region_next += span;
    g_regions[g_nregions].addr = addr; g_regions[g_nregions].size = size; g_nregions++;
    return addr;
}

uint32_t h2_virtual_size(uint32_t addr)
{
    for (unsigned i = 0; i < g_nregions; i++) if (g_regions[i].addr == addr) return g_regions[i].size;
    return 0;
}

uint32_t h2_virtual_find(uint32_t addr, uint32_t *size)
{
    for (unsigned i = 0; i < g_nregions; i++)
        if (addr >= g_regions[i].addr && addr - g_regions[i].addr < g_regions[i].size) {
            if (size) *size = g_regions[i].size;
            return g_regions[i].addr;
        }
    return 0;
}

int h2_virtual_free(uint32_t addr)
{
    for (unsigned i = 0; i < g_nregions; i++)
        if (g_regions[i].addr == addr) {
            guest_mem_decommit(addr, g_regions[i].size);
            g_regions[i] = g_regions[--g_nregions];
            return 0;
        }
    return -1;
}

/* ---- heap ---- */
#define HDR        8u
#define TAG_LIVE   0x48324C56u      /* "H2LV" */
#define TAG_FREE   0x48324652u      /* "H2FR" */
#define CHUNK      0x00400000u
#define CLASSES    64               /* 16-byte classes up to 1 KiB, then exact-size lists by search */

typedef struct FreeNode { uint32_t addr, size; struct FreeNode *next; } FreeNode;
static FreeNode *g_free_small[CLASSES + 1];
static FreeNode *g_free_large;
static uint32_t g_chunk_cur, g_chunk_end;

static uint32_t round16(uint32_t n) { return (n + 15u) & ~15u; }

uint32_t h2_heap_alloc(uint32_t size, int zero)
{
    if (size > 0x40000000u) return 0;
    uint32_t need = round16(size ? size : 1);
    uint32_t block = 0;
    FreeNode **list = need / 16 <= CLASSES ? &g_free_small[need / 16] : &g_free_large;
    for (FreeNode **p = list; *p; p = &(*p)->next)
        if ((*p)->size == need) { FreeNode *n = *p; block = n->addr; *p = n->next; free(n); break; }
    if (!block) {
        if (need + HDR > CHUNK / 2) {                           /* big blocks get their own region */
            block = h2_virtual_alloc(need + HDR);
            if (!block) return 0;
        } else {
            if (g_chunk_end - g_chunk_cur < need + HDR) {
                g_chunk_cur = h2_virtual_alloc(CHUNK);
                if (!g_chunk_cur) { g_chunk_end = 0; return 0; }
                g_chunk_end = g_chunk_cur + CHUNK;
            }
            block = g_chunk_cur; g_chunk_cur += need + HDR;
        }
    }
    /* The header records the rounded size, which is what h2_heap_size reports
     * (a capacity, >= the size requested). Blocks are always handed out
     * zeroed, recycled ones included, so `zero` needs no separate path and a
     * guest reading memory it never wrote behaves the same on every run. */
    (void)zero;
    wr32(block, need); wr32(block + 4, TAG_LIVE);
    memset(guest_ptr(block + HDR), 0, need);
    return block + HDR;
}

uint32_t h2_heap_size(uint32_t addr)
{
    if (addr < H2_ARENA_BASE + HDR || addr >= H2_ARENA_END || rd32(addr - 4) != TAG_LIVE) return 0xFFFFFFFFu;
    return rd32(addr - HDR);
}

int h2_heap_free(uint32_t addr)
{
    uint32_t size = h2_heap_size(addr);
    if (size == 0xFFFFFFFFu) {
        H2_ERROR(H2_LOG_MEMORY, "HeapFree of %08X: not a live heap block (double free or overrun)", addr);
        return -1;
    }
    FreeNode *n = (FreeNode *)malloc(sizeof *n);
    if (!n) return -1;
    wr32(addr - 4, TAG_FREE);
    n->addr = addr - HDR; n->size = size;
    FreeNode **list = size / 16 <= CLASSES ? &g_free_small[size / 16] : &g_free_large;
    n->next = *list; *list = n;
    return 0;
}

uint32_t h2_heap_realloc(uint32_t addr, uint32_t size, int zero)
{
    uint32_t old = h2_heap_size(addr);
    if (old == 0xFFFFFFFFu) return 0;
    if (round16(size ? size : 1) <= old) return addr;           /* fits in place */
    uint32_t fresh = h2_heap_alloc(size, zero);
    if (!fresh) return 0;
    memcpy(guest_ptr(fresh), guest_ptr(addr), old);
    h2_heap_free(addr);
    return fresh;
}

/* ---- OS-owned data ---- */
static uint32_t g_os_cur = H2_OSDATA_BASE;

uint32_t h2_os_alloc(uint32_t size)
{
    uint32_t need = (size + 7u) & ~7u;
    if (need > H2_OSDATA_BASE + H2_OSDATA_SIZE - g_os_cur)
        h2_guest_stop(H2_STOP_INTERNAL, "OS data area exhausted");
    uint32_t a = g_os_cur; g_os_cur += need;
    return a;
}

uint32_t h2_os_strdup(const char *s)
{
    uint32_t len = (uint32_t)strlen(s) + 1, a = h2_os_alloc(len);
    memcpy(guest_ptr(a), s, len);
    return a;
}
