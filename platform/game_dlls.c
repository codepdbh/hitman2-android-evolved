/*
 * game_dlls.c - third-party DLLs the game imports that are answered natively
 * instead of being recompiled.
 *
 * P5DLL.dll - driver library for the Essential Reality P5 glove (2002).
 *   The port has no such device. The four functions hitman2.exe imports are
 *   answered as the real library behaves with no glove attached. What each
 *   original function does was read from the DLL's own code:
 *
 *     CP5DLL::CP5DLL()            10003CA0  mov eax,ecx; mov [eax],0; mov [eax+4],0; ret
 *                                 -> reproduced exactly (two zeroed dwords, returns this)
 *     CP5DLL::~CP5DLL()           10003CB0  closes the library's device list (a global of the DLL)
 *                                 -> nothing was opened, nothing to close
 *     CP5DLL::P5_Init()           10003CC0  enumerates HID devices looking for gloves
 *                                 -> returns FALSE; the object keeps its zeroed device pointer/count.
 *                                    (Return value with no glove is from the P5 SDK's contract, not
 *                                    from tracing the HID enumeration - recorded as an assumption in
 *                                    docs/KNOWN_ISSUES.md.)
 *     CP5DLL::P5_SetMouseState()  10003CF0  forwards to the device with that index
 *                                 -> there is no device to forward to
 *
 * These are thiscall: `this` arrives in ECX, stack arguments are callee-popped.
 */
#include "h2_platform.h"

static uint32_t p5_ctor(CPU *c, const uint32_t *args)
{
    (void)args;
    wr32(c->ecx, 0); wr32(c->ecx + 4, 0);
    return c->ecx;
}
static uint32_t p5_dtor(CPU *c, const uint32_t *args) { (void)args; return c->eax; }   /* void: EAX untouched */
static uint32_t p5_init(CPU *c, const uint32_t *args)
{
    (void)c; (void)args;
    H2_INFO(H2_LOG_INPUT, "P5 glove: none (P5_Init -> FALSE)");
    return 0;
}
static uint32_t p5_set_mouse_state(CPU *c, const uint32_t *args) { (void)args; return c->eax; }

static const H2ImportDef k_defs[] = {
    { "P5DLL.dll", "??0CP5DLL@@QAE@XZ", 0, p5_ctor },
    { "P5DLL.dll", "??1CP5DLL@@QAE@XZ", 0, p5_dtor },
    { "P5DLL.dll", "?P5_Init@CP5DLL@@QAEIXZ", 0, p5_init },
    { "P5DLL.dll", "?P5_SetMouseState@CP5DLL@@QAEXHI@Z", 2, p5_set_mouse_state },
};

void h2_register_game_dlls(void) { h2_imports_register(k_defs, sizeof k_defs / sizeof k_defs[0]); }
