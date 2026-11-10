/*
 * dsound.c - DirectSound for the guest (the audio layer's front door).
 *
 * Done so far: DirectSoundEnumerateA (ordinal 2 of DSOUND.dll, which is what hitman2.exe imports).
 * Not done: IDirectSound8 / IDirectSoundBuffer8 / 3D listener and buffer COM objects, and the EAX layer
 * (EAX.DLL ordinal 6 = EAXDirectSoundCreate8). Calling any of them stops the guest by name.
 *
 * The planned design: guest-memory COM objects like dinput8.c; software mixing into an audio backend
 * interface (Android: AAudio/Oboe; PC host: a null sink), driven by the buffers the game fills.
 *
 * Devices: exactly one virtual output, reported as the primary driver. No capture devices.
 */
#include "h2_platform.h"

#define ARG(i) (args[(i)])
#define UNUSED (void)c

/* HRESULT DirectSoundEnumerateA(LPDSENUMCALLBACKA callback, LPVOID context)
 * The callback receives (GUID *guid, const char *description, const char *module, void *context); a NULL
 * guid is the primary driver. It returns FALSE to stop the enumeration. */
static uint32_t ds_DirectSoundEnumerateA(CPU *c, const uint32_t *args)
{
    if (!ARG(0)) return 0x80070057u;                              /* E_INVALIDARG */
    static uint32_t desc, module;
    if (!desc) { desc = h2_os_strdup("Primary Sound Driver"); module = h2_os_strdup(""); }
    H2_INFO(H2_LOG_AUDIO, "DirectSoundEnumerateA: offering the virtual output device");
    h2_call_guest(c, ARG(0), 4, 0u, desc, module, ARG(1));
    return 0;                                                     /* DS_OK */
}

static const H2ImportDef k_defs[] = {
    { "DSOUND.dll", "#2", 2, ds_DirectSoundEnumerateA },
};

void h2_register_dsound(void) { h2_imports_register(k_defs, sizeof k_defs / sizeof k_defs[0]); }
