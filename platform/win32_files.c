/*
 * win32_files.c - the guest's file system.
 *
 * The guest sees one drive, with the game installed at C:\Hitman2. On the host
 * that directory is the union of two roots:
 *
 *   game root   the user's own install. NEVER written to, on any path.
 *   user root   everything the game creates or modifies (saves, config, logs).
 *               A game file opened for writing is first copied here.
 *
 * Lookups check the user root, then the game root. Guest paths are
 * case-insensitive and use backslashes; host lookup is case-insensitive even
 * on case-sensitive file systems (Android). A guest path that leaves
 * C:\Hitman2 does not exist as far as the guest is concerned.
 *
 * Every failed open is logged, unconditionally: a game that silently cannot
 * find a file it needs tells you nothing else about why it stopped.
 */
#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#define _FILE_OFFSET_BITS 64
#endif
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#if defined(_WIN32)
#include <direct.h>
#include <io.h>
#define h2_mkdir(p) _mkdir(p)
#define h2_fseek64 _fseeki64
#define h2_ftell64 _ftelli64
#else
#include <dirent.h>
#include <unistd.h>
#define h2_mkdir(p) mkdir((p), 0755)
#define h2_fseek64 fseeko
#define h2_ftell64 ftello
#endif

#include "h2_platform.h"
#include "h2_handles.h"

#define ARG(i) (args[(i)])
#define UNUSED (void)c
#define GUEST_ROOT "C:\\Hitman2"
#define INVALID_HANDLE 0xFFFFFFFFu
enum { ERR_FILE_NOT_FOUND = 2, ERR_PATH_NOT_FOUND = 3, ERR_ACCESS_DENIED = 5, ERR_INVALID_HANDLE = 6,
       ERR_FILE_EXISTS = 80, ERR_INVALID_PARAMETER = 87, ERR_INSUFFICIENT_BUFFER = 122 };

static char g_game_root[400], g_user_root[400];
static char g_cwd[260] = "";                 /* relative to the guest root, '/'-separated, no leading slash */

void h2_files_configure(const char *game_root, const char *user_root)
{
    snprintf(g_game_root, sizeof g_game_root, "%s", game_root);
    snprintf(g_user_root, sizeof g_user_root, "%s", user_root);
    h2_mkdir(g_user_root);
    H2_INFO(H2_LOG_FILESYSTEM, "guest %s = [user] %s + [game, read-only] %s", GUEST_ROOT, g_user_root, g_game_root);
}

/* Guest path -> path relative to the guest root ('/' separators), or -1 if it
 * is outside the root or tries to climb out of it. */
static int guest_to_rel(const char *guest, char *rel, size_t cap)
{
    char tmp[520];
    size_t n = 0;
    const char *p = guest;
    if (isalpha((unsigned char)p[0]) && p[1] == ':') {
        size_t rl = strlen(GUEST_ROOT);
        for (size_t i = 0; i < rl; i++)
            if (tolower((unsigned char)p[i]) != tolower((unsigned char)GUEST_ROOT[i])) return -1;
        p += rl;
        if (*p && *p != '\\' && *p != '/') return -1;
        tmp[0] = 0;
    } else if (*p == '\\' || *p == '/') {
        return -1;                                           /* root of the drive: not inside the install */
    } else {
        snprintf(tmp, sizeof tmp, "%s", g_cwd);
    }
    n = strlen(tmp);
    while (*p) {
        while (*p == '\\' || *p == '/') p++;
        const char *e = p;
        while (*e && *e != '\\' && *e != '/') e++;
        size_t len = (size_t)(e - p);
        if (len == 0) break;
        if (len == 1 && p[0] == '.') { p = e; continue; }
        if (len == 2 && p[0] == '.' && p[1] == '.') {
            char *slash = strrchr(tmp, '/');
            if (!n) return -1;                               /* would leave the root */
            if (slash) *slash = 0; else tmp[0] = 0;
            n = strlen(tmp); p = e; continue;
        }
        if (n + len + 2 > sizeof tmp) return -1;
        if (n) tmp[n++] = '/';
        memcpy(tmp + n, p, len); n += len; tmp[n] = 0;
        p = e;
    }
    if (n + 1 > cap) return -1;
    memcpy(rel, tmp, n + 1);
    return 0;
}

static int host_exists(const char *path, int *is_dir)
{
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    if (is_dir) *is_dir = (st.st_mode & S_IFDIR) != 0;
    return 1;
}

/* Join root + rel into `out`. Returns 1 if the result exists. On hosts with
 * case-sensitive file systems each component is matched case-insensitively. */
static int resolve(const char *root, const char *rel, char *out, size_t cap)
{
#if defined(_WIN32)
    snprintf(out, cap, "%s%s%s", root, *rel ? "/" : "", rel);
    return host_exists(out, 0);
#else
    size_t n = (size_t)snprintf(out, cap, "%s", root);
    const char *p = rel;
    int exists = 1;
    while (*p) {
        const char *e = strchr(p, '/'); if (!e) e = p + strlen(p);
        size_t len = (size_t)(e - p);
        char comp[260];
        if (len >= sizeof comp || n + len + 2 > cap) return 0;
        memcpy(comp, p, len); comp[len] = 0;
        if (exists) {
            DIR *d = opendir(out);
            struct dirent *de;
            int found = 0;
            if (d) {
                while ((de = readdir(d)) != 0)
                    if (strcasecmp(de->d_name, comp) == 0) { snprintf(comp, sizeof comp, "%s", de->d_name); found = 1; break; }
                closedir(d);
            }
            exists = found;
        }
        out[n++] = '/'; memcpy(out + n, comp, strlen(comp) + 1); n += strlen(comp);
        p = *e ? e + 1 : e;
    }
    return exists && host_exists(out, 0);
#endif
}

int h2_files_resolve_read(const char *guest_path, char *host, size_t cap)
{
    char rel[520], user[920], game[920];
    if (guest_to_rel(guest_path, rel, sizeof rel) != 0) return -1;
    if (resolve(g_user_root, rel, user, sizeof user)) { snprintf(host, cap, "%s", user); return 0; }
    if (resolve(g_game_root, rel, game, sizeof game)) { snprintf(host, cap, "%s", game); return 0; }
    return -1;
}

/* Create the parent directories of a path under the user root. */
static void make_parents(char *path)
{
    for (char *p = path + strlen(g_user_root) + 1; *p; p++)
        if (*p == '/') { *p = 0; h2_mkdir(path); *p = '/'; }
}

static int copy_file(const char *from, const char *to)
{
    FILE *a = fopen(from, "rb"), *b;
    char buf[65536];
    size_t n;
    if (!a) return -1;
    if (!(b = fopen(to, "wb"))) { fclose(a); return -1; }
    while ((n = fread(buf, 1, sizeof buf, a)) > 0) if (fwrite(buf, 1, n, b) != n) { fclose(a); fclose(b); return -1; }
    fclose(a);
    return fclose(b);
}

static void file_close(H2Object *o) { if (o->u.file.fp) fclose(o->u.file.fp); o->u.file.fp = 0; }

static uint32_t k_CreateFileA(CPU *c, const uint32_t *args)
{
    UNUSED;
    const char *guest = (const char *)guest_ptr(ARG(0));
    uint32_t access = ARG(1), disp = ARG(4);
    char rel[520], user[920], game[920];
    int writing = (access & 0x40000000u) || (access & 0x10000000u) || disp == 1 || disp == 2 || disp == 5;

    if (guest_to_rel(guest, rel, sizeof rel) != 0) {
        H2_WARN(H2_LOG_FILESYSTEM, "CreateFileA(\"%s\") -> outside the guest install directory", guest);
        g_h2_last_error = ERR_PATH_NOT_FOUND; return INVALID_HANDLE;
    }
    int in_user = resolve(g_user_root, rel, user, sizeof user);
    int in_game = resolve(g_game_root, rel, game, sizeof game);
    int exists = in_user || in_game;
    const char *host = 0, *mode = 0;

    if (!writing) {
        if (disp != 3 && disp != 4) { g_h2_last_error = ERR_INVALID_PARAMETER; return INVALID_HANDLE; }
        if (!exists) {
            H2_WARN(H2_LOG_FILESYSTEM, "CreateFileA(\"%s\") -> not found", guest);
            g_h2_last_error = ERR_FILE_NOT_FOUND; return INVALID_HANDLE;
        }
        host = in_user ? user : game; mode = "rb";
    } else {
        if (disp == 1 && exists) { g_h2_last_error = ERR_FILE_EXISTS; return INVALID_HANDLE; }
        if ((disp == 3 || disp == 5) && !exists) {
            H2_WARN(H2_LOG_FILESYSTEM, "CreateFileA(\"%s\", write) -> not found", guest);
            g_h2_last_error = ERR_FILE_NOT_FOUND; return INVALID_HANDLE;
        }
        make_parents(user);
        if ((disp == 3 || disp == 4) && !in_user && in_game && copy_file(game, user) != 0) {
            H2_ERROR(H2_LOG_FILESYSTEM, "cannot copy \"%s\" into the user root for writing", guest);
            g_h2_last_error = ERR_ACCESS_DENIED; return INVALID_HANDLE;
        }
        host = user;
        mode = (disp == 1 || disp == 2 || disp == 5 || !exists) ? "w+b" : "r+b";
    }
    FILE *fp = fopen(host, mode);
    if (!fp) {
        H2_WARN(H2_LOG_FILESYSTEM, "CreateFileA(\"%s\") -> host open failed: %s", guest, strerror(errno));
        g_h2_last_error = ERR_ACCESS_DENIED; return INVALID_HANDLE;
    }
    H2Object *o;
    uint32_t h = h2_handle_new(H2_OBJ_FILE, &o);
    if (!h) { fclose(fp); g_h2_last_error = 4; return INVALID_HANDLE; }
    o->u.file.fp = fp; o->u.file.writable = writing; o->close = file_close;
    snprintf(o->u.file.host_path, sizeof o->u.file.host_path, "%s", host);
    snprintf(o->name, sizeof o->name, "%.63s", rel);
    g_h2_last_error = (disp == 2 || disp == 4) && exists ? 183 : 0;
    if (!writing) h2_game_context_note_open(rel);
    H2_INFO(H2_LOG_FILESYSTEM, "open \"%s\" (%s%s) -> handle %X", rel, writing ? "write" : "read",
            host == game ? ", game root" : ", user root", h);
    return h;
}

static uint32_t k_ReadFile(CPU *c, const uint32_t *args)
{
    UNUSED;
    H2Object *o = h2_handle_get(ARG(0), H2_OBJ_FILE);
    if (!o) { g_h2_last_error = ERR_INVALID_HANDLE; return 0; }
    if (ARG(4)) h2_guest_stop(H2_STOP_UNIMPLEMENTED_IMPORT, "ReadFile with OVERLAPPED");
    size_t n = ARG(2) ? fread(guest_ptr(ARG(1)), 1, ARG(2), o->u.file.fp) : 0;
    if (ARG(3)) wr32(ARG(3), (uint32_t)n);
    if (n < ARG(2) && ferror(o->u.file.fp)) { clearerr(o->u.file.fp); g_h2_last_error = 30; return 0; }   /* ERROR_READ_FAULT */
    return 1;                                                 /* a short read at end of file still succeeds */
}

static uint32_t k_WriteFile(CPU *c, const uint32_t *args)
{
    UNUSED;
    H2Object *o = h2_handle_get(ARG(0), H2_OBJ_FILE);
    if (!o) { g_h2_last_error = ERR_INVALID_HANDLE; return 0; }
    if (!o->u.file.writable) { g_h2_last_error = ERR_ACCESS_DENIED; return 0; }
    if (ARG(4)) h2_guest_stop(H2_STOP_UNIMPLEMENTED_IMPORT, "WriteFile with OVERLAPPED");
    h2_fseek64(o->u.file.fp, 0, SEEK_CUR);                    /* C requires a positioning call between read and write */
    size_t n = ARG(2) ? fwrite(guest_ptr(ARG(1)), 1, ARG(2), o->u.file.fp) : 0;
    if (ARG(3)) wr32(ARG(3), (uint32_t)n);
    if (n != ARG(2)) { g_h2_last_error = 29; return 0; }      /* ERROR_WRITE_FAULT */
    return 1;
}

static uint32_t k_SetFilePointer(CPU *c, const uint32_t *args)
{
    UNUSED;
    H2Object *o = h2_handle_get(ARG(0), H2_OBJ_FILE);
    if (!o) { g_h2_last_error = ERR_INVALID_HANDLE; return 0xFFFFFFFFu; }
    int64_t off = ARG(2) ? (int64_t)(((uint64_t)rd32(ARG(2)) << 32) | ARG(1)) : (int64_t)(int32_t)ARG(1);
    int whence = ARG(3) == 0 ? SEEK_SET : ARG(3) == 1 ? SEEK_CUR : ARG(3) == 2 ? SEEK_END : -1;
    if (whence < 0 || h2_fseek64(o->u.file.fp, off, whence) != 0) { g_h2_last_error = 131; return 0xFFFFFFFFu; }  /* ERROR_NEGATIVE_SEEK */
    int64_t pos = h2_ftell64(o->u.file.fp);
    if (ARG(2)) wr32(ARG(2), (uint32_t)((uint64_t)pos >> 32));
    g_h2_last_error = 0;                                      /* 0xFFFFFFFF can be a valid low dword; callers check this */
    return (uint32_t)pos;
}

static uint32_t k_GetFileSize(CPU *c, const uint32_t *args)
{
    UNUSED;
    H2Object *o = h2_handle_get(ARG(0), H2_OBJ_FILE);
    if (!o) { g_h2_last_error = ERR_INVALID_HANDLE; return 0xFFFFFFFFu; }
    int64_t here = h2_ftell64(o->u.file.fp);
    h2_fseek64(o->u.file.fp, 0, SEEK_END);
    int64_t size = h2_ftell64(o->u.file.fp);
    h2_fseek64(o->u.file.fp, here, SEEK_SET);
    if (ARG(1)) wr32(ARG(1), (uint32_t)((uint64_t)size >> 32));
    g_h2_last_error = 0;
    return (uint32_t)size;
}

static uint32_t k_FlushFileBuffers(CPU *c, const uint32_t *args)
{
    UNUSED;
    H2Object *o = h2_handle_get(ARG(0), H2_OBJ_FILE);
    if (!o) { g_h2_last_error = ERR_INVALID_HANDLE; return 0; }
    return fflush(o->u.file.fp) == 0;
}

static uint32_t k_GetCurrentDirectoryA(CPU *c, const uint32_t *args)
{
    UNUSED;
    char full[300];
    int n = snprintf(full, sizeof full, "%s%s%s", GUEST_ROOT, g_cwd[0] ? "\\" : "", g_cwd);
    for (char *p = full; *p; p++) if (*p == '/') *p = '\\';
    if ((uint32_t)n + 1 > ARG(0)) return (uint32_t)n + 1;     /* required size, including the NUL */
    memcpy(guest_ptr(ARG(1)), full, (size_t)n + 1);
    return (uint32_t)n;
}

static uint32_t k_SetCurrentDirectoryA(CPU *c, const uint32_t *args)
{
    UNUSED;
    const char *guest = (const char *)guest_ptr(ARG(0));
    char rel[260], host[920];
    int is_dir = 0;
    if (guest_to_rel(guest, rel, sizeof rel) != 0 ||
        !((resolve(g_user_root, rel, host, sizeof host) && host_exists(host, &is_dir) && is_dir) ||
          (resolve(g_game_root, rel, host, sizeof host) && host_exists(host, &is_dir) && is_dir))) {
        H2_WARN(H2_LOG_FILESYSTEM, "SetCurrentDirectoryA(\"%s\") -> not found", guest);
        g_h2_last_error = ERR_PATH_NOT_FOUND; return 0;
    }
    snprintf(g_cwd, sizeof g_cwd, "%s", rel);
    return 1;
}

/* DeleteFileA: only files the guest itself created (user root) can be deleted.
 * A file that exists in the game install is read-only to the guest: ACCESS_DENIED,
 * as for a read-only installation. (A game file the guest had to copy for
 * writing is also refused, so deleting can never "restore" the original.) */
static uint32_t k_DeleteFileA(CPU *c, const uint32_t *args)
{
    UNUSED;
    const char *guest = (const char *)guest_ptr(ARG(0));
    char rel[520], user[920], game[920];
    if (guest_to_rel(guest, rel, sizeof rel) != 0) { g_h2_last_error = ERR_PATH_NOT_FOUND; return 0; }
    int in_user = resolve(g_user_root, rel, user, sizeof user), in_game = resolve(g_game_root, rel, game, sizeof game);
    if (in_game) {
        H2_WARN(H2_LOG_FILESYSTEM, "DeleteFileA(\"%s\") -> refused: file belongs to the read-only game install", guest);
        g_h2_last_error = ERR_ACCESS_DENIED; return 0;
    }
    if (!in_user) { g_h2_last_error = ERR_FILE_NOT_FOUND; return 0; }
    if (remove(user) != 0) { g_h2_last_error = ERR_ACCESS_DENIED; return 0; }
    H2_INFO(H2_LOG_FILESYSTEM, "deleted \"%s\" (user root)", rel);
    return 1;
}

/* ---- directory search: FindFirstFileA / FindNextFileA / FindClose ----
 * The search covers the union of the user root and the read-only install (user entries win), like every other
 * path in this layer. The whole matching list is captured at FindFirstFileA time. Not provided: 8.3 short
 * names (cAlternateFileName is empty), file attributes other than DIRECTORY / NORMAL, search levels other than
 * the plain one. */
typedef struct { char name[260]; int is_dir; uint64_t size, mtime_unix; } FindEntry;
typedef struct { unsigned n, i; FindEntry e[1]; } FindState;     /* e[] is over-allocated */

static unsigned list_dir(const char *dir, FindEntry *out, unsigned cap)
{
    unsigned n = 0;
    char path[1000];
#if defined(_WIN32)
    struct _finddata_t fd; intptr_t h;
    snprintf(path, sizeof path, "%s/*", dir);
    h = _findfirst(path, &fd);
    if (h == -1) return 0;
    do {
        if (!strcmp(fd.name, ".") || !strcmp(fd.name, "..") || n >= cap) continue;
        snprintf(out[n].name, sizeof out[n].name, "%s", fd.name);
        out[n].is_dir = (fd.attrib & _A_SUBDIR) != 0; out[n].size = fd.size; out[n].mtime_unix = (uint64_t)fd.time_write; n++;
    } while (_findnext(h, &fd) == 0);
    _findclose(h);
#else
    DIR *d = opendir(dir);
    struct dirent *de;
    if (!d) return 0;
    while ((de = readdir(d)) != 0 && n < cap) {
        struct stat st;
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
        snprintf(path, sizeof path, "%s/%s", dir, de->d_name);
        if (stat(path, &st) != 0) continue;
        snprintf(out[n].name, sizeof out[n].name, "%s", de->d_name);
        out[n].is_dir = S_ISDIR(st.st_mode) != 0; out[n].size = (uint64_t)st.st_size; out[n].mtime_unix = (uint64_t)st.st_mtime; n++;
    }
    closedir(d);
#endif
    return n;
}

static int ci_eq(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
    return *a == *b;
}

/* Windows wildcard match: '*' and '?', case-insensitive; "*.*" matches every name (with or without a dot). */
static int wild(const char *p, const char *s)
{
    if (!strcmp(p, "*.*")) return 1;
    while (*p) {
        if (*p == '*') {
            while (*p == '*') p++;
            if (!*p) return 1;
            for (; *s; s++) if (wild(p, s)) return 1;
            return 0;
        }
        if (!*s || (*p != '?' && tolower((unsigned char)*p) != tolower((unsigned char)*s))) return 0;
        p++; s++;
    }
    return *s == 0;
}

static void fill_find_data(uint32_t out, const FindEntry *e)
{
    memset(guest_ptr(out), 0, 320);
    wr32(out, e->is_dir ? 0x10u : 0x80u);
    uint64_t ft = (e->mtime_unix + 11644473600ull) * 10000000ull;
    for (int i = 0; i < 3; i++) { wr32(out + 4 + 8u * (uint32_t)i, (uint32_t)ft); wr32(out + 8 + 8u * (uint32_t)i, (uint32_t)(ft >> 32)); }
    wr32(out + 28, (uint32_t)(e->size >> 32)); wr32(out + 32, (uint32_t)e->size);
    strncpy((char *)guest_ptr(out + 44), e->name, 259);
}

static void find_close(H2Object *o) { free(o->u.find.state); o->u.find.state = 0; }

static uint32_t k_FindFirstFileA(CPU *c, const uint32_t *args)
{
    UNUSED;
    const char *guest = (const char *)guest_ptr(ARG(0));
    char dirpart[520], rel[520], pat[260], host[920];
    const char *sep1 = strrchr(guest, '\\'), *sep2 = strrchr(guest, '/'), *sep = sep1 > sep2 ? sep1 : sep2;
    if (sep) { size_t n = (size_t)(sep - guest); if (n >= sizeof dirpart) n = sizeof dirpart - 1; memcpy(dirpart, guest, n); dirpart[n] = 0; snprintf(pat, sizeof pat, "%s", sep + 1); }
    else { dirpart[0] = 0; snprintf(pat, sizeof pat, "%s", guest); }
    if (!sep && strchr(guest, ':')) { g_h2_last_error = ERR_PATH_NOT_FOUND; return INVALID_HANDLE; }
    if (guest_to_rel(dirpart[0] ? dirpart : ".", rel, sizeof rel) != 0) { g_h2_last_error = ERR_PATH_NOT_FOUND; return INVALID_HANDLE; }
    enum { CAP = 4096 };
    FindEntry *all = (FindEntry *)malloc(sizeof(FindEntry) * CAP);
    if (!all) { g_h2_last_error = 8; return INVALID_HANDLE; }
    unsigned n = 0, nu = 0;
    int dir_found = 0;
    if (resolve(g_user_root, rel, host, sizeof host)) { n = list_dir(host, all, CAP); nu = n; dir_found = 1; }
    if (resolve(g_game_root, rel, host, sizeof host)) {
        FindEntry *g = (FindEntry *)malloc(sizeof(FindEntry) * CAP);
        if (g) {
            unsigned ng = list_dir(host, g, CAP);
            for (unsigned i = 0; i < ng && n < CAP; i++) {
                int dup = 0;
                for (unsigned j = 0; j < nu; j++) if (ci_eq(all[j].name, g[i].name)) { dup = 1; break; }
                if (!dup) all[n++] = g[i];
            }
            free(g); dir_found = 1;
        }
    }
    if (!dir_found) { free(all); g_h2_last_error = ERR_PATH_NOT_FOUND; return INVALID_HANDLE; }
    FindState *st = (FindState *)malloc(sizeof(FindState) + sizeof(FindEntry) * (n ? n : 1));
    if (!st) { free(all); g_h2_last_error = 8; return INVALID_HANDLE; }
    st->n = 0; st->i = 0;
    if (rel[0]) {                                              /* sub-directories list "." and ".." first, as Windows does */
        FindEntry dots = { "", 1, 0, 0 };
        if (wild(pat, ".")) { strcpy(dots.name, "."); st->e[st->n++] = dots; }
        if (wild(pat, "..")) { strcpy(dots.name, ".."); st->e[st->n++] = dots; }
    }
    for (unsigned i = 0; i < n; i++) if (wild(pat, all[i].name)) st->e[st->n++] = all[i];
    free(all);
    if (st->n == 0) { free(st); g_h2_last_error = ERR_FILE_NOT_FOUND; return INVALID_HANDLE; }
    H2Object *o; uint32_t h = h2_handle_new(H2_OBJ_FIND, &o);
    if (!h) { free(st); g_h2_last_error = 8; return INVALID_HANDLE; }
    o->u.find.state = st; o->close = find_close;
    st->i = 1;
    fill_find_data(ARG(1), &st->e[0]);
    H2_INFO(H2_LOG_FILESYSTEM, "FindFirstFileA(\"%s\") -> %u entries", guest, st->n);
    g_h2_last_error = 0;
    return h;
}

static uint32_t k_FindNextFileA(CPU *c, const uint32_t *args)
{
    UNUSED;
    H2Object *o = h2_handle_get(ARG(0), H2_OBJ_FIND);
    if (!o) { g_h2_last_error = ERR_INVALID_HANDLE; return 0; }
    FindState *st = (FindState *)o->u.find.state;
    if (st->i >= st->n) { g_h2_last_error = 18; return 0; }   /* ERROR_NO_MORE_FILES */
    fill_find_data(ARG(1), &st->e[st->i++]);
    return 1;
}

static uint32_t k_FindClose(CPU *c, const uint32_t *args)
{
    UNUSED;
    H2Object *o = h2_handle_get(ARG(0), H2_OBJ_FIND);
    if (!o) { g_h2_last_error = ERR_INVALID_HANDLE; return 0; }
    if (o->close) o->close(o);
    o->type = H2_OBJ_FREE;
    return 1;
}

static const H2ImportDef k_defs[] = {
#define K(name, n) { "KERNEL32.dll", #name, n, k_##name }
    K(FindFirstFileA, 2), K(FindNextFileA, 2), K(FindClose, 1),
    K(DeleteFileA, 1), K(CreateFileA, 7), K(ReadFile, 5), K(WriteFile, 5), K(SetFilePointer, 4), K(GetFileSize, 2), K(FlushFileBuffers, 1),
    K(GetCurrentDirectoryA, 2), K(SetCurrentDirectoryA, 1),
#undef K
};

void h2_register_files(void) { h2_imports_register(k_defs, sizeof k_defs / sizeof k_defs[0]); }
