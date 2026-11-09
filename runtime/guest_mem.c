/* guest_mem.c - see guest_mem.h. Windows (VirtualAlloc) and POSIX (mmap) hosts. */
#include "guest_mem.h"

uint8_t *g_guest_base = 0;

static uint32_t page_floor(uint32_t a) { return a & ~(GUEST_PAGE_SIZE - 1u); }
/* 64-bit so that a range ending at 4 GiB does not wrap to 0. */
static uint64_t page_span(uint32_t addr, uint32_t size)
{
    uint64_t end = ((uint64_t)addr + size + GUEST_PAGE_SIZE - 1u) & ~(uint64_t)(GUEST_PAGE_SIZE - 1u);
    return end - page_floor(addr);
}

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static DWORD win_prot(int prot)
{
    return prot == GUEST_PROT_RW ? PAGE_READWRITE : prot == GUEST_PROT_R ? PAGE_READONLY : PAGE_NOACCESS;
}

int guest_mem_init(void)
{
    if (g_guest_base) return 0;
    g_guest_base = (uint8_t *)VirtualAlloc(NULL, (SIZE_T)(GUEST_SPACE_SIZE + GUEST_GUARD_SIZE),
                                           MEM_RESERVE, PAGE_NOACCESS);
    return g_guest_base ? 0 : -1;
}

void guest_mem_shutdown(void)
{
    if (g_guest_base) VirtualFree(g_guest_base, 0, MEM_RELEASE);
    g_guest_base = 0;
}

int guest_mem_commit(gaddr_t addr, uint32_t size, int prot)
{
    if (!g_guest_base || !size) return -1;
    return VirtualAlloc(g_guest_base + page_floor(addr), (SIZE_T)page_span(addr, size),
                        MEM_COMMIT, win_prot(prot)) ? 0 : -1;
}

int guest_mem_protect(gaddr_t addr, uint32_t size, int prot)
{
    DWORD old;
    if (!g_guest_base || !size) return -1;
    return VirtualProtect(g_guest_base + page_floor(addr), (SIZE_T)page_span(addr, size),
                          win_prot(prot), &old) ? 0 : -1;
}

int guest_mem_decommit(gaddr_t addr, uint32_t size)
{
    if (!g_guest_base || !size) return -1;
    return VirtualFree(g_guest_base + page_floor(addr), (SIZE_T)page_span(addr, size), MEM_DECOMMIT) ? 0 : -1;
}

#else  /* POSIX: Android, Linux */
#include <sys/mman.h>

#ifndef MAP_NORESERVE
#define MAP_NORESERVE 0
#endif

static int posix_prot(int prot)
{
    return prot == GUEST_PROT_RW ? (PROT_READ | PROT_WRITE) : prot == GUEST_PROT_R ? PROT_READ : PROT_NONE;
}

int guest_mem_init(void)
{
    void *p;
    if (g_guest_base) return 0;
    p = mmap(NULL, (size_t)(GUEST_SPACE_SIZE + GUEST_GUARD_SIZE), PROT_NONE,
             MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (p == MAP_FAILED) return -1;
    g_guest_base = (uint8_t *)p;
    return 0;
}

void guest_mem_shutdown(void)
{
    if (g_guest_base) munmap(g_guest_base, (size_t)(GUEST_SPACE_SIZE + GUEST_GUARD_SIZE));
    g_guest_base = 0;
}

int guest_mem_commit(gaddr_t addr, uint32_t size, int prot)
{
    if (!g_guest_base || !size) return -1;
    /* Anonymous private pages are zero-filled on first touch. */
    return mprotect(g_guest_base + page_floor(addr), (size_t)page_span(addr, size), posix_prot(prot));
}

int guest_mem_protect(gaddr_t addr, uint32_t size, int prot)
{
    return guest_mem_commit(addr, size, prot);
}

int guest_mem_decommit(gaddr_t addr, uint32_t size)
{
    void *p;
    if (!g_guest_base || !size) return -1;
    /* Replace with fresh PROT_NONE pages: drops the contents and the RSS. */
    p = mmap(g_guest_base + page_floor(addr), (size_t)page_span(addr, size), PROT_NONE,
             MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED, -1, 0);
    return p == MAP_FAILED ? -1 : 0;
}
#endif
