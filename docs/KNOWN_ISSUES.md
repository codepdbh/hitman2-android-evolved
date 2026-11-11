# Problemas conocidos y bloqueos

Actualizado: 2026-11-09. Cada entrada dice cómo se detectó. Nada aquí está "resuelto" devolviendo éxito falso.

## Bloqueos

### B1. Edición de referencia del ejecutable por fijar
- **Hechos de la build analizada** (`hitman2.exe` 1.0.0.265, SHA-256 en `analysis/binary_versions.json`): sello PE 2012-09-10 (los DLL son de 2002); las 7 secciones tienen las mismas flags `0xC0000040` (ninguna marcada como código, de ahí `tools/prepare_image.py`); existen las secciones `.cms_t`/`.cms_d`; el punto de entrada es el arranque CRT normal de MSVC6 dentro de `.text`, con el código en claro; 56.783 bytes de overlay.
- **Consecuencia**: el proyecto no contiene ni contendrá código para eludir protección anticopia, y no lo necesita: trabaja solo sobre código que ya está en claro en la copia del usuario. Hay varias ediciones del juego en circulación (disco, reediciones digitales); el catálogo y los hashes son específicos de una. Pendiente: fijar como referencia una edición digital sin DRM y hacer que la importación en Android valide por hash.
- La cadena `Please insert Hitman2 CD in any drive` existe en el exe: hay una comprobación de disco cuya ruta de código no se ha analizado.

### B2. ~~ARM64 sin ejecutar~~ — resuelto el 2026-11-09
- Ejecutado en SM-S938B: 544/544 vectores idénticos al host. Sigue sin ejecutarse en ARM64 la imagen completa (solo compila).

### B4. El icono usa marca y arte de terceros — decisión del propietario
- `android/branding/icono.png` incluye el logotipo de Hitman 2 y la imagen del personaje. Sirve para uso personal; si el repositorio o el APK se publican, conviene sustituirlo o excluirlo. Hoy `git` lo incluiría.

### B3. Ghidra no instalado para este proyecto
- Hay un Ghidra 12.1.4 dentro de `mp2-androidevolved/external/` (otro proyecto, no se toca). Pendiente decidir si se referencia por ruta o se descarga una copia local. Mientras tanto la recuperación de funciones es solo la de `disasm32`, sin segunda opinión ni puntuación (`score_recovery`).

## Divergencias del lifter encontradas por la prueba diferencial

| # | Qué | Estado | Evidencia |
|---|---|---|---|
| 1 | `fldcw` ignorado y `fistp`/`frndint` siempre al más cercano → `_ftol` (todo `(int)float`) redondearía en vez de truncar | **Corregido** en `recomp/h2_lifter.py`; `_ftol` (0x0059DD9C) pasa 12/12 vectores | lectura de `lift32_cpu.py` + prueba dirigida |
| 2 | `fnstsw` sin el campo TOP | **Corregido** | 0x0045AAD0, 0x005BF510 fallaban en EAX |
| 3 | `std` (DF=1) no soportado: `strrchr` devolvía NULL | **Corregido** | 0x0059D7D0 |
| 4 | `fprem` hace la reducción completa (hardware: parcial, máx. 63 bits de exponente) | **Abierto, divergencia conocida** (decisión documentada de PCRecomp) | 0x00454DC0, 1 de 8 vectores, solo con exponentes separados ≥ 64 bits |
| 5 | `fist*` fuera de rango era comportamiento indefinido en C (x86 → 0x80000000, ARM64 satura) | **Corregido** (`h2_fist*`) | por inspección; cubierto por vectores de borde en `_ftol` |
| 6 | `pushal`/`popal` sin implementar en `lift32_cpu` | Abierto (falla ruidosamente con `RECOMP_TODO`) | 0x00600E1E en la primera pasada |
| 7 | División entera por cero: en x86 es `#DE`, el C generado es indefinido y en ARM64 no atrapa | Abierto | por inspección del lifter |
| 8 | x87 modelado como `double`: sin precisión de 80 bits ni control de precisión (PC). Coincide con el valor por defecto de Win32 (53 bits) | Abierto, impacto no medido | diseño de PCRecomp |
| 9 | DF no forma parte de `pushfd`/`popfd` | Abierto | por inspección |
| 10 | Excepciones x87 enmascaradas (pila vacía → NaN) no modeladas | Abierto; los vectores afectados se marcan inconclusos | 0x005A4772, 0x005A552E |

Las correcciones 1, 2, 3 y 5 son candidatas a enviarse a PCRecomp.

## Hallazgos de la sesión del 2026-11-10 (todos con evidencia)

| # | Qué | Estado |
|---|---|---|
| L1 | El lifter de PCRecomp decodifica en lineal. Las rutinas del CRT escritas a mano (`memmove`) **incrustan tablas de saltos en el código**: los bytes de datos se decodifican como instrucciones, se pierde la alineación y los destinos de la tabla acaban como `dispatch` a direcciones sin cuerpo. | **Corregido** en `recomp/h2_lifter.py` (decodificación por flujo de control). Efecto medido: sitios sin traducir del exe 298 → 29; del renderizador 5 → 0. La prueba diferencial no cambió (98 PASS) |
| L2 | Tablas de saltos con índice **negativo** (`jmp [ecx*4+base]`, ecx = −4…−1) o con la entrada 0 sin uso: el lifter las leía hacia delante y no encontraba brazos. | **Corregido** (heurística acotada a punteros dentro de la propia función). Es una heurística: una tabla que no encaje en ninguna de las tres formas seguiría sin resolverse y se vería como `unresolved-dispatch` |
| P1 | **Fallo mío**: `USER32!EnumDisplaySettingsA` borraba 154 bytes sobre un `DEVMODEA` de 156 desde el offset 38, es decir, 36 bytes de más: pisó los registros guardados en la pila del invitado y el juego acabó con `esi=0` y un acceso a `0x00000BB7`. | **Corregido**. Encontrado con el vigilante de memoria por dispatch (`--watch-mem`). Revisadas el resto de escrituras en estructuras del invitado. Pendiente: una prueba que escriba cada estructura sobre un búfer con centinelas |
| P2 | El rastreo de pila del propio diagnóstico leía 5 bytes antes de la base de una imagen y mataba el proceso. | **Corregido** |

## Límites de la capa OpenGL (`renderer/`)

- **Backend nulo**: cuenta lotes, vértices y texturas pero no dibuja; `glReadPixels` no devuelve nada (lo dice en el log) y deja la memoria del cliente intacta. No existe aún el backend GLES 3 ni una superficie.
- **Se anuncia OpenGL 1.1** más `GL_ARB_multitexture` (2 unidades), `GL_EXT_texture_env_add`, `GL_EXT_blend_subtract`, `GL_EXT_bgra`, `GL_EXT_packed_pixels` y `GL_SGIS_texture_edge_clamp`: solo lo implementado. No se anuncian compresión S3TC, paletas, anisotropía ni NV vertex array range.
- Texturas: tamaño potencia de dos (como GL 1.1; si no, error), RGBA8 en el backend; `GL_CLAMP` lo resuelve cada backend como `CLAMP_TO_EDGE` (diferencia visible en bordes).
- No implementado y que **no** devuelve éxito: `glPolygonMode` ≠ relleno (aviso, se dibuja relleno), planos de recorte, `GL_TEXTURE_ENV_COLOR`, `GL_LIGHT_MODEL_TWO_SIDE/LOCAL_VIEWER` (aviso), `glPixelStorei` con salto o longitud de fila (detiene el invitado), segundo contexto GL.
- `glTexGen` y la iluminación/niebla/combinadores se **almacenan** y se pasan al backend; el backend GLES deberá traducirlos a shaders. Nada de eso está verificado visualmente.
- El gamma (`SetDeviceGammaRamp`) nunca llega al monitor real: es un posproceso del backend.

## Límites del backend GLES 3 y de los nuevos bloques (2026-11-11)

- GLES 3: sin `glPolygonMode` LINE/POINT, sin sombreado FLAT, sin entorno de textura BLEND (se dibuja como MODULATE), sin
  iluminación a dos caras ni visor local, sin `glDrawPixels` (avisa una vez), sin stencil. Cada uso se registra una vez.
  Un filtro con mipmaps sobre una textura con solo el nivel 0 se degrada a su equivalente sin mipmaps.
- Solo superficie EGL sin ventana (pbuffer): no hay todavía superficie de pantalla ni presentación visible en el teléfono.
- Audio: sumidero nulo con reloj real; no hay mezclador ni salida audible; 3D guardado pero no espacializado; EAX
  (`IKsPropertySet`) rechazado con `E_NOINTERFACE`; efectos (`DSBCAPS_CTRLFX`) paran el invitado.
- Hilos: cooperativos, uno a la vez, prioridades sin efecto; `CREATE_SUSPENDED`/`ResumeThread` no implementados; un hilo que
  gira sin llamar a una API bloqueante deja sin turno a los demás (las esperas con tiempo 0 que fallan ceden el turno).
- Las ejecuciones dependen del reloj de pared: el número de fotogramas por unidad de trabajo varía entre ejecuciones, así que
  los contadores de PC y ARM64 no coinciden; la comparación válida es el motivo de parada y los errores GL.
- `FindFirstFileA`: sin nombres cortos 8.3 y con atributos solo DIRECTORY/NORMAL.
- Sin tratar todavía (sondas anteriores los mostraban): `RaiseException`/`RtlUnwind`/SEH, `FormatMessageA`, `GetFileTime`,
  `CreateDirectoryA`, `MoveFileA`, `CopyFileA`, `GetDiskFreeSpaceExA` y otros imports de la lista de `tools/missing_imports.py`.

## Límites de la capa de plataforma (`platform/`)

- **Ventanas**: modelo sin bordes sobre una pantalla virtual 800×450; `ChangeDisplaySettings` solo admite el modo virtual; `SetForegroundWindow`/`SetFocus`/`SetCursorPos`/`ClipCursor` actúan solo en el mundo del invitado (nunca tocan el escritorio real). Mensajes no generados: `WM_PAINT`, `WM_GETMINMAXINFO`, `WM_WINDOWPOSCHANGED`, no cliente. `LoadIconA` no comprueba que el recurso exista.
- **Bucle de mensajes**: `GetMessage`/`WaitMessage` sobre cola vacía detienen el invitado (no hay fuente de entrada que lo despierte). Hace falta el bucle del host (Android: un hilo + cola; PC: pruebas).
- **Registro**: en memoria, vacío salvo lo sembrado; sin persistencia. Solo las 4 funciones que importa el exe.
- **DirectInput 8**: teclado y ratón del sistema; sin mandos; formatos de datos personalizados detienen el invitado.
- **DirectSound**: solo `DirectSoundEnumerateA` (un dispositivo virtual). El siguiente bloqueo es `EAXDirectSoundCreate8` (EAX.DLL#6, ordinal 6 de `eax.dll`): hace falta el modelo COM completo (IDirectSound8, buffers, 3D) y un backend de audio. `P5DLL` está respondido como "sin guante" (suposición documentada en `platform/game_dlls.c`).
- **Un solo hilo invitado.** `CreateThread` no existe; las secciones críticas y los mutex solo son correctos mientras sea así, y una espera infinita sobre un evento no señalado detiene al invitado con un mensaje en vez de colgarse.
- **Sin SEH ni excepciones C++ del invitado.** El TIB simulado existe y los prólogos SEH funcionan, pero `RtlUnwind`, `RaiseException` y `UnhandledExceptionFilter` no están implementados: un `throw` del juego detendrá la ejecución nombrando el import.
- **DLL del juego**: solo se carga lo que está levantado y enlazado (hoy `RenderOpenGL.dll`). Todas las imágenes deben caber en su base preferida sin solaparse (el exe en `0x400000`, el renderizador en `0x10000000`); un segundo DLL con la misma base (`xmlparse`, `jpegdll`, `eax` prefieren `0x10000000`) **no se puede cargar** sin reubicación. `FreeLibrary` ejecuta `DllMain(DETACH)` pero no libera la memoria.
- **Pila del host.** Las llamadas invitadas anidan en la pila nativa: la sonda usa 256 MiB. En la app Android el invitado deberá correr en un hilo propio con pila grande.
- **Reloj en UTC**, sin zona horaria. **Código de página 1252** fijo. `LCMapString` solo convierte mayúsculas/minúsculas.
- **Memoria**: `VirtualAlloc` no reutiliza el espacio liberado y el montón no fusiona bloques libres (solo reutiliza tamaños iguales). Suficiente para arrancar; habrá que medirlo con una escena cargada.
- **Sistema de archivos sin estrenar**: `CreateFileA`/`ReadFile`/`WriteFile`/`SetFilePointer`/`GetFileSize` están implementados (raíz del juego de solo lectura + raíz de usuario con copia al escribir) pero el programa aún no los ha llamado; faltan `FindFirstFileA` y familia, borrado, renombrado y consultas de unidad.
- Registro (`ADVAPI32`), `GDI32`, `DINPUT8`, `DSOUND`, `EAX`, `P5DLL`: sin implementar.

## Huecos medidos al levantar la imagen completa

- `hitman2.exe`: 29 `RECOMP_TODO` en 23 funciones (`int` 16, `fpu` 6, `x87` 5, `int1` 2; detalle en `docs/evidence/lift_census_hitman2_2026-11-10.json`). Pendiente clasificar cada una como datos mal catalogados o código real con instrucción no soportada. `pushal/popal`, `xlatb`, `clc/stc/cmc` y `rdtsc` ya se traducen (`rdtsc` como contador de 1 tick/ns, no la frecuencia real).
- 195 destinos estáticos de `dispatch` sin cuerpo en el exe y 11 en el renderizador.
- Las entradas del catálogo recortadas al inicio de la siguiente función terminan en un `dispatch` de continuación; correcto, pero cada una es una transferencia indirecta más en tiempo de ejecución.

## Limitaciones del análisis actual

- `disasm32` cubre el 95,0 % de `.text`; el 5 % restante no está clasificado (datos, relleno o funciones no encontradas). Sin referencia con símbolos no se conoce la precisión del catálogo.
- 8.385 de las 21.336 entradas son alias (etiquetas internas, funclets de excepciones C++).
- RTTI: 3 descriptores de tipo, 0 vtables recuperadas (el juego se compiló sin RTTI útil). Las vtables habrá que hallarlas con `cpp/vtable_scan.py`.
- `RenderOpenGL.dll` y `RenderD3D.dll` aún no se han desensamblado; la decisión de renderizador se basa en imports y cadenas.
- Idioma: no existe carpeta `Locale` y el ini apunta a `Locale\English.xml`. No se ha determinado qué idiomas contienen los `.LOC` de las escenas.

## Incidencias de entorno ya resueltas

- Un fallo del arnés despertaba a `WerFault.exe` (informe de errores de Windows). Ahora el arnés captura sus propios fallos y `worksafe.py` fija el modo de error heredable; no debería volver a ocurrir.
- clang no localiza Visual Studio 18 por sí solo: `scripts/toolchain.py` le pasa MSVC y el SDK de Windows explícitamente.
