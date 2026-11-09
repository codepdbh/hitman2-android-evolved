/*
 * guest_mem.h - the guest (x86-32) address space on a host of any pointer width.
 *
 * A guest address is a uint32_t. It is never a host pointer. The whole 4 GiB
 * guest space is RESERVED once (no physical memory, no commit charge) and
 * pages are committed where the guest actually has something: the PE image,
 * stacks, heaps. A guest address `a` lives at host address g_guest_base + a.
 *
 * Because the reservation covers every value a uint32_t can hold (plus a guard
 * tail for a multi-byte access that starts at 0xFFFFFFFF), no guest address -
 * however corrupt - can reach host memory outside the reservation. A bad guest
 * pointer faults on an uncommitted guest page, which is the same failure the
 * original program would have had, at the same guest address.
 *
 * Works identically on x86-64 Windows (test host) and ARM64 Android (target).
 */
#ifndef H2_GUEST_MEM_H
#define H2_GUEST_MEM_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t gaddr_t;            /* a guest address; do not cast to a pointer */

#define GUEST_SPACE_SIZE   (1ull << 32)
#define GUEST_GUARD_SIZE   (1ull << 16)
#define GUEST_PAGE_SIZE    0x1000u

enum { GUEST_PROT_NONE = 0, GUEST_PROT_R = 1, GUEST_PROT_RW = 3 };

extern uint8_t *g_guest_base;        /* host address of guest address 0 */

/* The one sanctioned guest->host conversion. Used by the generated cpu.h. */
#define GUEST_HOST(a) ((uintptr_t)g_guest_base + (uintptr_t)(uint32_t)(a))

static inline void *guest_ptr(gaddr_t a) { return (void *)GUEST_HOST(a); }

/* Reserve the guest space. Returns 0 on success. Idempotent. */
int  guest_mem_init(void);
void guest_mem_shutdown(void);

/* Commit [addr, addr+size) (rounded out to pages), zero-filled. 0 on success. */
int  guest_mem_commit(gaddr_t addr, uint32_t size, int prot);
/* Change protection of already-committed pages. 0 on success. */
int  guest_mem_protect(gaddr_t addr, uint32_t size, int prot);
/* Return pages to the reserved-only state. */
int  guest_mem_decommit(gaddr_t addr, uint32_t size);

#ifdef __cplusplus
}
#endif
#endif /* H2_GUEST_MEM_H */
