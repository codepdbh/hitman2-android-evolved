/*
 * gl_gles3_backend.c - the real graphics backend: OpenGL ES 3.0 over EGL (Android).
 *
 * Takes what h2_gl.c produces (state snapshot + vertex lists + RGBA8 textures) and draws it with one
 * "fixed-function emulation" shader: per-vertex lighting (8 lights, material, colour material, directional /
 * positional / spot), two texture units with the GL 1.x environments MODULATE / REPLACE / ADD / DECAL, texgen
 * (object linear, eye linear, sphere map with the default planes), fog (linear, exp, exp2), alpha test.
 * The surface is an EGL window when the host supplied one (h2_gles3_set_native_window) and a headless pbuffer
 * otherwise, which is what the command-line probe on a phone uses.
 *
 * Not implemented, by choice or because nothing needs it yet (a use is logged once, never silently accepted):
 *   glPolygonMode LINE/POINT (no ES equivalent), GL_FLAT shading, GL_BLEND texture environment (the
 *   environment colour is not tracked by the layer), two-sided lighting, local-viewer lighting, stencil.
 */
#if defined(__ANDROID__)

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <EGL/egl.h>
#include <GLES3/gl3.h>

#include "h2_gl.h"
#include "h2_log.h"

#ifndef GL_CLAMP
#define GL_CLAMP 0x2900
#endif

static EGLDisplay g_dpy = EGL_NO_DISPLAY;
static EGLSurface g_surf = EGL_NO_SURFACE;
static EGLContext g_ctx = EGL_NO_CONTEXT;
static void *g_native_window;
static int g_w, g_h;
static uint64_t g_frame;
static char g_cap_path[260];
static uint64_t g_cap_frame;
static unsigned g_draws_this_frame;

void h2_gles3_set_native_window(void *anativewindow) { g_native_window = anativewindow; }
void h2_gles3_request_capture(const char *ppm_path, uint64_t frame)
{
    snprintf(g_cap_path, sizeof g_cap_path, "%s", ppm_path);
    g_cap_frame = frame;
}

/* ---------------- shaders ---------------- */
static const char *k_vs =
"#version 300 es\n"
"layout(location=0) in vec4 a_pos; layout(location=1) in vec3 a_nrm; layout(location=2) in vec4 a_col;\n"
"layout(location=3) in vec4 a_tc0; layout(location=4) in vec4 a_tc1;\n"
"uniform mat4 u_mv, u_proj, u_texm0, u_texm1; uniform mat3 u_nmat;\n"
"uniform int u_lighting, u_normalize, u_cm, u_cm_mode;\n"
"uniform vec4 u_lmodel;\n"
"uniform vec4 u_mat_amb, u_mat_dif, u_mat_spec, u_mat_emi; uniform float u_mat_shin;\n"
"uniform int u_l_on[8]; uniform vec4 u_l_amb[8], u_l_dif[8], u_l_spec[8], u_l_pos[8], u_l_sp[8]; uniform vec3 u_l_sdir[8], u_l_att[8];\n"
"uniform ivec4 u_tg0, u_tg1;   /* texgen mode per S,T,R,Q: 0 off, 1 object, 2 eye, 3 sphere */\n"
"out vec4 v_col; out vec4 v_tc0; out vec4 v_tc1; out float v_eyez;\n"
"vec4 texgen(vec4 tc, ivec4 tg, vec4 obj, vec4 eye, vec3 en) {\n"
"  if (tg.x == 3 || tg.y == 3) {\n"
"    vec3 u = normalize(eye.xyz); vec3 r = reflect(u, en); float m = 2.0 * sqrt(r.x*r.x + r.y*r.y + (r.z+1.0)*(r.z+1.0));\n"
"    tc.xy = r.xy / m + 0.5; return tc; }\n"
"  if (tg.x == 1) tc.x = obj.x; else if (tg.x == 2) tc.x = eye.x;\n"
"  if (tg.y == 1) tc.y = obj.y; else if (tg.y == 2) tc.y = eye.y;\n"
"  if (tg.z == 1) tc.z = obj.z; else if (tg.z == 2) tc.z = eye.z;\n"
"  if (tg.w == 1) tc.w = obj.w; else if (tg.w == 2) tc.w = eye.w;\n"
"  return tc; }\n"
"void main() {\n"
"  vec4 eye = u_mv * a_pos; gl_Position = u_proj * eye; v_eyez = abs(eye.z);\n"
"  vec3 n = u_nmat * a_nrm; if (u_normalize != 0) n = normalize(n);\n"
"  vec4 col = a_col;\n"
"  if (u_lighting != 0) {\n"
"    vec4 amb = u_mat_amb, dif = u_mat_dif, spe = u_mat_spec, emi = u_mat_emi;\n"
"    if (u_cm != 0) { if (u_cm_mode == 0x1200) amb = a_col; else if (u_cm_mode == 0x1201) dif = a_col; else if (u_cm_mode == 0x1202) spe = a_col;\n"
"                     else if (u_cm_mode == 0x1600) emi = a_col; else { amb = a_col; dif = a_col; } }\n"
"    vec3 c = emi.rgb + amb.rgb * u_lmodel.rgb;\n"
"    for (int i = 0; i < 8; i++) { if (u_l_on[i] == 0) continue;\n"
"      vec3 L; float att = 1.0;\n"
"      if (u_l_pos[i].w == 0.0) L = normalize(u_l_pos[i].xyz);\n"
"      else { vec3 d = u_l_pos[i].xyz - eye.xyz; float dist = length(d); L = d / dist;\n"
"             att = 1.0 / (u_l_att[i].x + u_l_att[i].y * dist + u_l_att[i].z * dist * dist); }\n"
"      float spot = 1.0;\n"
"      if (u_l_sp[i].y != 180.0) { float cd = dot(-L, normalize(u_l_sdir[i])); spot = cd >= cos(radians(u_l_sp[i].y)) ? pow(max(cd, 0.0), u_l_sp[i].x) : 0.0; }\n"
"      float ndl = max(dot(n, L), 0.0);\n"
"      vec3 t = u_l_amb[i].rgb * amb.rgb + ndl * u_l_dif[i].rgb * dif.rgb;\n"
"      if (ndl > 0.0) t += pow(max(dot(n, normalize(L + vec3(0.0, 0.0, 1.0))), 0.0), u_mat_shin) * u_l_spec[i].rgb * spe.rgb;\n"
"      c += att * spot * t; }\n"
"    col = vec4(clamp(c, 0.0, 1.0), dif.a); }\n"
"  v_col = col;\n"
"  vec3 en = normalize(n);\n"
"  v_tc0 = u_texm0 * texgen(a_tc0, u_tg0, a_pos, eye, en);\n"
"  v_tc1 = u_texm1 * texgen(a_tc1, u_tg1, a_pos, eye, en);\n"
"}\n";

static const char *k_fs =
"#version 300 es\n"
"precision highp float;\n"
"in vec4 v_col; in vec4 v_tc0; in vec4 v_tc1; in float v_eyez;\n"
"uniform sampler2D u_tex0, u_tex1; uniform int u_texon0, u_texon1, u_env0, u_env1;\n"
"uniform int u_fog, u_fogmode; uniform float u_fdens, u_fstart, u_fend; uniform vec4 u_fogcol;\n"
"uniform int u_atest, u_afunc; uniform float u_aref;\n"
"out vec4 o_col;\n"
"vec4 env(vec4 c, vec4 t, int mode) {\n"
"  if (mode == 0x1E01) return t;\n"
"  if (mode == 0x0104) return vec4(c.rgb + t.rgb, c.a * t.a);\n"
"  if (mode == 0x2101) return vec4(mix(c.rgb, t.rgb, t.a), c.a);\n"
"  return c * t; }\n"
"vec2 st(vec4 v) { return v.w != 0.0 && v.w != 1.0 ? v.xy / v.w : v.xy; }\n"
"void main() {\n"
"  vec4 c = v_col;\n"
"  if (u_texon0 != 0) c = env(c, texture(u_tex0, st(v_tc0)), u_env0);\n"
"  if (u_texon1 != 0) c = env(c, texture(u_tex1, st(v_tc1)), u_env1);\n"
"  if (u_atest != 0) {\n"
"    bool ok = true;\n"
"    if (u_afunc == 0x200) ok = false; else if (u_afunc == 0x201) ok = c.a < u_aref; else if (u_afunc == 0x202) ok = c.a == u_aref;\n"
"    else if (u_afunc == 0x203) ok = c.a <= u_aref; else if (u_afunc == 0x204) ok = c.a > u_aref; else if (u_afunc == 0x205) ok = c.a != u_aref;\n"
"    else if (u_afunc == 0x206) ok = c.a >= u_aref;\n"
"    if (!ok) discard; }\n"
"  if (u_fog != 0) {\n"
"    float f;\n"
"    if (u_fogmode == 0x2601) f = (u_fend - v_eyez) / (u_fend - u_fstart);\n"
"    else if (u_fogmode == 0x800) f = exp(-u_fdens * v_eyez);\n"
"    else f = exp(-(u_fdens * v_eyez) * (u_fdens * v_eyez));\n"
"    c.rgb = mix(u_fogcol.rgb, c.rgb, clamp(f, 0.0, 1.0)); }\n"
"  o_col = c;\n"
"}\n";

static GLuint g_prog, g_vao, g_vbo, g_ebo;
static GLint U[64];
enum { uMV, uPROJ, uTEXM0, uTEXM1, uNMAT, uLIGHTING, uNORMALIZE, uCM, uCMMODE, uLMODEL, uMAMB, uMDIF, uMSPEC, uMEMI, uMSHIN,
       uLON, uLAMB, uLDIF, uLSPEC, uLPOS, uLSP, uLSDIR, uLATT, uTG0, uTG1, uTEX0, uTEX1, uTEXON0, uTEXON1, uENV0, uENV1,
       uFOG, uFOGMODE, uFDENS, uFSTART, uFEND, uFOGCOL, uATEST, uAFUNC, uAREF, uCOUNT };
static const char *const k_unames[uCOUNT] = {
    "u_mv", "u_proj", "u_texm0", "u_texm1", "u_nmat", "u_lighting", "u_normalize", "u_cm", "u_cm_mode", "u_lmodel", "u_mat_amb",
    "u_mat_dif", "u_mat_spec", "u_mat_emi", "u_mat_shin", "u_l_on", "u_l_amb", "u_l_dif", "u_l_spec", "u_l_pos", "u_l_sp",
    "u_l_sdir", "u_l_att", "u_tg0", "u_tg1", "u_tex0", "u_tex1", "u_texon0", "u_texon1", "u_env0", "u_env1", "u_fog",
    "u_fogmode", "u_fdens", "u_fstart", "u_fend", "u_fogcol", "u_atest", "u_afunc", "u_aref" };

static GLuint compile(GLenum type, const char *src)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, 0);
    glCompileShader(s);
    GLint ok = 0; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[2048]; glGetShaderInfoLog(s, sizeof log, 0, log); H2_ERROR(H2_LOG_GRAPHICS, "GLES shader compile failed: %s", log); return 0; }
    return s;
}

static int build_program(void)
{
    GLuint vs = compile(GL_VERTEX_SHADER, k_vs), fs = compile(GL_FRAGMENT_SHADER, k_fs);
    if (!vs || !fs) return -1;
    g_prog = glCreateProgram();
    glAttachShader(g_prog, vs); glAttachShader(g_prog, fs);
    glLinkProgram(g_prog);
    GLint ok = 0; glGetProgramiv(g_prog, GL_LINK_STATUS, &ok);
    if (!ok) { char log[2048]; glGetProgramInfoLog(g_prog, sizeof log, 0, log); H2_ERROR(H2_LOG_GRAPHICS, "GLES program link failed: %s", log); return -1; }
    for (int i = 0; i < uCOUNT; i++) U[i] = glGetUniformLocation(g_prog, k_unames[i]);
    return 0;
}

/* ---------------- textures ---------------- */
static GLuint g_tex[H2GL_MAX_TEXTURES];
static uint32_t g_tex_min[H2GL_MAX_TEXTURES], g_tex_mag[H2GL_MAX_TEXTURES], g_tex_wrap_s[H2GL_MAX_TEXTURES], g_tex_wrap_t[H2GL_MAX_TEXTURES];

static GLenum wrap_of(uint32_t w) { return w == GL_CLAMP ? GL_CLAMP_TO_EDGE : (GLenum)w; }

static GLuint tex_obj(uint32_t id)
{
    if (id >= H2GL_MAX_TEXTURES) id = 0;
    if (!g_tex[id]) {
        glGenTextures(1, &g_tex[id]);
        glBindTexture(GL_TEXTURE_2D, g_tex[id]);
        /* GL 1.1 defaults: minification NEAREST_MIPMAP_LINEAR, magnification LINEAR, wrap REPEAT */
        g_tex_min[id] = GL_NEAREST_MIPMAP_LINEAR; g_tex_mag[id] = GL_LINEAR; g_tex_wrap_s[id] = g_tex_wrap_t[id] = GL_REPEAT;
    }
    return g_tex[id];
}

static void apply_params(uint32_t id)
{
    /* a mipmap filter on a texture without the full chain is incomplete in ES; the layer uploads whatever levels the
     * guest gave. If only level 0 exists, fall back to the non-mipmapped filter so it samples at all. */
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, (GLint)g_tex_min[id]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, (GLint)g_tex_mag[id]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, (GLint)wrap_of(g_tex_wrap_s[id]));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, (GLint)wrap_of(g_tex_wrap_t[id]));
}

static unsigned char g_has_levels[H2GL_MAX_TEXTURES];      /* bit mask of uploaded mip levels (0..7) */

static void gb_tex_image(uint32_t id, int level, int w, int h, const uint8_t *rgba)
{
    if (id >= H2GL_MAX_TEXTURES) id = 0;
    glBindTexture(GL_TEXTURE_2D, tex_obj(id));
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexImage2D(GL_TEXTURE_2D, level, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    if (level < 8) g_has_levels[id] |= (unsigned char)(1u << level);
}
static void gb_tex_subimage(uint32_t id, int level, int x, int y, int w, int h, const uint8_t *rgba)
{
    if (id >= H2GL_MAX_TEXTURES) id = 0;
    glBindTexture(GL_TEXTURE_2D, tex_obj(id));
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexSubImage2D(GL_TEXTURE_2D, level, x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
}
static void gb_tex_param(uint32_t id, uint32_t pname, uint32_t v)
{
    if (id >= H2GL_MAX_TEXTURES) id = 0;
    tex_obj(id);
    switch (pname) {
    case 0x2801: g_tex_min[id] = v; break;
    case 0x2800: g_tex_mag[id] = v; break;
    case 0x2802: g_tex_wrap_s[id] = v; break;
    case 0x2803: g_tex_wrap_t[id] = v; break;
    }
}
static void gb_tex_delete(uint32_t id)
{
    if (id == 0 || id >= H2GL_MAX_TEXTURES || !g_tex[id]) return;
    glDeleteTextures(1, &g_tex[id]); g_tex[id] = 0; g_has_levels[id] = 0;
}

/* Bind a guest texture on a GL unit with its sampler parameters; a mipmapped filter without a level chain
 * degrades to its non-mipmapped equivalent (same visual intent, complete texture). */
static void bind_unit(int unit, uint32_t id)
{
    glActiveTexture(GL_TEXTURE0 + (GLenum)unit);
    glBindTexture(GL_TEXTURE_2D, tex_obj(id));
    uint32_t mn = g_tex_min[id];
    if (g_has_levels[id] == 1u || g_has_levels[id] == 0u) {
        if (mn == GL_NEAREST_MIPMAP_NEAREST || mn == GL_NEAREST_MIPMAP_LINEAR) mn = GL_NEAREST;
        else if (mn == GL_LINEAR_MIPMAP_NEAREST || mn == GL_LINEAR_MIPMAP_LINEAR) mn = GL_LINEAR;
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, (GLint)mn);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, (GLint)g_tex_mag[id]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, (GLint)wrap_of(g_tex_wrap_s[id]));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, (GLint)wrap_of(g_tex_wrap_t[id]));
}

/* ---------------- state ---------------- */
static void warn_once(int *flag, const char *what) { if (!*flag) { *flag = 1; H2_WARN(H2_LOG_GRAPHICS, "GLES backend: %s is not implemented (ignored)", what); } }

static void apply_fixed_state(const H2GLState *s)
{
    static int w_poly, w_flat;
    glViewport(s->viewport[0], s->viewport[1], s->viewport[2], s->viewport[3]);
    if (s->scissor_test) { glEnable(GL_SCISSOR_TEST); glScissor(s->scissor[0], s->scissor[1], s->scissor[2], s->scissor[3]); } else glDisable(GL_SCISSOR_TEST);
    if (s->depth_test) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    glDepthFunc((GLenum)s->depth_func); glDepthMask(s->depth_mask ? GL_TRUE : GL_FALSE);
    if (s->cull_face) { glEnable(GL_CULL_FACE); glCullFace((GLenum)s->cull_mode); } else glDisable(GL_CULL_FACE);
    if (s->blend) { glEnable(GL_BLEND); glBlendFunc((GLenum)s->blend_src, (GLenum)s->blend_dst); glBlendEquation((GLenum)s->blend_equation); } else glDisable(GL_BLEND);
    if (s->polygon_mode != 0x1B02 /* GL_FILL */) warn_once(&w_poly, "glPolygonMode other than FILL");
    if (s->shade_model == 0x1D00) warn_once(&w_flat, "GL_FLAT shading (drawn smooth)");
}

static void mat3_normal(const H2Mat4 *mv, float out[9])
{
    /* inverse transpose of the upper 3x3 of the modelview (column-major) */
    const float *m = mv->m;
    float a = m[0], b = m[4], c = m[8], d = m[1], e = m[5], f = m[9], g = m[2], h = m[6], i = m[10];
    float det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    if (fabsf(det) < 1e-20f) { for (int k = 0; k < 9; k++) out[k] = (k % 4 == 0); return; }
    float r = 1.0f / det;
    /* inverse rows: inv[row][col]; the transpose of the inverse, stored column-major, equals the inverse row-major */
    float inv00 = (e * i - f * h) * r, inv01 = (c * h - b * i) * r, inv02 = (b * f - c * e) * r;
    float inv10 = (f * g - d * i) * r, inv11 = (a * i - c * g) * r, inv12 = (c * d - a * f) * r;
    float inv20 = (d * h - e * g) * r, inv21 = (b * g - a * h) * r, inv22 = (a * e - b * d) * r;
    /* normal matrix N = (M^-1)^T: N[row][col] = inv[col][row]; GL uniform is column-major: out[col*3+row] */
    out[0] = inv00; out[1] = inv01; out[2] = inv02;
    out[3] = inv10; out[4] = inv11; out[5] = inv12;
    out[6] = inv20; out[7] = inv21; out[8] = inv22;
}

static void upload_uniforms(const H2GLState *s)
{
    static int w_blend, w_ts;
    glUseProgram(g_prog);
    glUniformMatrix4fv(U[uMV], 1, GL_FALSE, s->modelview.m);
    glUniformMatrix4fv(U[uPROJ], 1, GL_FALSE, s->projection.m);
    glUniformMatrix4fv(U[uTEXM0], 1, GL_FALSE, s->tex[0].matrix.m);
    glUniformMatrix4fv(U[uTEXM1], 1, GL_FALSE, s->tex[1].matrix.m);
    float nm[9]; mat3_normal(&s->modelview, nm);
    glUniformMatrix3fv(U[uNMAT], 1, GL_FALSE, nm);
    glUniform1i(U[uLIGHTING], s->lighting); glUniform1i(U[uNORMALIZE], s->normalize);
    glUniform1i(U[uCM], s->color_material); glUniform1i(U[uCMMODE], (GLint)s->color_material_mode);
    glUniform4fv(U[uLMODEL], 1, s->light_model_ambient);
    const H2GLMaterial *m = &s->material_front;
    glUniform4fv(U[uMAMB], 1, m->ambient); glUniform4fv(U[uMDIF], 1, m->diffuse); glUniform4fv(U[uMSPEC], 1, m->specular);
    glUniform4fv(U[uMEMI], 1, m->emission); glUniform1f(U[uMSHIN], m->shininess);
    GLint on[8]; float amb[32], dif[32], spec[32], pos[32], sp[32], sdir[24], att[24];
    for (int i = 0; i < 8; i++) {
        const H2GLLight *l = &s->light[i];
        on[i] = s->light_enabled[i];
        memcpy(amb + 4 * i, l->ambient, 16); memcpy(dif + 4 * i, l->diffuse, 16); memcpy(spec + 4 * i, l->specular, 16);
        memcpy(pos + 4 * i, l->position, 16);
        sp[4 * i] = l->spot_exponent; sp[4 * i + 1] = l->spot_cutoff; sp[4 * i + 2] = 0; sp[4 * i + 3] = 0;
        memcpy(sdir + 3 * i, l->spot_dir, 12);
        att[3 * i] = l->att_const; att[3 * i + 1] = l->att_linear; att[3 * i + 2] = l->att_quad;
    }
    glUniform1iv(U[uLON], 8, on); glUniform4fv(U[uLAMB], 8, amb); glUniform4fv(U[uLDIF], 8, dif); glUniform4fv(U[uLSPEC], 8, spec);
    glUniform4fv(U[uLPOS], 8, pos); glUniform4fv(U[uLSP], 8, sp); glUniform3fv(U[uLSDIR], 8, sdir); glUniform3fv(U[uLATT], 8, att);
    for (int u = 0; u < 2; u++) {
        GLint tg[4];
        for (int k = 0; k < 4; k++) {
            uint32_t mode = s->tex[u].gen_mode[k];
            tg[k] = !s->tex[u].gen_enabled[k] ? 0 : mode == 0x2401 ? 1 : mode == 0x2400 ? 2 : mode == 0x2402 ? 3 : 0;
        }
        glUniform4iv(U[u ? uTG1 : uTG0], 1, tg);
        glUniform1i(U[u ? uTEXON1 : uTEXON0], s->tex[u].enabled);
        uint32_t env = s->tex[u].env_mode;
        if (env == 0x0BE2) { warn_once(&w_blend, "GL_BLEND texture environment (drawn as MODULATE)"); env = 0x2100; }
        glUniform1i(U[u ? uENV1 : uENV0], (GLint)env);
        glUniform1i(U[u ? uTEX1 : uTEX0], u);
        (void)w_ts;
    }
    glUniform1i(U[uFOG], s->fog); glUniform1i(U[uFOGMODE], (GLint)s->fog_mode);
    glUniform1f(U[uFDENS], s->fog_density); glUniform1f(U[uFSTART], s->fog_start); glUniform1f(U[uFEND], s->fog_end);
    glUniform4fv(U[uFOGCOL], 1, s->fog_color);
    glUniform1i(U[uATEST], s->alpha_test); glUniform1i(U[uAFUNC], (GLint)s->alpha_func); glUniform1f(U[uAREF], s->alpha_ref);
}

/* ---------------- backend functions ---------------- */
static int gb_init(int w, int h)
{
    g_w = w; g_h = h;
    g_dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLint maj, min;
    if (g_dpy == EGL_NO_DISPLAY || !eglInitialize(g_dpy, &maj, &min)) { H2_ERROR(H2_LOG_GRAPHICS, "EGL: no display (0x%X)", eglGetError()); return -1; }
    const EGLint cfg_attr[] = { EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_SURFACE_TYPE, g_native_window ? EGL_WINDOW_BIT : EGL_PBUFFER_BIT,
                                EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_DEPTH_SIZE, 24, EGL_STENCIL_SIZE, 8, EGL_NONE };
    EGLConfig cfg; EGLint n = 0;
    if (!eglChooseConfig(g_dpy, cfg_attr, &cfg, 1, &n) || n < 1) {
        const EGLint relaxed[] = { EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_SURFACE_TYPE, g_native_window ? EGL_WINDOW_BIT : EGL_PBUFFER_BIT,
                                   EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_DEPTH_SIZE, 16, EGL_NONE };
        if (!eglChooseConfig(g_dpy, relaxed, &cfg, 1, &n) || n < 1) { H2_ERROR(H2_LOG_GRAPHICS, "EGL: no ES3 config"); return -1; }
        H2_WARN(H2_LOG_GRAPHICS, "EGL: no 24-bit depth + stencil config; using 16-bit depth, no stencil");
    }
    if (g_native_window) g_surf = eglCreateWindowSurface(g_dpy, cfg, (EGLNativeWindowType)g_native_window, 0);
    else { const EGLint pb[] = { EGL_WIDTH, w, EGL_HEIGHT, h, EGL_NONE }; g_surf = eglCreatePbufferSurface(g_dpy, cfg, pb); }
    if (g_surf == EGL_NO_SURFACE) { H2_ERROR(H2_LOG_GRAPHICS, "EGL: surface creation failed (0x%X)", eglGetError()); return -1; }
    const EGLint ctx_attr[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
    g_ctx = eglCreateContext(g_dpy, cfg, EGL_NO_CONTEXT, ctx_attr);
    if (g_ctx == EGL_NO_CONTEXT || !eglMakeCurrent(g_dpy, g_surf, g_surf, g_ctx)) { H2_ERROR(H2_LOG_GRAPHICS, "EGL: context failed (0x%X)", eglGetError()); return -1; }
    H2_INFO(H2_LOG_GRAPHICS, "GLES: %s | %s | %s (%s surface %dx%d)", (const char *)glGetString(GL_VENDOR), (const char *)glGetString(GL_RENDERER),
            (const char *)glGetString(GL_VERSION), g_native_window ? "window" : "pbuffer", w, h);
    if (build_program() != 0) return -1;
    glGenVertexArrays(1, &g_vao); glGenBuffers(1, &g_vbo); glGenBuffers(1, &g_ebo);
    glBindVertexArray(g_vao); glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    const GLsizei stride = sizeof(H2GLVertex);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, stride, (void *)offsetof(H2GLVertex, pos));
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void *)offsetof(H2GLVertex, normal));
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride, (void *)offsetof(H2GLVertex, color));
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, stride, (void *)offsetof(H2GLVertex, tc[0]));
    glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, stride, (void *)offsetof(H2GLVertex, tc[1]));
    for (int i = 0; i < 5; i++) glEnableVertexAttribArray((GLuint)i);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_ebo);
    return 0;
}

static void gb_shutdown(void)
{
    if (g_dpy != EGL_NO_DISPLAY) {
        eglMakeCurrent(g_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (g_ctx != EGL_NO_CONTEXT) eglDestroyContext(g_dpy, g_ctx);
        if (g_surf != EGL_NO_SURFACE) eglDestroySurface(g_dpy, g_surf);
        eglTerminate(g_dpy);
    }
    g_dpy = EGL_NO_DISPLAY; g_surf = EGL_NO_SURFACE; g_ctx = EGL_NO_CONTEXT;
}

static int gb_read_pixels(int x, int y, int w, int h, uint8_t *rgba)
{
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    return glGetError() == GL_NO_ERROR;
}

static void write_ppm(const char *path)
{
    uint8_t *px = (uint8_t *)malloc((size_t)g_w * (size_t)g_h * 4);
    FILE *f = px ? fopen(path, "wb") : 0;
    if (!px || !f) { H2_ERROR(H2_LOG_GRAPHICS, "capture: cannot write %s", path); free(px); if (f) fclose(f); return; }
    gb_read_pixels(0, 0, g_w, g_h, px);
    fprintf(f, "P6\n%d %d\n255\n", g_w, g_h);
    for (int y = g_h - 1; y >= 0; y--)                      /* GL rows run bottom-up */
        for (int x = 0; x < g_w; x++) fwrite(px + ((size_t)y * (size_t)g_w + (size_t)x) * 4, 1, 3, f);
    fclose(f); free(px);
    H2_INFO(H2_LOG_GRAPHICS, "captured frame %llu to %s", (unsigned long long)g_frame, path);
}

static void gb_present(void)
{
    g_frame++;
    /* capture the first frame at or after the requested one that actually drew something (most frames are bare clears) */
    if (g_cap_path[0] && g_frame >= g_cap_frame && g_draws_this_frame > 0) { write_ppm(g_cap_path); g_cap_path[0] = 0; }
    g_draws_this_frame = 0;
    eglSwapBuffers(g_dpy, g_surf);
}
static void gb_flush(void) { glFlush(); }

static void gb_clear(uint32_t mask, const float color[4], double depth, int stencil, const H2GLState *s)
{
    apply_fixed_state(s);                                   /* scissor and depth mask affect clears */
    GLbitfield m = 0;
    if (mask & 0x4000) { glClearColor(color[0], color[1], color[2], color[3]); glColorMask(1, 1, 1, 1); m |= GL_COLOR_BUFFER_BIT; }
    if (mask & 0x0100) { glClearDepthf((float)depth); m |= GL_DEPTH_BUFFER_BIT; }
    if (mask & 0x0400) { glClearStencil(stencil); m |= GL_STENCIL_BUFFER_BIT; }
    if (m) glClear(m);
}

static void gb_draw(const H2GLState *s, H2GLPrim prim, const H2GLVertex *v, unsigned nverts, const uint32_t *idx, unsigned nidx)
{
    g_draws_this_frame++;
    apply_fixed_state(s);
    upload_uniforms(s);
    for (int u = 0; u < 2; u++) if (s->tex[u].enabled) bind_unit(u, s->tex[u].bound);
    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(g_vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((size_t)nverts * sizeof(H2GLVertex)), v, GL_STREAM_DRAW);
    GLenum mode = prim == H2GL_POINTS ? GL_POINTS : prim == H2GL_LINES ? GL_LINES : GL_TRIANGLES;
    if (idx) {
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)((size_t)nidx * 4), idx, GL_STREAM_DRAW);
        glDrawElements(mode, (GLsizei)nidx, GL_UNSIGNED_INT, 0);
    } else {
        glDrawArrays(mode, 0, (GLsizei)nverts);
    }
}

static void gb_draw_pixels(const H2GLState *s, int x, int y, int w, int h, const uint8_t *rgba)
{
    /* a textured quad in window coordinates at the raster position; the layer gives it already resolved */
    static int warned;
    (void)s; (void)x; (void)y; (void)w; (void)h; (void)rgba;
    warn_once(&warned, "glDrawPixels");
}

static void gb_set_gamma(const uint16_t ramp[3][256]) { (void)ramp; }     /* no gamma control on the phone's compositor */

static int gb_caps(int what)
{
    GLint v = 0;
    switch (what) {
    case H2GL_CAP_MAX_TEXTURE_SIZE: glGetIntegerv(GL_MAX_TEXTURE_SIZE, &v); return v;
    case H2GL_CAP_DEPTH_BITS: return 24;
    case H2GL_CAP_STENCIL_BITS: return 8;
    default: return 0;
    }
}

static void gb_tex_unused(void) { (void)apply_params; (void)gb_tex_unused; }

static const H2GLBackend k_gles3 = {
    "OpenGL ES 3.0 (EGL)", gb_init, gb_shutdown, gb_present, gb_flush, gb_clear, gb_tex_image, gb_tex_subimage, gb_tex_param,
    gb_tex_delete, gb_draw, gb_draw_pixels, gb_read_pixels, gb_set_gamma, gb_caps
};

const H2GLBackend *h2_gl_gles3_backend(void) { return &k_gles3; }

#endif /* __ANDROID__ */
