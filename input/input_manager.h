/*
 * input_manager.h - InputManager: one place where every input source lands.
 *
 * Sources (anything may call the h2_input_* feeders, from any layer):
 *     Android touch HUD, gamepads, a PC test window, synthetic test scripts.
 * Consumers (the Win32 layer the game talks to):
 *     DirectInput 8 keyboard/mouse (immediate state and buffered events),
 *     GetKeyboardState / GetAsyncKeyState / GetCursorPos, window messages.
 *
 * Keys are identified by DirectInput scan codes (DIK_*), which is what the
 * game's own key bindings (keyboard.cfg "K_W", ...) resolve to. Nothing here
 * touches the host's real keyboard or mouse: on the PC it is fed only by
 * tests, never by global hooks.
 *
 * Thread model: feeders may be called from another host thread (the Android UI
 * thread); consumers run on the guest thread. A small spin lock guards the state.
 */
#ifndef H2_INPUT_MANAGER_H
#define H2_INPUT_MANAGER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The DIK codes sources are most likely to need; any 0..255 value is accepted. */
enum {
    H2K_ESCAPE = 0x01, H2K_1 = 0x02, H2K_2 = 0x03, H2K_3 = 0x04, H2K_4 = 0x05, H2K_5 = 0x06, H2K_6 = 0x07,
    H2K_7 = 0x08, H2K_8 = 0x09, H2K_9 = 0x0A, H2K_0 = 0x0B, H2K_BACK = 0x0E, H2K_TAB = 0x0F,
    H2K_Q = 0x10, H2K_W = 0x11, H2K_E = 0x12, H2K_R = 0x13, H2K_T = 0x14, H2K_RETURN = 0x1C, H2K_LCONTROL = 0x1D,
    H2K_A = 0x1E, H2K_S = 0x1F, H2K_D = 0x20, H2K_F = 0x21, H2K_G = 0x22, H2K_LSHIFT = 0x2A,
    H2K_Z = 0x2C, H2K_X = 0x2D, H2K_C = 0x2E, H2K_V = 0x2F, H2K_M = 0x32, H2K_LALT = 0x38, H2K_SPACE = 0x39,
    H2K_CAPSLOCK = 0x3A, H2K_F1 = 0x3B, H2K_UP = 0xC8, H2K_LEFT = 0xCB, H2K_RIGHT = 0xCD, H2K_DOWN = 0xD0
};

typedef struct { uint32_t offset, data, time_ms, sequence; } H2InputEvent;   /* DIDEVICEOBJECTDATA fields */

/* ---- feeders (sources) ---- */
void h2_input_key(unsigned dik, int down);
void h2_input_mouse_move(int dx, int dy);             /* relative motion (camera) */
void h2_input_mouse_button(unsigned index, int down); /* 0 left, 1 right, 2 middle, 3.. extra */
void h2_input_mouse_wheel(int dz);
void h2_input_cursor(int x, int y);                   /* absolute pointer, for menus */
void h2_input_reset(void);

/* ---- consumers ---- */
void h2_input_keyboard_state(uint8_t out[256]);                 /* DIK-indexed, 0x80 = down */
void h2_input_vk_state(uint8_t out[256]);                       /* VK-indexed: bit 7 down, bit 0 toggled */
int  h2_input_vk_down(unsigned vk);
void h2_input_cursor_pos(int *x, int *y);
/* Mouse motion accumulated since the previous call, and current buttons (0x80 = down). */
void h2_input_mouse_state(int32_t *dx, int32_t *dy, int32_t *dz, uint8_t buttons[8]);
/* Buffered events, oldest first. device: 0 keyboard, 1 mouse. Returns how many were written. */
unsigned h2_input_take_events(int device, H2InputEvent *out, unsigned max, int remove);
/* How many events were dropped because a buffer was full (reported to the game as DI_BUFFEROVERFLOW). */
unsigned h2_input_overflowed(int device, int clear);

/* VK code for a DIK code, or 0 if there is no mapping. */
unsigned h2_input_dik_to_vk(unsigned dik);

#ifdef __cplusplus
}
#endif
#endif
