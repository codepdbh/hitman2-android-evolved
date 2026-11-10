/*
 * h2_log.h - structured logging for every native layer.
 *
 * One line per event: "<CATEGORY> <LEVEL> message". On Android it goes to
 * logcat under the tag "H2AE"; elsewhere to stderr. A sink can be installed
 * to also capture lines (the Android diagnostics screen and the tests do).
 */
#ifndef H2_LOG_H
#define H2_LOG_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    H2_LOG_RECOMP, H2_LOG_CPU, H2_LOG_MEMORY, H2_LOG_WIN32, H2_LOG_GRAPHICS, H2_LOG_AUDIO,
    H2_LOG_INPUT, H2_LOG_FILESYSTEM, H2_LOG_ANDROID, H2_LOG_GAME, H2_LOG_ERROR, H2_LOG__COUNT
} h2_log_cat;

typedef enum { H2_LVL_DEBUG, H2_LVL_INFO, H2_LVL_WARN, H2_LVL_ERROR, H2_LVL_FATAL } h2_log_lvl;

typedef void (*h2_log_sink)(h2_log_cat cat, h2_log_lvl lvl, const char *line, void *user);

#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 3, 4)))
#endif
void h2_log(h2_log_cat cat, h2_log_lvl lvl, const char *fmt, ...);
void h2_log_set_sink(h2_log_sink sink, void *user);
const char *h2_log_cat_name(h2_log_cat cat);

/* Why the guest stopped. Only H2_STOP_EXIT is a normal ending. */
typedef enum {
    H2_STOP_EXIT,                 /* the program called ExitProcess */
    H2_STOP_UNTRANSLATED,         /* reached an instruction the lifter could not express */
    H2_STOP_UNRESOLVED,           /* control transfer to a guest address with no lifted body */
    H2_STOP_UNIMPLEMENTED_IMPORT, /* called a Win32/DLL function the platform layer does not implement */
    H2_STOP_BUDGET,               /* a test's dispatch budget ran out */
    H2_STOP_FAULT,                /* host fault raised by guest code (bad guest pointer) */
    H2_STOP_REQUESTED,            /* the host ended the run on purpose (e.g. N frames presented) */
    H2_STOP_INTERNAL
} h2_stop_reason;

typedef void (*h2_stop_handler)(h2_stop_reason why, const char *message);
void h2_set_stop_handler(h2_stop_handler fn);
#if defined(__GNUC__) || defined(__clang__)
__attribute__((noreturn, format(printf, 2, 3)))
#endif
void h2_guest_stop(h2_stop_reason why, const char *fmt, ...);

#define H2_INFO(cat, ...)  h2_log((cat), H2_LVL_INFO,  __VA_ARGS__)
#define H2_WARN(cat, ...)  h2_log((cat), H2_LVL_WARN,  __VA_ARGS__)
#define H2_ERROR(cat, ...) h2_log((cat), H2_LVL_ERROR, __VA_ARGS__)

#ifdef __cplusplus
}
#endif
#endif /* H2_LOG_H */
