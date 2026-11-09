/*
 * win32_user32.c - USER32. Only what does not need a window system yet.
 *
 * Window classes, windows and the message queue are the next layer to design:
 * on Android they sit on one native surface and an input queue, and on the PC
 * test host they must stay headless by default (WORK_SAFE_MODE). Until that
 * exists, those imports are deliberately absent and stop the guest by name.
 */
#include <string.h>

#include "h2_platform.h"

#define ARG(i) (args[(i)])
#define UNUSED (void)c

/* Registered window messages: the same string always yields the same id in
 * 0xC000..0xFFFF, case-insensitively, as the system atom table does. */
#define MAX_ATOMS 256
static char g_atoms[MAX_ATOMS][64];
static unsigned g_natoms;

static uint32_t u_RegisterWindowMessageA(CPU *c, const uint32_t *args)
{
    UNUSED;
    const char *s = (const char *)guest_ptr(ARG(0));
    char key[64];
    size_t n = strlen(s);
    if (!n || n >= sizeof key) { g_h2_last_error = 87; return 0; }
    for (size_t i = 0; i <= n; i++) key[i] = (char)((s[i] >= 'a' && s[i] <= 'z') ? s[i] - 32 : s[i]);
    for (unsigned i = 0; i < g_natoms; i++) if (strcmp(g_atoms[i], key) == 0) return 0xC000u + i;
    if (g_natoms == MAX_ATOMS) { g_h2_last_error = 8; return 0; }
    memcpy(g_atoms[g_natoms], key, n + 1);
    return 0xC000u + g_natoms++;
}

static const H2ImportDef k_defs[] = {
    { "USER32.dll", "RegisterWindowMessageA", 1, u_RegisterWindowMessageA },
};

void h2_register_user32(void) { h2_imports_register(k_defs, sizeof k_defs / sizeof k_defs[0]); }
