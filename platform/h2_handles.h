/* h2_handles.h - the guest's kernel handle table (internal to platform/). */
#ifndef H2_HANDLES_H
#define H2_HANDLES_H

#include <stdint.h>
#include <stdio.h>

typedef enum { H2_OBJ_FREE = 0, H2_OBJ_MUTEX, H2_OBJ_EVENT, H2_OBJ_FILE, H2_OBJ_FIND, H2_OBJ_THREAD, H2_OBJ_ANY = 99 } H2ObjType;

typedef struct H2Object {
    H2ObjType type;
    char name[64];
    void (*close)(struct H2Object *);
    union {
        struct { int count, manual, owner; } sync;   /* mutex: count = recursion depth, owner = guest thread id (0 free) */
        struct { int index; } thread;                 /* index in the scheduler's table (win32_threads.c) */
        struct { FILE *fp; int writable; char host_path[520]; } file;
        struct { void *state; } find;
    } u;
} H2Object;

uint32_t  h2_handle_new(H2ObjType type, H2Object **out);    /* 0 if the table is full */
H2Object *h2_handle_get(uint32_t handle, H2ObjType type);   /* NULL if not a live handle of that type */

/* Wait semantics, shared with the scheduler. `tid` is the waiting guest thread. */
int h2_obj_signaled(H2Object *o, int tid);     /* would a wait by `tid` succeed right now? (no side effects) */
void h2_obj_acquire(H2Object *o, int tid);     /* take it: auto-reset events clear, mutexes are owned/recursed */
int h2_thread_done(int index);                 /* win32_threads.c: has that guest thread finished? */

void h2_register_objects(void);
void h2_register_files(void);

#endif
