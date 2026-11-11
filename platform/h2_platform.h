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
int  h2_import_defined(const char *dll, const char *name); /* is there a native implementation? (creates no thunk) */
const char *h2_import_name_at(uint32_t guest_va);          /* "DLL!Name" of a thunk address, or NULL (valid until the next call) */
void h2_imports_stats(unsigned *bound, unsigned *implemented);
/* Call counts per bound import, for the boot report. Returns entries written. */
typedef struct { const char *dll, *name; int implemented; uint64_t calls; } H2ImportStat;
size_t h2_imports_report(H2ImportStat *out, size_t max);
/* When non-zero, every import call is logged (WIN32 category). */
extern int g_h2_trace_imports;

/* ---- PE loader ---- */
typedef struct {
    uint32_t base, size, entry;            /* entry == 0 if the image has none */
    uint32_t export_rva, export_size;
    unsigned sections, import_slots;
    int      is_dll;
    char     name[40];                     /* file name, e.g. "hitman2.exe" */
    char     path[260];
} H2Image;
/* Map a PE32 i386 image from a host file into guest memory at its preferred
 * base and bind its imports. Returns 0, or -1 with a logged reason. The
 * lifted code of the module must already be linked in (it is addressed by
 * guest VA). A DLL's entry point is NOT run here; LoadLibrary does that. */
int h2_pe_load(const char *host_path, H2Image *img);

/* Loaded-image registry. Pointers stay valid for the life of the process. */
const H2Image *h2_image_by_name(const char *name);         /* case-insensitive, directory part ignored */
const H2Image *h2_image_by_handle(uint32_t base);          /* HMODULE == image base */
int      h2_image_contains(uint32_t addr, uint32_t size);  /* [addr, addr+size) lies inside one loaded image */
uint32_t h2_image_export(const H2Image *img, const char *name_or_ordinal);   /* guest VA, or 0; ordinals as "#n" */

/* Resolve a guest path to a host file for reading (user root first, then the
 * read-only game install). 0 on success. */
int h2_files_resolve_read(const char *guest_path, char *host, size_t cap);

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
/* ---- virtual display, input and the message queue (win32_user32.c) ---- */
void h2_display_configure(int width, int height);           /* what GetSystemMetrics reports; default 800x450 */
void h2_display_size(int *width, int *height);
int  h2_post_message(uint32_t hwnd, uint32_t msg, uint32_t wparam, uint32_t lparam);
uint32_t h2_main_window(void);                              /* the active guest window, or 0 */
/* Call a guest stdcall function with up to 8 uint32_t arguments. */
uint32_t h2_call_guest(CPU *c, uint32_t fn, int nargs, ...);
void h2_register_user32(void);
void h2_register_game_dlls(void);
void h2_register_advapi32(void);                           /* registry: in-memory, empty unless seeded */
int  h2_registry_set_string(const char *path, const char *name, const char *value);   /* path like "HKLM\\Software\\X" */
int  h2_registry_set_dword(const char *path, const char *name, uint32_t value);
void h2_register_dsound(void);                              /* DirectSound: device enumeration only, so far */
void h2_register_dinput8(void);                           /* DirectInput 8 over input/input_manager.h */
/* ---- guest threads (win32_threads.c) ----
 * Cooperative: exactly one guest thread runs at a time, each on its own host thread, and control moves only
 * inside Sleep / Wait* / SwitchToThread / a contended critical section / thread exit. Deterministic and
 * single-core by construction; thread priorities are recorded and have no effect. */
uint32_t h2_thread_tid(void);                                    /* id of the running guest thread (main = 0x104) */
uint32_t h2_wait_objects(CPU *c, unsigned n, const uint32_t *handles, int wait_all, uint32_t timeout_ms);
void     h2_thread_sleep(CPU *c, uint32_t ms);                   /* ms == 0 yields */
void     h2_thread_block_on_cs(CPU *c, uint32_t cs);             /* returns when the critical section's owner field is free */
/* How a secondary guest thread runs its entry point under the host's fault guard; on a fault it must call
 * h2_threads_forward_stop(reason, message) (never returns). The default runner is an unguarded dispatch. */
typedef void (*h2_thread_runner)(CPU *c, uint32_t entry);
void h2_threads_set_runner(h2_thread_runner fn);
void h2_threads_forward_stop(h2_stop_reason why, const char *message);
void h2_register_threads(void);
void h2_register_objects(void);
void h2_register_files(void);
void h2_register_kernel32(void);
void h2_kernel32_attach(const H2Image *img);               /* after h2_pe_load + h2_process_init */

#ifdef __cplusplus
}
#endif
#endif /* H2_PLATFORM_H */
