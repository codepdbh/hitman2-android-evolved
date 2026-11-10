/*
 * gl_null_backend.c - the headless backend: accepts everything the GL layer
 * produces, draws nothing, keeps the counters the tests and the boot probe
 * report. It is what runs on the PC test host (WORK_SAFE_MODE has no window).
 *
 * It does not pretend to be a GPU: read_pixels reports "no framebuffer", and
 * its capabilities are the ones the GL layer needs to run (2048-pixel
 * textures, 24-bit depth, 8-bit stencil), not a measurement of any hardware.
 */
#include "h2_gl.h"

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
{ (void)s; (void)p; (void)v; (void)nv; (void)idx; (void)ni; }
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
