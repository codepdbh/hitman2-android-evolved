/* process.c - the guest process: main-thread stack, simulated TIB, host clock. */
#include <string.h>
#include <time.h>

#include "h2_platform.h"

uint32_t g_h2_last_error = 0;

int h2_process_init(const H2Image *img)
{
    (void)img;
    if (guest_mem_commit(H2_STACK_TOP - H2_STACK_SIZE, H2_STACK_SIZE, GUEST_PROT_RW) != 0 ||
        guest_mem_commit(H2_TIB_ADDR, 0x1000, GUEST_PROT_RW) != 0 ||
        guest_mem_commit(H2_OSDATA_BASE, H2_OSDATA_SIZE, GUEST_PROT_RW) != 0) {
        H2_ERROR(H2_LOG_MEMORY, "cannot commit the guest stack / TIB / OS data");
        return -1;
    }
    /* NT_TIB as far as user code reads it. fs:[0] is the head of the SEH chain;
     * every MSVC function with a try block links a record here on entry. */
    wr32(H2_TIB_ADDR + 0x00, 0xFFFFFFFFu);                   /* ExceptionList: end of chain */
    wr32(H2_TIB_ADDR + 0x04, H2_STACK_TOP);                  /* StackBase */
    wr32(H2_TIB_ADDR + 0x08, H2_STACK_TOP - H2_STACK_SIZE);  /* StackLimit */
    wr32(H2_TIB_ADDR + 0x18, H2_TIB_ADDR);                   /* Self */
    wr32(H2_TIB_ADDR + 0x20, 0x00000100u);                   /* process id */
    wr32(H2_TIB_ADDR + 0x24, 0x00000104u);                   /* thread id */
    return 0;
}

void h2_cpu_init_main_thread(CPU *c)
{
    memset(c, 0, sizeof *c);
    c->esp = H2_STACK_TOP - 0x1000u;      /* leave room above for the caller's frame */
    c->fs_base = H2_TIB_ADDR;
    h2_cpu_reset_fpu(c);
}

uint64_t h2_host_monotonic_ns(void)
{
#if defined(_WIN32)
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);          /* UCRT; good enough for a test host */
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
#endif
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}
