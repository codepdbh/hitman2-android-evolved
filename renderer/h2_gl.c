/*
 * h2_gl.c - OpenGL 1.x fixed-function state machine for the recompiled renderer. See h2_gl.h.
 *
 * One GL context (the engine creates exactly one). Every call validates its
 * arguments the way OpenGL does: a bad enum or value sets the error flag, is
 * logged (the first few), and has no other effect. Nothing here draws; draws
 * are normalised and handed to the backend.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "h2_platform.h"
#include "h2_gl.h"

#define ARG(i) (args[(i)])
#define UNUSED (void)c; (void)args

/* ---- GL constants (only those used) ---- */
enum {
    GL_INVALID_ENUM = 0x0500, GL_INVALID_VALUE = 0x0501, GL_INVALID_OPERATION = 0x0502,
    GL_POINTS = 0, GL_LINES = 1, GL_LINE_LOOP = 2, GL_LINE_STRIP = 3, GL_TRIANGLES = 4, GL_TRIANGLE_STRIP = 5,
    GL_TRIANGLE_FAN = 6, GL_QUADS = 7, GL_QUAD_STRIP = 8, GL_POLYGON = 9,
    GL_MODELVIEW = 0x1700, GL_PROJECTION = 0x1701, GL_TEXTURE = 0x1702,
    GL_TEXTURE_2D = 0x0DE1, GL_UNPACK_ALIGNMENT = 0x0CF5, GL_UNPACK_ROW_LENGTH = 0x0CF2, GL_UNPACK_SKIP_ROWS = 0x0CF3,
    GL_UNPACK_SKIP_PIXELS = 0x0CF4, GL_UNPACK_SWAP_BYTES = 0x0CF0, GL_UNPACK_LSB_FIRST = 0x0CF1,
    GL_PACK_ALIGNMENT = 0x0D05, GL_PACK_ROW_LENGTH = 0x0D02,
    GL_VERTEX_ARRAY = 0x8074, GL_NORMAL_ARRAY = 0x8075, GL_COLOR_ARRAY = 0x8076, GL_TEXTURE_COORD_ARRAY = 0x8078,
    GL_COLOR_BUFFER_BIT = 0x4000, GL_DEPTH_BUFFER_BIT = 0x0100, GL_STENCIL_BUFFER_BIT = 0x0400, GL_ACCUM_BUFFER_BIT = 0x0200,
    GL_TEXTURE0_ARB = 0x84C0,
};

/* ---- state ---- */
typedef struct { int enabled, size; uint32_t type; int stride; uint32_t ptr; } ArrayPtr;
typedef struct { int used; int w, h; } TexObj;

static const H2GLBackend *g_be;
static H2GLState g_s;
static H2GLStats g_stats;
static uint32_t g_matrix_mode = GL_MODELVIEW;
static unsigned g_active_unit, g_client_unit;
static ArrayPtr g_vtx, g_nrm, g_col, g_tc[H2GL_MAX_TEX_UNITS];
static float g_cur_color[4] = { 1, 1, 1, 1 }, g_cur_normal[3] = { 0, 0, 1 }, g_cur_tc[H2GL_MAX_TEX_UNITS][4];
static float g_clear_color[4];
static int g_unpack_align = 4, g_pack_align = 4, g_raster_x, g_raster_y;
static TexObj g_tex[H2GL_MAX_TEXTURES];
static uint32_t g_next_tex = 1;
static int g_in_begin; static uint32_t g_begin_mode;
static int g_ctx_made_current, g_pixel_format_set;
static unsigned g_nerr_logged;
static uint16_t g_gamma[3][256];
static H2GLFrameHook g_frame_hook;

static void set_error(uint32_t code, const char *what)
{
    g_stats.errors++;
    if (!g_stats.last_error) g_stats.last_error = code;     /* GL keeps the FIRST error until read */
    if (g_nerr_logged++ < 25) H2_WARN(H2_LOG_GRAPHICS, "GL error 0x%04X: %s", code, what);
}

static float f32(uint32_t bits) { float f; memcpy(&f, &bits, 4); return f; }
static double f64(const uint32_t *a) { uint64_t u = a[0] | ((uint64_t)a[1] << 32); double d; memcpy(&d, &u, 8); return d; }
static void read_floats(uint32_t addr, float *out, int n) { for (int i = 0; i < n; i++) out[i] = f32(rd32(addr + 4u * (uint32_t)i)); }

/* ---- matrices (column-major) ---- */
static void mat_identity(H2Mat4 *m) { memset(m, 0, sizeof *m); m->m[0] = m->m[5] = m->m[10] = m->m[15] = 1.0f; }
static void mat_mul(H2Mat4 *dst, const H2Mat4 *a, const H2Mat4 *b)      /* dst = a * b */
{
    H2Mat4 r;
    for (int col = 0; col < 4; col++)
        for (int row = 0; row < 4; row++) {
            float s = 0;
            for (int k = 0; k < 4; k++) s += a->m[k * 4 + row] * b->m[col * 4 + k];
            r.m[col * 4 + row] = s;
        }
    *dst = r;
}
static H2Mat4 *cur_matrix(void)
{
    return g_matrix_mode == GL_MODELVIEW ? &g_s.modelview : g_matrix_mode == GL_PROJECTION ? &g_s.projection : &g_s.tex[g_active_unit].matrix;
}

/* ---- backend access ---- */
static const H2GLBackend *be(void) { return g_be ? g_be : (g_be = h2_gl_null_backend()); }

void h2_gl_set_backend(const H2GLBackend *b) { g_be = b; }
void h2_gl_set_frame_hook(H2GLFrameHook hook) { g_frame_hook = hook; }
void h2_gl_get_stats(H2GLStats *out) { *out = g_stats; unsigned n = 0; for (int i = 1; i < H2GL_MAX_TEXTURES; i++) n += g_tex[i].used;   /* name 0, the default texture, is not counted */ out->textures_live = n; }
const H2GLState *h2_gl_state(void) { return &g_s; }

static void state_defaults(void)
{
    memset(&g_s, 0, sizeof g_s);
    mat_identity(&g_s.modelview); mat_identity(&g_s.projection);
    for (int u = 0; u < H2GL_MAX_TEX_UNITS; u++) { mat_identity(&g_s.tex[u].matrix); g_s.tex[u].env_mode = 0x2100; /* MODULATE */ }
    g_s.depth_mask = 1; g_s.depth_func = 0x0201; /* LESS */
    g_s.cull_mode = 0x0405; /* BACK */ g_s.blend_src = 1; g_s.blend_dst = 0; g_s.blend_equation = 0x8006;
    g_s.alpha_func = 0x0207; /* ALWAYS */ g_s.shade_model = 0x1D01; /* SMOOTH */ g_s.polygon_mode = 0x1B02;
    g_s.color_material_face = 0x408; g_s.color_material_mode = 0x1602;
    for (int i = 0; i < H2GL_MAX_LIGHTS; i++) {
        H2GLLight *l = &g_s.light[i];
        l->ambient[3] = 1; l->position[2] = 1; l->spot_dir[2] = -1; l->spot_cutoff = 180; l->att_const = 1;
        if (i == 0) { l->diffuse[0] = l->diffuse[1] = l->diffuse[2] = l->diffuse[3] = 1; memcpy(l->specular, l->diffuse, 16); }
        else l->diffuse[3] = l->specular[3] = 1;
    }
    g_s.light_model_ambient[0] = g_s.light_model_ambient[1] = g_s.light_model_ambient[2] = 0.2f; g_s.light_model_ambient[3] = 1;
    for (int f = 0; f < 2; f++) {
        H2GLMaterial *m = f ? &g_s.material_back : &g_s.material_front;
        m->ambient[0] = m->ambient[1] = m->ambient[2] = 0.2f; m->ambient[3] = 1;
        m->diffuse[0] = m->diffuse[1] = m->diffuse[2] = 0.8f; m->diffuse[3] = 1; m->specular[3] = 1; m->emission[3] = 1;
    }
    g_s.fog_mode = 0x0800; g_s.fog_density = 1; g_s.fog_end = 1;
}

/* ---- errors/enums ---- */
static int *cap_flag(uint32_t cap)
{
    switch (cap) {
    case 0x0B71: return &g_s.depth_test;    case 0x0B44: return &g_s.cull_face;   case 0x0BE2: return &g_s.blend;
    case 0x0BC0: return &g_s.alpha_test;    case 0x0B50: return &g_s.lighting;    case 0x0B60: return &g_s.fog;
    case 0x0C11: return &g_s.scissor_test;  case 0x0B90: return &g_s.stencil_test; case 0x0BA1: return &g_s.normalize;
    case 0x0B57: return &g_s.color_material; case 0x8037: return &g_s.polygon_offset_fill;
    case 0x0DE1: return &g_s.tex[g_active_unit].enabled;
    case 0x0C60: case 0x0C61: case 0x0C62: case 0x0C63: return &g_s.tex[g_active_unit].gen_enabled[cap - 0x0C60];
    default:
        if (cap >= 0x4000 && cap < 0x4000u + H2GL_MAX_LIGHTS) return &g_s.light_enabled[cap - 0x4000];
        return 0;
    }
}
/* Capabilities that exist in OpenGL but this layer does not implement: enabling them is an error here. */
static int known_unsupported(uint32_t cap)
{
    return cap == 0x0B20 || cap == 0x0B10 || cap == 0x0B41 || cap == 0x0BD0 || cap == 0x0B43 /* line/point/polygon smooth, dither, line stipple */
        || (cap >= 0x3000 && cap < 0x3006);                                                  /* clip planes */
}

static uint32_t gl_Enable(CPU *c, const uint32_t *args)
{
    UNUSED;
    int *f = cap_flag(ARG(0));
    if (f) { *f = 1; return 0; }
    if (known_unsupported(ARG(0))) {
        /* Dither/smoothing are hints a GPU may ignore; clip planes are not. */
        if (ARG(0) >= 0x3000 && ARG(0) < 0x3006) H2_WARN(H2_LOG_GRAPHICS, "glEnable(GL_CLIP_PLANE%u): not implemented", ARG(0) - 0x3000);
        return 0;
    }
    set_error(GL_INVALID_ENUM, "glEnable: unknown capability");
    return 0;
}
static uint32_t gl_Disable(CPU *c, const uint32_t *args)
{
    UNUSED;
    int *f = cap_flag(ARG(0));
    if (f) *f = 0; else if (!known_unsupported(ARG(0))) set_error(GL_INVALID_ENUM, "glDisable: unknown capability");
    return 0;
}

static uint32_t gl_BlendFunc(CPU *c, const uint32_t *args) { UNUSED; g_s.blend_src = ARG(0); g_s.blend_dst = ARG(1); return 0; }
static uint32_t gl_AlphaFunc(CPU *c, const uint32_t *args)
{
    UNUSED;
    if (ARG(0) < 0x0200 || ARG(0) > 0x0207) { set_error(GL_INVALID_ENUM, "glAlphaFunc"); return 0; }
    g_s.alpha_func = ARG(0); g_s.alpha_ref = fminf(1.0f, fmaxf(0.0f, f32(ARG(1))));
    return 0;
}
static uint32_t gl_DepthFunc(CPU *c, const uint32_t *args)
{
    UNUSED;
    if (ARG(0) < 0x0200 || ARG(0) > 0x0207) { set_error(GL_INVALID_ENUM, "glDepthFunc"); return 0; }
    g_s.depth_func = ARG(0); return 0;
}
static uint32_t gl_DepthMask(CPU *c, const uint32_t *args) { UNUSED; g_s.depth_mask = (ARG(0) & 0xFFu) != 0; return 0; }
static uint32_t gl_CullFace(CPU *c, const uint32_t *args)
{
    UNUSED;
    if (ARG(0) != 0x0404 && ARG(0) != 0x0405 && ARG(0) != 0x0408) { set_error(GL_INVALID_ENUM, "glCullFace"); return 0; }
    g_s.cull_mode = ARG(0); return 0;
}
static uint32_t gl_ShadeModel(CPU *c, const uint32_t *args)
{
    UNUSED;
    if (ARG(0) != 0x1D00 && ARG(0) != 0x1D01) { set_error(GL_INVALID_ENUM, "glShadeModel"); return 0; }
    g_s.shade_model = ARG(0); return 0;
}
static uint32_t gl_PolygonMode(CPU *c, const uint32_t *args)
{
    UNUSED;
    if (ARG(1) < 0x1B00 || ARG(1) > 0x1B02) { set_error(GL_INVALID_ENUM, "glPolygonMode"); return 0; }
    if (ARG(1) != 0x1B02) H2_WARN(H2_LOG_GRAPHICS, "glPolygonMode(%s): no OpenGL ES equivalent, backends draw filled", ARG(1) == 0x1B01 ? "LINE" : "POINT");
    g_s.polygon_mode = ARG(1); return 0;
}
static uint32_t gl_ColorMaterial(CPU *c, const uint32_t *args)
{
    UNUSED;
    if ((ARG(0) != 0x404 && ARG(0) != 0x405 && ARG(0) != 0x408) || (ARG(1) < 0x1200 || (ARG(1) > 0x1202 && ARG(1) != 0x1600 && ARG(1) != 0x1602))) { set_error(GL_INVALID_ENUM, "glColorMaterial"); return 0; }
    g_s.color_material_face = ARG(0); g_s.color_material_mode = ARG(1); return 0;
}
static uint32_t gl_Scissor(CPU *c, const uint32_t *args)
{
    UNUSED;
    if ((int32_t)ARG(2) < 0 || (int32_t)ARG(3) < 0) { set_error(GL_INVALID_VALUE, "glScissor: negative size"); return 0; }
    for (int i = 0; i < 4; i++) g_s.scissor[i] = (int32_t)ARG(i);
    return 0;
}
static uint32_t gl_Viewport(CPU *c, const uint32_t *args)
{
    UNUSED;
    if ((int32_t)ARG(2) < 0 || (int32_t)ARG(3) < 0) { set_error(GL_INVALID_VALUE, "glViewport: negative size"); return 0; }
    for (int i = 0; i < 4; i++) g_s.viewport[i] = (int32_t)ARG(i);
    return 0;
}
static uint32_t gl_PixelStorei(CPU *c, const uint32_t *args)
{
    UNUSED;
    int v = (int32_t)ARG(1);
    switch (ARG(0)) {
    case GL_UNPACK_ALIGNMENT: if (v != 1 && v != 2 && v != 4 && v != 8) { set_error(GL_INVALID_VALUE, "glPixelStorei: alignment"); return 0; } g_unpack_align = v; return 0;
    case GL_PACK_ALIGNMENT:   if (v != 1 && v != 2 && v != 4 && v != 8) { set_error(GL_INVALID_VALUE, "glPixelStorei: alignment"); return 0; } g_pack_align = v; return 0;
    case GL_UNPACK_ROW_LENGTH: case GL_UNPACK_SKIP_ROWS: case GL_UNPACK_SKIP_PIXELS: case GL_PACK_ROW_LENGTH:
        if (v != 0) h2_guest_stop(H2_STOP_UNIMPLEMENTED_IMPORT, "glPixelStorei(0x%X, %d): row length / skip is not implemented", ARG(0), v);
        return 0;
    case GL_UNPACK_SWAP_BYTES: case GL_UNPACK_LSB_FIRST:
        if (v) h2_guest_stop(H2_STOP_UNIMPLEMENTED_IMPORT, "glPixelStorei(0x%X, %d): not implemented", ARG(0), v);
        return 0;
    default: set_error(GL_INVALID_ENUM, "glPixelStorei"); return 0;
    }
}
static uint32_t gl_DrawBuffer(CPU *c, const uint32_t *args)
{
    UNUSED;
    if (ARG(0) != 0x0405 /* BACK */ && ARG(0) != 0x0404) H2_WARN(H2_LOG_GRAPHICS, "glDrawBuffer(0x%X): only BACK/FRONT exist", ARG(0));
    return 0;
}
static uint32_t gl_ReadBuffer(CPU *c, const uint32_t *args) { UNUSED; return 0; }

/* ---- matrices ---- */
static uint32_t gl_MatrixMode(CPU *c, const uint32_t *args)
{
    UNUSED;
    if (ARG(0) != GL_MODELVIEW && ARG(0) != GL_PROJECTION && ARG(0) != GL_TEXTURE) { set_error(GL_INVALID_ENUM, "glMatrixMode"); return 0; }
    g_matrix_mode = ARG(0); return 0;
}
static uint32_t gl_LoadIdentity(CPU *c, const uint32_t *args) { UNUSED; mat_identity(cur_matrix()); return 0; }
static uint32_t gl_LoadMatrixf(CPU *c, const uint32_t *args) { UNUSED; read_floats(ARG(0), cur_matrix()->m, 16); return 0; }
static uint32_t gl_Frustum(CPU *c, const uint32_t *args)
{
    UNUSED;
    double l = f64(args), r = f64(args + 2), b = f64(args + 4), t = f64(args + 6), n = f64(args + 8), f = f64(args + 10);
    if (n <= 0 || f <= 0 || l == r || b == t || n == f) { set_error(GL_INVALID_VALUE, "glFrustum"); return 0; }
    H2Mat4 m; memset(&m, 0, sizeof m);
    m.m[0] = (float)(2 * n / (r - l)); m.m[5] = (float)(2 * n / (t - b));
    m.m[8] = (float)((r + l) / (r - l)); m.m[9] = (float)((t + b) / (t - b));
    m.m[10] = (float)(-(f + n) / (f - n)); m.m[11] = -1.0f; m.m[14] = (float)(-2 * f * n / (f - n));
    mat_mul(cur_matrix(), cur_matrix(), &m);
    return 0;
}
static uint32_t gl_Ortho(CPU *c, const uint32_t *args)
{
    UNUSED;
    double l = f64(args), r = f64(args + 2), b = f64(args + 4), t = f64(args + 6), n = f64(args + 8), f = f64(args + 10);
    if (l == r || b == t || n == f) { set_error(GL_INVALID_VALUE, "glOrtho"); return 0; }
    H2Mat4 m; mat_identity(&m);
    m.m[0] = (float)(2 / (r - l)); m.m[5] = (float)(2 / (t - b)); m.m[10] = (float)(-2 / (f - n));
    m.m[12] = (float)(-(r + l) / (r - l)); m.m[13] = (float)(-(t + b) / (t - b)); m.m[14] = (float)(-(f + n) / (f - n));
    mat_mul(cur_matrix(), cur_matrix(), &m);
    return 0;
}

/* ---- lighting, materials, fog ---- */
static H2GLLight *light_of(uint32_t id)
{
    if (id < 0x4000 || id >= 0x4000u + H2GL_MAX_LIGHTS) { set_error(GL_INVALID_ENUM, "light id"); return 0; }
    return &g_s.light[id - 0x4000];
}
static void light_set(H2GLLight *l, uint32_t pname, const float *v, int is_vec)
{
    switch (pname) {
    case 0x1200: if (is_vec) memcpy(l->ambient, v, 16); else goto bad; break;
    case 0x1201: if (is_vec) memcpy(l->diffuse, v, 16); else goto bad; break;
    case 0x1202: if (is_vec) memcpy(l->specular, v, 16); else goto bad; break;
    case 0x1203: if (is_vec) {                             /* transformed by the modelview current NOW (eye coordinates) */
        const float *m = g_s.modelview.m;
        for (int i = 0; i < 4; i++) l->position[i] = m[i] * v[0] + m[4 + i] * v[1] + m[8 + i] * v[2] + m[12 + i] * v[3];
    } else goto bad; break;
    case 0x1204: if (is_vec) {
        const float *m = g_s.modelview.m;
        for (int i = 0; i < 3; i++) l->spot_dir[i] = m[i] * v[0] + m[4 + i] * v[1] + m[8 + i] * v[2];
    } else goto bad; break;
    case 0x1205: if (v[0] < 0 || v[0] > 128) goto badval; l->spot_exponent = v[0]; break;
    case 0x1206: if (!((v[0] >= 0 && v[0] <= 90) || v[0] == 180)) goto badval; l->spot_cutoff = v[0]; break;
    case 0x1207: if (v[0] < 0) goto badval; l->att_const = v[0]; break;
    case 0x1208: if (v[0] < 0) goto badval; l->att_linear = v[0]; break;
    case 0x1209: if (v[0] < 0) goto badval; l->att_quad = v[0]; break;
    default: bad: set_error(GL_INVALID_ENUM, "glLight pname"); return;
    }
    return;
badval: set_error(GL_INVALID_VALUE, "glLight value");
}
static uint32_t gl_Lightf(CPU *c, const uint32_t *args) { UNUSED; H2GLLight *l = light_of(ARG(0)); if (l) { float v = f32(ARG(2)); light_set(l, ARG(1), &v, 0); } return 0; }
static uint32_t gl_Lightfv(CPU *c, const uint32_t *args)
{
    UNUSED;
    H2GLLight *l = light_of(ARG(0));
    if (!l) return 0;
    float v[4] = { 0, 0, 0, 0 };
    int n = (ARG(1) == 0x1204) ? 3 : (ARG(1) >= 0x1200 && ARG(1) <= 0x1203) ? 4 : 1;
    read_floats(ARG(2), v, n);
    light_set(l, ARG(1), v, n > 1);
    return 0;
}
static uint32_t gl_LightModelfv(CPU *c, const uint32_t *args)
{
    UNUSED;
    float v[4];
    if (ARG(0) == 0x0B53) { read_floats(ARG(1), v, 4); memcpy(g_s.light_model_ambient, v, 16); }
    else if (ARG(0) == 0x0B52 || ARG(0) == 0x0B51) {
        read_floats(ARG(1), v, 1);
        if (v[0] != 0) H2_WARN(H2_LOG_GRAPHICS, "glLightModel(%s): not implemented by backends", ARG(0) == 0x0B52 ? "TWO_SIDE" : "LOCAL_VIEWER");
    } else set_error(GL_INVALID_ENUM, "glLightModelfv");
    return 0;
}
static uint32_t gl_Materialfv(CPU *c, const uint32_t *args)
{
    UNUSED;
    float v[4];
    if (ARG(0) != 0x404 && ARG(0) != 0x405 && ARG(0) != 0x408) { set_error(GL_INVALID_ENUM, "glMaterial face"); return 0; }
    int n = ARG(1) == 0x1601 ? 1 : 4;
    if (ARG(1) != 0x1200 && ARG(1) != 0x1201 && ARG(1) != 0x1202 && ARG(1) != 0x1600 && ARG(1) != 0x1601 && ARG(1) != 0x1602) { set_error(GL_INVALID_ENUM, "glMaterial pname"); return 0; }
    read_floats(ARG(2), v, n);
    for (int side = 0; side < 2; side++) {
        if (!((side == 0 && ARG(0) != 0x405) || (side == 1 && ARG(0) != 0x404))) continue;
        H2GLMaterial *m = side ? &g_s.material_back : &g_s.material_front;
        switch (ARG(1)) {
        case 0x1200: memcpy(m->ambient, v, 16); break;   case 0x1201: memcpy(m->diffuse, v, 16); break;
        case 0x1202: memcpy(m->specular, v, 16); break;  case 0x1600: memcpy(m->emission, v, 16); break;
        case 0x1601: if (v[0] < 0 || v[0] > 128) set_error(GL_INVALID_VALUE, "shininess"); else m->shininess = v[0]; break;
        case 0x1602: memcpy(m->ambient, v, 16); memcpy(m->diffuse, v, 16); break;
        }
    }
    return 0;
}
static void fog_set(uint32_t pname, const float *v)
{
    switch (pname) {
    case 0x0B65: { uint32_t m = (uint32_t)v[0]; if (m != 0x2601 && m != 0x0800 && m != 0x0801) { set_error(GL_INVALID_ENUM, "GL_FOG_MODE"); return; } g_s.fog_mode = m; break; }
    case 0x0B62: if (v[0] < 0) { set_error(GL_INVALID_VALUE, "GL_FOG_DENSITY"); return; } g_s.fog_density = v[0]; break;
    case 0x0B63: g_s.fog_start = v[0]; break;
    case 0x0B64: g_s.fog_end = v[0]; break;
    case 0x0B66: memcpy(g_s.fog_color, v, 16); break;
    case 0x0B61: break;                                                     /* GL_FOG_INDEX: colour-index mode does not exist */
    default: set_error(GL_INVALID_ENUM, "glFog pname");
    }
}
static uint32_t gl_Fogi(CPU *c, const uint32_t *args) { UNUSED; float v[4] = { (float)(int32_t)ARG(1), 0, 0, 0 }; fog_set(ARG(0), v); return 0; }
static uint32_t gl_Fogf(CPU *c, const uint32_t *args) { UNUSED; float v[4] = { f32(ARG(1)), 0, 0, 0 }; fog_set(ARG(0), v); return 0; }
static uint32_t gl_Fogfv(CPU *c, const uint32_t *args) { UNUSED; float v[4] = { 0, 0, 0, 0 }; read_floats(ARG(1), v, ARG(0) == 0x0B66 ? 4 : 1); fog_set(ARG(0), v); return 0; }

/* ---- textures ---- */
static uint32_t gl_GenTextures(CPU *c, const uint32_t *args)
{
    UNUSED;
    if ((int32_t)ARG(0) < 0) { set_error(GL_INVALID_VALUE, "glGenTextures"); return 0; }
    for (uint32_t i = 0; i < ARG(0); i++) {
        while (g_next_tex < H2GL_MAX_TEXTURES && g_tex[g_next_tex].used) g_next_tex++;
        if (g_next_tex >= H2GL_MAX_TEXTURES) { H2_ERROR(H2_LOG_GRAPHICS, "texture name space exhausted"); wr32(ARG(1) + 4 * i, 0); continue; }
        g_tex[g_next_tex].used = 1; g_tex[g_next_tex].w = g_tex[g_next_tex].h = 0;
        wr32(ARG(1) + 4 * i, g_next_tex++);
    }
    return 0;
}
static uint32_t gl_DeleteTextures(CPU *c, const uint32_t *args)
{
    UNUSED;
    if ((int32_t)ARG(0) < 0) { set_error(GL_INVALID_VALUE, "glDeleteTextures"); return 0; }
    for (uint32_t i = 0; i < ARG(0); i++) {
        uint32_t t = rd32(ARG(1) + 4 * i);
        if (!t || t >= H2GL_MAX_TEXTURES || !g_tex[t].used) continue;
        g_tex[t].used = 0; if (t < g_next_tex) g_next_tex = t;
        for (int u = 0; u < H2GL_MAX_TEX_UNITS; u++) if (g_s.tex[u].bound == t) g_s.tex[u].bound = 0;
        be()->tex_delete(t);
    }
    return 0;
}
static uint32_t gl_BindTexture(CPU *c, const uint32_t *args)
{
    UNUSED;
    if (ARG(0) != GL_TEXTURE_2D) { set_error(GL_INVALID_ENUM, "glBindTexture target"); return 0; }
    if (ARG(1) >= H2GL_MAX_TEXTURES || (ARG(1) && !g_tex[ARG(1)].used)) {
        if (ARG(1) < H2GL_MAX_TEXTURES) g_tex[ARG(1)].used = 1;       /* GL 1.1: binding an unused name creates it */
        else { set_error(GL_INVALID_VALUE, "glBindTexture name"); return 0; }
    }
    g_s.tex[g_active_unit].bound = ARG(1);
    return 0;
}

static int pixel_bytes(uint32_t fmt, uint32_t type)
{
    int comps = fmt == 0x1908 || fmt == 0x80E1 ? 4 : fmt == 0x1907 || fmt == 0x80E0 ? 3 : fmt == 0x190A ? 2 : (fmt == 0x1909 || fmt == 0x1906) ? 1 : 0;
    if (!comps) return 0;
    switch (type) {
    case 0x1401: return comps;                                             /* UNSIGNED_BYTE */
    case 0x8363: return fmt == 0x1907 ? 2 : 0;                            /* 5_6_5, RGB */
    case 0x8033: case 0x8034: case 0x8365: case 0x8366: return (fmt == 0x1908 || fmt == 0x80E1) ? 2 : 0;
    default: return 0;
    }
}

/* Convert a block of client pixels to tightly packed RGBA8. Returns 0 on success. */
static int to_rgba8(uint32_t fmt, uint32_t type, int w, int h, uint32_t src, int keep_alpha, uint8_t *out)
{
    int pb = pixel_bytes(fmt, type);
    if (!pb) return -1;
    int stride = ((w * pb + g_unpack_align - 1) / g_unpack_align) * g_unpack_align;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            uint32_t p = src + (uint32_t)(y * stride + x * pb);
            uint8_t r = 0, g = 0, b = 0, a = 255;
            if (type == 0x1401) {
                uint8_t v0 = rd8(p), v1 = pb > 1 ? rd8(p + 1) : 0, v2 = pb > 2 ? rd8(p + 2) : 0, v3 = pb > 3 ? rd8(p + 3) : 255;
                switch (fmt) {
                case 0x1908: r = v0; g = v1; b = v2; a = v3; break;
                case 0x80E1: b = v0; g = v1; r = v2; a = v3; break;
                case 0x1907: r = v0; g = v1; b = v2; break;
                case 0x80E0: b = v0; g = v1; r = v2; break;
                case 0x1909: r = g = b = v0; break;
                case 0x190A: r = g = b = v0; a = v1; break;
                case 0x1906: r = g = b = 255; a = v0; break;
                }
            } else {
                uint16_t v = rd16(p);
                if (type == 0x8363) { r = (uint8_t)(((v >> 11) & 31) * 255 / 31); g = (uint8_t)(((v >> 5) & 63) * 255 / 63); b = (uint8_t)((v & 31) * 255 / 31); }
                else if (type == 0x8033) { r = (uint8_t)(((v >> 12) & 15) * 17); g = (uint8_t)(((v >> 8) & 15) * 17); b = (uint8_t)(((v >> 4) & 15) * 17); a = (uint8_t)((v & 15) * 17); }
                else if (type == 0x8034) { r = (uint8_t)(((v >> 11) & 31) * 255 / 31); g = (uint8_t)(((v >> 6) & 31) * 255 / 31); b = (uint8_t)(((v >> 1) & 31) * 255 / 31); a = (v & 1) ? 255 : 0; }
                else if (type == 0x8365) { a = (uint8_t)(((v >> 12) & 15) * 17); r = (uint8_t)(((v >> 8) & 15) * 17); g = (uint8_t)(((v >> 4) & 15) * 17); b = (uint8_t)((v & 15) * 17); }   /* 4_4_4_4_REV */
                else { a = (v & 0x8000) ? 255 : 0; r = (uint8_t)(((v >> 10) & 31) * 255 / 31); g = (uint8_t)(((v >> 5) & 31) * 255 / 31); b = (uint8_t)((v & 31) * 255 / 31); }          /* 1_5_5_5_REV */
                if (fmt == 0x80E1 && type != 0x8365 && type != 0x8366) { uint8_t t = r; r = b; b = t; }
            }
            uint8_t *o = out + 4 * (y * w + x);
            o[0] = r; o[1] = g; o[2] = b; o[3] = keep_alpha ? a : 255;
        }
    return 0;
}

static int internal_has_alpha(uint32_t ifmt)
{
    switch (ifmt) {
    case 1: case 3: case 0x1907: case 0x1909: case 0x8050: case 0x8051: case 0x804F: case 0x8040: case 0x8041: case 0x8042: case 0x8043: return 0;   /* RGB / LUMINANCE families */
    default: return 1;
    }
}
static int is_pot(int v) { return v > 0 && (v & (v - 1)) == 0; }

static uint8_t *scratch(size_t n)
{
    static uint8_t *buf; static size_t cap;
    if (n > cap) { free(buf); buf = (uint8_t *)malloc(cap = n); }
    return buf;
}

static uint32_t gl_TexImage2D(CPU *c, const uint32_t *args)
{
    UNUSED;
    /* (target, level, internalformat, width, height, border, format, type, pixels) */
    int level = (int32_t)ARG(1), w = (int32_t)ARG(3), h = (int32_t)ARG(4);
    uint32_t tex = g_s.tex[g_active_unit].bound;
    if (ARG(0) != GL_TEXTURE_2D) { set_error(GL_INVALID_ENUM, "glTexImage2D target"); return 0; }
    int maxs = be()->get_caps(H2GL_CAP_MAX_TEXTURE_SIZE);
    if (level < 0 || level > 12 || ARG(5) != 0 || !is_pot(w) || !is_pot(h) || w > maxs || h > maxs) {
        set_error(GL_INVALID_VALUE, "glTexImage2D: size must be a power of two <= max, no border"); return 0;
    }
    /* texture name 0 is the default texture object of GL 1.1: valid to specify and parameterise */
    if (ARG(8) && !pixel_bytes(ARG(6), ARG(7))) { set_error(GL_INVALID_ENUM, "glTexImage2D: format/type combination"); return 0; }
    uint8_t *rgba = scratch((size_t)w * (size_t)h * 4);
    if (ARG(8)) to_rgba8(ARG(6), ARG(7), w, h, ARG(8), internal_has_alpha(ARG(2)), rgba);
    else memset(rgba, 0, (size_t)w * (size_t)h * 4);
    if (level == 0) { g_tex[tex].w = w; g_tex[tex].h = h; }
    be()->tex_image(tex, level, w, h, rgba);
    g_stats.tex_uploads++; g_stats.tex_bytes += (uint64_t)w * (uint64_t)h * 4u;
    return 0;
}
static uint32_t gl_TexSubImage2D(CPU *c, const uint32_t *args)
{
    UNUSED;
    /* (target, level, xoffset, yoffset, width, height, format, type, pixels) */
    int level = (int32_t)ARG(1), x = (int32_t)ARG(2), y = (int32_t)ARG(3), w = (int32_t)ARG(4), h = (int32_t)ARG(5);
    uint32_t tex = g_s.tex[g_active_unit].bound;
    if (ARG(0) != GL_TEXTURE_2D) { set_error(GL_INVALID_ENUM, "glTexSubImage2D target"); return 0; }
    if (!g_tex[tex].w) { set_error(GL_INVALID_OPERATION, "glTexSubImage2D: no image"); return 0; }
    if (level != 0 || x < 0 || y < 0 || w < 0 || h < 0 || x + w > g_tex[tex].w || y + h > g_tex[tex].h) { set_error(GL_INVALID_VALUE, "glTexSubImage2D: region"); return 0; }
    if (!w || !h) return 0;
    if (!ARG(8) || !pixel_bytes(ARG(6), ARG(7))) { set_error(GL_INVALID_ENUM, "glTexSubImage2D: format/type/pixels"); return 0; }
    uint8_t *rgba = scratch((size_t)w * (size_t)h * 4);
    to_rgba8(ARG(6), ARG(7), w, h, ARG(8), 1, rgba);
    be()->tex_subimage(tex, level, x, y, w, h, rgba);
    g_stats.tex_uploads++; g_stats.tex_bytes += (uint64_t)w * (uint64_t)h * 4u;
    return 0;
}
static uint32_t gl_TexParameteri(CPU *c, const uint32_t *args)
{
    UNUSED;
    uint32_t tex = g_s.tex[g_active_unit].bound;
    if (ARG(0) != GL_TEXTURE_2D) { set_error(GL_INVALID_ENUM, "glTexParameteri target"); return 0; }
    if (ARG(1) < 0x2800 || ARG(1) > 0x2803) { set_error(GL_INVALID_ENUM, "glTexParameteri pname"); return 0; }
    be()->tex_param(tex, ARG(1), ARG(2));
    return 0;
}
static uint32_t gl_TexEnv(CPU *c, const uint32_t *args, int is_float)
{
    UNUSED;
    if (ARG(0) != 0x2300 || ARG(1) != 0x2200) {
        if (ARG(0) == 0x2300 && ARG(1) == 0x2201) h2_guest_stop(H2_STOP_UNIMPLEMENTED_IMPORT, "glTexEnv GL_TEXTURE_ENV_COLOR via the scalar form");
        set_error(GL_INVALID_ENUM, "glTexEnv target/pname"); return 0;
    }
    uint32_t m = is_float ? (uint32_t)f32(ARG(2)) : ARG(2);
    if (m != 0x2100 && m != 0x2101 && m != 0x0BE2 && m != 0x1E01 && m != 0x0104) { set_error(GL_INVALID_ENUM, "glTexEnv mode"); return 0; }
    g_s.tex[g_active_unit].env_mode = m;
    return 0;
}
static uint32_t gl_TexEnvi(CPU *c, const uint32_t *args) { return gl_TexEnv(c, args, 0); }
static uint32_t gl_TexEnvf(CPU *c, const uint32_t *args) { return gl_TexEnv(c, args, 1); }
static uint32_t gl_TexGenf(CPU *c, const uint32_t *args)
{
    UNUSED;
    if (ARG(0) < 0x2000 || ARG(0) > 0x2003 || ARG(1) != 0x2500) { set_error(GL_INVALID_ENUM, "glTexGenf coord/pname"); return 0; }
    uint32_t mode = (uint32_t)f32(ARG(2));
    if (mode != 0x2400 && mode != 0x2401 && mode != 0x2402) { set_error(GL_INVALID_ENUM, "glTexGenf mode"); return 0; }
    g_s.tex[g_active_unit].gen_mode[ARG(0) - 0x2000] = mode;
    return 0;
}

/* ---- multitexture (ARB) ---- */
static uint32_t gl_ActiveTextureARB(CPU *c, const uint32_t *args)
{
    UNUSED;
    if (ARG(0) < GL_TEXTURE0_ARB || ARG(0) >= GL_TEXTURE0_ARB + H2GL_MAX_TEX_UNITS) { set_error(GL_INVALID_ENUM, "glActiveTextureARB"); return 0; }
    g_active_unit = ARG(0) - GL_TEXTURE0_ARB; return 0;
}
static uint32_t gl_ClientActiveTextureARB(CPU *c, const uint32_t *args)
{
    UNUSED;
    if (ARG(0) < GL_TEXTURE0_ARB || ARG(0) >= GL_TEXTURE0_ARB + H2GL_MAX_TEX_UNITS) { set_error(GL_INVALID_ENUM, "glClientActiveTextureARB"); return 0; }
    g_client_unit = ARG(0) - GL_TEXTURE0_ARB; return 0;
}
static uint32_t gl_MultiTexCoord2fvARB(CPU *c, const uint32_t *args)
{
    UNUSED;
    if (ARG(0) < GL_TEXTURE0_ARB || ARG(0) >= GL_TEXTURE0_ARB + H2GL_MAX_TEX_UNITS) { set_error(GL_INVALID_ENUM, "glMultiTexCoord2fvARB"); return 0; }
    float *t = g_cur_tc[ARG(0) - GL_TEXTURE0_ARB];
    read_floats(ARG(1), t, 2); t[2] = 0; t[3] = 1; return 0;
}
static uint32_t gl_MultiTexCoord4fvARB(CPU *c, const uint32_t *args)
{
    UNUSED;
    if (ARG(0) < GL_TEXTURE0_ARB || ARG(0) >= GL_TEXTURE0_ARB + H2GL_MAX_TEX_UNITS) { set_error(GL_INVALID_ENUM, "glMultiTexCoord4fvARB"); return 0; }
    read_floats(ARG(1), g_cur_tc[ARG(0) - GL_TEXTURE0_ARB], 4); return 0;
}
static uint32_t gl_BlendEquationEXT(CPU *c, const uint32_t *args)
{
    UNUSED;
    if (ARG(0) != 0x8006 && ARG(0) != 0x800A && ARG(0) != 0x800B) { set_error(GL_INVALID_ENUM, "glBlendEquationEXT"); return 0; }
    g_s.blend_equation = ARG(0); return 0;
}

/* ---- primitives: gather, normalise, submit ---- */
static uint32_t *g_idx; static size_t g_idx_cap;
static H2GLVertex *g_vbuf; static size_t g_vbuf_cap;

static uint32_t *idx_buf(size_t n) { if (n > g_idx_cap) { free(g_idx); g_idx = (uint32_t *)malloc((g_idx_cap = n + 1024) * 4); } return g_idx; }
static H2GLVertex *vtx_buf(size_t n) { if (n > g_vbuf_cap) { free(g_vbuf); g_vbuf = (H2GLVertex *)malloc((g_vbuf_cap = n + 1024) * sizeof(H2GLVertex)); } return g_vbuf; }

/* Turn a GL primitive over the vertex sequence seq[0..n) into a point/line/triangle index LIST and submit it. */
static void submit(uint32_t mode, const H2GLVertex *v, unsigned nverts, const uint32_t *seq, unsigned n)
{
    size_t cap = (size_t)n * 6 + 8;
    uint32_t *o = idx_buf(cap); unsigned k = 0;
    H2GLPrim prim = H2GL_TRIANGLES;
    switch (mode) {
    case GL_POINTS: prim = H2GL_POINTS; for (unsigned i = 0; i < n; i++) o[k++] = seq[i]; break;
    case GL_LINES: prim = H2GL_LINES; for (unsigned i = 0; i + 1 < n; i += 2) { o[k++] = seq[i]; o[k++] = seq[i + 1]; } break;
    case GL_LINE_STRIP: prim = H2GL_LINES; for (unsigned i = 0; i + 1 < n; i++) { o[k++] = seq[i]; o[k++] = seq[i + 1]; } break;
    case GL_LINE_LOOP: prim = H2GL_LINES; for (unsigned i = 0; i + 1 < n; i++) { o[k++] = seq[i]; o[k++] = seq[i + 1]; } if (n > 2) { o[k++] = seq[n - 1]; o[k++] = seq[0]; } break;
    case GL_TRIANGLES: for (unsigned i = 0; i + 2 < n; i += 3) { o[k++] = seq[i]; o[k++] = seq[i + 1]; o[k++] = seq[i + 2]; } break;
    case GL_TRIANGLE_STRIP:
        for (unsigned i = 0; i + 2 < n; i++) {
            if (i & 1) { o[k++] = seq[i + 1]; o[k++] = seq[i]; o[k++] = seq[i + 2]; } else { o[k++] = seq[i]; o[k++] = seq[i + 1]; o[k++] = seq[i + 2]; }
        } break;
    case GL_TRIANGLE_FAN: case GL_POLYGON: for (unsigned i = 1; i + 1 < n; i++) { o[k++] = seq[0]; o[k++] = seq[i]; o[k++] = seq[i + 1]; } break;
    case GL_QUADS: for (unsigned i = 0; i + 3 < n; i += 4) { o[k++] = seq[i]; o[k++] = seq[i + 1]; o[k++] = seq[i + 2]; o[k++] = seq[i]; o[k++] = seq[i + 2]; o[k++] = seq[i + 3]; } break;
    case GL_QUAD_STRIP: for (unsigned i = 0; i + 3 < n; i += 2) { o[k++] = seq[i]; o[k++] = seq[i + 1]; o[k++] = seq[i + 3]; o[k++] = seq[i]; o[k++] = seq[i + 3]; o[k++] = seq[i + 2]; } break;
    }
    if (!k) return;
    be()->draw(&g_s, prim, v, nverts, o, k);
    g_stats.draw_calls++; g_stats.vertices_submitted += n;
    g_stats.triangles += prim == H2GL_TRIANGLES ? k / 3 : 0;
}

static int type_size(uint32_t t) { switch (t) { case 0x1400: case 0x1401: return 1; case 0x1402: case 0x1403: return 2; case 0x1404: case 0x1405: case 0x1406: return 4; case 0x140A: return 8; default: return 0; } }
static float component(uint32_t type, uint32_t p, int normalized)
{
    switch (type) {
    case 0x1406: return f32(rd32(p));
    case 0x140A: { uint64_t u = rd64(p); double d; memcpy(&d, &u, 8); return (float)d; }
    case 0x1401: return normalized ? rd8(p) / 255.0f : (float)rd8(p);
    case 0x1400: return normalized ? ((float)(int8_t)rd8(p) * 2 + 1) / 255.0f : (float)(int8_t)rd8(p);
    case 0x1403: return normalized ? rd16(p) / 65535.0f : (float)rd16(p);
    case 0x1402: return normalized ? ((float)(int16_t)rd16(p) * 2 + 1) / 65535.0f : (float)(int16_t)rd16(p);
    case 0x1405: return normalized ? (float)((double)rd32(p) / 4294967295.0) : (float)rd32(p);
    case 0x1404: return normalized ? (float)((double)(int32_t)rd32(p) / 2147483647.0) : (float)(int32_t)rd32(p);
    }
    return 0;
}
static void fetch(const ArrayPtr *a, uint32_t index, float *out, int maxc, int normalized)
{
    int ts = type_size(a->type), stride = a->stride ? a->stride : a->size * ts;
    uint32_t p = a->ptr + (uint32_t)index * (uint32_t)stride;
    for (int i = 0; i < a->size && i < maxc; i++) out[i] = component(a->type, p + (uint32_t)(i * ts), normalized);
}

static uint32_t gl_EnableClientState(CPU *c, const uint32_t *args)
{
    UNUSED;
    switch (ARG(0)) {
    case GL_VERTEX_ARRAY: g_vtx.enabled = 1; break;  case GL_NORMAL_ARRAY: g_nrm.enabled = 1; break;
    case GL_COLOR_ARRAY: g_col.enabled = 1; break;   case GL_TEXTURE_COORD_ARRAY: g_tc[g_client_unit].enabled = 1; break;
    default: set_error(GL_INVALID_ENUM, "glEnableClientState"); }
    return 0;
}
static uint32_t gl_DisableClientState(CPU *c, const uint32_t *args)
{
    UNUSED;
    switch (ARG(0)) {
    case GL_VERTEX_ARRAY: g_vtx.enabled = 0; break;  case GL_NORMAL_ARRAY: g_nrm.enabled = 0; break;
    case GL_COLOR_ARRAY: g_col.enabled = 0; break;   case GL_TEXTURE_COORD_ARRAY: g_tc[g_client_unit].enabled = 0; break;
    default: set_error(GL_INVALID_ENUM, "glDisableClientState"); }
    return 0;
}
static int set_pointer(ArrayPtr *a, int size, uint32_t type, int stride, uint32_t ptr, int min_size, int max_size, const char *what)
{
    if (size < min_size || size > max_size || stride < 0 || !type_size(type)) { set_error(size < min_size || size > max_size ? GL_INVALID_VALUE : GL_INVALID_ENUM, what); return 0; }
    a->size = size; a->type = type; a->stride = stride; a->ptr = ptr; return 1;
}
static uint32_t gl_VertexPointer(CPU *c, const uint32_t *args) { UNUSED; set_pointer(&g_vtx, (int32_t)ARG(0), ARG(1), (int32_t)ARG(2), ARG(3), 2, 4, "glVertexPointer"); return 0; }
static uint32_t gl_NormalPointer(CPU *c, const uint32_t *args) { UNUSED; set_pointer(&g_nrm, 3, ARG(0), (int32_t)ARG(1), ARG(2), 3, 3, "glNormalPointer"); return 0; }
static uint32_t gl_ColorPointer(CPU *c, const uint32_t *args) { UNUSED; set_pointer(&g_col, (int32_t)ARG(0), ARG(1), (int32_t)ARG(2), ARG(3), 3, 4, "glColorPointer"); return 0; }
static uint32_t gl_TexCoordPointer(CPU *c, const uint32_t *args) { UNUSED; set_pointer(&g_tc[g_client_unit], (int32_t)ARG(0), ARG(1), (int32_t)ARG(2), ARG(3), 1, 4, "glTexCoordPointer"); return 0; }

static void gather_vertex(H2GLVertex *o, uint32_t index)
{
    o->pos[0] = o->pos[1] = o->pos[2] = 0; o->pos[3] = 1;
    memcpy(o->color, g_cur_color, 16); memcpy(o->normal, g_cur_normal, 12);
    if (g_vtx.enabled) fetch(&g_vtx, index, o->pos, 4, 0);
    if (g_col.enabled) { o->color[3] = 1; fetch(&g_col, index, o->color, 4, 1); }
    if (g_nrm.enabled) fetch(&g_nrm, index, o->normal, 3, 1);
    for (int u = 0; u < H2GL_MAX_TEX_UNITS; u++) {
        memcpy(o->tc[u], g_cur_tc[u], 16);
        if (g_tc[u].enabled) { o->tc[u][2] = 0; o->tc[u][3] = 1; fetch(&g_tc[u], index, o->tc[u], 4, 0); }
    }
}

static uint32_t gl_DrawElements(CPU *c, const uint32_t *args)
{
    UNUSED;
    uint32_t mode = ARG(0), type = ARG(2), ip = ARG(3);
    int32_t count = (int32_t)ARG(1);
    if (mode > GL_POLYGON) { set_error(GL_INVALID_ENUM, "glDrawElements mode"); return 0; }
    if (type != 0x1401 && type != 0x1403 && type != 0x1405) { set_error(GL_INVALID_ENUM, "glDrawElements type"); return 0; }
    if (count < 0) { set_error(GL_INVALID_VALUE, "glDrawElements count"); return 0; }
    if (g_in_begin) { set_error(GL_INVALID_OPERATION, "glDrawElements between glBegin/glEnd"); return 0; }
    if (!count || !g_vtx.enabled) return 0;
    uint32_t lo = 0xFFFFFFFFu, hi = 0;
    uint32_t *seq = (uint32_t *)malloc((size_t)count * 4);
    if (!seq) return 0;
    for (int32_t i = 0; i < count; i++) {
        uint32_t v = type == 0x1401 ? rd8(ip + (uint32_t)i) : type == 0x1403 ? rd16(ip + 2u * (uint32_t)i) : rd32(ip + 4u * (uint32_t)i);
        seq[i] = v; if (v < lo) lo = v; if (v > hi) hi = v;
    }
    if (hi - lo > 4u * 1024u * 1024u) { free(seq); h2_guest_stop(H2_STOP_UNIMPLEMENTED_IMPORT, "glDrawElements index range %u..%u is implausible", lo, hi); }
    unsigned nv = hi - lo + 1;
    H2GLVertex *vb = vtx_buf(nv);
    for (unsigned i = 0; i < nv; i++) gather_vertex(&vb[i], lo + i);
    for (int32_t i = 0; i < count; i++) seq[i] -= lo;
    submit(mode, vb, nv, seq, (unsigned)count);
    free(seq);
    return 0;
}

/* ---- immediate mode ---- */
#define IMM_MAX 32768
static H2GLVertex g_imm[IMM_MAX];
static unsigned g_imm_n;

static uint32_t gl_Begin(CPU *c, const uint32_t *args)
{
    UNUSED;
    if (ARG(0) > GL_POLYGON) { set_error(GL_INVALID_ENUM, "glBegin mode"); return 0; }
    if (g_in_begin) { set_error(GL_INVALID_OPERATION, "glBegin inside glBegin"); return 0; }
    g_in_begin = 1; g_begin_mode = ARG(0); g_imm_n = 0; return 0;
}
static void imm_vertex(float x, float y, float z, float w)
{
    if (!g_in_begin) { set_error(GL_INVALID_OPERATION, "glVertex outside glBegin"); return; }
    if (g_imm_n == IMM_MAX) { H2_ERROR(H2_LOG_GRAPHICS, "immediate-mode batch exceeds %d vertices: dropped", IMM_MAX); return; }
    H2GLVertex *v = &g_imm[g_imm_n++];
    v->pos[0] = x; v->pos[1] = y; v->pos[2] = z; v->pos[3] = w;
    memcpy(v->color, g_cur_color, 16); memcpy(v->normal, g_cur_normal, 12);
    for (int u = 0; u < H2GL_MAX_TEX_UNITS; u++) memcpy(v->tc[u], g_cur_tc[u], 16);
}
static uint32_t gl_Vertex2f(CPU *c, const uint32_t *args) { UNUSED; imm_vertex(f32(ARG(0)), f32(ARG(1)), 0, 1); return 0; }
static uint32_t gl_Vertex3f(CPU *c, const uint32_t *args) { UNUSED; imm_vertex(f32(ARG(0)), f32(ARG(1)), f32(ARG(2)), 1); return 0; }
static uint32_t gl_Vertex3fv(CPU *c, const uint32_t *args) { UNUSED; float v[3]; read_floats(ARG(0), v, 3); imm_vertex(v[0], v[1], v[2], 1); return 0; }
static uint32_t gl_TexCoord2f(CPU *c, const uint32_t *args) { UNUSED; g_cur_tc[0][0] = f32(ARG(0)); g_cur_tc[0][1] = f32(ARG(1)); g_cur_tc[0][2] = 0; g_cur_tc[0][3] = 1; return 0; }
static uint32_t gl_Color4f(CPU *c, const uint32_t *args) { UNUSED; for (int i = 0; i < 4; i++) g_cur_color[i] = f32(ARG(i)); return 0; }
static uint32_t gl_Color4ub(CPU *c, const uint32_t *args) { UNUSED; for (int i = 0; i < 4; i++) g_cur_color[i] = (ARG(i) & 0xFFu) / 255.0f; return 0; }
static uint32_t gl_End(CPU *c, const uint32_t *args)
{
    UNUSED;
    if (!g_in_begin) { set_error(GL_INVALID_OPERATION, "glEnd without glBegin"); return 0; }
    g_in_begin = 0;
    if (!g_imm_n) return 0;
    uint32_t *seq = (uint32_t *)malloc(g_imm_n * 4);
    if (!seq) return 0;
    for (unsigned i = 0; i < g_imm_n; i++) seq[i] = i;
    submit(g_begin_mode, g_imm, g_imm_n, seq, g_imm_n);
    free(seq);
    return 0;
}

/* ---- framebuffer ---- */
static uint32_t gl_ClearColor(CPU *c, const uint32_t *args) { UNUSED; for (int i = 0; i < 4; i++) g_clear_color[i] = fminf(1, fmaxf(0, f32(ARG(i)))); return 0; }
static uint32_t gl_Clear(CPU *c, const uint32_t *args)
{
    UNUSED;
    if (ARG(0) & ~(uint32_t)(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT | GL_ACCUM_BUFFER_BIT)) { set_error(GL_INVALID_VALUE, "glClear mask"); return 0; }
    if (g_in_begin) { set_error(GL_INVALID_OPERATION, "glClear between glBegin/glEnd"); return 0; }
    be()->clear(ARG(0), g_clear_color, 1.0, 0, &g_s);
    g_stats.clears++;
    return 0;
}
static uint32_t gl_Flush(CPU *c, const uint32_t *args) { UNUSED; be()->flush(); return 0; }
static uint32_t gl_Finish(CPU *c, const uint32_t *args) { UNUSED; be()->flush(); return 0; }
static uint32_t gl_RasterPos2i(CPU *c, const uint32_t *args) { UNUSED; g_raster_x = (int32_t)ARG(0); g_raster_y = (int32_t)ARG(1); return 0; }
static uint32_t gl_DrawPixels(CPU *c, const uint32_t *args)
{
    UNUSED;
    int w = (int32_t)ARG(0), h = (int32_t)ARG(1);
    if (w < 0 || h < 0) { set_error(GL_INVALID_VALUE, "glDrawPixels size"); return 0; }
    if (!w || !h) return 0;
    if (!pixel_bytes(ARG(2), ARG(3))) { set_error(GL_INVALID_ENUM, "glDrawPixels format/type"); return 0; }
    uint8_t *rgba = scratch((size_t)w * (size_t)h * 4);
    to_rgba8(ARG(2), ARG(3), w, h, ARG(4), 1, rgba);
    be()->draw_pixels(&g_s, g_raster_x, g_raster_y, w, h, rgba);
    g_stats.draw_calls++;
    return 0;
}
static uint32_t gl_ReadPixels(CPU *c, const uint32_t *args)
{
    UNUSED;
    /* (x, y, width, height, format, type, pixels) */
    int x = (int32_t)ARG(0), y = (int32_t)ARG(1), w = (int32_t)ARG(2), h = (int32_t)ARG(3);
    if (w < 0 || h < 0) { set_error(GL_INVALID_VALUE, "glReadPixels size"); return 0; }
    if (ARG(5) != 0x1401 || (ARG(4) != 0x1908 && ARG(4) != 0x1907 && ARG(4) != 0x80E0 && ARG(4) != 0x80E1)) { set_error(GL_INVALID_ENUM, "glReadPixels format/type"); return 0; }
    if (!w || !h) return 0;
    uint8_t *rgba = scratch((size_t)w * (size_t)h * 4);
    if (!be()->read_pixels(x, y, w, h, rgba)) {
        H2_WARN(H2_LOG_GRAPHICS, "glReadPixels: backend '%s' has no framebuffer to read; client memory left unchanged", be()->name);
        return 0;
    }
    int comps = (ARG(4) == 0x1908 || ARG(4) == 0x80E1) ? 4 : 3, stride = ((w * comps + g_pack_align - 1) / g_pack_align) * g_pack_align;
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) {
            const uint8_t *s = rgba + 4 * (j * w + i);
            uint32_t d = ARG(6) + (uint32_t)(j * stride + i * comps);
            int bgr = ARG(4) == 0x80E0 || ARG(4) == 0x80E1;
            wr8(d, bgr ? s[2] : s[0]); wr8(d + 1, s[1]); wr8(d + 2, bgr ? s[0] : s[2]);
            if (comps == 4) wr8(d + 3, s[3]);
        }
    return 0;
}

/* ---- queries ---- */
static uint32_t g_str_vendor, g_str_renderer, g_str_version, g_str_ext;
static const char k_extensions[] = "GL_ARB_multitexture GL_EXT_texture_env_add GL_EXT_blend_subtract GL_EXT_bgra GL_EXT_packed_pixels GL_SGIS_texture_edge_clamp";

static uint32_t gl_GetString(CPU *c, const uint32_t *args)
{
    UNUSED;
    char buf[128];
    switch (ARG(0)) {
    case 0x1F00: if (!g_str_vendor) g_str_vendor = h2_os_strdup("H2AE"); return g_str_vendor;
    case 0x1F01: if (!g_str_renderer) { snprintf(buf, sizeof buf, "H2AE fixed-function layer (%s)", be()->name); g_str_renderer = h2_os_strdup(buf); } return g_str_renderer;
    case 0x1F02: if (!g_str_version) g_str_version = h2_os_strdup("1.1 H2AE"); return g_str_version;
    case 0x1F03: if (!g_str_ext) g_str_ext = h2_os_strdup(k_extensions); return g_str_ext;
    default: set_error(GL_INVALID_ENUM, "glGetString"); return 0;
    }
}
static uint32_t gl_GetIntegerv(CPU *c, const uint32_t *args)
{
    UNUSED;
    uint32_t p = ARG(1);
    switch (ARG(0)) {
    case 0x0D33: wr32(p, (uint32_t)be()->get_caps(H2GL_CAP_MAX_TEXTURE_SIZE)); break;
    case 0x0D31: wr32(p, H2GL_MAX_LIGHTS); break;
    case 0x84E2: wr32(p, H2GL_MAX_TEX_UNITS); break;
    case 0x0D32: wr32(p, 0); break;                                         /* MAX_CLIP_PLANES */
    case 0x0BA2: for (int i = 0; i < 4; i++) wr32(p + 4u * (uint32_t)i, (uint32_t)g_s.viewport[i]); break;
    case 0x0D3A: { int w, h; h2_display_size(&w, &h); wr32(p, (uint32_t)w); wr32(p + 4, (uint32_t)h); break; }
    case 0x0D52: case 0x0D53: case 0x0D54: case 0x0D55: wr32(p, 8); break;
    case 0x0D56: wr32(p, (uint32_t)be()->get_caps(H2GL_CAP_DEPTH_BITS)); break;
    case 0x0D57: wr32(p, (uint32_t)be()->get_caps(H2GL_CAP_STENCIL_BITS)); break;
    default: set_error(GL_INVALID_ENUM, "glGetIntegerv pname");              /* output left unchanged, as specified */
    }
    return 0;
}

/* ---- WGL + GDI pixel formats ---- */
typedef struct { uint8_t color, red, green, blue, alpha, depth, stencil; uint8_t rs, gs, bs, as; } PFmt;
static const PFmt k_formats[] = {
    { 32, 8, 8, 8, 8, 24, 8, 0, 8, 16, 24 },
    { 32, 8, 8, 8, 8, 16, 0, 0, 8, 16, 24 },
    { 16, 5, 6, 5, 0, 16, 0, 11, 5, 0, 0 },
};
#define N_FORMATS ((int)(sizeof k_formats / sizeof k_formats[0]))

static uint32_t g_DescribePixelFormat(CPU *c, const uint32_t *args)
{
    UNUSED;
    /* (hdc, iPixelFormat, nBytes, ppfd) -> number of formats (the maximum index), 0 on failure */
    int i = (int32_t)ARG(1);
    if (ARG(3)) {
        if (i < 1 || i > N_FORMATS || ARG(2) < 40) { g_h2_last_error = 87; return 0; }
        const PFmt *f = &k_formats[i - 1];
        uint32_t p = ARG(3);
        memset(guest_ptr(p), 0, 40);
        wr16(p, 40); wr16(p + 2, 1);
        wr32(p + 4, 0x4u | 0x20u | 0x1u);                                  /* PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER */
        wr8(p + 8, 0); wr8(p + 9, f->color);
        wr8(p + 10, f->red); wr8(p + 11, f->rs); wr8(p + 12, f->green); wr8(p + 13, f->gs);
        wr8(p + 14, f->blue); wr8(p + 15, f->bs); wr8(p + 16, f->alpha); wr8(p + 17, f->as);
        wr8(p + 23, f->depth); wr8(p + 24, f->stencil);
    }
    return N_FORMATS;
}
static uint32_t g_SetPixelFormat(CPU *c, const uint32_t *args)
{
    UNUSED;
    int i = (int32_t)ARG(1);
    if (g_pixel_format_set || i < 1 || i > N_FORMATS) { g_h2_last_error = g_pixel_format_set ? 2000 : 87; return 0; }
    g_pixel_format_set = i;
    H2_INFO(H2_LOG_GRAPHICS, "pixel format %d: %d-bit colour, %d-bit depth, %d-bit stencil", i, k_formats[i - 1].color, k_formats[i - 1].depth, k_formats[i - 1].stencil);
    return 1;
}
static uint32_t g_GetPixelFormat(CPU *c, const uint32_t *args) { UNUSED; return (uint32_t)g_pixel_format_set; }
static uint32_t g_SwapBuffers(CPU *c, const uint32_t *args)
{
    UNUSED;
    if (!g_ctx_made_current) { g_h2_last_error = 6; return 0; }
    be()->present();
    g_stats.frames++;
    if (g_frame_hook) g_frame_hook(g_stats.frames);
    return 1;
}
static uint32_t g_SetDeviceGammaRamp(CPU *c, const uint32_t *args)
{
    UNUSED;
    /* Never applied to the real monitor: handed to the backend, which uses it as a post-process (or ignores it). */
    for (int ch = 0; ch < 3; ch++) for (int i = 0; i < 256; i++) g_gamma[ch][i] = rd16(ARG(1) + (uint32_t)(ch * 512 + i * 2));
    if (be()->set_gamma) be()->set_gamma(g_gamma);
    return 1;
}

static uint32_t w_wglCreateContext(CPU *c, const uint32_t *args)
{
    UNUSED;
    if (!g_pixel_format_set) { g_h2_last_error = 2000; return 0; }          /* ERROR_INVALID_PIXEL_FORMAT */
    if (g_stats.contexts_created) { H2_WARN(H2_LOG_GRAPHICS, "wglCreateContext: a second context is not supported"); g_h2_last_error = 8; return 0; }
    g_stats.contexts_created++;
    return 0x00040001u;
}
static uint32_t w_wglMakeCurrent(CPU *c, const uint32_t *args)
{
    UNUSED;
    if (!ARG(1)) { g_ctx_made_current = 0; return 1; }
    if (ARG(1) != 0x00040001u) { g_h2_last_error = 6; return 0; }
    if (!g_ctx_made_current) {
        int w, h; h2_display_size(&w, &h);
        state_defaults();
        g_s.viewport[2] = g_s.scissor[2] = w; g_s.viewport[3] = g_s.scissor[3] = h;
        if (be()->init(w, h) != 0) { H2_ERROR(H2_LOG_GRAPHICS, "backend '%s' failed to initialise", be()->name); g_h2_last_error = 8; return 0; }
        H2_INFO(H2_LOG_GRAPHICS, "GL context current, backend '%s', surface %dx%d", be()->name, w, h);
        g_ctx_made_current = 1;
    }
    return 1;
}
static uint32_t w_wglDeleteContext(CPU *c, const uint32_t *args)
{
    UNUSED;
    if (ARG(0) != 0x00040001u) { g_h2_last_error = 6; return 0; }
    if (g_ctx_made_current) { g_ctx_made_current = 0; }
    be()->shutdown();
    return 1;
}
static uint32_t w_wglGetProcAddress(CPU *c, const uint32_t *args)
{
    UNUSED;
    const char *name = (const char *)guest_ptr(ARG(0));
    /* NULL for anything this layer does not implement: the renderer treats that as "extension absent". */
    if (!h2_import_defined("OPENGL32.dll", name)) { H2_INFO(H2_LOG_GRAPHICS, "wglGetProcAddress(\"%s\") -> NULL", name); return 0; }
    H2_INFO(H2_LOG_GRAPHICS, "wglGetProcAddress(\"%s\")", name);
    return h2_import_bind("OPENGL32.dll", name);
}

static const H2ImportDef k_defs[] = {
#define GL(name, n) { "OPENGL32.dll", "gl" #name, n, gl_##name }
    GL(Enable, 1), GL(Disable, 1), GL(BlendFunc, 2), GL(AlphaFunc, 2), GL(DepthFunc, 1), GL(DepthMask, 1), GL(CullFace, 1),
    GL(ShadeModel, 1), GL(PolygonMode, 2), GL(ColorMaterial, 2), GL(Scissor, 4), GL(Viewport, 4), GL(PixelStorei, 2),
    GL(DrawBuffer, 1), GL(ReadBuffer, 1),
    GL(MatrixMode, 1), GL(LoadIdentity, 0), GL(LoadMatrixf, 1), GL(Frustum, 12), GL(Ortho, 12),
    GL(Lightf, 3), GL(Lightfv, 3), GL(LightModelfv, 2), GL(Materialfv, 3), GL(Fogi, 2), GL(Fogf, 2), GL(Fogfv, 2),
    GL(GenTextures, 2), GL(DeleteTextures, 2), GL(BindTexture, 2), GL(TexImage2D, 9), GL(TexSubImage2D, 9),
    GL(TexParameteri, 3), GL(TexEnvi, 3), GL(TexEnvf, 3), GL(TexGenf, 3),
    GL(EnableClientState, 1), GL(DisableClientState, 1), GL(VertexPointer, 4), GL(NormalPointer, 3), GL(ColorPointer, 4),
    GL(TexCoordPointer, 4), GL(DrawElements, 4),
    GL(Begin, 1), GL(End, 0), GL(Vertex2f, 2), GL(Vertex3f, 3), GL(Vertex3fv, 1), GL(TexCoord2f, 2), GL(Color4f, 4), GL(Color4ub, 4),
    GL(Clear, 1), GL(ClearColor, 4), GL(Flush, 0), GL(Finish, 0), GL(RasterPos2i, 2), GL(DrawPixels, 5), GL(ReadPixels, 7),
    GL(GetString, 1), GL(GetIntegerv, 2),
    { "OPENGL32.dll", "glActiveTextureARB", 1, gl_ActiveTextureARB },
    { "OPENGL32.dll", "glClientActiveTextureARB", 1, gl_ClientActiveTextureARB },
    { "OPENGL32.dll", "glMultiTexCoord2fvARB", 2, gl_MultiTexCoord2fvARB },
    { "OPENGL32.dll", "glMultiTexCoord4fvARB", 2, gl_MultiTexCoord4fvARB },
    { "OPENGL32.dll", "glBlendEquationEXT", 1, gl_BlendEquationEXT },
#undef GL
    { "OPENGL32.dll", "wglCreateContext", 1, w_wglCreateContext }, { "OPENGL32.dll", "wglMakeCurrent", 2, w_wglMakeCurrent },
    { "OPENGL32.dll", "wglDeleteContext", 1, w_wglDeleteContext }, { "OPENGL32.dll", "wglGetProcAddress", 1, w_wglGetProcAddress },
    { "GDI32.dll", "DescribePixelFormat", 4, g_DescribePixelFormat }, { "GDI32.dll", "SetPixelFormat", 3, g_SetPixelFormat },
    { "GDI32.dll", "GetPixelFormat", 1, g_GetPixelFormat }, { "GDI32.dll", "SwapBuffers", 1, g_SwapBuffers },
    { "GDI32.dll", "SetDeviceGammaRamp", 2, g_SetDeviceGammaRamp },
};

void h2_register_opengl(void)
{
    state_defaults();
    h2_imports_register(k_defs, sizeof k_defs / sizeof k_defs[0]);
}
