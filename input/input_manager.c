/* input_manager.c - see input_manager.h. Portable C11, no platform headers. */
#include <stdatomic.h>
#include <string.h>
#include <time.h>

#include "input_manager.h"

#define QUEUE 128
typedef struct { H2InputEvent ev[QUEUE]; unsigned head, count, overflow; } Queue;

static atomic_flag g_lock = ATOMIC_FLAG_INIT;
static uint8_t g_dik[256], g_vk[256], g_buttons[8];
static int32_t g_dx, g_dy, g_dz;
static int g_cx, g_cy;
static Queue g_q[2];
static uint32_t g_seq;

static void lock(void) { while (atomic_flag_test_and_set_explicit(&g_lock, memory_order_acquire)) { } }
static void unlock(void) { atomic_flag_clear_explicit(&g_lock, memory_order_release); }

static uint32_t now_ms(void)
{
    struct timespec ts;
#if defined(_WIN32)
    timespec_get(&ts, TIME_UTC);
#else
    clock_gettime(CLOCK_MONOTONIC, &ts);       /* timespec_get needs Android API 29; minSdk is 26 */
#endif
    return (uint32_t)((uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u);
}

static void push(int device, uint32_t offset, uint32_t data)
{
    Queue *q = &g_q[device];
    if (q->count == QUEUE) { q->overflow++; return; }
    H2InputEvent *e = &q->ev[(q->head + q->count++) % QUEUE];
    e->offset = offset; e->data = data; e->time_ms = now_ms(); e->sequence = g_seq++;
}

/* DIK -> VK. Row by row of a US keyboard; unmapped keys are 0. */
unsigned h2_input_dik_to_vk(unsigned dik)
{
    static const uint8_t row1[] = { 0x1B, '1','2','3','4','5','6','7','8','9','0', 0xBD, 0xBB, 0x08, 0x09,
                                    'Q','W','E','R','T','Y','U','I','O','P', 0xDB, 0xDD, 0x0D, 0x11,
                                    'A','S','D','F','G','H','J','K','L', 0xBA, 0xDE, 0xC0, 0x10, 0xDC,
                                    'Z','X','C','V','B','N','M', 0xBC, 0xBE, 0xBF, 0x10, 0x6A, 0x12, 0x20, 0x14,
                                    0x70,0x71,0x72,0x73,0x74,0x75,0x76,0x77,0x78,0x79 };   /* DIK 0x01..0x44 */
    if (dik >= 0x01 && dik <= 0x44) return row1[dik - 1];
    switch (dik) {
    case 0x57: return 0x7A; case 0x58: return 0x7B;                 /* F11, F12 */
    case 0x9C: return 0x0D; case 0x9D: return 0x11; case 0xB8: return 0x12;   /* numpad enter, right ctrl, right alt */
    case 0xC7: return 0x24; case 0xC8: return 0x26; case 0xC9: return 0x21;   /* home, up, page up */
    case 0xCB: return 0x25; case 0xCD: return 0x27;                 /* left, right */
    case 0xCF: return 0x23; case 0xD0: return 0x28; case 0xD1: return 0x22;   /* end, down, page down */
    case 0xD2: return 0x2D; case 0xD3: return 0x2E;                 /* insert, delete */
    default:   return 0;
    }
}

void h2_input_key(unsigned dik, int down)
{
    if (dik > 255) return;
    lock();
    if (((g_dik[dik] & 0x80) != 0) != (down != 0)) {
        unsigned vk = h2_input_dik_to_vk(dik);
        g_dik[dik] = down ? 0x80 : 0;
        if (vk) {
            if (down) g_vk[vk] ^= 1;                                /* toggle state flips on each press */
            g_vk[vk] = (uint8_t)((g_vk[vk] & 1) | (down ? 0x80 : 0));
        }
        push(0, dik, down ? 0x80 : 0);
    }
    unlock();
}

void h2_input_mouse_move(int dx, int dy)
{
    lock();
    g_dx += dx; g_dy += dy;
    if (dx) push(1, 0, (uint32_t)dx);                               /* DIMOFS_X */
    if (dy) push(1, 4, (uint32_t)dy);                               /* DIMOFS_Y */
    unlock();
}

void h2_input_mouse_wheel(int dz)
{
    lock();
    g_dz += dz;
    if (dz) push(1, 8, (uint32_t)dz);                               /* DIMOFS_Z */
    unlock();
}

void h2_input_mouse_button(unsigned index, int down)
{
    static const uint8_t vk[3] = { 0x01, 0x02, 0x04 };              /* VK_LBUTTON, VK_RBUTTON, VK_MBUTTON */
    if (index > 7) return;
    lock();
    if (((g_buttons[index] & 0x80) != 0) != (down != 0)) {
        g_buttons[index] = down ? 0x80 : 0;
        if (index < 3) g_vk[vk[index]] = down ? 0x80 : 0;
        push(1, 12 + index, down ? 0x80 : 0);                       /* DIMOFS_BUTTON0 + n */
    }
    unlock();
}

void h2_input_cursor(int x, int y) { lock(); g_cx = x; g_cy = y; unlock(); }

void h2_input_reset(void)
{
    lock();
    memset(g_dik, 0, sizeof g_dik); memset(g_vk, 0, sizeof g_vk); memset(g_buttons, 0, sizeof g_buttons);
    g_dx = g_dy = g_dz = 0; memset(g_q, 0, sizeof g_q);
    unlock();
}

void h2_input_keyboard_state(uint8_t out[256]) { lock(); memcpy(out, g_dik, 256); unlock(); }
void h2_input_vk_state(uint8_t out[256]) { lock(); memcpy(out, g_vk, 256); unlock(); }
int  h2_input_vk_down(unsigned vk) { int d; lock(); d = vk < 256 && (g_vk[vk] & 0x80); unlock(); return d; }
void h2_input_cursor_pos(int *x, int *y) { lock(); *x = g_cx; *y = g_cy; unlock(); }

void h2_input_mouse_state(int32_t *dx, int32_t *dy, int32_t *dz, uint8_t buttons[8])
{
    lock();
    *dx = g_dx; *dy = g_dy; *dz = g_dz; g_dx = g_dy = g_dz = 0;
    memcpy(buttons, g_buttons, 8);
    unlock();
}

unsigned h2_input_take_events(int device, H2InputEvent *out, unsigned max, int remove)
{
    unsigned n;
    if (device < 0 || device > 1) return 0;
    lock();
    Queue *q = &g_q[device];
    n = q->count < max ? q->count : max;
    for (unsigned i = 0; i < n && out; i++) out[i] = q->ev[(q->head + i) % QUEUE];
    if (remove) { q->head = (q->head + n) % QUEUE; q->count -= n; }
    unlock();
    return n;
}

unsigned h2_input_overflowed(int device, int clear)
{
    unsigned n;
    if (device < 0 || device > 1) return 0;
    lock(); n = g_q[device].overflow; if (clear) g_q[device].overflow = 0; unlock();
    return n;
}
