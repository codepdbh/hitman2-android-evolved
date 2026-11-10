# Renderer backend: decision and evidence

**Decision (provisional, 2026-11-09): recompile `RenderOpenGL.dll` and answer
its OpenGL imports with a fixed-function emulation layer on OpenGL ES 3.x.**
Do not build a Direct3D 8 layer unless the evidence below is overturned.

Status: **decision made from static evidence only**. No frame has been rendered
by either path yet. See `docs/STATUS.md`.

## How the engine selects a renderer (verified)

- `Hitman2.ini` has `DrawDll RenderD3D.dll`; the string `DrawDll` is an option
  name inside `hitman2.exe`. The renderer is a DLL chosen by configuration.
- Both renderer DLLs export exactly one symbol, `Create`.
- Both import the **same 28 symbols** from `hitman2.exe` (`g_pSysInterface`,
  `g_pSysMem`, `g_pSysFile`, `g_pSysCom`, `ZEngineDataBase::GeomRefToPtr`,
  `ZSaveGame`/`ZLoadGame` operators, `ConstrainedParticleSystem`, …).
  The engine↔renderer contract is therefore identical for the two; switching
  is a one-line configuration change, not an engine change.
- Both were linked on 2002-09-18 with linker 6.0, and both have relocations.

## What each renderer needs from the platform (verified from import tables and strings)

| | `RenderOpenGL.dll` | `RenderD3D.dll` |
|---|---|---|
| Code size (`.text`) | 0x3ACD2 (240 KB) | 0x3D862 (252 KB) |
| Graphics API entry | 64 flat functions from `OPENGL32.dll` | 1 function, `Direct3DCreate8`; everything else is COM vtable calls |
| API reached at run time by name | 19 extension functions via `wglGetProcAddress` | — |
| Indirect `call [reg+disp]` sites (all COM/virtual calls, engine and API mixed) | 1,206 | 1,687 |
| Shaders | none (fixed function) | vertex + pixel shader files: `Shaders\vsmps.vsh`, `vsmpsdp3.vsh`, `psbeml.vsh`, `psdp3.psh` |
| Window-system glue | `wglCreateContext`, `wglMakeCurrent`, `wglDeleteContext`, `SwapBuffers`, `SetPixelFormat`, `DescribePixelFormat`, `SetDeviceGammaRamp` | `GetDeviceCaps`, `DescribePixelFormat`, D3D presentation |

### The 64 OpenGL imports

All OpenGL 1.1 fixed-function:

- **State**: `glEnable glDisable glBlendFunc glAlphaFunc glDepthFunc glDepthMask glCullFace glShadeModel glPolygonMode glColorMaterial glScissor glViewport glPixelStorei glDrawBuffer glReadBuffer`
- **Matrices**: `glMatrixMode glLoadIdentity glLoadMatrixf glFrustum glOrtho`
- **Lighting / material / fog**: `glLightf glLightfv glLightModelfv glMaterialfv glFogi glFogf glFogfv`
- **Texturing**: `glGenTextures glDeleteTextures glBindTexture glTexImage2D glTexSubImage2D glTexParameteri glTexEnvi glTexEnvf glTexGenf`
- **Vertex arrays**: `glEnableClientState glDisableClientState glVertexPointer glNormalPointer glColorPointer glTexCoordPointer glDrawElements`
- **Immediate mode**: `glBegin glEnd glVertex2f glVertex3f glVertex3fv glTexCoord2f glColor4f glColor4ub`
- **Framebuffer**: `glClear glClearColor glFlush glFinish glReadPixels glDrawPixels glRasterPos2i`
- **Queries**: `glGetString glGetIntegerv`
- **WGL**: `wglCreateContext wglMakeCurrent wglDeleteContext wglGetProcAddress`

### Extensions it asks for by name

`GL_ARB_multitexture` (`glActiveTextureARB`, `glClientActiveTextureARB`,
`glMultiTexCoord2fvARB`, `glMultiTexCoord4fvARB`), `GL_ARB_texture_compression`
(`glCompressedTexImage2DARB`), `GL_EXT_blend_subtract` (`glBlendEquationEXT`),
`GL_EXT_texture_env_add`, `GL_EXT_texture_filter_anisotropic`,
`GL_EXT_paletted_texture` (`glColorTableEXT`), `GL_NV_vertex_array_range` +
`GL_NV_fence` (`glVertexArrayRangeNV`, `wglAllocateMemoryNV`, fences),
`WGL_EXT_swap_control` (`wglSwapIntervalEXT`).

These are probed through `glGetString(GL_EXTENSIONS)`; the layer decides which
to advertise. NV vertex-array-range/fence is a pure optimisation and will not
be advertised. Whether the renderer has a working fallback for each
non-advertised extension is a **hypothesis until the code paths are read**.

## Compatibility matrix (OpenGL path → OpenGL ES 3.x)

| Need | ES 3.0 equivalent | State |
|---|---|---|
| Vertex arrays + `glDrawElements` | VBO/VAO streaming | Pendiente |
| Immediate mode (`glBegin`…`glEnd`) | batch into a client buffer, draw on `glEnd` | Pendiente |
| Matrix stack (modelview/projection/texture) | CPU matrices → uniforms | Pendiente |
| Fixed-function lighting, material, colour material | generated GLSL ES shader keyed by state | Pendiente |
| Fog (linear/exp) | fragment shader | Pendiente |
| Alpha test | `discard` in fragment shader | Pendiente |
| `glTexEnv` modes incl. `ADD`, 2+ texture units | fragment shader combiner | Pendiente |
| `glTexGen` | vertex shader | Pendiente (which modes are used: unknown) |
| `glPolygonMode(LINE)` | no ES equivalent; emulate or drop (debug feature?) | Unknown use |
| `glDrawPixels` / `glRasterPos2i` | textured quad | Pendiente |
| `glReadPixels` (screenshots: `AutoDumpName`) | native | Pendiente |
| S3TC/DXT compressed textures | `GL_EXT_texture_compression_s3tc` where the GPU has it, else decode to RGBA on load (or do not advertise; ini has `DisableDXT`) | Pendiente |
| Paletted textures | expand to RGBA on upload, or do not advertise | Pendiente |
| Blend subtract | core in ES 3.0 | Pendiente |
| Anisotropic filtering | `GL_EXT_texture_filter_anisotropic` | Pendiente |
| Gamma ramp (`SetDeviceGammaRamp`) | post-process or ignore with a logged warning | Pendiente |
| WGL context / pixel format / swap | EGL, owned by the Android layer | Pendiente |

## What is given up by not taking the D3D path (to be confirmed visually)

`RenderD3D.dll` carries shader files whose names suggest per-pixel effects the
fixed-function GL renderer cannot have: `psbeml` (bump environment mapping with
luminance — the ini option `DisableEMBM` exists) and `dp3` (dot3 bump). The GL
renderer contains no shader strings. **Hypothesis**: some surfaces (water,
glass, some floors) look flatter under the GL renderer. This is the original
game's own OpenGL look, not a regression introduced by the port, but it is a
visual difference from the default D3D configuration and must be documented
with screenshots once both exist.

## Why not Direct3D 8

- The API surface is COM: every call is `call [reg+disp]` through
  `IDirect3D8`, `IDirect3DDevice8`, `IDirect3DTexture8`, vertex/index buffers
  and surfaces. The layer would have to provide guest-visible COM objects with
  x86 `stdcall` vtables for each interface — more machinery than 83 flat
  functions.
- It needs a D3D8 vertex/pixel **shader assembler and translator** (`.vsh`/`.psh`
  → GLSL ES) in addition to the fixed-function pipeline, which D3D8 also has.
  The GL path needs only the fixed-function half.
- PCRecomp's `win32hle` has DirectDraw and an early Direct3D rasteriser, not
  Direct3D 8 (its README lists "DirectX past DirectDraw 2 / DirectSound 1" as
  out of scope). Nothing to reuse either way.

## Evidence update, 2026-11-10 (the decision now rests on a run, not only on static reading)

`RenderOpenGL.dll` is lifted whole (1,706 functions, 0 lifter exceptions, 0 untranslated instructions)
and runs in the boot probe on the PC and on an ARM64 phone with identical results. Through
`renderer/h2_gl.c` (backend: null) the original renderer's own initialisation executes: it
enumerates pixel formats, picks and sets one, creates and activates a context, reads
`glGetString(GL_EXTENSIONS)`, resolves extension functions, clears the buffer and uploads two
textures, with **0 GL errors**.

Which extensions it actually asked for, out of the 9 names found in its strings: with the layer
advertising multitexture and blend-subtract (and not S3TC, paletted textures, anisotropy, NV vertex
array range or swap control) it called `wglGetProcAddress` for exactly `glActiveTextureARB`,
`glClientActiveTextureARB`, `glMultiTexCoord2fvARB`, `glMultiTexCoord4fvARB` and
`glBlendEquationEXT`, and nothing for the ones not advertised. So the renderer does degrade by
extension as hypothesised; whether every non-advertised path then draws correctly is still untested
(no pixels are produced yet).

## What would overturn this

- The GL renderer turning out to be visibly broken or incomplete relative to
  D3D in ways that matter (missing shadows, missing weather effects).
- A hard dependency on an extension that cannot be emulated.

Both are checkable once `RenderOpenGL.dll` is lifted and its extension-probe
code is read. That is the next renderer task.
