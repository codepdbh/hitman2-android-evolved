/*
 * dsound.c - DirectSound 8 for the guest (the audio layer's front door).
 *
 * Done: DirectSoundEnumerateA (DSOUND ordinal 2), EAX.DLL ordinal 6 (EAXDirectSoundCreate8) answered as a plain
 * DirectSound 8 device without any EAX effects, IDirectSound8 (CreateSoundBuffer, GetCaps, SetCooperativeLevel,
 * speaker config, Compact) and IDirectSoundBuffer8 (primary and secondary: Lock/Unlock, Play/Stop, status, cursors,
 * volume/pan/frequency, format).
 * Not done: 3D listener and 3D buffers, IKsPropertySet / EAX properties, effects (SetFX), DuplicateSoundBuffer,
 * notifications. QueryInterface for those answers E_NOINTERFACE (logged); calling an unimplemented method stops the
 * guest naming it.
 *
 * Audio model: there is no mixer and no output yet. The device is a *null sink with a real clock*: a playing
 * buffer's play cursor advances at the buffer's byte rate in real time, loops or stops at the end exactly as the
 * hardware would, and the written audio data is kept in guest memory. Nothing is audible. The mixer feeding an
 * Android audio backend replaces this clock later (docs/KNOWN_ISSUES.md).
 *
 * COM in guest memory exactly as in dinput8.c: object = { vtable ptr, refcount, slot }, vtable slots are thunks.
 */
#include <string.h>

#include "h2_platform.h"

#define ARG(i) (args[(i)])
#define UNUSED (void)c

#define DS_OK 0u
#define DSERR_INVALIDPARAM 0x80070057u
#define DSERR_OUTOFMEMORY 0x8007000Eu
#define DSERR_NOINTERFACE 0x80004002u
#define DSERR_UNSUPPORTED 0x80004001u
#define DSERR_BUFFERLOST 0x88780096u
#define DSERR_INVALIDCALL 0x88780032u
#define DSERR_PRIOLEVELNEEDED 0x88780046u
#define DSERR_UNINITIALIZED 0x887800AFu

#define DSBCAPS_PRIMARYBUFFER 0x00000001u
#define DSBCAPS_CTRL3D 0x00000010u
#define DSBCAPS_CTRLFX 0x00000200u
#define DSBPLAY_LOOPING 0x00000001u
#define DSBSTATUS_PLAYING 0x00000001u
#define DSBSTATUS_LOOPING 0x00000004u
#define DSBLOCK_FROMWRITECURSOR 0x00000001u
#define DSBLOCK_ENTIREBUFFER 0x00000002u

/* guest object layout */
enum { OBJ_VTBL = 0, OBJ_REFS = 4, OBJ_SLOT = 8, OBJ_SIZE = 12 };

typedef struct {
    int      used, primary;
    uint32_t flags, size, data;           /* DSBCAPS flags, bytes, guest address of the sample data */
    uint32_t freq, align;                 /* from the WAVEFORMATEX: samples/s, bytes per sample frame */
    uint32_t channels, bits, tag;
    int32_t  volume, pan;
    uint32_t pos;                         /* play cursor at 'since' */
    uint64_t since;                       /* host ns when 'pos' was captured while playing */
    int      playing, looping;
    uint32_t p3d[15];                     /* 3D parameters as raw float dwords: listener (primary) or 3D buffer (secondary) */
} Buf;

enum { P3_POS = 0, P3_VEL = 3 };          /* both layouts start with position, velocity */
enum { L_FRONT = 6, L_TOP = 9, L_DIST = 12, L_DOPPLER = 13, L_ROLLOFF = 14 };
enum { B_INSIDE = 6, B_OUTSIDE = 7, B_CONEDIR = 8, B_OUTVOL = 11, B_MIN = 12, B_MAX = 13, B_MODE = 14 };
#define F_ONE 0x3F800000u

static void init_3d_defaults(Buf *b)
{
    memset(b->p3d, 0, sizeof b->p3d);
    if (b->primary) {                     /* DS3DLISTENER defaults */
        b->p3d[L_FRONT + 2] = F_ONE; b->p3d[L_TOP + 1] = F_ONE;
        b->p3d[L_DIST] = F_ONE; b->p3d[L_DOPPLER] = F_ONE; b->p3d[L_ROLLOFF] = F_ONE;
    } else {                              /* DS3DBUFFER defaults */
        b->p3d[B_INSIDE] = 360; b->p3d[B_OUTSIDE] = 360; b->p3d[B_CONEDIR + 2] = F_ONE;
        b->p3d[B_MIN] = F_ONE; b->p3d[B_MAX] = 0x4E6E6B28u /* 1e9 */;
    }
}

#define MAX_BUFS 256
static Buf g_bufs[MAX_BUFS];
static uint32_t g_vtbl_ds, g_vtbl_buf, g_vtbl_l3d, g_vtbl_b3d;
static const char *const k_l3d_methods[] = {
    "QueryInterface", "AddRef", "Release", "GetAllParameters", "GetDistanceFactor", "GetDopplerFactor", "GetOrientation",
    "GetPosition", "GetRolloffFactor", "GetVelocity", "SetAllParameters", "SetDistanceFactor", "SetDopplerFactor",
    "SetOrientation", "SetPosition", "SetRolloffFactor", "SetVelocity", "CommitDeferredSettings" };
static const char *const k_b3d_methods[] = {
    "QueryInterface", "AddRef", "Release", "GetAllParameters", "GetConeAngles", "GetConeOrientation", "GetConeOutsideVolume",
    "GetMaxDistance", "GetMinDistance", "GetMode", "GetPosition", "GetVelocity", "SetAllParameters", "SetConeAngles",
    "SetConeOrientation", "SetConeOutsideVolume", "SetMaxDistance", "SetMinDistance", "SetMode", "SetPosition", "SetVelocity" };
static uint32_t g_device;                 /* the one device object */
static uint32_t g_coop_level, g_coop_hwnd;
static unsigned g_live_bufs;

static const char *const k_ds_methods[] = {
    "QueryInterface", "AddRef", "Release", "CreateSoundBuffer", "GetCaps", "DuplicateSoundBuffer", "SetCooperativeLevel",
    "Compact", "GetSpeakerConfig", "SetSpeakerConfig", "Initialize", "VerifyCertification" };
static const char *const k_buf_methods[] = {
    "QueryInterface", "AddRef", "Release", "GetCaps", "GetCurrentPosition", "GetFormat", "GetVolume", "GetPan",
    "GetFrequency", "GetStatus", "Initialize", "Lock", "Play", "SetCurrentPosition", "SetFormat", "SetVolume", "SetPan",
    "SetFrequency", "Stop", "Unlock", "Restore", "SetFX", "AcquireResources", "GetObjectInPath" };

static uint32_t build_vtable(const char *iface, const char *const *names, unsigned n)
{
    uint32_t v = h2_os_alloc(4u * n);
    for (unsigned i = 0; i < n; i++) wr32(v + 4u * i, h2_import_bind(iface, names[i]));
    return v;
}

static void ensure_vtables(void)
{
    if (g_vtbl_ds) return;
    g_vtbl_ds = build_vtable("DSOUND.dll#IDirectSound8", k_ds_methods, sizeof k_ds_methods / sizeof *k_ds_methods);
    g_vtbl_buf = build_vtable("DSOUND.dll#IDirectSoundBuffer8", k_buf_methods, sizeof k_buf_methods / sizeof *k_buf_methods);
    g_vtbl_l3d = build_vtable("DSOUND.dll#IDirectSound3DListener", k_l3d_methods, sizeof k_l3d_methods / sizeof *k_l3d_methods);
    g_vtbl_b3d = build_vtable("DSOUND.dll#IDirectSound3DBuffer", k_b3d_methods, sizeof k_b3d_methods / sizeof *k_b3d_methods);
}

static int guid_is(uint32_t g, uint32_t d1, uint16_t d2, uint16_t d3)
{
    return g && rd32(g) == d1 && (uint16_t)(rd32(g + 4) & 0xFFFFu) == d2 && (uint16_t)(rd32(g + 4) >> 16) == d3;
}
/* IID_IUnknown, IDirectSound, IDirectSound8 / IDirectSoundBuffer, IDirectSoundBuffer8 */
static int iid_ds(uint32_t g)  { return guid_is(g, 0x279AFA83u, 0x4981, 0x11CE) || guid_is(g, 0xC50A7E93u, 0xF395, 0x4834); }
static int iid_buf(uint32_t g) { return guid_is(g, 0x279AFA85u, 0x4981, 0x11CE) || guid_is(g, 0x6825A449u, 0x7524, 0x4D82); }
static int iid_unknown(uint32_t g) { return guid_is(g, 0u, 0, 0) && rd32(g + 8) == 0xC0u; }

static Buf *buf_of(uint32_t obj) { uint32_t s = rd32(obj + OBJ_SLOT); return s < MAX_BUFS && g_bufs[s].used ? &g_bufs[s] : NULL; }

/* Bring the play cursor up to date from the host clock; stops a finished one-shot buffer. */
static void buf_advance(Buf *b)
{
    if (!b->playing || b->primary || !b->size) return;
    uint64_t now = h2_host_monotonic_ns();
    uint64_t bytes = (now - b->since) * (uint64_t)b->freq / 1000000000ull * b->align;
    uint64_t abs = (uint64_t)b->pos + bytes;
    if (b->looping) {
        b->pos = (uint32_t)(abs % b->size);
    } else if (abs >= b->size) {
        b->pos = 0; b->playing = 0;
    } else {
        b->pos = (uint32_t)abs;
    }
    b->since = now;
}

/* ---- IUnknown ---- */
static uint32_t com_AddRef(CPU *c, const uint32_t *args) { UNUSED; uint32_t n = rd32(ARG(0) + OBJ_REFS) + 1; wr32(ARG(0) + OBJ_REFS, n); return n; }

static uint32_t ds_Release(CPU *c, const uint32_t *args)
{
    UNUSED;
    uint32_t n = rd32(ARG(0) + OBJ_REFS);
    if (n) wr32(ARG(0) + OBJ_REFS, --n);
    if (!n) { if (g_device == ARG(0)) g_device = 0; h2_heap_free(ARG(0)); }
    return n;
}

static uint32_t make_3d_object(uint32_t parent, uint32_t vtbl, uint32_t out);
static uint32_t buf_Release(CPU *c, const uint32_t *args);

static uint32_t buf_Release(CPU *c, const uint32_t *args)
{
    UNUSED;
    uint32_t obj = ARG(0), n = rd32(obj + OBJ_REFS);
    if (n) wr32(obj + OBJ_REFS, --n);
    if (!n) {
        Buf *b = buf_of(obj);
        if (b) { if (b->data) h2_heap_free(b->data); memset(b, 0, sizeof *b); g_live_bufs--; }
        h2_heap_free(obj);
    }
    return n;
}

static uint32_t ds_QueryInterface(CPU *c, const uint32_t *args)
{
    UNUSED;
    if (!ARG(2)) return DSERR_INVALIDPARAM;
    if (iid_unknown(ARG(1)) || iid_ds(ARG(1))) { wr32(ARG(2), ARG(0)); com_AddRef(c, args); return DS_OK; }
    H2_WARN(H2_LOG_AUDIO, "IDirectSound8::QueryInterface: %08X-... is not supported", ARG(1) ? rd32(ARG(1)) : 0);
    wr32(ARG(2), 0);
    return DSERR_NOINTERFACE;
}

static uint32_t buf_QueryInterface(CPU *c, const uint32_t *args)
{
    UNUSED;
    if (!ARG(2)) return DSERR_INVALIDPARAM;
    if (iid_unknown(ARG(1)) || iid_buf(ARG(1))) { wr32(ARG(2), ARG(0)); com_AddRef(c, args); return DS_OK; }
    Buf *b = buf_of(ARG(0));
    if (b && (b->flags & DSBCAPS_CTRL3D) && b->primary && guid_is(ARG(1), 0x279AFA84u, 0x4981, 0x11CE)) return make_3d_object(ARG(0), g_vtbl_l3d, ARG(2));
    if (b && (b->flags & DSBCAPS_CTRL3D) && !b->primary && guid_is(ARG(1), 0x279AFA86u, 0x4981, 0x11CE)) return make_3d_object(ARG(0), g_vtbl_b3d, ARG(2));
    H2_WARN(H2_LOG_AUDIO, "IDirectSoundBuffer8::QueryInterface: %08X-... is not supported (property set / EAX are not implemented)",
            ARG(1) ? rd32(ARG(1)) : 0);
    wr32(ARG(2), 0);
    return DSERR_NOINTERFACE;
}

/* ---- the device ---- */
static uint32_t new_buffer_object(Buf **out_buf)
{
    unsigned s;
    for (s = 0; s < MAX_BUFS; s++) if (!g_bufs[s].used) break;
    if (s == MAX_BUFS) return 0;
    uint32_t obj = h2_heap_alloc(OBJ_SIZE, 1);
    if (!obj) return 0;
    memset(&g_bufs[s], 0, sizeof g_bufs[s]);
    g_bufs[s].used = 1;
    wr32(obj + OBJ_VTBL, g_vtbl_buf); wr32(obj + OBJ_REFS, 1); wr32(obj + OBJ_SLOT, s);
    g_live_bufs++;
    *out_buf = &g_bufs[s];
    return obj;
}

static void read_format(Buf *b, uint32_t wfx)
{
    /* WAVEFORMATEX: tag u16, channels u16, samplesPerSec u32, avgBytes u32, blockAlign u16, bits u16 */
    uint32_t w0 = rd32(wfx), w1 = rd32(wfx + 4), w3 = rd32(wfx + 12);
    b->tag = w0 & 0xFFFFu; b->channels = w0 >> 16; b->freq = w1;
    b->align = w3 & 0xFFFFu; b->bits = w3 >> 16;
}

/* CreateSoundBuffer(this, const DSBUFFERDESC *desc, IDirectSoundBuffer **out, IUnknown *outer) */
static uint32_t ds_CreateSoundBuffer(CPU *c, const uint32_t *args)
{
    UNUSED;
    uint32_t desc = ARG(1), out = ARG(2);
    if (!desc || !out || ARG(3)) return DSERR_INVALIDPARAM;
    uint32_t size = rd32(desc), flags = rd32(desc + 4), bytes = rd32(desc + 8), wfx = rd32(desc + 16);
    if (size != 20 && size != 36) return DSERR_INVALIDPARAM;
    if (flags & DSBCAPS_CTRLFX)
        h2_guest_stop(H2_STOP_UNIMPLEMENTED_IMPORT, "IDirectSound8::CreateSoundBuffer: DSBCAPS_CTRLFX (effects are not implemented)");
    Buf *b; uint32_t obj;
    if (flags & DSBCAPS_PRIMARYBUFFER) {
        if (bytes || wfx) return DSERR_INVALIDPARAM;
        obj = new_buffer_object(&b);
        if (!obj) return DSERR_OUTOFMEMORY;
        b->primary = 1; b->flags = flags;
        b->freq = 44100; b->channels = 2; b->bits = 16; b->align = 4; b->tag = 1;   /* the device default until SetFormat */
    } else {
        if (!bytes || !wfx || bytes < 4 || bytes > 0x4000000u) return DSERR_INVALIDPARAM;
        obj = new_buffer_object(&b);
        if (!obj) return DSERR_OUTOFMEMORY;
        read_format(b, wfx);
        if (b->tag != 1 || !b->freq || !b->align || (b->bits != 8 && b->bits != 16) || b->channels < 1 || b->channels > 2 ||
            b->align != b->channels * b->bits / 8) {
            H2_WARN(H2_LOG_AUDIO, "CreateSoundBuffer: unsupported format tag=%u ch=%u rate=%u bits=%u align=%u", b->tag, b->channels, b->freq, b->bits, b->align);
            memset(b, 0, sizeof *b); g_live_bufs--; h2_heap_free(obj);
            return DSERR_INVALIDPARAM;                              /* only PCM 8/16 mono/stereo exists in this layer */
        }
        b->size = bytes - bytes % b->align; b->flags = flags;
        b->data = h2_heap_alloc(b->size, 1);
        if (!b->data) { memset(b, 0, sizeof *b); g_live_bufs--; h2_heap_free(obj); return DSERR_OUTOFMEMORY; }
        b->size = b->size ? b->size : bytes;
    }
    if (flags & DSBCAPS_CTRL3D) init_3d_defaults(b);
    wr32(out, obj);
    H2_INFO(H2_LOG_AUDIO, "CreateSoundBuffer: %s %u bytes, %u Hz, %u ch, %u bit, flags %08X", b->primary ? "primary" : "secondary",
            b->size, b->freq, b->channels, b->bits, flags);
    return DS_OK;
}

/* GetCaps(this, DSCAPS *): a software-only stereo device; no hardware buffers or memory. */
static uint32_t ds_GetCaps(CPU *c, const uint32_t *args)
{
    UNUSED;
    uint32_t caps = ARG(1);
    if (!caps || rd32(caps) != 96) return DSERR_INVALIDPARAM;
    memset(guest_ptr(caps + 4), 0, 92);
    /* PRIMARYMONO|STEREO|8BIT|16BIT = 1|2|4|8; SECONDARY* the same shifted to 0x100.. */
    wr32(caps + 4, 0x0Fu | 0xF00u);
    wr32(caps + 8, 100); wr32(caps + 12, 200000);                  /* min/max secondary sample rate */
    wr32(caps + 16, 1);                                           /* one primary buffer */
    return DS_OK;
}

static uint32_t ds_SetCooperativeLevel(CPU *c, const uint32_t *args)
{
    UNUSED;
    if (ARG(2) < 1 || ARG(2) > 4) return DSERR_INVALIDPARAM;       /* NORMAL, PRIORITY, EXCLUSIVE, WRITEPRIMARY */
    g_coop_hwnd = ARG(1); g_coop_level = ARG(2);
    return DS_OK;
}
static uint32_t ds_Compact(CPU *c, const uint32_t *args) { UNUSED; return g_coop_level == 0 ? DSERR_PRIOLEVELNEEDED : DS_OK; }
static uint32_t ds_GetSpeakerConfig(CPU *c, const uint32_t *args)
{
    UNUSED;
    if (!ARG(1)) return DSERR_INVALIDPARAM;
    wr32(ARG(1), 0x00000003u | (0u << 16));                        /* DSSPEAKER_STEREO, DSSPEAKER_GEOMETRY_NARROW=0 */
    return DS_OK;
}
static uint32_t ds_Initialize(CPU *c, const uint32_t *args) { UNUSED; (void)args; return 0x88780276u; }   /* DSERR_ALREADYINITIALIZED */

/* ---- buffers ---- */
static uint32_t buf_GetCaps(CPU *c, const uint32_t *args)
{
    UNUSED;
    Buf *b = buf_of(ARG(0));
    if (!b || !ARG(1) || rd32(ARG(1)) != 20) return DSERR_INVALIDPARAM;
    wr32(ARG(1) + 4, b->flags); wr32(ARG(1) + 8, b->size); wr32(ARG(1) + 12, 0); wr32(ARG(1) + 16, 0);
    return DS_OK;
}

static uint32_t buf_GetCurrentPosition(CPU *c, const uint32_t *args)
{
    UNUSED;
    Buf *b = buf_of(ARG(0));
    if (!b) return DSERR_INVALIDPARAM;
    buf_advance(b);
    uint32_t play = b->pos, write = b->size ? (play + b->freq / 100 * b->align) % b->size : 0;   /* 10 ms ahead */
    if (ARG(1)) wr32(ARG(1), play);
    if (ARG(2)) wr32(ARG(2), write);
    return DS_OK;
}

static uint32_t buf_GetFormat(CPU *c, const uint32_t *args)
{
    UNUSED;
    Buf *b = buf_of(ARG(0));
    if (!b) return DSERR_INVALIDPARAM;
    if (ARG(3)) wr32(ARG(3), 16);
    if (ARG(1)) {
        if (ARG(2) < 16) return DSERR_INVALIDPARAM;
        wr32(ARG(1), (b->tag & 0xFFFFu) | (b->channels << 16)); wr32(ARG(1) + 4, b->freq); wr32(ARG(1) + 8, b->freq * b->align);
        wr32(ARG(1) + 12, (b->align & 0xFFFFu) | (b->bits << 16));
        if (ARG(2) >= 18) memset(guest_ptr(ARG(1) + 16), 0, 2);   /* cbSize */
    }
    return DS_OK;
}

static uint32_t buf_GetVolume(CPU *c, const uint32_t *args) { UNUSED; Buf *b = buf_of(ARG(0)); if (!b || !ARG(1)) return DSERR_INVALIDPARAM; wr32(ARG(1), (uint32_t)b->volume); return DS_OK; }
static uint32_t buf_GetPan(CPU *c, const uint32_t *args)    { UNUSED; Buf *b = buf_of(ARG(0)); if (!b || !ARG(1)) return DSERR_INVALIDPARAM; wr32(ARG(1), (uint32_t)b->pan); return DS_OK; }
static uint32_t buf_GetFrequency(CPU *c, const uint32_t *args) { UNUSED; Buf *b = buf_of(ARG(0)); if (!b || !ARG(1)) return DSERR_INVALIDPARAM; wr32(ARG(1), b->freq); return DS_OK; }

static uint32_t buf_GetStatus(CPU *c, const uint32_t *args)
{
    UNUSED;
    Buf *b = buf_of(ARG(0));
    if (!b || !ARG(1)) return DSERR_INVALIDPARAM;
    buf_advance(b);
    wr32(ARG(1), (b->playing ? DSBSTATUS_PLAYING : 0u) | (b->playing && b->looping ? DSBSTATUS_LOOPING : 0u));
    return DS_OK;
}

static uint32_t buf_Initialize(CPU *c, const uint32_t *args) { UNUSED; (void)args; return 0x88780276u; }   /* ALREADYINITIALIZED */

/* Lock(this, offset, bytes, void **p1, DWORD *n1, void **p2, DWORD *n2, flags) */
static uint32_t buf_Lock(CPU *c, const uint32_t *args)
{
    UNUSED;
    Buf *b = buf_of(ARG(0));
    if (!b || !ARG(3) || !ARG(4)) return DSERR_INVALIDPARAM;
    if (b->primary) return DSERR_INVALIDCALL;                      /* writing the primary buffer needs WRITEPRIMARY mixing, not implemented */
    uint32_t off = ARG(1), bytes = ARG(2), flags = ARG(7);
    if (flags & DSBLOCK_ENTIREBUFFER) { off = 0; bytes = b->size; }
    else if (flags & DSBLOCK_FROMWRITECURSOR) {
        buf_advance(b);
        off = b->size ? (b->pos + b->freq / 100 * b->align) % b->size : 0;
    }
    if (off >= b->size || bytes > b->size) return DSERR_INVALIDPARAM;
    uint32_t first = bytes <= b->size - off ? bytes : b->size - off;
    wr32(ARG(3), b->data + off); wr32(ARG(4), first);
    if (ARG(5)) wr32(ARG(5), bytes > first ? b->data : 0);
    if (ARG(6)) wr32(ARG(6), bytes - first);
    return DS_OK;
}

static uint32_t buf_Unlock(CPU *c, const uint32_t *args)
{
    UNUSED;
    Buf *b = buf_of(ARG(0));
    if (!b) return DSERR_INVALIDPARAM;
    if (b->primary) return DSERR_INVALIDCALL;
    if (ARG(1) && ARG(2) > b->size) return DSERR_INVALIDPARAM;
    return DS_OK;                                                  /* the data already lives in the buffer */
}

static uint32_t buf_Play(CPU *c, const uint32_t *args)
{
    UNUSED;
    Buf *b = buf_of(ARG(0));
    if (!b || ARG(1) != 0) return DSERR_INVALIDPARAM;
    if (b->primary) { b->playing = 1; return DS_OK; }
    if (!b->playing) { b->since = h2_host_monotonic_ns(); b->playing = 1; }
    b->looping = (ARG(3) & DSBPLAY_LOOPING) != 0;
    return DS_OK;
}

static uint32_t buf_Stop(CPU *c, const uint32_t *args)
{
    UNUSED;
    Buf *b = buf_of(ARG(0));
    if (!b) return DSERR_INVALIDPARAM;
    buf_advance(b);
    b->playing = 0;
    return DS_OK;
}

static uint32_t buf_SetCurrentPosition(CPU *c, const uint32_t *args)
{
    UNUSED;
    Buf *b = buf_of(ARG(0));
    if (!b || b->primary || ARG(1) >= b->size) return DSERR_INVALIDPARAM;
    b->pos = ARG(1) - ARG(1) % b->align; b->since = h2_host_monotonic_ns();
    return DS_OK;
}

static uint32_t buf_SetFormat(CPU *c, const uint32_t *args)
{
    UNUSED;
    Buf *b = buf_of(ARG(0));
    if (!b || !ARG(1)) return DSERR_INVALIDPARAM;
    if (!b->primary) return DSERR_INVALIDCALL;                     /* only the primary buffer's format can be set */
    read_format(b, ARG(1));
    H2_INFO(H2_LOG_AUDIO, "primary buffer format: %u Hz, %u ch, %u bit", b->freq, b->channels, b->bits);
    return DS_OK;
}

static uint32_t buf_SetVolume(CPU *c, const uint32_t *args)
{
    UNUSED;
    Buf *b = buf_of(ARG(0)); int32_t v = (int32_t)ARG(1);
    if (!b || v < -10000 || v > 0) return DSERR_INVALIDPARAM;
    b->volume = v;
    return DS_OK;
}
static uint32_t buf_SetPan(CPU *c, const uint32_t *args)
{
    UNUSED;
    Buf *b = buf_of(ARG(0)); int32_t v = (int32_t)ARG(1);
    if (!b || v < -10000 || v > 10000) return DSERR_INVALIDPARAM;
    b->pan = v;
    return DS_OK;
}
static uint32_t buf_SetFrequency(CPU *c, const uint32_t *args)
{
    UNUSED;
    Buf *b = buf_of(ARG(0)); uint32_t f = ARG(1);
    if (!b || b->primary || (f != 0 && (f < 100 || f > 200000))) return DSERR_INVALIDPARAM;
    if (f == 0) return DS_OK;                                      /* DSBFREQUENCY_ORIGINAL: the format's own rate; rate changes keep the format rate here */
    buf_advance(b);
    b->freq = f;
    return DS_OK;
}
static uint32_t buf_Restore(CPU *c, const uint32_t *args) { UNUSED; (void)args; return DS_OK; }   /* buffers are never lost */

/* ---- IDirectSound3DListener (on the primary buffer) and IDirectSound3DBuffer (on a secondary) ----
 * The parameters are stored and read back exactly; there is no spatialisation because there is no mixer yet
 * (KNOWN_ISSUES). Object: { vtbl, refs, buffer slot, parent buffer object }. The parent stays alive while the
 * 3D interface does. */
static uint32_t make_3d_object(uint32_t parent, uint32_t vtbl, uint32_t out)
{
    uint32_t obj = h2_heap_alloc(16, 1);
    if (!obj) return DSERR_OUTOFMEMORY;
    wr32(obj + OBJ_VTBL, vtbl); wr32(obj + OBJ_REFS, 1); wr32(obj + OBJ_SLOT, rd32(parent + OBJ_SLOT)); wr32(obj + 12, parent);
    wr32(parent + OBJ_REFS, rd32(parent + OBJ_REFS) + 1);
    wr32(out, obj);
    return DS_OK;
}

static uint32_t i3d_Release(CPU *c, const uint32_t *args)
{
    uint32_t obj = ARG(0), n = rd32(obj + OBJ_REFS);
    if (n) wr32(obj + OBJ_REFS, --n);
    if (!n) {
        uint32_t parent = rd32(obj + 12), a[1] = { parent };
        h2_heap_free(obj);
        buf_Release(c, a);
    }
    return n;
}
static uint32_t i3d_QueryInterface(CPU *c, const uint32_t *args)
{
    UNUSED;
    if (!ARG(2)) return DSERR_INVALIDPARAM;
    if (iid_unknown(ARG(1))) { wr32(ARG(2), ARG(0)); com_AddRef(c, args); return DS_OK; }
    wr32(ARG(2), 0);
    return DSERR_NOINTERFACE;
}

#define P3_GET(name, off, n) static uint32_t name(CPU *c, const uint32_t *args) { UNUSED; Buf *b = buf_of(ARG(0)); \
    if (!b || !ARG(1)) return DSERR_INVALIDPARAM; for (unsigned i = 0; i < (n); i++) wr32(ARG(1) + 4u * i, b->p3d[(off) + i]); return DS_OK; }
#define P3_SET(name, off, n) static uint32_t name(CPU *c, const uint32_t *args) { UNUSED; Buf *b = buf_of(ARG(0)); \
    if (!b) return DSERR_INVALIDPARAM; for (unsigned i = 0; i < (n); i++) b->p3d[(off) + i] = ARG(1 + i); return DS_OK; }
/* the trailing "apply" argument (DS3D_IMMEDIATE / DS3D_DEFERRED) needs no handling: settings are never mixed yet */

P3_GET(l3_GetDistanceFactor, L_DIST, 1) P3_GET(l3_GetDopplerFactor, L_DOPPLER, 1) P3_GET(l3_GetRolloffFactor, L_ROLLOFF, 1)
P3_GET(l3_GetPosition, P3_POS, 3) P3_GET(l3_GetVelocity, P3_VEL, 3)
P3_SET(l3_SetDistanceFactor, L_DIST, 1) P3_SET(l3_SetDopplerFactor, L_DOPPLER, 1) P3_SET(l3_SetRolloffFactor, L_ROLLOFF, 1)
P3_SET(l3_SetPosition, P3_POS, 3) P3_SET(l3_SetVelocity, P3_VEL, 3) P3_SET(l3_SetOrientation, L_FRONT, 6)
P3_GET(b3_GetConeOrientation, B_CONEDIR, 3) P3_GET(b3_GetConeOutsideVolume, B_OUTVOL, 1) P3_GET(b3_GetMaxDistance, B_MAX, 1)
P3_GET(b3_GetMinDistance, B_MIN, 1) P3_GET(b3_GetMode, B_MODE, 1) P3_GET(b3_GetPosition, P3_POS, 3) P3_GET(b3_GetVelocity, P3_VEL, 3)
P3_SET(b3_SetConeAngles, B_INSIDE, 2) P3_SET(b3_SetConeOrientation, B_CONEDIR, 3) P3_SET(b3_SetConeOutsideVolume, B_OUTVOL, 1)
P3_SET(b3_SetMaxDistance, B_MAX, 1) P3_SET(b3_SetMinDistance, B_MIN, 1) P3_SET(b3_SetMode, B_MODE, 1)
P3_SET(b3_SetPosition, P3_POS, 3) P3_SET(b3_SetVelocity, P3_VEL, 3)

static uint32_t l3_GetOrientation(CPU *c, const uint32_t *args)
{
    UNUSED;
    Buf *b = buf_of(ARG(0));
    if (!b || !ARG(1) || !ARG(2)) return DSERR_INVALIDPARAM;
    for (unsigned i = 0; i < 3; i++) { wr32(ARG(1) + 4u * i, b->p3d[L_FRONT + i]); wr32(ARG(2) + 4u * i, b->p3d[L_TOP + i]); }
    return DS_OK;
}
static uint32_t b3_GetConeAngles(CPU *c, const uint32_t *args)
{
    UNUSED;
    Buf *b = buf_of(ARG(0));
    if (!b || !ARG(1) || !ARG(2)) return DSERR_INVALIDPARAM;
    wr32(ARG(1), b->p3d[B_INSIDE]); wr32(ARG(2), b->p3d[B_OUTSIDE]);
    return DS_OK;
}
/* Get/SetAllParameters: DS3DLISTENER / DS3DBUFFER are dwSize + the same 15 dwords as p3d, in the same order. */
static uint32_t p3_GetAll(CPU *c, const uint32_t *args)
{
    UNUSED;
    Buf *b = buf_of(ARG(0));
    if (!b || !ARG(1) || rd32(ARG(1)) != 64) return DSERR_INVALIDPARAM;
    for (unsigned i = 0; i < 15; i++) wr32(ARG(1) + 4 + 4u * i, b->p3d[i]);
    return DS_OK;
}
static uint32_t p3_SetAll(CPU *c, const uint32_t *args)
{
    UNUSED;
    Buf *b = buf_of(ARG(0));
    if (!b || !ARG(1) || rd32(ARG(1)) != 64) return DSERR_INVALIDPARAM;
    for (unsigned i = 0; i < 15; i++) b->p3d[i] = rd32(ARG(1) + 4 + 4u * i);
    return DS_OK;
}
static uint32_t l3_Commit(CPU *c, const uint32_t *args) { UNUSED; (void)args; return DS_OK; }

/* ---- creation ---- */
static uint32_t create_device(uint32_t out)
{
    if (!out) return DSERR_INVALIDPARAM;
    ensure_vtables();
    if (g_device) return 0x88780078u;                              /* DSERR_ALLOCATED: this device is single-open here */
    uint32_t obj = h2_heap_alloc(OBJ_SIZE, 1);
    if (!obj) return DSERR_OUTOFMEMORY;
    wr32(obj + OBJ_VTBL, g_vtbl_ds); wr32(obj + OBJ_REFS, 1);
    g_device = obj;
    wr32(out, obj);
    H2_INFO(H2_LOG_AUDIO, "IDirectSound8 created (null sink with a real clock; no EAX effects)");
    return DS_OK;
}

/* HRESULT EAXDirectSoundCreate8(LPGUID, LPDIRECTSOUND8 *, IUnknown *) : the EAX wrapper only adds property sets;
 * without them it is DirectSoundCreate8. */
static uint32_t eax_EAXDirectSoundCreate8(CPU *c, const uint32_t *args)
{
    UNUSED;
    if (ARG(2)) return DSERR_INVALIDPARAM;
    return create_device(ARG(1));
}

/* HRESULT DirectSoundEnumerateA(LPDSENUMCALLBACKA callback, LPVOID context)
 * The callback receives (GUID *guid, const char *description, const char *module, void *context); a NULL
 * guid is the primary driver. It returns FALSE to stop the enumeration. */
static uint32_t ds_DirectSoundEnumerateA(CPU *c, const uint32_t *args)
{
    if (!ARG(0)) return DSERR_INVALIDPARAM;
    static uint32_t desc, module;
    if (!desc) { desc = h2_os_strdup("Primary Sound Driver"); module = h2_os_strdup(""); }
    H2_INFO(H2_LOG_AUDIO, "DirectSoundEnumerateA: offering the virtual output device");
    h2_call_guest(c, ARG(0), 4, 0u, desc, module, ARG(1));
    return DS_OK;
}

static const H2ImportDef k_defs[] = {
    { "DSOUND.dll", "#2", 2, ds_DirectSoundEnumerateA },
    { "EAX.DLL", "#6", 3, eax_EAXDirectSoundCreate8 },
#define DS(name, n)  { "DSOUND.dll#IDirectSound8", #name, n, ds_##name }
#define BUF(name, n) { "DSOUND.dll#IDirectSoundBuffer8", #name, n, buf_##name }
    { "DSOUND.dll#IDirectSound8", "AddRef", 1, com_AddRef },
    DS(QueryInterface, 3), DS(Release, 1), DS(CreateSoundBuffer, 4), DS(GetCaps, 2), DS(SetCooperativeLevel, 3),
    DS(Compact, 1), DS(GetSpeakerConfig, 2), DS(Initialize, 2),
    { "DSOUND.dll#IDirectSoundBuffer8", "AddRef", 1, com_AddRef },
    BUF(QueryInterface, 3), BUF(Release, 1), BUF(GetCaps, 2), BUF(GetCurrentPosition, 3), BUF(GetFormat, 4),
    BUF(GetVolume, 2), BUF(GetPan, 2), BUF(GetFrequency, 2), BUF(GetStatus, 2), BUF(Initialize, 2), BUF(Lock, 8),
    BUF(Play, 4), BUF(SetCurrentPosition, 2), BUF(SetFormat, 2), BUF(SetVolume, 2), BUF(SetPan, 2), BUF(SetFrequency, 2),
    BUF(Stop, 1), BUF(Unlock, 5), BUF(Restore, 1),
#undef DS
#undef BUF
#define L3(name, n) { "DSOUND.dll#IDirectSound3DListener", #name, n, l3_##name }
#define B3(name, n) { "DSOUND.dll#IDirectSound3DBuffer", #name, n, b3_##name }
    { "DSOUND.dll#IDirectSound3DListener", "QueryInterface", 3, i3d_QueryInterface }, { "DSOUND.dll#IDirectSound3DListener", "AddRef", 1, com_AddRef },
    { "DSOUND.dll#IDirectSound3DListener", "Release", 1, i3d_Release },
    { "DSOUND.dll#IDirectSound3DListener", "GetAllParameters", 2, p3_GetAll }, { "DSOUND.dll#IDirectSound3DListener", "SetAllParameters", 3, p3_SetAll },
    L3(GetDistanceFactor, 2), L3(GetDopplerFactor, 2), L3(GetOrientation, 3), L3(GetPosition, 2), L3(GetRolloffFactor, 2), L3(GetVelocity, 2),
    L3(SetDistanceFactor, 3), L3(SetDopplerFactor, 3), L3(SetOrientation, 8), L3(SetPosition, 5), L3(SetRolloffFactor, 3), L3(SetVelocity, 5),
    { "DSOUND.dll#IDirectSound3DListener", "CommitDeferredSettings", 1, l3_Commit },
    { "DSOUND.dll#IDirectSound3DBuffer", "QueryInterface", 3, i3d_QueryInterface }, { "DSOUND.dll#IDirectSound3DBuffer", "AddRef", 1, com_AddRef },
    { "DSOUND.dll#IDirectSound3DBuffer", "Release", 1, i3d_Release },
    { "DSOUND.dll#IDirectSound3DBuffer", "GetAllParameters", 2, p3_GetAll }, { "DSOUND.dll#IDirectSound3DBuffer", "SetAllParameters", 3, p3_SetAll },
    B3(GetConeAngles, 3), B3(GetConeOrientation, 2), B3(GetConeOutsideVolume, 2), B3(GetMaxDistance, 2), B3(GetMinDistance, 2),
    B3(GetMode, 2), B3(GetPosition, 2), B3(GetVelocity, 2), B3(SetConeAngles, 4), B3(SetConeOrientation, 5),
    B3(SetConeOutsideVolume, 3), B3(SetMaxDistance, 3), B3(SetMinDistance, 3), B3(SetMode, 3), B3(SetPosition, 5), B3(SetVelocity, 5),
#undef L3
#undef B3
};

void h2_register_dsound(void) { h2_imports_register(k_defs, sizeof k_defs / sizeof k_defs[0]); }
