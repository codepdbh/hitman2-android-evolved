/*
 * h2_platform.h - layer 3: what the recompiled program believes is Windows.
 *
 * Portable C over runtime/ (guest memory, CPU model, dispatcher). No Windows
 * headers and no Android headers: the same code is the Win32 answer on the PC
 * test host and on the phone.
 *
 * Rule of this layer: an imported function is either implemented with its
 * documented behaviour, or it is NOT implemented and calling it stops the
 * guest with its name (H2_STOP_UNIMPLEMENTED_IMPORT). There are no stubs that
 * return success for work they did not do.
 */
#ifndef H2_PLATFORM_H
#define H2_PLATFORM_H

#include <stddef.h>
#include <stdint.h>

#include "recomp_prelude.h"
#include "h2_log.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- guest address-space layout (all inside the 4 GiB reservation) ---- */
#define H2_STACK_TOP        0x0FF00000u   /* main thread stack grows down from here */
#define H2_STACK_SIZE       0x00400000u   /* 4 MiB committed */
#define H2_TIB_ADDR         0x0FF10000u   /* simulated TIB of the main thread */
#define H2_OSDATA_BASE      0x0FF20000u   /* strings/structs the "OS" hands to the guest */
#define H2_OSDATA_SIZE      0x00010000u
#define H2_ARENA_BASE       0x20000000u   /* VirtualAlloc / heap arenas */
#define H2_ARENA_END        0x70000000u
#define H2_THUNK_BASE       0xFE000000u   /* never committed: one fake address per bound import */
#define H2_THUNK_STRIDE     16u

/* ---- imports ---- */
/* args[i] is the i-th stdcall/cdecl stack argument. Return value goes to EAX.
 * nargs >= 0: stdcall, the dispatcher purges nargs*4 bytes.
 * nargs == H2_CDECL: caller cleans up (varargs such as wsprintfA). */
#define H2_CDECL (-1)
typedef uint32_t (*h2_import_fn)(CPU *c, const uint32_t *args);
typedef struct { const char *dll, *name; int nargs; h2_import_fn fn; } H2ImportDef;

void h2_imports_register(const H2ImportDef *defs, size_t count);
/* Bind (dll, name) to a thunk address for an IAT slot. Always succeeds: an
 * unimplemented import gets a thunk that stops the guest, naming it, if called. */
uint32_t h2_import_bind(const char *dll, const char *name);
int  h2_import_dispatch(CPU *c, uint32_t guest_va);        /* the dispatcher's import hook */
void h2_imports_stats(unsigned *bound, unsigned *implemented);
/* Call counts per bound import, for the boot report. Returns entries written. */
typedef struct { const char *dll, *name; int implemented; uint64_t calls; } H2ImportStat;
size_t h2_imports_report(H2ImportStat *out, size_t max);
/* When non-zero, every import call is logged (WIN32 category). */
extern int g_h2_trace_imports;

/* ---- PE loader ---- */
typedef struct {
    uint32_t base, size, entry;
    unsigned sections, import_slots;
    char     path[260];
} H2Image;
/* Map a PE32 i386 image from a host file into guest memory at its preferred
 * base and bind its imports. Returns 0, or -1 with a logged reason. */
int h2_pe_load(const char *host_path, H2Image *img);

/* ---- guest memory services ---- */
uint32_t h2_virtual_alloc(uint32_t size);                  /* page-granular, zeroed; 0 on failure */
int      h2_virtual_free(uint32_t addr);
uint32_t h2_virtual_size(uint32_t addr);                   /* 0 if not an allocation base */
uint32_t h2_virtual_find(uint32_t addr, uint32_t *size);   /* base of the region containing addr, or 0 */
uint64_t h2_host_monotonic_ns(void);
uint32_t h2_heap_alloc(uint32_t size, int zero);           /* 0 on failure */
uint32_t h2_heap_realloc(uint32_t addr, uint32_t size, int zero);
int      h2_heap_free(uint32_t addr);
uint32_t h2_heap_size(uint32_t addr);                      /* 0xFFFFFFFF if not a live block */
/* Copy a host string into OS-owned guest memory; returns its guest address. */
uint32_t h2_os_strdup(const char *s);
uint32_t h2_os_alloc(uint32_t size);

/* ---- guest process / thread ---- */
int  h2_process_init(const H2Image *img);                  /* stack, TIB, OS data */
void h2_cpu_init_main_thread(CPU *c);
extern uint32_t g_h2_last_error;                           /* GetLastError value of the (single) guest thread */

/* Host directories behind the guest's C:\Hitman2: the read-only game install
 * and the writable user root (created if missing). Call before running. */
void h2_files_configure(const char *game_root, const char *user_root);
void h2_register_user32(void);
void h2_register_objects(void);
void h2_register_files(void);
void h2_register_kernel32(void);
void h2_kernel32_attach(const H2Image *img);               /* after h2_pe_load + h2_process_init */

#ifdef __cplusplus
}
#endif
#endif /* H2_PLATFORM_H */
