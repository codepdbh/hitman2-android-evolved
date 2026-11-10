/*
 * pe_loader.c - map PE32 (i386) images into guest memory, bind their imports,
 * and keep the registry of loaded images.
 *
 * Images come from the user's own game files at run time; nothing from them is
 * compiled into the port. Each image is mapped at its preferred base: the
 * guest address space is ours and the lifted code of a module was generated for
 * that base, so there is never a reason to relocate. Two images that want
 * overlapping ranges cannot both be loaded; that is reported, not worked around.
 *
 * Imports are bound in two ways:
 *   - from a DLL that is itself a loaded guest image (the renderer imports 28
 *     symbols from hitman2.exe): the slot receives that image's real export
 *     address, found by name in its export table;
 *   - from anything else: a thunk from the import registry (imports.c), which
 *     reaches either a native implementation or, if there is none, stops the
 *     guest naming the function when it is first called.
 *
 * Every offset read from a file is bounds-checked: the file is input.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "h2_platform.h"

static uint16_t u16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t u32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

#define MAX_IMAGES 8
static H2Image g_images[MAX_IMAGES];
static unsigned g_nimages;

static int fail(const char *path, const char *why)
{
    H2_ERROR(H2_LOG_FILESYSTEM, "cannot load %s: %s", path, why);
    return -1;
}

static int ieq(const char *a, const char *b)
{
    for (;; a++, b++) {
        char x = *a, y = *b;
        if (x >= 'A' && x <= 'Z') x = (char)(x + 32);
        if (y >= 'A' && y <= 'Z') y = (char)(y + 32);
        if (x != y) return 0;
        if (!x) return 1;
    }
}

/* ---- the registry of loaded images ---- */
const H2Image *h2_image_by_name(const char *name)
{
    const char *p = strrchr(name, '\\'), *q = strrchr(name, '/');
    if (q > p) p = q;
    p = p ? p + 1 : name;
    for (unsigned i = 0; i < g_nimages; i++)
        if (ieq(g_images[i].name, p)) return &g_images[i];
    return 0;
}

const H2Image *h2_image_by_handle(uint32_t base)
{
    for (unsigned i = 0; i < g_nimages; i++) if (g_images[i].base == base) return &g_images[i];
    return 0;
}

int h2_image_contains(uint32_t addr, uint32_t size)
{
    uint64_t end = (uint64_t)addr + size;
    for (unsigned i = 0; i < g_nimages; i++)
        if (addr >= g_images[i].base && end <= (uint64_t)g_images[i].base + g_images[i].size) return 1;
    return 0;
}

/* Export by name, or by ordinal when `name` is "#<n>". 0 if absent or a forwarder. */
uint32_t h2_image_export(const H2Image *img, const char *name)
{
    if (!img->export_size || img->export_rva + 40u > img->size) return 0;
    uint32_t ed = img->base + img->export_rva;
    uint32_t base_ord = rd32(ed + 16), nfunc = rd32(ed + 20), nname = rd32(ed + 24);
    uint32_t aof = rd32(ed + 28), aon = rd32(ed + 32), aoo = rd32(ed + 36);
    uint32_t idx = 0xFFFFFFFFu;
    if ((uint64_t)aof + 4ull * nfunc > img->size || (uint64_t)aon + 4ull * nname > img->size || (uint64_t)aoo + 2ull * nname > img->size)
        return 0;
    if (name[0] == '#') {
        uint32_t ord = (uint32_t)strtoul(name + 1, 0, 10);
        if (ord >= base_ord) idx = ord - base_ord;
    } else {
        for (uint32_t i = 0; i < nname; i++) {
            uint32_t nrva = rd32(img->base + aon + 4u * i);
            if (nrva < img->size && strcmp((const char *)guest_ptr(img->base + nrva), name) == 0) { idx = rd16(img->base + aoo + 2u * i); break; }
        }
    }
    if (idx >= nfunc) return 0;
    uint32_t rva = rd32(img->base + aof + 4u * idx);
    if (rva >= img->export_rva && rva < img->export_rva + img->export_size) {
        H2_WARN(H2_LOG_FILESYSTEM, "export %s of %s is a forwarder: not supported", name, img->name);
        return 0;
    }
    return rva ? img->base + rva : 0;
}

/* A NUL-terminated string inside the mapped image, or NULL if it runs off the end. */
static const char *image_str(const H2Image *img, uint32_t rva)
{
    if (rva >= img->size) return 0;
    const char *s = (const char *)guest_ptr(img->base + rva);
    return memchr(s, 0, img->size - rva) ? s : 0;
}

int h2_pe_load(const char *path, H2Image *out)
{
    FILE *f = fopen(path, "rb");
    if (!f) return fail(path, "file not found");
    fseek(f, 0, SEEK_END);
    long flen = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (flen < 0x200 || flen > 64 * 1024 * 1024) { fclose(f); return fail(path, "implausible size"); }
    uint8_t *file = (uint8_t *)malloc((size_t)flen);
    if (!file || fread(file, 1, (size_t)flen, f) != (size_t)flen) { fclose(f); free(file); return fail(path, "read error"); }
    fclose(f);
    size_t n = (size_t)flen;
    int rc = -1;
    H2Image *img = 0;

    uint32_t pe = u32(file + 0x3C);
    if (u16(file) != 0x5A4D || pe > n - 0x100 || u32(file + pe) != 0x00004550) { fail(path, "not a PE file"); goto done; }
    const uint8_t *fh = file + pe + 4, *oh = fh + 20;
    if (u16(fh) != 0x014C || u16(oh) != 0x010B) { fail(path, "not PE32 for i386"); goto done; }
    unsigned nsec = u16(fh + 2), opt = u16(fh + 16);
    const uint8_t *sec = oh + opt;
    if (opt < 96 + 16 || nsec == 0 || nsec > 64 || (size_t)(sec - file) + nsec * 40u > n) { fail(path, "bad section table"); goto done; }
    if (g_nimages == MAX_IMAGES) { fail(path, "too many images"); goto done; }

    img = &g_images[g_nimages];
    memset(img, 0, sizeof *img);
    snprintf(img->path, sizeof img->path, "%s", path);
    {   const char *b = strrchr(path, '/'), *b2 = strrchr(path, '\\');
        if (b2 > b) b = b2;
        snprintf(img->name, sizeof img->name, "%s", b ? b + 1 : path); }
    img->base = u32(oh + 28);
    img->size = (u32(oh + 56) + 0xFFFu) & ~0xFFFu;
    img->entry = u32(oh + 16) ? img->base + u32(oh + 16) : 0;
    img->sections = nsec;
    img->is_dll = (u16(fh + 18) & 0x2000) != 0;
    uint32_t hdr_size = u32(oh + 60);

    /* Layout: the image may not touch the stack/TIB/OS-data block, the arena used by VirtualAlloc and the
     * heap, the import-thunk range, or another loaded image. */
    uint64_t lo = img->base, hi = (uint64_t)img->base + img->size;
    int bad = img->size == 0 || img->size > 0x10000000u || (img->base & 0xFFFFu) || hdr_size > n || hdr_size > img->size
           || img->base < 0x10000u
           || (lo < H2_OSDATA_BASE + H2_OSDATA_SIZE && hi > H2_STACK_TOP - H2_STACK_SIZE)
           || (lo < H2_ARENA_END && hi > H2_ARENA_BASE) || hi > H2_THUNK_BASE;
    for (unsigned i = 0; i < g_nimages && !bad; i++)
        if (lo < (uint64_t)g_images[i].base + g_images[i].size && hi > g_images[i].base) {
            H2_ERROR(H2_LOG_FILESYSTEM, "%s wants %08X-%08X, which %s already occupies", img->name, img->base,
                     (uint32_t)hi, g_images[i].name);
            bad = 2;
        }
    if (bad) { if (bad == 1) fail(path, "image does not fit the guest layout"); goto done; }

    if (guest_mem_commit(img->base, img->size, GUEST_PROT_RW) != 0) { fail(path, "cannot commit guest memory"); goto done; }
    memcpy(guest_ptr(img->base), file, hdr_size);
    for (unsigned i = 0; i < nsec; i++) {
        const uint8_t *s = sec + i * 40;
        uint32_t vsize = u32(s + 8), rva = u32(s + 12), rsize = u32(s + 16), raw = u32(s + 20);
        uint32_t copy = rsize < vsize || vsize == 0 ? rsize : vsize;
        if ((uint64_t)rva + copy > img->size || (uint64_t)raw + copy > n) { fail(path, "section outside file or image"); goto done; }
        memcpy(guest_ptr(img->base + rva), file + raw, copy);     /* the rest of the section stays zero (bss) */
    }
    if (u32(oh + 92) > 0) { img->export_rva = u32(oh + 96); img->export_size = u32(oh + 100); }
    g_nimages++;                                  /* visible to export lookups from here on (imports of itself are not a thing) */

    /* ---- imports: data directory 1 ---- */
    if (u32(oh + 92) > 1) {
        uint32_t imp = u32(oh + 96 + 8), imp_size = u32(oh + 96 + 12);
        for (uint32_t d = imp; imp_size && (uint64_t)d + 20 <= img->size; d += 20) {
            const uint8_t *e = (const uint8_t *)guest_ptr(img->base + d);
            uint32_t ilt = u32(e), name_rva = u32(e + 12), iat = u32(e + 16);
            if (!name_rva && !iat) break;
            const char *dll = image_str(img, name_rva);
            if (!dll) { fail(path, "import descriptor with a bad DLL name"); g_nimages--; goto done; }
            if (!ilt) ilt = iat;                                  /* old linkers: only the IAT */
            const H2Image *provider = h2_image_by_name(dll);
            if (provider == img) provider = 0;
            for (uint32_t k = 0; ; k += 4) {
                if ((uint64_t)ilt + k + 4 > img->size || (uint64_t)iat + k + 4 > img->size) { fail(path, "import thunks outside image"); g_nimages--; goto done; }
                uint32_t t = rd32(img->base + ilt + k);
                char ord[24];
                const char *name;
                if (!t) break;
                if (t & 0x80000000u) { snprintf(ord, sizeof ord, "#%u", t & 0xFFFFu); name = ord; }
                else if (!(name = image_str(img, t + 2))) { fail(path, "import with a bad name"); g_nimages--; goto done; }
                uint32_t target;
                if (provider) {
                    target = h2_image_export(provider, name);
                    if (!target) {
                        H2_ERROR(H2_LOG_FILESYSTEM, "%s imports %s!%s, which %s does not export", img->name, dll, name, provider->name);
                        g_nimages--; goto done;
                    }
                } else {
                    target = h2_import_bind(dll, name);
                }
                wr32(img->base + iat + k, target);
                img->import_slots++;
            }
        }
    }
    H2_INFO(H2_LOG_FILESYSTEM, "loaded %s base=%08X size=%08X entry=%08X sections=%u import slots=%u%s", img->name,
            img->base, img->size, img->entry, img->sections, img->import_slots, img->is_dll ? " (DLL)" : "");
    *out = *img;
    rc = 0;
done:
    free(file);
    return rc;
}
