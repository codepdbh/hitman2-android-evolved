/*
 * win32_user32.c - USER32: window classes, windows, the message queue, and the
 * few input/utility calls the engine makes.
 *
 * Model
 * -----
 * There is no desktop. The guest gets one virtual display (h2_display_configure)
 * and its windows are bookkeeping: class, window procedure, rectangle, style,
 * visibility. Nothing is drawn here; the renderer layer attaches the real
 * surface (an Android native window; nothing at all on the headless PC host).
 * The window is borderless: client area == window rectangle.
 *
 * What IS real is the protocol the game's code depends on: window procedures
 * are called (in lifted code) with the creation, activation, sizing and
 * destruction messages in Windows' order, posted messages are queued and
 * retrieved in order, and WM_QUIT ends the loop.
 *
 * Messages sent on creation:  WM_NCCREATE, WM_CREATE
 * On first show:              WM_SHOWWINDOW, WM_ACTIVATEAPP, WM_ACTIVATE, WM_SETFOCUS, WM_SIZE, WM_MOVE
 * On destruction:             WM_DESTROY, WM_NCDESTROY
 * Not generated (yet): WM_GETMINMAXINFO, WM_NCCALCSIZE, WM_WINDOWPOSCHANGING/CHANGED,
 * WM_PAINT, WM_ERASEBKGND, non-client messages.
 *
 * One guest thread: SendMessage is a direct call, and GetMessage on an empty
 * queue can only be woken by the host's input source (h2_post_message).
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "h2_platform.h"
#include "input_manager.h"

#define ARG(i) (args[(i)])
#define UNUSED (void)c

enum { WM_CREATE = 1, WM_DESTROY = 2, WM_MOVE = 3, WM_SIZE = 5, WM_ACTIVATE = 6, WM_SETFOCUS = 7, WM_CLOSE = 0x10,
       WM_QUIT = 0x12, WM_SHOWWINDOW = 0x18, WM_ACTIVATEAPP = 0x1C, WM_SETTEXT = 0x0C, WM_NCCREATE = 0x81,
       WM_NCDESTROY = 0x82, WM_KEYDOWN = 0x100, WM_CHAR = 0x102, WM_SYSKEYDOWN = 0x104 };

/* ---- virtual display (input state lives in input/input_manager.c) ---- */
static int g_disp_w = 800, g_disp_h = 450;

void h2_display_configure(int width, int height) { g_disp_w = width; g_disp_h = height; }
void h2_display_size(int *width, int *height) { *width = g_disp_w; *height = g_disp_h; }

/* ---- calling back into the guest ----
 * stdcall: arguments pushed right to left, callee pops them. The lifted callee
 * returns structurally, so the return address is only a marker. */
uint32_t h2_call_guest(CPU *c, uint32_t fn, int nargs, ...)
{
    uint32_t a[8];
    va_list ap;
    va_start(ap, nargs);
    for (int i = 0; i < nargs && i < 8; i++) a[i] = va_arg(ap, uint32_t);
    va_end(ap);
    uint32_t esp0 = c->esp;
    for (int i = nargs - 1; i >= 0; i--) push32(c, a[i]);
    push32(c, 0xCA11BACCu);
    dispatch(c, fn);
    if (c->esp != esp0)
        h2_guest_stop(H2_STOP_INTERNAL, "guest callback %08X left esp=%08X, expected %08X (wrong calling convention?)", fn, c->esp, esp0);
    return c->eax;
}

/* ---- atoms (registered messages) ---- */
#define MAX_ATOMS 256
static char g_atoms[MAX_ATOMS][64];
static unsigned g_natoms;

static void upper_copy(char *dst, size_t cap, const char *s)
{
    size_t i = 0;
    for (; s[i] && i + 1 < cap; i++) dst[i] = (char)((s[i] >= 'a' && s[i] <= 'z') ? s[i] - 32 : s[i]);
    dst[i] = 0;
}

static uint32_t u_RegisterWindowMessageA(CPU *c, const uint32_t *args)
{
    UNUSED;
    char key[64];
    const char *s = (const char *)guest_ptr(ARG(0));
    if (!s[0] || strlen(s) >= sizeof key) { g_h2_last_error = 87; return 0; }
    upper_copy(key, sizeof key, s);
    for (unsigned i = 0; i < g_natoms; i++) if (strcmp(g_atoms[i], key) == 0) return 0xC000u + i;
    if (g_natoms == MAX_ATOMS) { g_h2_last_error = 8; return 0; }
    strcpy(g_atoms[g_natoms], key);
    return 0xC000u + g_natoms++;
}

/* ---- window classes ---- */
#define MAX_CLASSES 32
typedef struct { int used; char name[64]; uint32_t wcx[12]; uint32_t name_guest; } WndClass;   /* wcx: the 48-byte WNDCLASSEXA */
static WndClass g_classes[MAX_CLASSES];

static WndClass *find_class(uint32_t name_or_atom)
{
    char key[64];
    if (name_or_atom < 0x10000u) {
        unsigned i = name_or_atom - 0xC100u;
        return (i < MAX_CLASSES && g_classes[i].used) ? &g_classes[i] : 0;
    }
    upper_copy(key, sizeof key, (const char *)guest_ptr(name_or_atom));
    for (int i = 0; i < MAX_CLASSES; i++) if (g_classes[i].used && strcmp(g_classes[i].name, key) == 0) return &g_classes[i];
    return 0;
}

static uint32_t u_GetClassInfoExA(CPU *c, const uint32_t *args)
{
    UNUSED;
    WndClass *k = find_class(ARG(1));
    if (!k) { g_h2_last_error = 1411; return 0; }                /* ERROR_CLASS_DOES_NOT_EXIST */
    for (int i = 1; i < 12; i++) wr32(ARG(2) + 4u * (uint32_t)i, k->wcx[i]);   /* cbSize is the caller's */
    wr32(ARG(2) + 40, k->name_guest);
    return 0xC100u + (uint32_t)(k - g_classes);
}

static uint32_t u_RegisterClassExA(CPU *c, const uint32_t *args)
{
    UNUSED;
    uint32_t p = ARG(0);
    if (rd32(p) != 48 || !rd32(p + 8) || rd32(p + 40) < 0x10000u) { g_h2_last_error = 87; return 0; }
    if (find_class(rd32(p + 40))) { g_h2_last_error = 1410; return 0; }   /* ERROR_CLASS_ALREADY_EXISTS */
    for (int i = 0; i < MAX_CLASSES; i++)
        if (!g_classes[i].used) {
            WndClass *k = &g_classes[i];
            k->used = 1;
            upper_copy(k->name, sizeof k->name, (const char *)guest_ptr(rd32(p + 40)));
            for (int j = 0; j < 12; j++) k->wcx[j] = rd32(p + 4u * (uint32_t)j);
            k->name_guest = h2_os_strdup((const char *)guest_ptr(rd32(p + 40)));
            H2_INFO(H2_LOG_WIN32, "RegisterClassEx \"%s\" wndproc=%08X", k->name, k->wcx[2]);
            return 0xC100u + (uint32_t)i;
        }
    g_h2_last_error = 8;
    return 0;
}

static uint32_t u_UnregisterClassA(CPU *c, const uint32_t *args)
{
    UNUSED;
    WndClass *k = find_class(ARG(0));
    if (!k) { g_h2_last_error = 1411; return 0; }
    k->used = 0;
    return 1;
}

/* ---- windows ---- */
#define MAX_WINDOWS 16
#define HWND_BASE 0x00010100u
typedef struct {
    int used, visible, shown_once, destroying;
    WndClass *cls;
    uint32_t wndproc, style, exstyle, userdata, parent, hinst, id;
    int x, y, w, h;
    char title[128];
    uint32_t extra[8];
} Window;
static Window g_windows[MAX_WINDOWS];
static uint32_t g_active, g_focus;

static Window *win(uint32_t hwnd)
{
    if (hwnd < HWND_BASE || (hwnd - HWND_BASE) % 4u) return 0;
    unsigned i = (hwnd - HWND_BASE) / 4u;
    return (i < MAX_WINDOWS && g_windows[i].used) ? &g_windows[i] : 0;
}
static uint32_t hwnd_of(const Window *w) { return HWND_BASE + (uint32_t)(w - g_windows) * 4u; }

static uint32_t send(CPU *c, uint32_t hwnd, uint32_t msg, uint32_t wp, uint32_t lp)
{
    Window *w = win(hwnd);
    if (!w) return 0;
    return h2_call_guest(c, w->wndproc, 4, hwnd, msg, wp, lp);
}

static uint32_t u_CreateWindowExA(CPU *c, const uint32_t *args)
{
    WndClass *k = find_class(ARG(1));
    Window *w = 0;
    if (!k) { g_h2_last_error = 1407; return 0; }                 /* ERROR_CANNOT_FIND_WND_CLASS */
    for (int i = 0; i < MAX_WINDOWS; i++) if (!g_windows[i].used) { w = &g_windows[i]; break; }
    if (!w) { g_h2_last_error = 8; return 0; }
    memset(w, 0, sizeof *w);
    w->used = 1; w->cls = k; w->wndproc = k->wcx[2];
    w->exstyle = ARG(0); w->style = ARG(3) & ~0x10000000u;        /* WS_VISIBLE is applied by the show below */
    w->x = (int32_t)ARG(4); w->y = (int32_t)ARG(5); w->w = (int32_t)ARG(6); w->h = (int32_t)ARG(7);
    if ((uint32_t)w->x == 0x80000000u) { w->x = 0; w->y = 0; }    /* CW_USEDEFAULT */
    if ((uint32_t)w->w == 0x80000000u) { w->w = g_disp_w; w->h = g_disp_h; }
    w->parent = ARG(8); w->id = ARG(9); w->hinst = ARG(10);
    if (ARG(2)) snprintf(w->title, sizeof w->title, "%s", (const char *)guest_ptr(ARG(2)));
    uint32_t hwnd = hwnd_of(w);

    /* CREATESTRUCTA on the guest stack, as the real call does. */
    uint32_t esp0 = c->esp;
    c->esp -= 48;
    uint32_t cs = c->esp;
    wr32(cs, ARG(11)); wr32(cs + 4, ARG(10)); wr32(cs + 8, ARG(9)); wr32(cs + 12, ARG(8));
    wr32(cs + 16, (uint32_t)w->h); wr32(cs + 20, (uint32_t)w->w); wr32(cs + 24, (uint32_t)w->y); wr32(cs + 28, (uint32_t)w->x);
    wr32(cs + 32, ARG(3)); wr32(cs + 36, ARG(2)); wr32(cs + 40, ARG(1)); wr32(cs + 44, ARG(0));
    H2_INFO(H2_LOG_WIN32, "CreateWindowEx class=\"%s\" title=\"%s\" %dx%d at (%d,%d) style=%08X -> hwnd %X",
            k->name, w->title, w->w, w->h, w->x, w->y, ARG(3), hwnd);
    uint32_t ok = send(c, hwnd, WM_NCCREATE, 0, cs);
    if (ok && win(hwnd)) ok = send(c, hwnd, WM_CREATE, 0, cs) != 0xFFFFFFFFu;
    c->esp = esp0;
    if (!ok || !win(hwnd)) {
        H2_WARN(H2_LOG_WIN32, "window creation refused by its window procedure");
        if (win(hwnd)) { send(c, hwnd, WM_NCDESTROY, 0, 0); w->used = 0; }
        return 0;
    }
    if (ARG(3) & 0x10000000u) {                                   /* WS_VISIBLE */
        uint32_t show_args[2] = { hwnd, 5 };
        extern uint32_t u_ShowWindow(CPU *, const uint32_t *);
        u_ShowWindow(c, show_args);
    }
    return hwnd;
}

uint32_t u_ShowWindow(CPU *c, const uint32_t *args)
{
    Window *w = win(ARG(0));
    if (!w) return 0;
    int was = w->visible, show = ARG(1) != 0;                     /* SW_HIDE == 0 */
    if (show == was && w->shown_once) return (uint32_t)was;
    w->visible = show;
    if (show) w->style |= 0x10000000u; else w->style &= ~0x10000000u;
    send(c, ARG(0), WM_SHOWWINDOW, (uint32_t)show, 0);
    if (show && win(ARG(0))) {
        g_active = g_focus = ARG(0);
        send(c, ARG(0), WM_ACTIVATEAPP, 1, 0);
        send(c, ARG(0), WM_ACTIVATE, 1 /* WA_ACTIVE */, 0);
        send(c, ARG(0), WM_SETFOCUS, 0, 0);
        if (!w->shown_once && win(ARG(0))) {
            w->shown_once = 1;
            send(c, ARG(0), WM_SIZE, 0, ((uint32_t)w->h << 16) | ((uint32_t)w->w & 0xFFFFu));
            send(c, ARG(0), WM_MOVE, 0, ((uint32_t)w->y << 16) | ((uint32_t)w->x & 0xFFFFu));
        }
    }
    return (uint32_t)was;
}

static uint32_t u_SetWindowPos(CPU *c, const uint32_t *args)
{
    Window *w = win(ARG(0));
    uint32_t flags = ARG(6);
    if (!w) { g_h2_last_error = 1400; return 0; }                 /* ERROR_INVALID_WINDOW_HANDLE */
    int moved = 0, sized = 0;
    if (!(flags & 2)) { moved = w->x != (int32_t)ARG(2) || w->y != (int32_t)ARG(3); w->x = (int32_t)ARG(2); w->y = (int32_t)ARG(3); }
    if (!(flags & 1)) { sized = w->w != (int32_t)ARG(4) || w->h != (int32_t)ARG(5); w->w = (int32_t)ARG(4); w->h = (int32_t)ARG(5); }
    if (sized) send(c, ARG(0), WM_SIZE, 0, ((uint32_t)w->h << 16) | ((uint32_t)w->w & 0xFFFFu));
    if (moved && win(ARG(0))) send(c, ARG(0), WM_MOVE, 0, ((uint32_t)w->y << 16) | ((uint32_t)w->x & 0xFFFFu));
    if (win(ARG(0)) && (flags & 0x40)) { uint32_t a[2] = { ARG(0), 5 }; u_ShowWindow(c, a); }   /* SWP_SHOWWINDOW */
    if (win(ARG(0)) && (flags & 0x80)) { uint32_t a[2] = { ARG(0), 0 }; u_ShowWindow(c, a); }   /* SWP_HIDEWINDOW */
    return 1;
}

static uint32_t u_DestroyWindow(CPU *c, const uint32_t *args)
{
    Window *w = win(ARG(0));
    if (!w || w->destroying) { g_h2_last_error = 1400; return 0; }
    w->destroying = 1;
    send(c, ARG(0), WM_DESTROY, 0, 0);
    send(c, ARG(0), WM_NCDESTROY, 0, 0);
    if (g_active == ARG(0)) g_active = 0;
    if (g_focus == ARG(0)) g_focus = 0;
    w->used = 0;
    return 1;
}

static uint32_t u_SetWindowTextA(CPU *c, const uint32_t *args)
{
    UNUSED;
    Window *w = win(ARG(0));
    if (!w) { g_h2_last_error = 1400; return 0; }
    snprintf(w->title, sizeof w->title, "%s", ARG(1) ? (const char *)guest_ptr(ARG(1)) : "");
    H2_INFO(H2_LOG_WIN32, "window title: \"%s\"", w->title);
    return 1;
}

static uint32_t u_SetWindowLongA(CPU *c, const uint32_t *args)
{
    UNUSED;
    Window *w = win(ARG(0));
    int32_t idx = (int32_t)ARG(1);
    uint32_t *slot;
    if (!w) { g_h2_last_error = 1400; return 0; }
    if (idx == -4) slot = &w->wndproc;
    else if (idx == -16) slot = &w->style;
    else if (idx == -20) slot = &w->exstyle;
    else if (idx == -21) slot = &w->userdata;
    else if (idx == -12) slot = &w->id;
    else if (idx >= 0 && idx + 4 <= (int32_t)sizeof w->extra && (uint32_t)idx + 4 <= w->cls->wcx[4]) slot = &w->extra[idx / 4];
    else { g_h2_last_error = 1413; return 0; }                    /* ERROR_INVALID_INDEX */
    uint32_t prev = *slot; *slot = ARG(2);
    g_h2_last_error = 0;
    return prev;
}

static uint32_t u_FindWindowA(CPU *c, const uint32_t *args)
{
    UNUSED;
    WndClass *k = ARG(0) ? find_class(ARG(0)) : 0;
    if (ARG(0) && !k) return 0;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        Window *w = &g_windows[i];
        if (!w->used || (k && w->cls != k)) continue;
        if (ARG(1) && strcmp(w->title, (const char *)guest_ptr(ARG(1))) != 0) continue;
        return hwnd_of(w);
    }
    return 0;                                                     /* no other process exists to own one */
}

/* ---- message queue ---- */
#define QUEUE 256
typedef struct { uint32_t hwnd, msg, wp, lp, time; } Msg;
static Msg g_queue[QUEUE];
static unsigned g_qhead, g_qcount;
static int g_quit_posted;
static uint32_t g_quit_code;

int h2_post_message(uint32_t hwnd, uint32_t msg, uint32_t wp, uint32_t lp)
{
    if (g_qcount == QUEUE) return 0;
    Msg *m = &g_queue[(g_qhead + g_qcount++) % QUEUE];
    m->hwnd = hwnd; m->msg = msg; m->wp = wp; m->lp = lp;
    m->time = (uint32_t)(h2_host_monotonic_ns() / 1000000ull);
    return 1;
}
uint32_t h2_main_window(void) { return g_active; }

static void write_msg(uint32_t p, const Msg *m)
{
    wr32(p, m->hwnd); wr32(p + 4, m->msg); wr32(p + 8, m->wp); wr32(p + 12, m->lp); wr32(p + 16, m->time);
    int cx, cy;
    h2_input_cursor_pos(&cx, &cy);
    wr32(p + 20, (uint32_t)cx); wr32(p + 24, (uint32_t)cy);
}

/* Returns 1 and fills *out if a message matching the filter exists. */
static int take(uint32_t hwnd, uint32_t lo, uint32_t hi, int remove, Msg *out)
{
    for (unsigned i = 0; i < g_qcount; i++) {
        Msg *m = &g_queue[(g_qhead + i) % QUEUE];
        if (hwnd && m->hwnd != hwnd) continue;
        if ((lo || hi) && (m->msg < lo || m->msg > hi)) continue;
        *out = *m;
        if (remove) {
            for (unsigned j = i; j + 1 < g_qcount; j++) g_queue[(g_qhead + j) % QUEUE] = g_queue[(g_qhead + j + 1) % QUEUE];
            g_qcount--;
        }
        return 1;
    }
    if (g_quit_posted && (!(lo || hi) || (lo <= WM_QUIT && WM_QUIT <= hi))) {   /* WM_QUIT comes after everything else */
        out->hwnd = 0; out->msg = WM_QUIT; out->wp = g_quit_code; out->lp = 0; out->time = 0;
        if (remove) g_quit_posted = 0;
        return 1;
    }
    return 0;
}

static uint32_t u_PeekMessageA(CPU *c, const uint32_t *args)
{
    UNUSED;
    Msg m;
    if (!take(ARG(1), ARG(2), ARG(3), (ARG(4) & 1) != 0, &m)) return 0;
    write_msg(ARG(0), &m);
    return 1;
}

static uint32_t u_GetMessageA(CPU *c, const uint32_t *args)
{
    UNUSED;
    Msg m;
    if (!take(ARG(1), ARG(2), ARG(3), 1, &m))
        h2_guest_stop(H2_STOP_UNIMPLEMENTED_IMPORT,
                      "GetMessageA on an empty queue: would block, and this host has no input source to wake it");
    write_msg(ARG(0), &m);
    return m.msg != WM_QUIT;
}

static uint32_t u_PostMessageA(CPU *c, const uint32_t *args)
{
    UNUSED;
    if (ARG(0) && !win(ARG(0))) { g_h2_last_error = 1400; return 0; }
    if (!h2_post_message(ARG(0), ARG(1), ARG(2), ARG(3))) { g_h2_last_error = 1816; return 0; }   /* ERROR_NOT_ENOUGH_QUOTA */
    return 1;
}

static uint32_t u_PostQuitMessage(CPU *c, const uint32_t *args) { UNUSED; g_quit_posted = 1; g_quit_code = ARG(0); return 0; }
static uint32_t u_SendMessageA(CPU *c, const uint32_t *args) { return send(c, ARG(0), ARG(1), ARG(2), ARG(3)); }

static uint32_t u_DispatchMessageA(CPU *c, const uint32_t *args)
{
    uint32_t p = ARG(0);
    if (!rd32(p)) return 0;                                       /* thread message: no window to deliver to */
    return send(c, rd32(p), rd32(p + 4), rd32(p + 8), rd32(p + 12));
}

/* VK -> character for WM_CHAR, US layout, honouring Shift and Caps Lock. */
static uint32_t vk_to_char(uint32_t vk)
{
    uint8_t keys[256];
    h2_input_vk_state(keys);
    int shift = (keys[0x10] & 0x80) != 0, caps = (keys[0x14] & 1) != 0;
    static const char digits_shift[] = ")!@#$%^&*(";
    if (vk >= 'A' && vk <= 'Z') return (shift != caps) ? vk : vk + 32;
    if (vk >= '0' && vk <= '9') return shift ? (uint32_t)digits_shift[vk - '0'] : vk;
    if (vk == 0x20 || vk == 0x0D || vk == 0x08 || vk == 0x09 || vk == 0x1B) return vk;
    return 0;
}

static uint32_t u_TranslateMessage(CPU *c, const uint32_t *args)
{
    UNUSED;
    uint32_t msg = rd32(ARG(0) + 4);
    if (msg != WM_KEYDOWN && msg != WM_SYSKEYDOWN) return 0;
    uint32_t ch = vk_to_char(rd32(ARG(0) + 8));
    if (ch && msg == WM_KEYDOWN) h2_post_message(rd32(ARG(0)), WM_CHAR, ch, rd32(ARG(0) + 12));
    return 1;
}

static uint32_t u_DefWindowProcA(CPU *c, const uint32_t *args)
{
    Window *w = win(ARG(0));
    if (!w) return 0;
    switch (ARG(1)) {
    case WM_NCCREATE:
        if (rd32(ARG(3) + 36)) snprintf(w->title, sizeof w->title, "%s", (const char *)guest_ptr(rd32(ARG(3) + 36)));
        return 1;
    case WM_SETTEXT:
        snprintf(w->title, sizeof w->title, "%s", ARG(3) ? (const char *)guest_ptr(ARG(3)) : "");
        return 1;
    case WM_CLOSE: { uint32_t a[1] = { ARG(0) }; u_DestroyWindow(c, a); return 0; }
    default:
        return 0;
    }
}

static uint32_t u_WaitMessage(CPU *c, const uint32_t *args)
{
    UNUSED; (void)args;
    if (g_qcount || g_quit_posted) return 1;
    h2_guest_stop(H2_STOP_UNIMPLEMENTED_IMPORT, "WaitMessage on an empty queue: would block, no input source in this host");
}

/* ---- metrics, cursor, keys ---- */
static uint32_t u_GetSystemMetrics(CPU *c, const uint32_t *args)
{
    UNUSED;
    switch (ARG(0)) {
    case 0: case 16: case 61: return (uint32_t)g_disp_w;          /* SM_CXSCREEN, SM_CXFULLSCREEN, SM_CXMAXIMIZED */
    case 1: case 17: case 62: return (uint32_t)g_disp_h;
    case 4: case 5: case 6: case 7: case 8: case 15: case 32: case 33: case 45: case 46: return 0;   /* borderless model: no caption, borders or frames */
    case 19: return 1;                                            /* SM_MOUSEPRESENT: a pointer (touch) exists */
    case 76: case 77: return 0;                                   /* SM_X/YVIRTUALSCREEN: the virtual desktop is the one display */
    case 78: return (uint32_t)g_disp_w; case 79: return (uint32_t)g_disp_h;   /* SM_CX/CYVIRTUALSCREEN */
    case 80: case 81: return 1;                                   /* SM_CMONITORS, SM_SAMEDISPLAYFORMAT */
    case 43: return 3;                                            /* SM_CMOUSEBUTTONS */
    default:
        H2_WARN(H2_LOG_WIN32, "GetSystemMetrics(%u): metric not modelled, returning 0", ARG(0));
        return 0;
    }
}

static uint32_t u_LoadCursorA(CPU *c, const uint32_t *args)
{
    UNUSED;
    /* Stock cursors (hInstance NULL, id IDC_*) get a stable pseudo-handle. Nothing draws a cursor yet. */
    if (!ARG(0) && ARG(1) < 0x10000u) return 0x00010000u | ARG(1);
    g_h2_last_error = 1813;                                       /* ERROR_RESOURCE_TYPE_NOT_FOUND: resource cursors not loaded */
    return 0;
}

static uint32_t u_GetCursorPos(CPU *c, const uint32_t *args)
{
    UNUSED;
    int cx, cy;
    h2_input_cursor_pos(&cx, &cy);
    wr32(ARG(0), (uint32_t)cx); wr32(ARG(0) + 4, (uint32_t)cy);
    return 1;
}

static uint32_t u_ScreenToClient(CPU *c, const uint32_t *args)
{
    UNUSED;
    Window *w = win(ARG(0));
    if (!w) return 0;
    wr32(ARG(1), rd32(ARG(1)) - (uint32_t)w->x); wr32(ARG(1) + 4, rd32(ARG(1) + 4) - (uint32_t)w->y);
    return 1;
}

static uint32_t u_GetKeyboardState(CPU *c, const uint32_t *args) { UNUSED; h2_input_vk_state((uint8_t *)guest_ptr(ARG(0))); return 1; }
static uint32_t u_GetAsyncKeyState(CPU *c, const uint32_t *args)
{
    UNUSED;
    return h2_input_vk_down(ARG(0)) ? 0x8000u : 0;
}

/* ---- message boxes ----
 * There is nobody to click. The text is logged (it is usually the program
 * saying exactly what is wrong) and the DEFAULT button is taken, which is what
 * pressing Enter does. */
static uint32_t u_MessageBoxA(CPU *c, const uint32_t *args)
{
    UNUSED;
    static const uint8_t buttons[6][3] = { {1,0,0}, {1,2,0}, {3,4,5}, {6,7,2}, {6,7,0}, {4,2,0} };   /* OK, OKCANCEL, ABORTRETRYIGNORE, YESNOCANCEL, YESNO, RETRYCANCEL */
    uint32_t type = ARG(3) & 0xF, def = (ARG(3) >> 8) & 3;
    uint32_t answer = type < 6 ? buttons[type][def < 3 ? def : 0] : 1;
    if (!answer) answer = buttons[type][0];
    H2_WARN(H2_LOG_GAME, "MessageBox [%s] \"%s\" (type %X) -> answered %u", ARG(2) ? (const char *)guest_ptr(ARG(2)) : "Error",
            ARG(1) ? (const char *)guest_ptr(ARG(1)) : "", ARG(3), answer);
    return answer;
}

/* SPI: only the screen-saver switches a game toggles. The guest world has no screen saver. */
static uint32_t u_SystemParametersInfoA(CPU *c, const uint32_t *args)
{
    UNUSED;
    switch (ARG(0)) {
    case 16: if (ARG(2)) wr32(ARG(2), 0); return 1;               /* SPI_GETSCREENSAVEACTIVE -> FALSE */
    case 17: case 97: return 1;                                   /* SPI_SETSCREENSAVEACTIVE, SPI_SCREENSAVERRUNNING */
    /* Accessibility shortcuts (FilterKeys 50/51, ToggleKeys 52/53, StickyKeys 58/59). Games read the
     * state, switch the shortcuts off for play, and restore it. None of these features exists in the
     * guest world: reads report every flag clear; writes are accepted and have nothing to change. */
    case 50: case 52: case 58: {
        uint32_t size = ARG(0) == 50 ? 24u : 8u;
        if (!ARG(2) || ARG(1) != size || rd32(ARG(2)) != size) { g_h2_last_error = 87; return 0; }
        memset(guest_ptr(ARG(2) + 4), 0, size - 4);               /* dwFlags (and the rest) = 0 */
        return 1;
    }
    case 51: case 53: case 59: return 1;
    /* Key repeat: the Windows defaults (fastest rate, 250 ms delay). Touch/gamepad sources never auto-repeat. */
    /* Setters are accepted and change nothing: the guest must never alter host system settings. */
    case 11: case 23: return 1;                                   /* SPI_SETKEYBOARDSPEED, SPI_SETKEYBOARDDELAY */
    case 10: if (!ARG(2)) { g_h2_last_error = 87; return 0; } wr32(ARG(2), 31); return 1;   /* SPI_GETKEYBOARDSPEED */
    case 22: if (!ARG(2)) { g_h2_last_error = 87; return 0; } wr32(ARG(2), 1); return 1;    /* SPI_GETKEYBOARDDELAY */
    default:
        h2_guest_stop(H2_STOP_UNIMPLEMENTED_IMPORT, "SystemParametersInfoA action %u", ARG(0));
    }
}

/* ---- wsprintf ---- */
static uint32_t format_guest(uint32_t dst, uint32_t cap, const char *fmt, uint32_t ap)
{
    uint32_t n = 0;
#define PUT(ch) do { if (n + 1 < cap) wr8(dst + n, (uint8_t)(ch)); n++; } while (0)
    for (const char *p = fmt; *p; p++) {
        if (*p != '%') { PUT(*p); continue; }
        char spec[24], tmp[64];
        size_t sl = 0;
        int left = 0, zero = 0, width = 0, prec = -1, is_long = 0;
        const char *start = p++;
        if (*p == '%') { PUT('%'); continue; }
        for (;; p++) { if (*p == '-') left = 1; else if (*p == '0') zero = 1; else if (*p == '#') {} else break; }
        while (*p >= '0' && *p <= '9') width = width * 10 + (*p++ - '0');
        if (*p == '.') { prec = 0; p++; while (*p >= '0' && *p <= '9') prec = prec * 10 + (*p++ - '0'); }
        if (*p == 'l') { is_long = 1; p++; } else if (*p == 'h') p++;
        (void)is_long; (void)spec; (void)sl;
        const char *s = tmp;
        int len;
        uint32_t v = 0;
        switch (*p) {
        case 'd': case 'i': v = rd32(ap); ap += 4; len = snprintf(tmp, sizeof tmp, "%d", (int32_t)v); break;
        case 'u': v = rd32(ap); ap += 4; len = snprintf(tmp, sizeof tmp, "%u", v); break;
        case 'x': v = rd32(ap); ap += 4; len = snprintf(tmp, sizeof tmp, "%x", v); break;
        case 'X': v = rd32(ap); ap += 4; len = snprintf(tmp, sizeof tmp, "%X", v); break;
        case 'c': v = rd32(ap); ap += 4; tmp[0] = (char)v; tmp[1] = 0; len = 1; break;
        case 's': v = rd32(ap); ap += 4; s = v ? (const char *)guest_ptr(v) : "(null)"; len = (int)strlen(s);
                  if (prec >= 0 && len > prec) len = prec; break;
        default:
            h2_guest_stop(H2_STOP_UNIMPLEMENTED_IMPORT, "wsprintf conversion \"%.8s\" in format \"%.80s\"", start, fmt);
        }
        int pad = width > len ? width - len : 0;
        if (!left) while (pad-- > 0) PUT(zero && *p != 's' && *p != 'c' ? '0' : ' ');
        for (int i = 0; i < len; i++) PUT(s[i]);
        if (left) while (pad-- > 0) PUT(' ');
    }
#undef PUT
    if (cap) wr8(dst + (n < cap ? n : cap - 1), 0);
    return n < cap ? n : cap - 1;
}

/* wsprintfA is cdecl: (buf, fmt, ...). Output is limited to 1024 bytes, as documented. */
static uint32_t u_wsprintfA(CPU *c, const uint32_t *args)
{
    return format_guest(ARG(0), 1024, (const char *)guest_ptr(ARG(1)), c->esp + 12);
}
static uint32_t u_wvsprintfA(CPU *c, const uint32_t *args)
{
    UNUSED;
    return format_guest(ARG(0), 1024, (const char *)guest_ptr(ARG(1)), ARG(2));
}

/* ---- geometry, device contexts, cursor, focus (all virtual: nothing touches the host desktop) ---- */
static uint32_t u_GetClientRect(CPU *c, const uint32_t *args)
{
    UNUSED;
    Window *w = win(ARG(0));
    if (!w) { g_h2_last_error = 1400; return 0; }
    wr32(ARG(1), 0); wr32(ARG(1) + 4, 0); wr32(ARG(1) + 8, (uint32_t)w->w); wr32(ARG(1) + 12, (uint32_t)w->h);
    return 1;
}
static uint32_t u_GetWindowRect(CPU *c, const uint32_t *args)
{
    UNUSED;
    Window *w = win(ARG(0));
    if (!w) { g_h2_last_error = 1400; return 0; }
    wr32(ARG(1), (uint32_t)w->x); wr32(ARG(1) + 4, (uint32_t)w->y);
    wr32(ARG(1) + 8, (uint32_t)(w->x + w->w)); wr32(ARG(1) + 12, (uint32_t)(w->y + w->h));
    return 1;
}
static uint32_t u_ClientToScreen(CPU *c, const uint32_t *args)
{
    UNUSED;
    Window *w = win(ARG(0));
    if (!w) return 0;
    wr32(ARG(1), rd32(ARG(1)) + (uint32_t)w->x); wr32(ARG(1) + 4, rd32(ARG(1) + 4) + (uint32_t)w->y);
    return 1;
}
/* Borderless model (GetSystemMetrics reports no frame, caption or menu): window rect == client rect. */
static uint32_t u_AdjustWindowRectEx(CPU *c, const uint32_t *args) { UNUSED; (void)args; return 1; }
static uint32_t u_MoveWindow(CPU *c, const uint32_t *args)
{
    uint32_t a[7] = { ARG(0), 0, ARG(1), ARG(2), ARG(3), ARG(4), 0x4u /* SWP_NOZORDER */ };
    return u_SetWindowPos(c, a);
}
static uint32_t u_GetDesktopWindow(CPU *c, const uint32_t *args) { UNUSED; (void)args; return 0x00010010u; }
static uint32_t u_GetParent(CPU *c, const uint32_t *args)
{
    UNUSED;
    Window *w = win(ARG(0));
    if (!w) { g_h2_last_error = 1400; return 0; }
    return w->parent;
}
static uint32_t u_GetWindowLongA(CPU *c, const uint32_t *args)
{
    UNUSED;
    Window *w = win(ARG(0));
    int32_t idx = (int32_t)ARG(1);
    if (!w) { g_h2_last_error = 1400; return 0; }
    g_h2_last_error = 0;
    switch (idx) {
    case -4: return w->wndproc;  case -16: return w->style;  case -20: return w->exstyle;
    case -21: return w->userdata; case -12: return w->id;    case -6: return w->hinst;   case -8: return w->parent;
    default:
        if (idx >= 0 && idx + 4 <= (int32_t)sizeof w->extra && (uint32_t)idx + 4 <= w->cls->wcx[4]) return w->extra[idx / 4];
        g_h2_last_error = 1413; return 0;
    }
}

/* Device contexts are identities: a DC names a window (or the screen); nothing draws with GDI.
 * The OpenGL layer attaches its context to one by handle value (renderer/). */
static uint32_t u_GetDC(CPU *c, const uint32_t *args)
{
    UNUSED;
    if (ARG(0) && !win(ARG(0))) { g_h2_last_error = 1400; return 0; }
    return 0x00020000u | (ARG(0) & 0xFFFFu);
}
static uint32_t u_ReleaseDC(CPU *c, const uint32_t *args) { UNUSED; return (ARG(1) & 0xFFFF0000u) == 0x00020000u; }

/* Cursor: a counter and a handle, as the API keeps them. The host cursor is never touched. */
static int g_cursor_count;
static uint32_t g_cursor_handle, g_capture, g_clip_active;
static uint32_t u_ShowCursor(CPU *c, const uint32_t *args) { UNUSED; g_cursor_count += ARG(0) ? 1 : -1; return (uint32_t)g_cursor_count; }
static uint32_t u_SetCursor(CPU *c, const uint32_t *args) { UNUSED; uint32_t p = g_cursor_handle; g_cursor_handle = ARG(0); return p; }
static uint32_t u_GetCursor(CPU *c, const uint32_t *args) { UNUSED; (void)args; return g_cursor_handle; }
static uint32_t u_SetCursorPos(CPU *c, const uint32_t *args) { UNUSED; h2_input_cursor((int)ARG(0), (int)ARG(1)); return 1; }
static uint32_t u_ClipCursor(CPU *c, const uint32_t *args) { UNUSED; g_clip_active = ARG(0) != 0; return 1; }
static uint32_t u_SetCapture(CPU *c, const uint32_t *args) { UNUSED; uint32_t p = g_capture; g_capture = ARG(0); return p; }
static uint32_t u_ReleaseCapture(CPU *c, const uint32_t *args) { UNUSED; (void)args; g_capture = 0; return 1; }
static uint32_t u_LoadIconA(CPU *c, const uint32_t *args)
{
    UNUSED;
    /* Icons are never drawn; the handle only has to be a stable non-NULL value. Whether the named
     * resource exists in the image is NOT checked (docs/KNOWN_ISSUES.md). */
    return ARG(1) < 0x10000u ? 0x00030000u | ARG(1) : 0x00030000u | (ARG(1) & 0xFFFFu);
}

/* Focus and activation are guest-level only: SetForegroundWindow here never reaches the host. */
static uint32_t u_SetFocus(CPU *c, const uint32_t *args)
{
    uint32_t prev = g_focus;
    if (ARG(0) && !win(ARG(0))) { g_h2_last_error = 1400; return 0; }
    if (ARG(0) == prev) return prev;
    g_focus = ARG(0);
    if (prev && win(prev)) send(c, prev, 8 /* WM_KILLFOCUS */, ARG(0), 0);
    if (ARG(0) && win(ARG(0))) send(c, ARG(0), WM_SETFOCUS, prev, 0);
    return prev;
}
static uint32_t u_SetForegroundWindow(CPU *c, const uint32_t *args)
{
    UNUSED;
    if (!win(ARG(0))) return 0;
    g_active = ARG(0);
    return 1;
}

/* Display modes: exactly one exists, the virtual display. The real monitor is never changed. */
static uint32_t u_EnumDisplaySettingsA(CPU *c, const uint32_t *args)
{
    UNUSED;
    uint32_t mode = ARG(1), dm = ARG(2);
    if (mode != 0 && mode != 0xFFFFFFFFu && mode != 0xFFFFFFFEu) return 0;     /* no mode #1: enumeration ends */
    /* DEVMODEA is 156 bytes: dmDeviceName[32] is kept, dmSpecVersion..the end (offsets 32..155) is cleared. */
    memset(guest_ptr(dm + 32), 0, 156 - 32);
    wr16(dm + 36, 156);                                                       /* dmSize */
    wr32(dm + 40, 0x40000u | 0x80000u | 0x100000u | 0x400000u);               /* BITSPERPEL | PELSWIDTH | PELSHEIGHT | FREQUENCY */
    wr32(dm + 104, 32); wr32(dm + 108, (uint32_t)g_disp_w); wr32(dm + 112, (uint32_t)g_disp_h); wr32(dm + 120, 60);
    return 1;
}
static uint32_t u_ChangeDisplaySettingsA(CPU *c, const uint32_t *args)
{
    UNUSED;
    if (!ARG(0)) return 0;                                                    /* restore the default: nothing was changed */
    if (rd32(ARG(0) + 108) == (uint32_t)g_disp_w && rd32(ARG(0) + 112) == (uint32_t)g_disp_h) return 0;
    H2_WARN(H2_LOG_WIN32, "ChangeDisplaySettings %ux%u refused: the only mode is the virtual display %dx%d",
            rd32(ARG(0) + 108), rd32(ARG(0) + 112), g_disp_w, g_disp_h);
    return 0xFFFFFFFEu;                                                       /* DISP_CHANGE_BADMODE */
}

/* GDI: capabilities of the virtual display. */
static uint32_t g_GetDeviceCaps(CPU *c, const uint32_t *args)
{
    UNUSED;
    switch (ARG(1)) {
    case 8: return (uint32_t)g_disp_w;  case 10: return (uint32_t)g_disp_h;   /* HORZRES, VERTRES */
    case 12: return 32; case 14: return 1;                                    /* BITSPIXEL, PLANES */
    case 24: return 0xFFFFFFFFu;                                              /* NUMCOLORS: more than 256 */
    case 88: case 90: return 96;                                              /* LOGPIXELSX/Y */
    case 116: return 60;                                                      /* VREFRESH */
    default:
        H2_WARN(H2_LOG_WIN32, "GetDeviceCaps(index %u): not modelled, returning 0", ARG(1));
        return 0;
    }
}

/* GDI stock objects (brushes, pens, fonts) are identities here: the engine
 * passes one as its window-class background and nothing paints with GDI. */
static uint32_t g_GetStockObject(CPU *c, const uint32_t *args)
{
    UNUSED;
    return ARG(0) <= 19 ? 0x01900010u + ARG(0) : 0;
}

static const H2ImportDef k_defs[] = {
    { "GDI32.dll", "GetStockObject", 1, g_GetStockObject },
    { "GDI32.dll", "GetDeviceCaps", 2, g_GetDeviceCaps },
#define U(name, n) { "USER32.dll", #name, n, u_##name }
    U(GetClientRect, 2), U(GetWindowRect, 2), U(ClientToScreen, 2), U(AdjustWindowRectEx, 4), U(MoveWindow, 6),
    U(GetDesktopWindow, 0), U(GetParent, 1), U(GetWindowLongA, 2), U(GetDC, 1), { "USER32.dll", "GetWindowDC", 1, u_GetDC },
    U(ReleaseDC, 2), U(ShowCursor, 1), U(SetCursor, 1), U(GetCursor, 0), U(SetCursorPos, 2), U(ClipCursor, 1),
    U(SetCapture, 1), U(ReleaseCapture, 0), U(LoadIconA, 2), U(SetFocus, 1), U(SetForegroundWindow, 1),
    U(EnumDisplaySettingsA, 3), U(ChangeDisplaySettingsA, 2),
    U(RegisterWindowMessageA, 1), U(GetClassInfoExA, 3), U(RegisterClassExA, 1), U(UnregisterClassA, 2),
    U(CreateWindowExA, 12), U(ShowWindow, 2), U(SetWindowPos, 7), U(DestroyWindow, 1), U(SetWindowTextA, 2),
    U(SetWindowLongA, 3), U(FindWindowA, 2),
    U(PeekMessageA, 5), U(GetMessageA, 4), U(PostMessageA, 4), U(PostQuitMessage, 1), U(SendMessageA, 4),
    U(DispatchMessageA, 1), U(TranslateMessage, 1), U(DefWindowProcA, 4), U(WaitMessage, 0),
    U(GetSystemMetrics, 1), U(LoadCursorA, 2), U(GetCursorPos, 1), U(ScreenToClient, 2),
    U(GetKeyboardState, 1), U(GetAsyncKeyState, 1), U(MessageBoxA, 4), U(SystemParametersInfoA, 4),
    { "USER32.dll", "wsprintfA", H2_CDECL, u_wsprintfA }, U(wvsprintfA, 3),
#undef U
};

void h2_register_user32(void) { h2_imports_register(k_defs, sizeof k_defs / sizeof k_defs[0]); }
