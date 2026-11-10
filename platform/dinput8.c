/*
 * dinput8.c - DirectInput 8 for the guest, backed by InputManager.
 *
 * COM in guest memory: an interface pointer is the guest address of an object
 * whose first dword is the address of a vtable, and each vtable slot holds a
 * thunk address bound through the import registry ("DINPUT8.dll#Interface",
 * "Method"). Lifted code does `mov ecx,[obj]; call [ecx+N]` and lands in the
 * functions below exactly like an imported function - and a method that is
 * not implemented stops the guest naming the interface and the method.
 *
 * Devices: the system keyboard and the system mouse. Game controllers are not
 * exposed through DirectInput yet (EnumDevices is not implemented).
 * The cooperative level is accepted and has no effect: there is no other
 * application to share the devices with, and nothing is ever grabbed on the host.
 */
#include <string.h>

#include "h2_platform.h"
#include "input_manager.h"

#define ARG(i) (args[(i)])
#define UNUSED (void)c
#define DI_OK 0u
#define DI_NOEFFECT 1u
#define DI_BUFFEROVERFLOW 1u
#define E_INVALIDARG 0x80070057u
#define E_NOINTERFACE 0x80004002u
#define DIERR_DEVICENOTREG 0x80040154u
#define DIERR_NOTACQUIRED 0x8007000Cu
#define DIERR_NOTBUFFERED 0x80040207u   /* reported when GetDeviceData is used without a buffer size */

/* Guest object layouts (our own; the game only ever sees the vtable pointer). */
enum { OBJ_VTBL = 0, OBJ_REFS = 4, DEV_KIND = 8, DEV_DATASIZE = 12, DEV_BUFSIZE = 16, DEV_ACQUIRED = 20, OBJ_SIZE = 24 };

static const char *const k_di_methods[] = {
    "QueryInterface", "AddRef", "Release", "CreateDevice", "EnumDevices", "GetDeviceStatus", "RunControlPanel",
    "Initialize", "FindDevice", "EnumDevicesBySemantics", "ConfigureDevices" };
static const char *const k_dev_methods[] = {
    "QueryInterface", "AddRef", "Release", "GetCapabilities", "EnumObjects", "GetProperty", "SetProperty", "Acquire",
    "Unacquire", "GetDeviceState", "GetDeviceData", "SetDataFormat", "SetEventNotification", "SetCooperativeLevel",
    "GetObjectInfo", "GetDeviceInfo", "RunControlPanel", "Initialize", "CreateEffect", "EnumEffects", "GetEffectInfo",
    "GetForceFeedbackState", "SendForceFeedbackCommand", "EnumCreatedEffectObjects", "Escape", "Poll", "SendDeviceData",
    "EnumEffectsInFile", "WriteEffectToFile", "BuildActionMap", "SetActionMap", "GetImageInfo" };

static uint32_t g_vtbl_di, g_vtbl_dev;

static uint32_t build_vtable(const char *iface, const char *const *names, unsigned n)
{
    uint32_t v = h2_os_alloc(4u * n);
    for (unsigned i = 0; i < n; i++) wr32(v + 4u * i, h2_import_bind(iface, names[i]));
    return v;
}

/* ---- shared IUnknown ---- */
static uint32_t com_AddRef(CPU *c, const uint32_t *args) { UNUSED; uint32_t n = rd32(ARG(0) + OBJ_REFS) + 1; wr32(ARG(0) + OBJ_REFS, n); return n; }
static uint32_t com_Release(CPU *c, const uint32_t *args)
{
    UNUSED;
    uint32_t n = rd32(ARG(0) + OBJ_REFS);
    if (n) wr32(ARG(0) + OBJ_REFS, --n);
    if (!n) h2_heap_free(ARG(0));
    return n;
}

/* ---- IDirectInput8A ---- */
static const uint8_t k_guid_tail[12] = { 0xA0, 0xD5, 0xCF, 0x11, 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00 };

static uint32_t di_CreateDevice(CPU *c, const uint32_t *args)
{
    UNUSED;
    uint32_t guid = ARG(1), out = ARG(2);
    if (!guid || !out) return E_INVALIDARG;
    /* GUID_SysMouse 6F1D2B60-D5A0-11CF-BFC7-444553540000, GUID_SysKeyboard ...61-... */
    uint32_t d1 = rd32(guid);
    if ((d1 != 0x6F1D2B60u && d1 != 0x6F1D2B61u) || memcmp(guest_ptr(guid + 4), k_guid_tail, 12) != 0) {
        H2_WARN(H2_LOG_INPUT, "DirectInput CreateDevice: device %08X-... is not available (only system keyboard and mouse)", d1);
        wr32(out, 0);
        return DIERR_DEVICENOTREG;
    }
    uint32_t dev = h2_heap_alloc(OBJ_SIZE, 1);
    if (!dev) return 0x8007000Eu;                                 /* E_OUTOFMEMORY */
    wr32(dev + OBJ_VTBL, g_vtbl_dev); wr32(dev + OBJ_REFS, 1);
    wr32(dev + DEV_KIND, d1 == 0x6F1D2B61u ? 0u : 1u);            /* 0 keyboard, 1 mouse */
    wr32(out, dev);
    H2_INFO(H2_LOG_INPUT, "DirectInput device created: %s", d1 == 0x6F1D2B61u ? "keyboard" : "mouse");
    return DI_OK;
}

/* EnumDevices(this, dwDevType, callback, pvRef, flags): calls the guest's
 * BOOL CALLBACK(const DIDEVICEINSTANCEA *, void *) once per matching device.
 * Classes: 0 all, 1 other, 2 pointer, 3 keyboard, 4 game controller; or an
 * exact DI8DEVTYPE (0x12 mouse, 0x13 keyboard). No game controllers exist here. */
static uint32_t di_EnumDevices(CPU *c, const uint32_t *args)
{
    static uint32_t inst;                                        /* 580-byte DIDEVICEINSTANCEA, reused */
    uint32_t type = ARG(1) & 0xFF, cb = ARG(2), ref = ARG(3);
    if (!cb) return E_INVALIDARG;
    if (!inst) inst = h2_os_alloc(580);
    for (int kind = 1; kind >= 0; kind--) {                      /* mouse first, then keyboard (the system's order) */
        uint32_t devtype = kind ? 0x12u : 0x13u, cls = kind ? 2u : 3u;
        if (type != 0 && type != cls && type != devtype) continue;
        memset(guest_ptr(inst), 0, 580);
        wr32(inst, 580);
        for (int g = 0; g < 2; g++) {                            /* guidInstance, guidProduct */
            wr32(inst + 4 + 16u * (uint32_t)g, kind ? 0x6F1D2B60u : 0x6F1D2B61u);
            memcpy(guest_ptr(inst + 8 + 16u * (uint32_t)g), k_guid_tail, 12);
        }
        wr32(inst + 36, devtype | (kind ? 0x0200u : 0x0400u));   /* traditional mouse / enhanced PC keyboard */
        strcpy((char *)guest_ptr(inst + 40), kind ? "Mouse" : "Keyboard");
        strcpy((char *)guest_ptr(inst + 300), kind ? "Mouse" : "Keyboard");
        if (h2_call_guest(c, cb, 2, inst, ref) == 0) break;      /* DIENUM_STOP */
    }
    return DI_OK;
}

/* ---- IDirectInputDevice8A ---- */
static uint32_t dev_SetDataFormat(CPU *c, const uint32_t *args)
{
    UNUSED;
    uint32_t fmt = ARG(1);
    if (!fmt || rd32(fmt) != 24) return E_INVALIDARG;             /* DIDATAFORMAT.dwSize */
    uint32_t size = rd32(fmt + 12), kind = rd32(ARG(0) + DEV_KIND);
    /* c_dfDIKeyboard is 256 bytes; c_dfDIMouse 16, c_dfDIMouse2 20. Custom formats are not translated. */
    if ((kind == 0 && size != 256) || (kind == 1 && size != 16 && size != 20))
        h2_guest_stop(H2_STOP_UNIMPLEMENTED_IMPORT, "DirectInput SetDataFormat: custom data format (%u bytes) for %s",
                      size, kind ? "mouse" : "keyboard");
    wr32(ARG(0) + DEV_DATASIZE, size);
    return DI_OK;
}

static uint32_t dev_SetCooperativeLevel(CPU *c, const uint32_t *args) { UNUSED; (void)args; return DI_OK; }

static uint32_t dev_SetProperty(CPU *c, const uint32_t *args)
{
    UNUSED;
    uint32_t prop = ARG(1), hdr = ARG(2);
    if (prop == 1) {                                              /* DIPROP_BUFFERSIZE: DIPROPDWORD */
        if (!hdr || rd32(hdr) != 20) return E_INVALIDARG;
        wr32(ARG(0) + DEV_BUFSIZE, rd32(hdr + 16));
        return DI_OK;
    }
    if (prop == 2) {                                              /* DIPROP_AXISMODE: relative is what the mouse reports */
        if (!hdr || rd32(hdr) != 20) return E_INVALIDARG;
        if (rd32(hdr + 16) != 1 /* DIPROPAXISMODE_REL */)
            h2_guest_stop(H2_STOP_UNIMPLEMENTED_IMPORT, "DirectInput: absolute axis mode");
        return DI_OK;
    }
    h2_guest_stop(H2_STOP_UNIMPLEMENTED_IMPORT, "DirectInput SetProperty(%u)", prop);
}

static uint32_t dev_Acquire(CPU *c, const uint32_t *args)
{
    UNUSED;
    if (!rd32(ARG(0) + DEV_DATASIZE)) return E_INVALIDARG;        /* no data format set */
    if (rd32(ARG(0) + DEV_ACQUIRED)) return DI_NOEFFECT;
    wr32(ARG(0) + DEV_ACQUIRED, 1);
    return DI_OK;
}
static uint32_t dev_Unacquire(CPU *c, const uint32_t *args)
{
    UNUSED;
    if (!rd32(ARG(0) + DEV_ACQUIRED)) return DI_NOEFFECT;
    wr32(ARG(0) + DEV_ACQUIRED, 0);
    return DI_OK;
}
static uint32_t dev_Poll(CPU *c, const uint32_t *args)
{
    UNUSED;
    return rd32(ARG(0) + DEV_ACQUIRED) ? DI_NOEFFECT : DIERR_NOTACQUIRED;   /* nothing needs polling */
}

static uint32_t dev_GetDeviceState(CPU *c, const uint32_t *args)
{
    UNUSED;
    uint32_t dev = ARG(0), cb = ARG(1), out = ARG(2);
    if (!rd32(dev + DEV_ACQUIRED)) return DIERR_NOTACQUIRED;
    if (!out || cb != rd32(dev + DEV_DATASIZE)) return E_INVALIDARG;
    if (rd32(dev + DEV_KIND) == 0) {
        h2_input_keyboard_state((uint8_t *)guest_ptr(out));
    } else {
        int32_t dx, dy, dz; uint8_t b[8];
        h2_input_mouse_state(&dx, &dy, &dz, b);
        wr32(out, (uint32_t)dx); wr32(out + 4, (uint32_t)dy); wr32(out + 8, (uint32_t)dz);
        memcpy(guest_ptr(out + 12), b, cb - 12);                  /* 4 buttons (DIMOUSESTATE) or 8 (DIMOUSESTATE2) */
    }
    return DI_OK;
}

static uint32_t dev_GetDeviceData(CPU *c, const uint32_t *args)
{
    UNUSED;
    uint32_t dev = ARG(0), cb = ARG(1), out = ARG(2), pcount = ARG(3), flags = ARG(4);
    H2InputEvent ev[128];
    if (!rd32(dev + DEV_ACQUIRED)) return DIERR_NOTACQUIRED;
    if (!rd32(dev + DEV_BUFSIZE)) return DIERR_NOTBUFFERED;
    if (cb != 16 || !pcount) return E_INVALIDARG;                 /* sizeof(DIDEVICEOBJECTDATA) in DirectX 8 */
    int device = (int)rd32(dev + DEV_KIND);
    uint32_t want = rd32(pcount);
    if (want > 128) want = 128;
    unsigned n = h2_input_take_events(device, ev, want, !(flags & 1) /* DIGDD_PEEK */);
    for (unsigned i = 0; i < n && out; i++) {
        uint32_t p = out + 16u * i;
        wr32(p, ev[i].offset); wr32(p + 4, ev[i].data); wr32(p + 8, ev[i].time_ms); wr32(p + 12, ev[i].sequence);
    }
    wr32(pcount, n);
    return h2_input_overflowed(device, 1) ? DI_BUFFEROVERFLOW : DI_OK;
}

/* ---- DirectInput8Create ---- */
static uint32_t di_DirectInput8Create(CPU *c, const uint32_t *args)
{
    UNUSED;
    /* (hinst, dwVersion, riid, ppvOut, punkOuter). IID_IDirectInput8A = BF798030-483A-4DA2-AA99-5D64ED369700 */
    if (!ARG(3)) return E_INVALIDARG;
    if (!ARG(2) || rd32(ARG(2)) != 0xBF798030u) {
        H2_WARN(H2_LOG_INPUT, "DirectInput8Create: interface %08X-... not supported (only IDirectInput8A)", ARG(2) ? rd32(ARG(2)) : 0);
        wr32(ARG(3), 0);
        return E_NOINTERFACE;
    }
    if (!g_vtbl_di) {
        g_vtbl_di = build_vtable("DINPUT8.dll#IDirectInput8A", k_di_methods, sizeof k_di_methods / sizeof *k_di_methods);
        g_vtbl_dev = build_vtable("DINPUT8.dll#IDirectInputDevice8A", k_dev_methods, sizeof k_dev_methods / sizeof *k_dev_methods);
    }
    uint32_t obj = h2_heap_alloc(OBJ_SIZE, 1);
    if (!obj) return 0x8007000Eu;
    wr32(obj + OBJ_VTBL, g_vtbl_di); wr32(obj + OBJ_REFS, 1);
    wr32(ARG(3), obj);
    H2_INFO(H2_LOG_INPUT, "DirectInput 8 created (version %04X)", ARG(1));
    return DI_OK;
}

static const H2ImportDef k_defs[] = {
    { "DINPUT8.dll", "DirectInput8Create", 5, di_DirectInput8Create },
#define DI(name, n)  { "DINPUT8.dll#IDirectInput8A", #name, n, di_##name }
#define DEV(name, n) { "DINPUT8.dll#IDirectInputDevice8A", #name, n, dev_##name }
    { "DINPUT8.dll#IDirectInput8A", "AddRef", 1, com_AddRef }, { "DINPUT8.dll#IDirectInput8A", "Release", 1, com_Release },
    DI(CreateDevice, 4), DI(EnumDevices, 5),
    { "DINPUT8.dll#IDirectInputDevice8A", "AddRef", 1, com_AddRef }, { "DINPUT8.dll#IDirectInputDevice8A", "Release", 1, com_Release },
    DEV(SetDataFormat, 2), DEV(SetCooperativeLevel, 3), DEV(SetProperty, 3), DEV(Acquire, 1), DEV(Unacquire, 1),
    DEV(Poll, 1), DEV(GetDeviceState, 3), DEV(GetDeviceData, 5),
#undef DI
#undef DEV
};

void h2_register_dinput8(void) { h2_imports_register(k_defs, sizeof k_defs / sizeof k_defs[0]); }
