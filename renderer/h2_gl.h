/*
 * h2_gl.h - the OpenGL 1.x fixed-function layer the recompiled renderer
 * (RenderOpenGL.dll) talks to, and the backend interface under it.
 *
 *   guest code --(OPENGL32 / wgl / GDI pixel-format imports)--> h2_gl.c
 *        state machine: validates calls, tracks state, gathers vertex data,
 *        converts every texture to RGBA8 and every primitive to a point /
 *        line / triangle LIST
 *   h2_gl.c --(H2GLBackend)--> a backend that owns a surface and a GPU:
 *        gl_null_backend.c  headless; counts what it is given (PC test host)
 *        GLES 3.x backend   Android (not written yet)
 *
 * The layer implements exactly the functions RenderOpenGL.dll imports (see
 * docs/RENDERER_DECISION.md). It advertises only extensions it implements.
 * A GL call outside that set is not in this layer: it is bound as an unimplemented
 * import and stops the guest naming it, like every other missing import.
 *
 * Portable C11, no platform headers; guest memory only through guest_mem.h.
 */
#ifndef H2_GL_H
#define H2_GL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define H2GL_MAX_TEX_UNITS 2
#define H2GL_MAX_LIGHTS 8
#define H2GL_MAX_TEXTURES 4096

typedef struct { float m[16]; } H2Mat4;                 /* column-major, as OpenGL */

typedef struct {
    float ambient[4], diffuse[4], specular[4];
    float position[4];                                  /* eye coordinates */
    float spot_dir[3], spot_exponent, spot_cutoff;
    float att_const, att_linear, att_quad;
} H2GLLight;

typedef struct { float ambient[4], diffuse[4], specular[4], emission[4], shininess; } H2GLMaterial;

typedef struct {
    uint32_t bound;                                     /* texture object name, 0 = none */
    int      enabled;
    uint32_t env_mode;                                  /* GL_MODULATE ... GL_ADD */
    int      gen_enabled[4];                            /* S T R Q */
    uint32_t gen_mode[4];
    H2Mat4   matrix;
} H2GLTexUnit;

/* Everything a draw needs besides the vertices. Handed to the backend by const pointer; valid for the call. */
typedef struct {
    H2Mat4 modelview, projection;
    int viewport[4], scissor[4];
    int depth_test, depth_mask, cull_face, scissor_test, stencil_test, blend, alpha_test, lighting, fog, normalize,
        color_material, polygon_offset_fill;
    uint32_t depth_func, cull_mode, blend_src, blend_dst, blend_equation, alpha_func, shade_model, polygon_mode;
    float alpha_ref;
    uint32_t color_material_face, color_material_mode;
    int light_enabled[H2GL_MAX_LIGHTS];
    H2GLLight light[H2GL_MAX_LIGHTS];
    float light_model_ambient[4];
    H2GLMaterial material_front, material_back;
    uint32_t fog_mode; float fog_density, fog_start, fog_end, fog_color[4];
    H2GLTexUnit tex[H2GL_MAX_TEX_UNITS];
} H2GLState;

typedef struct {
    float pos[4], normal[3], color[4], tc[H2GL_MAX_TEX_UNITS][4];
} H2GLVertex;

typedef enum { H2GL_POINTS = 0, H2GL_LINES = 1, H2GL_TRIANGLES = 4 } H2GLPrim;

typedef struct H2GLBackend {
    const char *name;
    int  (*init)(int width, int height);                /* the surface exists; 0 on success */
    void (*shutdown)(void);
    void (*present)(void);                              /* SwapBuffers */
    void (*flush)(void);
    void (*clear)(uint32_t gl_mask, const float color[4], double depth, int stencil, const H2GLState *s);
    /* rgba8: width*height*4 bytes, valid only during the call (copy it). */
    void (*tex_image)(uint32_t tex, int level, int w, int h, const uint8_t *rgba8);
    void (*tex_subimage)(uint32_t tex, int level, int x, int y, int w, int h, const uint8_t *rgba8);
    void (*tex_param)(uint32_t tex, uint32_t pname, uint32_t value);   /* GL_TEXTURE_MIN_FILTER etc. */
    void (*tex_delete)(uint32_t tex);
    void (*draw)(const H2GLState *s, H2GLPrim prim, const H2GLVertex *v, unsigned nverts, const uint32_t *idx, unsigned nidx);
    void (*draw_pixels)(const H2GLState *s, int x, int y, int w, int h, const uint8_t *rgba8);
    int  (*read_pixels)(int x, int y, int w, int h, uint8_t *rgba8);   /* 0 if the backend has no framebuffer to read */
    void (*set_gamma)(const uint16_t ramp[3][256]);
    int  (*get_caps)(int what);                          /* H2GL_CAP_* */
} H2GLBackend;

enum { H2GL_CAP_MAX_TEXTURE_SIZE = 1, H2GL_CAP_ANISOTROPY = 2, H2GL_CAP_DEPTH_BITS = 3, H2GL_CAP_STENCIL_BITS = 4 };

/* ---- host API ---- */
void h2_gl_set_backend(const H2GLBackend *backend);     /* before the guest creates its context */
/* Called after every SwapBuffers with the number of frames presented so far. May stop the guest. */
typedef void (*H2GLFrameHook)(uint64_t frames_presented);
void h2_gl_set_frame_hook(H2GLFrameHook hook);
void h2_register_opengl(void);                          /* registers OPENGL32 + wgl + GDI pixel-format imports */
const H2GLBackend *h2_gl_null_backend(void);

typedef struct {
    uint64_t frames, draw_calls, vertices_submitted, triangles, tex_uploads, tex_bytes, clears, errors;
    unsigned textures_live, contexts_created;
    uint32_t last_error;
} H2GLStats;
void h2_gl_get_stats(H2GLStats *out);
const H2GLState *h2_gl_state(void);

#ifdef __cplusplus
}
#endif
#endif
