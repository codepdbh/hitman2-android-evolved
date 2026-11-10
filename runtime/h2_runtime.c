/* h2_runtime.c - logging and the fatal paths lifted code can reach. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include "h2_log.h"

#if defined(__ANDROID__)
#include <android/log.h>
#endif

/* Guest images are mapped at their preferred base inside the guest address
 * space (see guest_mem.h), so the relocation delta the lifter's GVA() applies
 * is zero. */
uint32_t g_image_delta = 0;

static h2_log_sink g_sink;
static void *g_sink_user;

static const char *const k_cat[H2_LOG__COUNT] = {
    "RECOMP", "CPU", "MEMORY", "WIN32", "GRAPHICS", "AUDIO", "INPUT", "FILESYSTEM", "ANDROID", "GAME", "ERROR"
};
static const char *const k_lvl[] = { "D", "I", "W", "E", "F" };

const char *h2_log_cat_name(h2_log_cat cat)
{
    return (unsigned)cat < H2_LOG__COUNT ? k_cat[cat] : "?";
}

void h2_log_set_sink(h2_log_sink sink, void *user) { g_sink = sink; g_sink_user = user; }

void h2_log(h2_log_cat cat, h2_log_lvl lvl, const char *fmt, ...)
{
    char msg[1024], line[1100];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    snprintf(line, sizeof line, "%-10s %s %s", h2_log_cat_name(cat), k_lvl[lvl <= H2_LVL_FATAL ? lvl : 0], msg);
#if defined(__ANDROID__)
    {
        static const int prio[] = { ANDROID_LOG_DEBUG, ANDROID_LOG_INFO, ANDROID_LOG_WARN,
                                    ANDROID_LOG_ERROR, ANDROID_LOG_FATAL };
        __android_log_write(prio[lvl <= H2_LVL_FATAL ? lvl : 0], "H2AE", line);
    }
#else
    fprintf(stderr, "%s\n", line);
#endif
    if (g_sink) g_sink(cat, lvl, line, g_sink_user);
}

/* ---- stopping the guest ----
 * Anything the port cannot do correctly ends the guest here, with the reason.
 * A host (the boot probe, the Android activity) may install a handler that
 * unwinds to its own frame; the handler must not return. With none installed,
 * or if it returns, the process aborts: continuing would run on wrong state. */
static h2_stop_handler g_stop;

void h2_set_stop_handler(h2_stop_handler fn) { g_stop = fn; }

void h2_guest_stop(h2_stop_reason why, const char *fmt, ...)
{
    char msg[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    int normal = why == H2_STOP_EXIT || why == H2_STOP_REQUESTED;
    h2_log(normal ? H2_LOG_GAME : H2_LOG_ERROR, normal ? H2_LVL_INFO : H2_LVL_FATAL, "guest stopped: %s", msg);
    if (g_stop) g_stop(why, msg);
    abort();
}

/* An instruction the lifter could not translate was reached. This is never
 * survivable: the guest state after it would be wrong. Say exactly where. */
void h2_todo(unsigned guest_va, const char *text)
{
    h2_guest_stop(H2_STOP_UNTRANSLATED, "untranslated instruction at guest %08X: %s", guest_va, text);
}
