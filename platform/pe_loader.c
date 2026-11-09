/*
 * pe_loader.c - map a PE32 (i386) image into guest memory and bind its imports.
 *
 * The image comes from the user's own game file at run time; nothing from it
 * is compiled into the port. It is mapped at its preferred base: the guest
 * address space is ours, so there is never a reason to relocate.
 *
 * Every offset read from the file is bounds-checked: the file is input.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "h2_platform.h"

static uint16_t u16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t u32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

static int fail(const char *path, const char *why)
{
    H2_ERROR(H2_LOG_FILESYSTEM, "cannot load %s: %s", path, why);
    return -1;
}

/* A NUL-terminated string inside the mapped image, or NULL if it runs off the end. */
static const char *image_str(const H2Image *img, uint32_t rva)
{
    if (rva >= img->size) return 0;
    const char *s = (const char *)guest_ptr(img->base + rva);
    return memchr(s, 0, img->size - rva) ? s : 0;
}

int h2_pe_load(const char *path, H2Image *img)
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

    uint32_t pe = u32(file + 0x3C);
    if (u16(file) != 0x5A4D || pe > n - 0x100 || u32(file + pe) != 0x00004550) { fail(path, "not a PE file"); goto done; }
    const uint8_t *fh = file + pe + 4, *oh = fh + 20;
    if (u16(fh) != 0x014C || u16(oh) != 0x010B) { fail(path, "not PE32 for i386"); goto done; }
    unsigned nsec = u16(fh + 2), opt = u16(fh + 16);
    const uint8_t *sec = oh + opt;
    if (opt < 96 + 16 || nsec == 0 || nsec > 64 || (size_t)(sec - file) + nsec * 40u > n) { fail(path, "bad section table"); goto done; }

    memset(img, 0, sizeof *img);
    snprintf(img->path, sizeof img->path, "%s", path);
    img->base = u32(oh + 28);
    img->size = (u32(oh + 56) + 0xFFFu) & ~0xFFFu;
    img->entry = img->base + u32(oh + 16);
    img->sections = nsec;
    uint32_t hdr_size = u32(oh + 60);
    if (img->size == 0 || img->size > 0x10000000u || (uint64_t)img->base + img->size > H2_STACK_TOP - H2_STACK_SIZE
        || (img->base & 0xFFFFu) || hdr_size > n || hdr_size > img->size) { fail(path, "image does not fit the guest layout"); goto done; }

    if (guest_mem_commit(img->base, img->size, GUEST_PROT_RW) != 0) { fail(path, "cannot commit guest memory"); goto done; }
    memcpy(guest_ptr(img->base), file, hdr_size);
    for (unsigned i = 0; i < nsec; i++) {
        const uint8_t *s = sec + i * 40;
        uint32_t vsize = u32(s + 8), rva = u32(s + 12), rsize = u32(s + 16), raw = u32(s + 20);
        uint32_t copy = rsize < vsize || vsize == 0 ? rsize : vsize;
        if ((uint64_t)rva + copy > img->size || (uint64_t)raw + copy > n) { fail(path, "section outside file or image"); goto done; }
        memcpy(guest_ptr(img->base + rva), file + raw, copy);     /* the rest of the section stays zero (bss) */
    }

    /* ---- imports: data directory 1 ---- */
    if (u32(oh + 92) > 1) {
        uint32_t imp = u32(oh + 96 + 8), imp_size = u32(oh + 96 + 12);
        for (uint32_t d = imp; imp_size && (uint64_t)d + 20 <= img->size; d += 20) {
            const uint8_t *e = (const uint8_t *)guest_ptr(img->base + d);
            uint32_t ilt = u32(e), name_rva = u32(e + 12), iat = u32(e + 16);
            if (!name_rva && !iat) break;
            const char *dll = image_str(img, name_rva);
            if (!dll) { fail(path, "import descriptor with a bad DLL name"); goto done; }
            if (!ilt) ilt = iat;                                  /* old linkers: only the IAT */
            for (uint32_t k = 0; ; k += 4) {
                if ((uint64_t)ilt + k + 4 > img->size || (uint64_t)iat + k + 4 > img->size) { fail(path, "import thunks outside image"); goto done; }
                uint32_t t = rd32(img->base + ilt + k);
                char ord[24];
                const char *name;
                if (!t) break;
                if (t & 0x80000000u) { snprintf(ord, sizeof ord, "#%u", t & 0xFFFFu); name = ord; }
                else if (!(name = image_str(img, t + 2))) { fail(path, "import with a bad name"); goto done; }
                wr32(img->base + iat + k, h2_import_bind(dll, name));
                img->import_slots++;
            }
        }
    }
    H2_INFO(H2_LOG_FILESYSTEM, "loaded image base=%08X size=%08X entry=%08X sections=%u import slots=%u",
            img->base, img->size, img->entry, img->sections, img->import_slots);
    rc = 0;
done:
    free(file);
    return rc;
}
