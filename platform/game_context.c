/*
 * game_context.c - which kind of screen the game is showing, inferred from the scene archive it opens.
 *
 * The recompiled game loads each screen from SCENES/<dir>/<name>.ZIP: "AllLevels" holds the logos, the
 * menus and the error screens; every other directory is a mission (or the intro movie). That is the only signal
 * used - it is the file the game itself opened, not a guess about its memory. The touch HUD shows the controls
 * that fit the current context.
 *
 * Limits: it says which scene was loaded last, not what the player is doing inside it (no distinction between
 * walking, a cutscene inside a mission, a loading screen). InventoryMenu and AlwaysGfx are overlays loaded
 * during missions and do not change the context.
 */
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "h2_platform.h"

static volatile int g_ctx = H2_CTX_BOOT;

static int ci_contains(const char *s, const char *needle)
{
    size_t n = strlen(needle);
    for (; *s; s++) { size_t i = 0; while (i < n && s[i] && tolower((unsigned char)s[i]) == tolower((unsigned char)needle[i])) i++; if (i == n) return 1; }
    return 0;
}

void h2_game_context_note_open(const char *rel_path)
{
    /* expected: SCENES/<dir>/<name>.ZIP */
    if (!ci_contains(rel_path, ".zip") || !ci_contains(rel_path, "scenes/")) return;
    const char *dir = rel_path;
    char d[128] = "", n[128] = "";
    const char *p = strchr(dir, '/'); if (!p) return;
    const char *q = strchr(p + 1, '/'); if (!q) return;
    snprintf(d, sizeof d, "%.*s", (int)(q - p - 1), p + 1);
    const char *dot = strrchr(q + 1, '.');
    snprintf(n, sizeof n, "%.*s", (int)(dot ? (size_t)(dot - q - 1) : strlen(q + 1)), q + 1);
    int ctx = g_ctx;
    if (ci_contains(d, "alllevels")) {
        if (ci_contains(n, "inventorymenu") || ci_contains(n, "alwaysgfx")) return;
        if (ci_contains(n, "logos") || ci_contains(n, "scratcheddisc") || ci_contains(n, "mcbootcheck") || ci_contains(n, "errormessages")) ctx = H2_CTX_INTRO;
        else if (ci_contains(n, "menu")) ctx = H2_CTX_MENU;
        else return;
    } else if (ci_contains(d, "intro")) ctx = H2_CTX_INTRO;
    else ctx = H2_CTX_MISSION;
    if (ctx != g_ctx) { g_ctx = ctx; H2_INFO(H2_LOG_GAME, "context -> %s (%s)", ctx == H2_CTX_INTRO ? "intro" : ctx == H2_CTX_MENU ? "menu" : "mission", rel_path); }
}

int h2_game_context(void) { return g_ctx; }
