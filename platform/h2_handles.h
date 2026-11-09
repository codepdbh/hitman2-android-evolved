/* h2_handles.h - the guest's kernel handle table (internal to platform/). */
#ifndef H2_HANDLES_H
#define H2_HANDLES_H

#include <stdint.h>
#include <stdio.h>

typedef enum { H2_OBJ_FREE = 0, H2_OBJ_MUTEX, H2_OBJ_EVENT, H2_OBJ_FILE, H2_OBJ_FIND, H2_OBJ_ANY = 99 } H2ObjType;

typedef struct H2Object {
    H2ObjType type;
    char name[64];
    void (*close)(struct H2Object *);
    union {
        struct { int count, manual; } sync;
        struct { FILE *fp; int writable; char host_path[520]; } file;
        struct { void *state; } find;
    } u;
} H2Object;

uint32_t  h2_handle_new(H2ObjType type, H2Object **out);    /* 0 if the table is full */
H2Object *h2_handle_get(uint32_t handle, H2ObjType type);   /* NULL if not a live handle of that type */

void h2_register_objects(void);
void h2_register_files(void);

#endif
