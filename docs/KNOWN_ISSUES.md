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

## Límites actuales de la capa de plataforma (`platform/`)

- **Siguiente bloqueo del arranque**: clases de ventana, ventanas y cola de mensajes (`GetClassInfoExA`, `RegisterClassExA`, `CreateWindowExA`, `PeekMessageA`…). Hay que diseñarlos sobre una única superficie nativa en Android y sin ventana por defecto en el PC.
- **Un solo hilo invitado.** `CreateThread` no existe; las secciones críticas y los mutex solo son correctos mientras sea así, y una espera infinita sobre un evento no señalado detiene al invitado con un mensaje en vez de colgarse.
- **Sin SEH ni excepciones C++ del invitado.** El TIB simulado existe y los prólogos SEH funcionan, pero `RtlUnwind`, `RaiseException` y `UnhandledExceptionFilter` no están implementados: un `throw` del juego detendrá la ejecución nombrando el import.
- **Sin carga de DLL del juego.** `LoadLibraryA` solo conoce los DLL de sistema; el renderizador (`RenderOpenGL.dll`) debe levantarse y mapearse como segunda imagen.
- **Pila del host.** Las llamadas invitadas anidan en la pila nativa: la sonda usa 256 MiB. En la app Android el invitado deberá correr en un hilo propio con pila grande.
- **Reloj en UTC**, sin zona horaria. **Código de página 1252** fijo. `LCMapString` solo convierte mayúsculas/minúsculas.
- **Memoria**: `VirtualAlloc` no reutiliza el espacio liberado y el montón no fusiona bloques libres (solo reutiliza tamaños iguales). Suficiente para arrancar; habrá que medirlo con una escena cargada.
- **Sistema de archivos sin estrenar**: `CreateFileA`/`ReadFile`/`WriteFile`/`SetFilePointer`/`GetFileSize` están implementados (raíz del juego de solo lectura + raíz de usuario con copia al escribir) pero el programa aún no los ha llamado; faltan `FindFirstFileA` y familia, borrado, renombrado y consultas de unidad.
- Registro (`ADVAPI32`), `GDI32`, `DINPUT8`, `DSOUND`, `EAX`, `P5DLL`: sin implementar.

## Huecos medidos al levantar la imagen completa

- 505 `RECOMP_TODO` en 112 funciones (detalle en `docs/evidence/lift_census_2026-11-09.json`). Pendiente clasificar cada función como "dato mal catalogado" o "código real con instrucción no soportada"; `pushal`/`popal`, `rdtsc`, `clc`/`stc`/`cmc` y `xlatb` son instrucciones reales que el lifter debería cubrir.
- 233 destinos estáticos de `dispatch` sin cuerpo levantado.
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
