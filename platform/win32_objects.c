/*
 * win32_objects.c - kernel handles: the handle table, mutexes, events and
 * WaitForSingleObject, under the current one-guest-thread model.
 *
 * With a single guest thread nothing else can ever signal an object, so a wait
 * that would block forever is reported as a stop instead of hanging silently.
 * This file is where real blocking goes when CreateThread is implemented.
 */
#include <string.h>

#include "h2_platform.h"
#include "h2_handles.h"

#define ARG(i) (args[(i)])
#define UNUSED (void)c
#define MAX_HANDLES 1024
#define HANDLE_BASE 0x00000100u

static H2Object g_objs[MAX_HANDLES];

uint32_t h2_handle_new(H2ObjType type, H2Object **out)
{
    for (unsigned i = 0; i < MAX_HANDLES; i++)
        if (g_objs[i].type == H2_OBJ_FREE) {
            memset(&g_objs[i], 0, sizeof g_objs[i]);
            g_objs[i].type = type;
            if (out) *out = &g_objs[i];
            return HANDLE_BASE + i * 4u;
        }
    H2_ERROR(H2_LOG_WIN32, "handle table full");
    return 0;
}

H2Object *h2_handle_get(uint32_t h, H2ObjType type)
{
    if (h < HANDLE_BASE || (h - HANDLE_BASE) % 4u) return 0;
    unsigned i = (h - HANDLE_BASE) / 4u;
    if (i >= MAX_HANDLES || g_objs[i].type == H2_OBJ_FREE) return 0;
    if (type != H2_OBJ_ANY && g_objs[i].type != type) return 0;
    return &g_objs[i];
}

static uint32_t find_named(H2ObjType type, const char *name)
{
    for (unsigned i = 0; i < MAX_HANDLES; i++)
        if (g_objs[i].type == type && g_objs[i].name[0] && strcmp(g_objs[i].name, name) == 0)
            return HANDLE_BASE + i * 4u;
    return 0;
}

static uint32_t k_CreateMutexA(CPU *c, const uint32_t *args)
{
    UNUSED;
    const char *name = ARG(2) ? (const char *)guest_ptr(ARG(2)) : "";
    H2Object *o;
    uint32_t h;
    if (name[0] && (h = find_named(H2_OBJ_MUTEX, name)) != 0) {
        g_h2_last_error = 183;                                  /* ERROR_ALREADY_EXISTS: same process opened it twice */
        return h;
    }
    if (!(h = h2_handle_new(H2_OBJ_MUTEX, &o))) { g_h2_last_error = 8; return 0; }
    strncpy(o->name, name, sizeof o->name - 1);
    o->u.sync.count = ARG(1) ? 1 : 0;                           /* bInitialOwner */
    o->u.sync.owner = ARG(1) ? (int)h2_thread_tid() : 0;
    g_h2_last_error = 0;                                        /* callers test GetLastError() == ERROR_ALREADY_EXISTS */
    return h;
}

static uint32_t k_CreateEventA(CPU *c, const uint32_t *args)
{
    UNUSED;
    const char *name = ARG(3) ? (const char *)guest_ptr(ARG(3)) : "";
    H2Object *o;
    uint32_t h;
    if (name[0] && (h = find_named(H2_OBJ_EVENT, name)) != 0) { g_h2_last_error = 183; return h; }
    if (!(h = h2_handle_new(H2_OBJ_EVENT, &o))) { g_h2_last_error = 8; return 0; }
    strncpy(o->name, name, sizeof o->name - 1);
    o->u.sync.manual = ARG(1) != 0; o->u.sync.count = ARG(2) != 0;
    g_h2_last_error = 0;
    return h;
}

static uint32_t k_SetEvent(CPU *c, const uint32_t *args)
{
    UNUSED;
    H2Object *o = h2_handle_get(ARG(0), H2_OBJ_EVENT);
    if (!o) { g_h2_last_error = 6; return 0; }
    o->u.sync.count = 1;
    return 1;
}

static uint32_t k_ResetEvent(CPU *c, const uint32_t *args)
{
    UNUSED;
    H2Object *o = h2_handle_get(ARG(0), H2_OBJ_EVENT);
    if (!o) { g_h2_last_error = 6; return 0; }
    o->u.sync.count = 0;
    return 1;
}

int h2_obj_signaled(H2Object *o, int tid)
{
    switch (o->type) {
    case H2_OBJ_EVENT:  return o->u.sync.count != 0;
    case H2_OBJ_MUTEX:  return o->u.sync.count == 0 || o->u.sync.owner == tid;
    case H2_OBJ_THREAD: return h2_thread_done(o->u.thread.index);
    default:            return 0;
    }
}

void h2_obj_acquire(H2Object *o, int tid)
{
    if (o->type == H2_OBJ_EVENT) { if (!o->u.sync.manual) o->u.sync.count = 0; }
    else if (o->type == H2_OBJ_MUTEX) { o->u.sync.count++; o->u.sync.owner = tid; }
}

static uint32_t k_WaitForSingleObject(CPU *c, const uint32_t *args)
{
    uint32_t h = ARG(0);
    return h2_wait_objects(c, 1, &h, 0, ARG(1));
}

/* (count, const HANDLE *handles, bWaitAll, timeout) */
static uint32_t k_WaitForMultipleObjects(CPU *c, const uint32_t *args)
{
    uint32_t n = ARG(0), h[16];
    if (n < 1 || n > 16 || !ARG(1)) { g_h2_last_error = 87; return 0xFFFFFFFFu; }
    for (uint32_t i = 0; i < n; i++) h[i] = rd32(ARG(1) + 4 * i);
    return h2_wait_objects(c, n, h, ARG(2) != 0, ARG(3));
}

static uint32_t k_ReleaseMutex(CPU *c, const uint32_t *args)
{
    UNUSED;
    H2Object *o = h2_handle_get(ARG(0), H2_OBJ_MUTEX);
    if (!o || o->u.sync.count == 0 || o->u.sync.owner != (int)h2_thread_tid()) { g_h2_last_error = 288; return 0; }   /* ERROR_NOT_OWNER */
    if (--o->u.sync.count == 0) o->u.sync.owner = 0;
    return 1;
}

static uint32_t k_CloseHandle(CPU *c, const uint32_t *args)
{
    UNUSED;
    H2Object *o = h2_handle_get(ARG(0), H2_OBJ_ANY);
    if (!o) { g_h2_last_error = 6; return 0; }
    if (o->close) o->close(o);
    o->type = H2_OBJ_FREE;
    return 1;
}

static const H2ImportDef k_defs[] = {
#define K(name, n) { "KERNEL32.dll", #name, n, k_##name }
    K(CreateMutexA, 3), K(CreateEventA, 4), K(SetEvent, 1), K(ResetEvent, 1), K(WaitForSingleObject, 2), K(CloseHandle, 1),
    K(WaitForMultipleObjects, 4), K(ReleaseMutex, 1),
#undef K
};

void h2_register_objects(void) { h2_imports_register(k_defs, sizeof k_defs / sizeof k_defs[0]); }
