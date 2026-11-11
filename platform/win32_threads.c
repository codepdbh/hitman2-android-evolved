/*
 * win32_threads.c - guest threads: CreateThread and the scheduler behind Sleep / Wait* / critical sections.
 *
 * Model: cooperative, one guest thread runs at a time. Each guest thread has its own CPU, stack and TIB (fs:)
 * and runs on its own host thread, but only the thread holding the "baton" executes; control is handed over
 * only inside the blocking calls listed in h2_platform.h. Consequences, stated plainly:
 *   - data races between guest threads cannot happen, and runs are repeatable except for wall-clock timeouts;
 *   - a guest thread that spins without ever calling a blocking API starves the others (the original code polls
 *     with Sleep, which yields);
 *   - thread priorities are accepted and ignored.
 * If every thread is blocked with no timeout the guest is stopped as a deadlock instead of hanging.
 *
 * Host side: Windows (CreateThread + events as batons) or POSIX (pthreads + semaphores). The Win32 calls are
 * declared by hand so that no Windows header enters this layer.
 */
#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE 1
#endif
#include <string.h>
#include <time.h>

#include "h2_platform.h"
#include "h2_handles.h"

#define ARG(i) (args[(i)])
#define UNUSED (void)c

/* ---------------- host primitives ---------------- */
#if defined(_WIN32)
extern void *__stdcall CreateEventA(void *, int, int, const char *);
extern int __stdcall SetEvent(void *);
extern unsigned long __stdcall WaitForSingleObject(void *, unsigned long);
extern void *__stdcall CreateThread(void *, size_t, unsigned long(__stdcall *)(void *), void *, unsigned long, unsigned long *);
extern void __stdcall Sleep(unsigned long);
typedef void *Baton;
static void baton_init(Baton *b) { *b = CreateEventA(0, 0, 0, 0); }
static void baton_post(Baton *b) { SetEvent(*b); }
static void baton_wait(Baton *b) { WaitForSingleObject(*b, 0xFFFFFFFFul); }
static void host_sleep_ms(unsigned ms) { Sleep(ms); }
typedef struct { void (*fn)(void *); void *arg; } HostStart;
static unsigned long __stdcall host_entry(void *p) { HostStart s = *(HostStart *)p; s.fn(s.arg); return 0; }
static int host_thread_start(void (*fn)(void *), void *arg)
{
    static HostStart starts[64]; static unsigned n;
    if (n >= 64) return -1;
    starts[n].fn = fn; starts[n].arg = arg;
    void *h = CreateThread(0, 64u << 20, host_entry, &starts[n++], 0x10000 /* STACK_SIZE_PARAM_IS_A_RESERVATION */, 0);
    return h ? 0 : -1;
}
#else
#include <pthread.h>
#include <semaphore.h>
typedef sem_t Baton;
static void baton_init(Baton *b) { sem_init(b, 0, 0); }
static void baton_post(Baton *b) { sem_post(b); }
static void baton_wait(Baton *b) { while (sem_wait(b) != 0) {} }
static void host_sleep_ms(unsigned ms) { struct timespec ts = { (time_t)(ms / 1000u), (long)(ms % 1000u) * 1000000L }; nanosleep(&ts, 0); }
typedef struct { void (*fn)(void *); void *arg; } HostStart;
static void *host_entry(void *p) { HostStart s = *(HostStart *)p; s.fn(s.arg); return 0; }
static int host_thread_start(void (*fn)(void *), void *arg)
{
    static HostStart starts[64]; static unsigned n;
    if (n >= 64) return -1;
    starts[n].fn = fn; starts[n].arg = arg;
    pthread_attr_t at; pthread_t t;
    pthread_attr_init(&at); pthread_attr_setstacksize(&at, 64u << 20); pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
    int r = pthread_create(&t, &at, host_entry, &starts[n++]);
    pthread_attr_destroy(&at);
    return r == 0 ? 0 : -1;
}
#endif

/* ---------------- scheduler ---------------- */
enum { T_FREE = 0, T_RUN, T_BLOCK, T_DONE };
#define MAX_THREADS 16
#define THREAD_STACK 0x00100000u      /* 1 MiB guest stack per secondary thread */

typedef struct {
    int      state;
    CPU     *cpu;
    CPU      cpu_store;
    Baton    go;
    int      host_started;
    uint32_t tid, start, param, exit_code, tib;
    unsigned nwait;  uint32_t wait[16];  int wait_all;
    int      has_deadline;  uint64_t deadline;
    uint32_t cs;                      /* blocked on this critical section (0 = no) */
    uint32_t result;                  /* what the blocking call returns once this thread is picked again */
} GThread;

static GThread g_th[MAX_THREADS];
static int g_cur, g_inited;
static h2_thread_runner g_runner;

static volatile int g_abort;
static h2_stop_reason g_abort_why;
static char g_abort_msg[512];

static void sched_init(CPU *c)
{
    if (g_inited) return;
    g_inited = 1;
    g_th[0].state = T_RUN; g_th[0].tid = 0x104; g_th[0].cpu = c; g_th[0].host_started = 1;
    baton_init(&g_th[0].go);
    g_cur = 0;
}

uint32_t h2_thread_tid(void) { return g_inited ? g_th[g_cur].tid : 0x104u; }
int h2_thread_done(int index) { return index >= 0 && index < MAX_THREADS && g_th[index].state == T_DONE; }
void h2_threads_set_runner(h2_thread_runner fn) { g_runner = fn; }

static void default_runner(CPU *c, uint32_t entry) { dispatch(c, entry); }

/* Is thread t able to run now? If it is blocked and its wait is satisfied, completes the wait (acquires). */
static int ready(GThread *t, uint64_t now, int idx)
{
    if (t->state == T_RUN) return 1;
    if (t->state != T_BLOCK) return 0;
    if (t->cs) {
        if (rd32(t->cs + 12) == 0) { t->cs = 0; t->state = T_RUN; return 1; }
        return 0;
    }
    if (t->nwait) {
        H2Object *o[16]; unsigned sig = 0, first = 0xFFFFFFFFu;
        for (unsigned i = 0; i < t->nwait; i++) {
            o[i] = h2_handle_get(t->wait[i], H2_OBJ_ANY);
            if (!o[i]) { t->result = 0xFFFFFFFFu; t->state = T_RUN; return 1; }          /* WAIT_FAILED */
            if (h2_obj_signaled(o[i], (int)t->tid)) { sig++; if (first == 0xFFFFFFFFu) first = i; }
        }
        if (t->wait_all ? sig == t->nwait : sig > 0) {
            if (t->wait_all) { for (unsigned i = 0; i < t->nwait; i++) h2_obj_acquire(o[i], (int)t->tid); t->result = 0; }
            else { h2_obj_acquire(o[first], (int)t->tid); t->result = first; }
            t->state = T_RUN; return 1;
        }
    }
    if (t->has_deadline && now >= t->deadline) {
        t->result = t->nwait ? 0x102u : 0u;                                              /* WAIT_TIMEOUT / Sleep done */
        t->state = T_RUN; return 1;
    }
    (void)idx;
    return 0;
}

static void thread_main(void *arg);

/* Choose the next thread to run. If none can run now, sleep (on the host) until the earliest deadline;
 * if there is none, the guest is deadlocked. Never returns -1. */
static int pick_next(void)
{
    for (;;) {
        uint64_t now = h2_host_monotonic_ns(), earliest = 0; int have = 0;
        for (int k = 1; k <= MAX_THREADS; k++) {
            int i = (g_cur + k) % MAX_THREADS;
            if (g_th[i].state && ready(&g_th[i], now, i)) return i;
        }
        for (int i = 0; i < MAX_THREADS; i++)
            if (g_th[i].state == T_BLOCK && g_th[i].has_deadline && (!have || g_th[i].deadline < earliest)) { earliest = g_th[i].deadline; have = 1; }
        if (!have)
            h2_guest_stop(H2_STOP_UNIMPLEMENTED_IMPORT,
                          "all guest threads are blocked with no timeout (deadlock): the awaited signal can never come");
        uint64_t ms = earliest > now ? (earliest - now) / 1000000ull + 1 : 1;
        host_sleep_ms((unsigned)(ms > 50 ? 50 : ms));                                   /* idle: no CPU use while everyone waits */
    }
}

static void transfer_to(int next)
{
    int me = g_cur;
    g_cur = next;
    if (!g_th[next].host_started) {
        g_th[next].host_started = 1;
        if (host_thread_start(thread_main, &g_th[next]) != 0) { g_cur = me; h2_guest_stop(H2_STOP_INTERNAL, "cannot start a host thread for a guest thread"); }
    } else {
        baton_post(&g_th[next].go);
    }
}

/* Called by the running thread (state already T_RUN / T_BLOCK) to let others run until it is picked again. */
static void reschedule(void)
{
    int me = g_cur, next = pick_next();
    if (next == me) return;
    transfer_to(next);
    baton_wait(&g_th[me].go);
    if (g_abort && me == 0) { g_abort = 0; h2_guest_stop(g_abort_why, "%s", g_abort_msg); }
}

void h2_threads_forward_stop(h2_stop_reason why, const char *message)
{
    if (!g_inited || g_cur == 0) return;                       /* on the main thread the stop handler runs as usual */
    g_abort_why = why;
    strncpy(g_abort_msg, message, sizeof g_abort_msg - 1);
    g_abort_msg[sizeof g_abort_msg - 1] = 0;
    g_abort = 1;
    g_cur = 0;
    baton_post(&g_th[0].go);
    for (;;) host_sleep_ms(1000);                              /* this host thread is parked until the process ends */
}

static void thread_main(void *arg)
{
    GThread *t = (GThread *)arg;
    CPU *c = t->cpu;
    (g_runner ? g_runner : default_runner)(c, t->start);
    t->exit_code = c->eax;
    t->state = T_DONE;
    H2_INFO(H2_LOG_WIN32, "guest thread %04X returned %08X", t->tid, t->exit_code);
    int next = pick_next();                                    /* never returns to this thread */
    transfer_to(next);
}

/* ---------------- blocking API ---------------- */
uint32_t h2_wait_objects(CPU *c, unsigned n, const uint32_t *handles, int wait_all, uint32_t timeout_ms)
{
    sched_init(c);
    GThread *t = &g_th[g_cur];
    H2Object *o[16];
    for (unsigned i = 0; i < n; i++) {
        o[i] = h2_handle_get(handles[i], H2_OBJ_ANY);
        if (!o[i] || (o[i]->type != H2_OBJ_EVENT && o[i]->type != H2_OBJ_MUTEX && o[i]->type != H2_OBJ_THREAD))
            h2_guest_stop(H2_STOP_UNIMPLEMENTED_IMPORT, "Wait on handle %08X: %s", handles[i], o[i] ? "object type cannot be waited on yet" : "invalid handle");
    }
    /* fast path: satisfied now, no switch */
    unsigned sig = 0, first = 0xFFFFFFFFu;
    for (unsigned i = 0; i < n; i++) if (h2_obj_signaled(o[i], (int)t->tid)) { sig++; if (first == 0xFFFFFFFFu) first = i; }
    if (wait_all ? sig == n : sig > 0) {
        if (wait_all) { for (unsigned i = 0; i < n; i++) h2_obj_acquire(o[i], (int)t->tid); return 0; }
        h2_obj_acquire(o[first], (int)t->tid);
        return first;
    }
    if (timeout_ms == 0) {
        h2_thread_sleep(c, 0);       /* a failed poll ends this thread's time slice: otherwise a polling loop starves the thread it waits for */
        return 0x102;
    }
    t->nwait = n; memcpy(t->wait, handles, n * sizeof *handles); t->wait_all = wait_all;
    t->has_deadline = timeout_ms != 0xFFFFFFFFu;
    if (t->has_deadline) t->deadline = h2_host_monotonic_ns() + (uint64_t)timeout_ms * 1000000ull;
    t->cs = 0; t->state = T_BLOCK;
    reschedule();
    t = &g_th[g_cur];
    t->nwait = 0; t->has_deadline = 0;
    return t->result;
}

void h2_thread_sleep(CPU *c, uint32_t ms)
{
    sched_init(c);
    GThread *t = &g_th[g_cur];
    t->nwait = 0; t->cs = 0;
    if (ms == 0) { t->state = T_RUN; reschedule(); return; }          /* yield */
    t->has_deadline = 1; t->deadline = h2_host_monotonic_ns() + (uint64_t)ms * 1000000ull; t->state = T_BLOCK;
    reschedule();
    g_th[g_cur].has_deadline = 0;
}

void h2_thread_block_on_cs(CPU *c, uint32_t cs)
{
    sched_init(c);
    GThread *t = &g_th[g_cur];
    t->nwait = 0; t->has_deadline = 0; t->cs = cs; t->state = T_BLOCK;
    reschedule();
}

/* ---------------- Win32 entry points ---------------- */
/* HANDLE CreateThread(attrs, stackSize, start, param, flags, DWORD *tid) */
static uint32_t k_CreateThread(CPU *c, const uint32_t *args)
{
    sched_init(c);
    uint32_t start = ARG(2), param = ARG(3), flags = ARG(4);
    if (flags & 0x4) h2_guest_stop(H2_STOP_UNIMPLEMENTED_IMPORT, "CreateThread(CREATE_SUSPENDED) / ResumeThread are not implemented");
    int idx = -1;
    for (int i = 1; i < MAX_THREADS; i++) if (g_th[i].state == T_FREE || g_th[i].state == T_DONE) { if (g_th[i].state == T_FREE) { idx = i; break; } }
    if (idx < 0) { g_h2_last_error = 8; return 0; }                    /* ERROR_NOT_ENOUGH_MEMORY: thread table full */
    GThread *t = &g_th[idx];
    uint32_t stack = h2_virtual_alloc(THREAD_STACK), tib = h2_virtual_alloc(0x1000);
    H2Object *o; uint32_t h = h2_handle_new(H2_OBJ_THREAD, &o);
    if (!stack || !tib || !h) { g_h2_last_error = 8; return 0; }
    o->u.thread.index = idx;
    memset(t, 0, sizeof *t);
    t->cpu = &t->cpu_store;
    t->tid = 0x104u + 4u * (uint32_t)idx; t->start = start; t->param = param; t->tib = tib;
    wr32(tib + 0x00, 0xFFFFFFFFu); wr32(tib + 0x04, stack + THREAD_STACK); wr32(tib + 0x08, stack);
    wr32(tib + 0x18, tib); wr32(tib + 0x20, 0x100u); wr32(tib + 0x24, t->tid);
    h2_cpu_init_main_thread(t->cpu);
    t->cpu->esp = stack + THREAD_STACK - 0x100u; t->cpu->fs_base = tib;
    push32(t->cpu, param);
    push32(t->cpu, 0xDEAD0001u);                                       /* return address: the entry's ret goes back to the host */
    baton_init(&t->go);
    t->state = T_RUN;
    if (ARG(5)) wr32(ARG(5), t->tid);
    H2_INFO(H2_LOG_WIN32, "CreateThread: guest thread %04X starts at %08X (param %08X); runs when the creator blocks", t->tid, start, param);
    g_h2_last_error = 0;
    return h;
}

static uint32_t k_SetThreadPriority(CPU *c, const uint32_t *args) { UNUSED; (void)args; return 1; }   /* cooperative scheduling: priorities have no effect */
static uint32_t k_SetThreadPriorityBoost(CPU *c, const uint32_t *args) { UNUSED; (void)args; return 1; }   /* same: no effect */
static uint32_t k_SwitchToThread(CPU *c, const uint32_t *args) { (void)args; h2_thread_sleep(c, 0); return 1; }
static uint32_t k_Sleep(CPU *c, const uint32_t *args) { h2_thread_sleep(c, ARG(0)); return 0; }
static uint32_t k_GetCurrentThreadId(CPU *c, const uint32_t *args) { UNUSED; (void)args; return h2_thread_tid(); }
static uint32_t k_ExitThread(CPU *c, const uint32_t *args)
{
    sched_init(c);
    if (g_cur == 0) h2_guest_stop(H2_STOP_EXIT, "ExitThread on the main thread (exit code %u)", ARG(0));
    GThread *t = &g_th[g_cur];
    t->exit_code = ARG(0); t->state = T_DONE;
    H2_INFO(H2_LOG_WIN32, "guest thread %04X called ExitThread(%u)", t->tid, ARG(0));
    transfer_to(pick_next());
    for (;;) host_sleep_ms(1000);                                      /* this host thread is finished; it is never resumed */
}

static const H2ImportDef k_defs[] = {
#define K(name, n) { "KERNEL32.dll", #name, n, k_##name }
    K(CreateThread, 6), K(SetThreadPriority, 2), K(SetThreadPriorityBoost, 2), K(SwitchToThread, 0), K(Sleep, 1), K(GetCurrentThreadId, 0), K(ExitThread, 1),
#undef K
};

void h2_register_threads(void) { h2_imports_register(k_defs, sizeof k_defs / sizeof k_defs[0]); }
