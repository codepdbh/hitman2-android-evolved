#include <stdlib.h>
#include <stdio.h>
#include "egl_config_policy.h"

/* A failing headless check prints and exits; it must never open a Windows CRT assertion dialog. */
#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); exit(1); } } while (0)

typedef struct { int calls, accept_at, fail_at; int depth[3], stencil[3]; } Probe;
static int attempt(void *user, int depth, int stencil)
{
    Probe *p = user;
    CHECK(p->calls < 3);
    int i = p->calls++;
    p->depth[i] = depth; p->stencil[i] = stencil;
    if (p->fail_at == p->calls) return -1;
    return p->accept_at == p->calls;
}
int main(void)
{
    Probe normal = { .accept_at = 1 };
    CHECK(h2_egl_select_config(0, attempt, &normal) == 0);
    CHECK(normal.calls == 1 && normal.depth[0] == 24 && normal.stencil[0] == 8);
    Probe no_stencil = { .accept_at = 2 };
    CHECK(h2_egl_select_config(0, attempt, &no_stencil) == 1);
    CHECK(no_stencil.calls == 2 && no_stencil.depth[1] == 24 && no_stencil.stencil[1] == 0);
    Probe old_driver = { .accept_at = 3 };
    CHECK(h2_egl_select_config(0, attempt, &old_driver) == 2);
    CHECK(old_driver.depth[2] == 16 && old_driver.stencil[2] == 0);
    Probe mali = { .accept_at = 1 };
    CHECK(h2_egl_select_config(1, attempt, &mali) == 1);
    CHECK(mali.depth[0] == 24 && mali.stencil[0] == 0);
    Probe unisoc = { .accept_at = 1 };
    CHECK(h2_egl_select_config(2, attempt, &unisoc) == 2);
    CHECK(unisoc.depth[0] == 16 && unisoc.stencil[0] == 0);
    Probe adreno = { .accept_at = 1 };
    CHECK(h2_egl_select_config(3, attempt, &adreno) == 0);
    Probe absent = {0};
    CHECK(h2_egl_select_config(0, attempt, &absent) == -1 && absent.calls == 3);
    Probe api_error = { .fail_at = 1, .accept_at = 2 };
    CHECK(h2_egl_select_config(0, attempt, &api_error) == -1 && api_error.calls == 1);
    Probe invalid = {0};
    CHECK(h2_egl_select_config(4, attempt, &invalid) == -1 && invalid.calls == 0);
    puts("PASS: auto, no-stencil, 16-bit, Mali, Unisoc, Adreno, unavailable, API error, invalid profile");
    return 0;
}
