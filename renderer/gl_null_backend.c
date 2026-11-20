/*
 * gl_null_backend.c - the headless backend: accepts everything the GL layer
 * produces, draws nothing, keeps the counters the tests and the boot probe
 * report. It is what runs on the PC test host (WORK_SAFE_MODE has no window).
 *
 * It does not pretend to be a GPU: read_pixels reports "no framebuffer", and
 * its capabilities are the ones the GL layer needs to run (2048-pixel
 * textures, 24-bit depth, 8-bit stencil), not a measurement of any hardware.
 */
#include <stdio.h>
#include <string.h>

#include "h2_gl.h"

/* Draw-state histogram: which combinations of GL state the game actually draws with. Used to decide what the
 * real backend must emulate faithfully (shadows, reflections, fog ...). */
typedef struct { char key[200]; unsigned long long draws, verts; } Combo;
static Combo g_combos[256];
static unsigned g_ncombos;

static void note_draw(const H2GLState *s, H2GLPrim p, unsigned nv)
{
    char k[200];
    snprintf(k, sizeof k, "prim=%d blend=%d(%X,%X,eq%X) depth=%d/%X mask=%d cull=%d alpha=%d(%X) light=%d fog=%d poly=%X | t0=%d env%X gen%d%d%d%d m%X%X%X%X | t1=%d env%X gen%d%d%d%d m%X%X%X%X",
             (int)p, s->blend, s->blend_src, s->blend_dst, s->blend_equation, s->depth_test, s->depth_func, s->depth_mask, s->cull_face,
             s->alpha_test, s->alpha_func, s->lighting, s->fog, s->polygon_mode,
             s->tex[0].enabled, s->tex[0].env_mode, s->tex[0].gen_enabled[0], s->tex[0].gen_enabled[1], s->tex[0].gen_enabled[2], s->tex[0].gen_enabled[3],
             s->tex[0].gen_mode[0] & 15, s->tex[0].gen_mode[1] & 15, s->tex[0].gen_mode[2] & 15, s->tex[0].gen_mode[3] & 15,
             s->tex[1].enabled, s->tex[1].env_mode, s->tex[1].gen_enabled[0], s->tex[1].gen_enabled[1], s->tex[1].gen_enabled[2], s->tex[1].gen_enabled[3],
             s->tex[1].gen_mode[0] & 15, s->tex[1].gen_mode[1] & 15, s->tex[1].gen_mode[2] & 15, s->tex[1].gen_mode[3] & 15);
    for (unsigned i = 0; i < g_ncombos; i++)
        if (!strcmp(g_combos[i].key, k)) { g_combos[i].draws++; g_combos[i].verts += nv; return; }
    if (g_ncombos < 256) { memcpy(g_combos[g_ncombos].key, k, strlen(k) + 1); g_combos[g_ncombos].draws = 1; g_combos[g_ncombos].verts = nv; g_ncombos++; }
}

void h2_gl_null_report(void)
{
    for (unsigned pass = 0; pass < 18 && pass < g_ncombos; pass++) {      /* top 18 by draw count */
        unsigned best = pass;
        for (unsigned i = pass + 1; i < g_ncombos; i++) if (g_combos[i].draws > g_combos[best].draws) best = i;
        Combo t = g_combos[pass]; g_combos[pass] = g_combos[best]; g_combos[best] = t;
        printf("  DRAWSTATE %8llu draws %10llu verts  %s%c", g_combos[pass].draws, g_combos[pass].verts, g_combos[pass].key, 10);
    }
    printf("  DRAWSTATE distinct combinations: %u%c", g_ncombos, 10);
}

static uint32_t g_frames_presented;

static int  nb_init(int w, int h) { (void)w; (void)h; return 0; }
static void nb_shutdown(void) {}
static void nb_present(void) { g_frames_presented++; }
static void nb_flush(void) {}
static void nb_clear(uint32_t mask, const float color[4], double depth, int stencil, const H2GLState *s)
{ (void)mask; (void)color; (void)depth; (void)stencil; (void)s; }
static void nb_tex_image(uint32_t t, int l, int w, int h, const uint8_t *p) { (void)t; (void)l; (void)w; (void)h; (void)p; }
static void nb_tex_subimage(uint32_t t, int l, int x, int y, int w, int h, const uint8_t *p) { (void)t; (void)l; (void)x; (void)y; (void)w; (void)h; (void)p; }
static void nb_tex_param(uint32_t t, uint32_t p, uint32_t v) { (void)t; (void)p; (void)v; }
static void nb_tex_delete(uint32_t t) { (void)t; }
static void nb_draw(const H2GLState *s, H2GLPrim p, const H2GLVertex *v, unsigned nv, const uint32_t *idx, unsigned ni)
{ (void)v; (void)idx; (void)ni; note_draw(s, p, nv); }
static void nb_draw_pixels(const H2GLState *s, int x, int y, int w, int h, const uint8_t *p)
{ (void)s; (void)x; (void)y; (void)w; (void)h; (void)p; }
static int  nb_read_pixels(int x, int y, int w, int h, uint8_t *p) { (void)x; (void)y; (void)w; (void)h; (void)p; return 0; }
static void nb_set_gamma(const uint16_t r[3][256]) { (void)r; }
static int  nb_caps(int what)
{
    switch (what) {
    case H2GL_CAP_MAX_TEXTURE_SIZE: return 2048;
    case H2GL_CAP_DEPTH_BITS: return 24;
    case H2GL_CAP_STENCIL_BITS: return 8;
    default: return 0;
    }
}

static const H2GLBackend k_null = {
    "null (headless)", nb_init, nb_shutdown, nb_present, nb_flush, nb_clear, nb_tex_image, nb_tex_subimage, nb_tex_param,
    nb_tex_delete, nb_draw, nb_draw_pixels, nb_read_pixels, nb_set_gamma, nb_caps
};

const H2GLBackend *h2_gl_null_backend(void) { return &k_null; }
