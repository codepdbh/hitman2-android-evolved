/*
 * h2ae_game.c - starts the recompiled game inside the app (libh2game.so). Android glue only: it owns the
 * Surface, a big-stack host thread and the fault guard, and calls the same layers the PC probe uses
 * (runtime/, platform/, renderer/, input/). No game logic here.
 *
 * Kotlin -> startGame(surface, gameDir, userDir): the guest runs on its own native thread until it exits,
 * stops (reason logged under H2AE) or stopGame() is called. The EGL surface is the app's SurfaceView.
 */
#define _GNU_SOURCE
#include <android/log.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <jni.h>
#include <pthread.h>
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "h2_audio.h"
#include "h2_dispatch.h"
#include "h2_gl.h"
#include "h2_log.h"
#include "h2_platform.h"

static char g_game_dir[400], g_user_dir[400];
static ANativeWindow *g_window;
static volatile int g_stop_requested, g_running;
static pthread_t g_thread;

static _Thread_local sigjmp_buf t_fault_jmp;
static jmp_buf g_stop_jmp;
static h2_stop_reason g_reason;
static char g_message[512];

static void on_stop(h2_stop_reason why, const char *message)
{
    h2_threads_forward_stop(why, message);                  /* from a secondary guest thread: handed to the game thread */
    g_reason = why;
    snprintf(g_message, sizeof g_message, "%s", message);
    longjmp(g_stop_jmp, 1);
}

static void on_signal(int sig, siginfo_t *si, void *ctx)
{
    (void)ctx;
    uintptr_t base = (uintptr_t)g_guest_base, a = (uintptr_t)si->si_addr;
    g_reason = H2_STOP_FAULT;
    if (sig != SIGFPE && a >= base && a - base < GUEST_SPACE_SIZE + GUEST_GUARD_SIZE)
        snprintf(g_message, sizeof g_message, "invalid memory access at guest address %08X", (unsigned)(a - base));
    else
        snprintf(g_message, sizeof g_message, "%s (host fault)", sig == SIGFPE ? "integer divide by zero" : "invalid memory access");
    siglongjmp(t_fault_jmp, 1);
}

static void install_fault_guard(void)
{
    static _Thread_local uint8_t alt[65536];
    stack_t ss = { .ss_sp = alt, .ss_size = sizeof alt, .ss_flags = 0 };
    sigaltstack(&ss, 0);
}

static int run_guarded(CPU *c, uint32_t entry)
{
    install_fault_guard();
    if (sigsetjmp(t_fault_jmp, 1)) return 1;
    dispatch(c, entry);
    return 0;
}

static void thread_runner(CPU *c, uint32_t entry)
{
    if (run_guarded(c, entry)) h2_threads_forward_stop(H2_STOP_FAULT, g_message);
}

static void on_frame(uint64_t frames)
{
    if (g_stop_requested) h2_guest_stop(H2_STOP_REQUESTED, "the app asked the game to stop after %llu frames", (unsigned long long)frames);
}

static void *game_main(void *arg)
{
    (void)arg;
    static H2Image img;
    static CPU cpu;
    char exe[500];
    struct sigaction sa;
    memset(&sa, 0, sizeof sa); sa.sa_sigaction = on_signal; sa.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_NODEFER;
    sigaction(SIGSEGV, &sa, 0); sigaction(SIGBUS, &sa, 0); sigaction(SIGFPE, &sa, 0);

    if (guest_mem_init() != 0) { H2_ERROR(H2_LOG_ANDROID, "cannot reserve the guest address space"); g_running = 0; return 0; }
    h2_register_kernel32(); h2_register_objects(); h2_register_threads(); h2_threads_set_runner(thread_runner);
    h2_register_files(); h2_register_user32(); h2_register_game_dlls(); h2_register_dinput8(); h2_register_advapi32(); h2_register_dsound();
    h2_audio_set_backend(h2_audio_aaudio_backend());
    h2_gl_set_backend(h2_gl_gles3_backend());
    h2_gles3_set_native_window(g_window);
    h2_gl_set_frame_hook(on_frame);
    h2_register_opengl();
    h2_files_configure(g_game_dir, g_user_dir);
    snprintf(exe, sizeof exe, "%s/hitman2.exe", g_game_dir);
    if (h2_pe_load(exe, &img) != 0 || h2_process_init(&img) != 0) { H2_ERROR(H2_LOG_ANDROID, "cannot load %s", exe); g_running = 0; return 0; }
    h2_kernel32_attach(&img);
    h2_dispatch_set_import_hook(h2_import_dispatch);
    h2_dispatch_set_name_resolver(h2_import_name_at);
    h2_dispatch_set_budget(0);                              /* no budget in the app: it runs until it stops */
    h2_set_stop_handler(on_stop);
    h2_cpu_init_main_thread(&cpu);
    push32(&cpu, 0xDEAD0000u);

    int returned = 0;
    if (setjmp(g_stop_jmp) == 0) { if (run_guarded(&cpu, img.entry) == 0) returned = 1; }
    if (returned) { g_reason = H2_STOP_EXIT; snprintf(g_message, sizeof g_message, "entry point returned, eax=%08X", cpu.eax); }
    __android_log_print(ANDROID_LOG_INFO, "H2AE", "GAME STOPPED reason=%d: %s (dispatches=%llu)", (int)g_reason, g_message,
                        (unsigned long long)h2_dispatch_count());
    h2_audio_stop();
    g_running = 0;
    return 0;
}

JNIEXPORT jboolean JNICALL
Java_com_h2ae_android_NativeBridge_startGame(JNIEnv *env, jobject thiz, jobject surface, jstring game_dir, jstring user_dir)
{
    (void)thiz;
    if (g_running) return JNI_FALSE;
    const char *g = (*env)->GetStringUTFChars(env, game_dir, 0), *u = (*env)->GetStringUTFChars(env, user_dir, 0);
    snprintf(g_game_dir, sizeof g_game_dir, "%s", g); snprintf(g_user_dir, sizeof g_user_dir, "%s", u);
    (*env)->ReleaseStringUTFChars(env, game_dir, g); (*env)->ReleaseStringUTFChars(env, user_dir, u);
    g_window = ANativeWindow_fromSurface(env, surface);
    if (!g_window) return JNI_FALSE;
    g_stop_requested = 0; g_running = 1;
    pthread_attr_t at;
    pthread_attr_init(&at);
    pthread_attr_setstacksize(&at, 256u << 20);              /* lifted calls nest on the host stack */
    int r = pthread_create(&g_thread, &at, game_main, 0);
    pthread_attr_destroy(&at);
    if (r != 0) { g_running = 0; return JNI_FALSE; }
    return JNI_TRUE;
}

JNIEXPORT void JNICALL
Java_com_h2ae_android_NativeBridge_stopGame(JNIEnv *env, jobject thiz)
{
    (void)env; (void)thiz;
    if (!g_running) return;
    g_stop_requested = 1;
    pthread_join(g_thread, 0);
}

JNIEXPORT jboolean JNICALL
Java_com_h2ae_android_NativeBridge_gameRunning(JNIEnv *env, jobject thiz) { (void)env; (void)thiz; return g_running ? JNI_TRUE : JNI_FALSE; }

/* ---- input from the touch HUD (Kotlin UI thread) -> InputManager; the guest reads it through DirectInput ---- */
#include "input_manager.h"

JNIEXPORT void JNICALL Java_com_h2ae_android_NativeBridge_inputKey(JNIEnv *e, jobject t, jint dik, jboolean down)
{
    (void)e; (void)t;
    h2_input_key((unsigned)dik, down);
    /* Windows delivers a key to the window as a message as well as to DirectInput: some game code (skipping a
     * cutscene, menus) listens to the message. Same virtual key and scan code the real keyboard would give. */
    unsigned vk = h2_input_dik_to_vk((unsigned)dik), scan = (unsigned)dik & 0x7Fu, ext = ((unsigned)dik & 0x80u) ? 1u : 0u;
    uint32_t hwnd = h2_main_window();
    H2_INFO(H2_LOG_INPUT, "key dik=%02X vk=%02X %s (window %08X)", (unsigned)dik, vk, down ? "down" : "up", hwnd);
    if (vk && hwnd)
        h2_post_message(hwnd, down ? 0x100u : 0x101u, vk, 1u | (scan << 16) | (ext << 24) | (down ? 0u : 0xC0000000u));
}
JNIEXPORT void JNICALL Java_com_h2ae_android_NativeBridge_inputMouseMove(JNIEnv *e, jobject t, jint dx, jint dy) { (void)e; (void)t; h2_input_mouse_move(dx, dy); }
JNIEXPORT void JNICALL Java_com_h2ae_android_NativeBridge_inputMouseButton(JNIEnv *e, jobject t, jint i, jboolean down) { (void)e; (void)t; H2_INFO(H2_LOG_INPUT, "mouse button %d %s", (int)i, down ? "down" : "up"); h2_input_mouse_button((unsigned)i, down); }
JNIEXPORT void JNICALL Java_com_h2ae_android_NativeBridge_inputWheel(JNIEnv *e, jobject t, jint dz) { (void)e; (void)t; h2_input_mouse_wheel(dz); }
JNIEXPORT void JNICALL Java_com_h2ae_android_NativeBridge_inputCursor(JNIEnv *e, jobject t, jint x, jint y) { (void)e; (void)t; h2_input_cursor(x, y); }
JNIEXPORT void JNICALL Java_com_h2ae_android_NativeBridge_inputReset(JNIEnv *e, jobject t) { (void)e; (void)t; h2_input_reset(); }

/* 0 boot, 1 intro/logos, 2 menu, 3 mission: see platform/game_context.c */
JNIEXPORT jint JNICALL Java_com_h2ae_android_NativeBridge_gameContext(JNIEnv *e, jobject t) { (void)e; (void)t; return h2_game_context(); }
