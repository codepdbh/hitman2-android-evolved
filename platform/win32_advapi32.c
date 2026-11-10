/*
 * win32_advapi32.c - the registry (the four ADVAPI32 functions hitman2.exe imports).
 *
 * An in-memory registry, empty at start: keys exist only if the host seeded
 * them (h2_registry_set_*) or the guest created them. A lookup of something
 * absent fails with ERROR_FILE_NOT_FOUND, exactly as on a machine where the
 * game was never installed, so the program takes whatever "no settings yet"
 * path it has instead of being fed invented values.
 *
 * The guest never touches the host's real registry. Nothing is persisted yet
 * (see docs/KNOWN_ISSUES.md).
 */
#include <stdio.h>
#include <string.h>

#include "h2_platform.h"

#define ARG(i) (args[(i)])
#define UNUSED (void)c
enum { ERR_OK = 0, ERR_NOT_FOUND = 2, ERR_INVALID_HANDLE = 6, ERR_MORE_DATA = 234, ERR_INVALID_PARAMETER = 87,
       ERR_NOT_ENOUGH_MEMORY = 8 };
enum { REG_NONE = 0, REG_SZ = 1, REG_BINARY = 3, REG_DWORD = 4 };

#define MAX_KEYS 128
#define MAX_VALUES 256
#define MAX_OPEN 128
#define HKEY_LOCAL_MACHINE 0x80000002u
#define HKEY_CURRENT_USER 0x80000001u
#define HKEY_CLASSES_ROOT 0x80000000u
#define OPEN_BASE 0x00002000u

typedef struct { char path[200]; } Key;                                      /* upper-case, '\'-separated, with root prefix */
typedef struct { int key; char name[64]; uint32_t type, size; uint8_t data[256]; } Value;
static Key g_keys[MAX_KEYS];
static unsigned g_nkeys;
static Value g_values[MAX_VALUES];
static unsigned g_nvalues;
static int g_open[MAX_OPEN];                                                 /* key index + 1, 0 = free */

static void upcase(char *dst, size_t cap, const char *s)
{
    size_t i = 0;
    for (; s[i] && i + 1 < cap; i++) dst[i] = (char)((s[i] >= 'a' && s[i] <= 'z') ? s[i] - 32 : s[i]);
    dst[i] = 0;
}

static const char *root_name(uint32_t h)
{
    return h == HKEY_LOCAL_MACHINE ? "HKLM" : h == HKEY_CURRENT_USER ? "HKCU" : h == HKEY_CLASSES_ROOT ? "HKCR" : 0;
}

/* Full path for (hKey, subKey), or 0 if hKey is not a live handle / root. */
static int full_path(uint32_t hkey, const char *sub, char *out, size_t cap)
{
    char s[200], base[200];
    upcase(s, sizeof s, sub ? sub : "");
    size_t n = strlen(s);
    while (n && s[n - 1] == '\\') s[--n] = 0;
    if (root_name(hkey)) snprintf(base, sizeof base, "%s", root_name(hkey));
    else if (hkey >= OPEN_BASE && (hkey - OPEN_BASE) / 4u < MAX_OPEN && g_open[(hkey - OPEN_BASE) / 4u])
        snprintf(base, sizeof base, "%s", g_keys[g_open[(hkey - OPEN_BASE) / 4u] - 1].path);
    else return 0;
    snprintf(out, cap, "%s%s%s", base, n ? "\\" : "", s);
    return 1;
}

static int find_key(const char *path)
{
    for (unsigned i = 0; i < g_nkeys; i++) if (strcmp(g_keys[i].path, path) == 0) return (int)i;
    return -1;
}

static int ensure_key(const char *path)
{
    int k = find_key(path);
    if (k >= 0) return k;
    if (g_nkeys == MAX_KEYS) return -1;
    snprintf(g_keys[g_nkeys].path, sizeof g_keys[0].path, "%s", path);
    return (int)g_nkeys++;
}

static uint32_t open_handle(int key)
{
    for (unsigned i = 0; i < MAX_OPEN; i++) if (!g_open[i]) { g_open[i] = key + 1; return OPEN_BASE + 4u * i; }
    return 0;
}

/* ---- host seeding API ---- */
static int seed_value(const char *path, const char *name, uint32_t type, const void *data, uint32_t size)
{
    char p[200], n[64];
    upcase(p, sizeof p, path); upcase(n, sizeof n, name);
    int k = ensure_key(p);
    if (k < 0 || size > 256) return -1;
    for (unsigned i = 0; i < g_nvalues; i++)
        if (g_values[i].key == k && strcmp(g_values[i].name, n) == 0) { g_values[i].type = type; g_values[i].size = size; memcpy(g_values[i].data, data, size); return 0; }
    if (g_nvalues == MAX_VALUES) return -1;
    Value *v = &g_values[g_nvalues++];
    v->key = k; snprintf(v->name, sizeof v->name, "%s", n); v->type = type; v->size = size; memcpy(v->data, data, size);
    return 0;
}
int h2_registry_set_string(const char *path, const char *name, const char *value)
{
    return seed_value(path, name, REG_SZ, value, (uint32_t)strlen(value) + 1);
}
int h2_registry_set_dword(const char *path, const char *name, uint32_t value)
{
    return seed_value(path, name, REG_DWORD, &value, 4);                     /* host is little-endian on both targets */
}

/* ---- imports ---- */
static uint32_t a_RegOpenKeyExA(CPU *c, const uint32_t *args)
{
    UNUSED;
    char path[260];
    if (!ARG(4) || !full_path(ARG(0), ARG(1) ? (const char *)guest_ptr(ARG(1)) : "", path, sizeof path)) return ERR_INVALID_HANDLE;
    int k = find_key(path);
    if (k < 0) {
        H2_INFO(H2_LOG_WIN32, "RegOpenKeyEx %s -> not found", path);
        return ERR_NOT_FOUND;
    }
    uint32_t h = open_handle(k);
    if (!h) return ERR_NOT_ENOUGH_MEMORY;
    wr32(ARG(4), h);
    return ERR_OK;
}

static uint32_t a_RegCreateKeyExA(CPU *c, const uint32_t *args)
{
    UNUSED;
    char path[260];
    /* (hKey, subKey, reserved, class, options, sam, security, phkResult, lpdwDisposition) */
    if (!ARG(7) || !ARG(1) || !full_path(ARG(0), (const char *)guest_ptr(ARG(1)), path, sizeof path)) return ERR_INVALID_HANDLE;
    int existed = find_key(path) >= 0, k = ensure_key(path);
    if (k < 0) return ERR_NOT_ENOUGH_MEMORY;
    uint32_t h = open_handle(k);
    if (!h) return ERR_NOT_ENOUGH_MEMORY;
    wr32(ARG(7), h);
    if (ARG(8)) wr32(ARG(8), existed ? 2u : 1u);                             /* REG_OPENED_EXISTING_KEY / REG_CREATED_NEW_KEY */
    H2_INFO(H2_LOG_WIN32, "RegCreateKeyEx %s (%s)", path, existed ? "existing" : "created");
    return ERR_OK;
}

static uint32_t a_RegCloseKey(CPU *c, const uint32_t *args)
{
    UNUSED;
    if (root_name(ARG(0))) return ERR_OK;
    if (ARG(0) < OPEN_BASE || (ARG(0) - OPEN_BASE) / 4u >= MAX_OPEN || !g_open[(ARG(0) - OPEN_BASE) / 4u]) return ERR_INVALID_HANDLE;
    g_open[(ARG(0) - OPEN_BASE) / 4u] = 0;
    return ERR_OK;
}

static uint32_t a_RegQueryValueExA(CPU *c, const uint32_t *args)
{
    UNUSED;
    char path[260], name[64];
    /* (hKey, name, reserved, lpType, lpData, lpcbData) */
    if (!full_path(ARG(0), "", path, sizeof path) || find_key(path) < 0) return ERR_INVALID_HANDLE;
    int k = find_key(path);
    upcase(name, sizeof name, ARG(1) ? (const char *)guest_ptr(ARG(1)) : "");
    for (unsigned i = 0; i < g_nvalues; i++) {
        Value *v = &g_values[i];
        if (v->key != k || strcmp(v->name, name) != 0) continue;
        if (ARG(3)) wr32(ARG(3), v->type);
        if (!ARG(4)) { if (ARG(5)) wr32(ARG(5), v->size); return ERR_OK; }   /* size query */
        if (!ARG(5)) return ERR_INVALID_PARAMETER;
        uint32_t cap = rd32(ARG(5));
        wr32(ARG(5), v->size);
        if (cap < v->size) return ERR_MORE_DATA;
        memcpy(guest_ptr(ARG(4)), v->data, v->size);
        return ERR_OK;
    }
    H2_INFO(H2_LOG_WIN32, "RegQueryValueEx %s\\\"%s\" -> not found", path, name);
    return ERR_NOT_FOUND;
}

static const H2ImportDef k_defs[] = {
#define A(name, n) { "ADVAPI32.dll", #name, n, a_##name }
    A(RegOpenKeyExA, 5), A(RegCreateKeyExA, 9), A(RegCloseKey, 1), A(RegQueryValueExA, 6),
#undef A
};

void h2_register_advapi32(void)
{
    /* The branches every Windows installation has, so programs can open them as a base. */
    static const char *const k_roots[] = { "HKLM\\SOFTWARE", "HKLM\\SYSTEM", "HKLM\\HARDWARE", "HKCU\\SOFTWARE",
                                           "HKCU\\CONTROL PANEL", "HKCR\\CLSID" };
    for (size_t i = 0; i < sizeof k_roots / sizeof k_roots[0]; i++) ensure_key(k_roots[i]);
    h2_imports_register(k_defs, sizeof k_defs / sizeof k_defs[0]);
}
