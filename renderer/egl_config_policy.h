#ifndef H2_EGL_CONFIG_POLICY_H
#define H2_EGL_CONFIG_POLICY_H

/* 0 automatic, 1 Mali compatibility, 2 Unisoc compatibility, 3 Adreno.
 * The callback returns 1 for a usable config, 0 for no match, -1 for an API error.
 * All paths use the Android system GLES driver; these are surface preferences. */
typedef int (*H2EGLTryConfig)(void *user, int depth, int stencil);
static const int h2_egl_formats[3][2] = { {24, 8}, {24, 0}, {16, 0} };
static inline int h2_egl_select_config(int profile, H2EGLTryConfig attempt, void *user)
{
    if (profile < 0 || profile > 3 || !attempt) return -1;
    int first = profile == 1 ? 1 : profile == 2 ? 2 : 0;
    for (int i = first; i < 3; i++) {
        int result = attempt(user, h2_egl_formats[i][0], h2_egl_formats[i][1]);
        if (result < 0) return -1;
        if (result) return i;
    }
    return -1;
}
#endif
