# Estado del proyecto

Última actualización: 2026-11-09 (sesión 1, bloque 2).
Estados: Implementado · Compilado · Probado · Parcial · Bloqueado · Pendiente.
"Probado" exige una prueba ejecutada con evidencia en `docs/evidence/` o un comando reproducible.

**En una frase:** el ejecutable y el renderizador OpenGL originales se levantan enteros a C y compilan para
ARM64; arrancan desde su punto de entrada, crean su ventana lógica, inicializan OpenGL y el sondeo de
dispositivos de entrada, y se detienen al pedir el sonido (`EAXDirectSoundCreate8`), con resultado
**idéntico en el PC y en un teléfono ARM64 real**. Todavía no se dibuja ni un píxel (backend gráfico
nulo), no hay audio, ni lectura de escenas, ni controles táctiles: no es jugable.

## Actualización 2026-11-11 (bloque 3)

- **El juego recompilado entra en su bucle principal** en PC y en el Galaxy S25 (ARM64): pasa el sonido, crea hilos, lee
  `Logos.ZIP`/`.whd`/`.wav`, y presenta fotogramas (40.000 en una ejecución en PC: 667 mil llamadas de dibujo, 0 errores GL;
  300 fotogramas en ARM64 con los datos leídos de `/sdcard/h2androidevolved`). **Sigue sin dibujarse ningún píxel** (backend nulo).
- **Primer fotograma real (2026-11-11):** con el backend GLES 3 (`renderer/gl_gles3_backend.c`, EGL pbuffer sin ventana, en el
  Galaxy S25) el renderizador OpenGL original dibuja una escena 3D del juego (cielo nocturno y edificios con bandas de formato
  panorámico) tras ~20.000 fotogramas, y un fotograma anterior muestra una barra roja de carga. Reproducir:
  `python scripts/boot_probe.py --no-build --android <serie> --budget 3000000000 --frames 40000 --shot 20000` (guarda
  `build/boot/shot_20000.png`; la captura no se versiona porque es salida con arte del juego). **Probado solo como captura única**:
  no hay revisión píxel a píxel contra el original, no se han comparado efectos (niebla, luces, multitextura, texgen) y los
  fotogramas intermedios están sin examinar. Aún no hay ventana (EGL de pantalla), audio audible ni entrada.
- **El juego corre dentro del APK (2026-11-11):** `GameActivity` (SurfaceView 800x450 escalada por el compositor) arranca
  `libh2game.so` (código levantado + runtime + plataforma + GLES 3, ~2 min de compilación con 4 trabajos) en un hilo de 256 MiB;
  lee los datos de `/sdcard/h2androidevolved` (requiere acceso a todos los archivos: `adb shell appops set com.h2ae.android
  MANAGE_EXTERNAL_STORAGE allow`) y escribe solo en los ficheros privados de la app. Captura de pantalla del teléfono: la secuencia
  de logos (aparece el logo de IO-Interactive). **Sin entrada táctil, sin audio audible, sin pausa/ciclo de vida** (al destruirse la
  superficie el juego se detiene). Comando: `adb shell am start -n com.h2ae.android/.GameActivity`.
- **HUD táctil, audio y entrada (2026-11-11, segunda tanda):**
  - HUD (`android/.../TouchHudView.kt`, assets en `android/app/src/main/assets/touch_hud/`): botones según el contexto del juego
    (`platform/game_context.c`: logos/introducción → solo pausa para saltar con Esc; menús → cruceta + aceptar + pausa/atrás;
    misión → joystick, disparo, acción, recargar, agacharse, correr, inventario, inclinarse y un panel «⋯» con armas,
    herramientas, mapa, informe, cámara, zoom y navegación). Teclas tomadas del `keyboard.cfg` real de la instalación, no del
    manual. El contexto se deduce del archivo de escena que el juego abre (no del estado interno): no distingue caminar de una
    escena cinemática dentro de una misión. `holster` no tiene tecla en este `keyboard.cfg` y no está conectado.
  - Audio audible: mezclador de DirectSound (`platform/dsound.c`) → AAudio (`audio/android_aaudio.c`, 44,1 kHz estéreo 16 bit);
    8/16 bit mono/estéreo, remuestreo lineal, volumen, pan, atenuación por distancia y panorama de 3D (sin Doppler, conos ni
    EAX). Verificado: el mezclador entrega muestras no nulas (pico ≈6000/32767 con un búfer sonando) y AAudio arranca; **no se ha
    verificado por oído** ni la calidad.
  - Faltaban `GetKeyNameTextA`, `MapVirtualKeyA` (tipos 0-2) y `ToAscii` (teclado EE. UU., sin teclas muertas): el juego se
    detenía en ellos al cargar la introducción; ahora la pasa y reproduce los créditos.
  - Clic táctil en menús: el juego solo usa ratón relativo de DirectInput; el toque lleva el cursor a la esquina y lo mueve
    (objetivo / `MouseSpeed`) antes de pulsar. **Sin confirmar en una pantalla de menú**: el modelo del cursor del juego es una
    suposición.
- **Teclado corregido (2026-11-11):** el juego lee el teclado con `GetDeviceData` de DirectInput usando registros de 20 bytes
  (`DIDEVICEOBJECTDATA` de DX8, con `uAppData`) y mi capa solo aceptaba 16, así que devolvía error y el juego **nunca recibía
  ninguna tecla** (tampoco las de `keyboard.cfg` en misión). Ahora acepta 16 y 20. Comprobado en el teléfono: el botón de pausa
  (Esc) salta los logos y la introducción pasa a `IntroSequence` en ~55 s en lugar de >4 min. Además las teclas se envían también
  como `WM_KEYDOWN/UP` a la ventana. Las pulsaciones de ratón siguen por el estado inmediato de DirectInput.
- Arranque directo a una escena para pruebas: `adb shell am start -n com.h2ae.android/.GameActivity --es scene MainMenu.gms`
  (reescribe `DefaultScene` en el ini del perfil; solo si la actividad no estaba ya abierta).
- Limitación: si el sistema destruye la superficie (pantalla apagada, app en segundo plano) el juego se detiene y no se reanuda.
- **HUD rediseñado (2026-11-11, tercera tanda):** por defecto solo 6 botones en misión (disparo, acción, agacharse, correr,
  inventario, pausa) + joystick + «⋯» (el resto va al panel) + ⚙; en logos/introducción un botón rotulado **SALTAR ▶▶** (Esc +
  clic); en menús cruceta, aceptar, atrás y ⚙. El ⚙ abre los ajustes: elegir qué botones quedan en pantalla, mover arrastrando,
  tamaño, transparencia, ocultar el panel y restaurar (guardado en SharedPreferences). No se reasignan las teclas: son las del
  `keyboard.cfg` del juego. Comprobado en el S25: SALTAR visible y funcional en la introducción, menú principal con cruceta/ajustes;
  **sin comprobar**: el arrastre de botones, los diálogos de ajustes, ni el HUD de misión (no se ha llegado a una misión).
- Menú principal alcanzado en el teléfono (pantalla de opciones del juego, en español) tras saltar la introducción. Una parada en
  `0059EAF4` (salto indirecto del `memcpy` sin cuerpo) se cerró con `config/lift_extra_entries.json`.
- **Primera misión en el teléfono (2026-11-11):** desde el menú principal (Empezar → dificultad → Normal) el juego carga
  `C0-1` (el cobertizo del tutorial) y se ve la escena 3D con HUD del juego, brújula y subtítulos; el joystick táctil (W) mueve
  al personaje (la pared se acerca entre dos capturas). Para aceptar en los menús el botón envía Enter **y** E (el juego usa
  `DoAction` = E en `keyboard.cfg`; Enter solo no seleccionaba). `RaiseException` implementado solo para el nombre de hilo del
  depurador (0x406D1388); cualquier otra excepción sigue parando el invitado (era la parada al empezar la misión).
  **Sin probar:** girar la cámara, disparar, inventario, el resto de acciones, guardar/cargar, el rendimiento sostenido, las demás
  misiones, y que el audio de la misión suene bien. Tampoco hay comprobación píxel a píxel contra el original.
- Nuevo: DirectSound 8 (`platform/dsound.c`: dispositivo, búfer primario/secundarios, 3D listener/buffer con parámetros
  guardados; sumidero nulo con reloj real, sin mezclador ni salida audible; EAX/IKsPropertySet y efectos devuelven
  `E_NOINTERFACE`), hilos de invitado cooperativos (`platform/win32_threads.c`: `CreateThread`, `Sleep`, `Wait*`, secciones
  críticas; uno a la vez, prioridades sin efecto), `FindFirstFileA`/`FindNextFileA`/`FindClose`, textura 0 de GL.
- Lifter: cierre de huecos (`gap_entries_added` = 253 entradas extra para destinos de salto que el catálogo no tenía): destinos
  sin cuerpo en el exe 195 → 2.
- Los contadores de una ejecución dependen del reloj de pared, así que PC y ARM64 ya no dan contadores idénticos; la
  comparación válida es el motivo de parada y la ausencia de errores.
- Datos del juego en el teléfono: `/sdcard/h2androidevolved` (copia del usuario, fuera del repositorio).

## Fases

| Fase | Estado | Resumen |
|---|---|---|
| 0 Preparación | **Probado** | Entorno inventariado, PCRecomp fijado, `WORK_SAFE_MODE` con autoprueba |
| 1 Análisis de ejecutables | **Parcial** | Inventario PE, imports, catálogo de 21.336 funciones. Falta: DLL de render, vtables, grafo por subsistema, Ghidra |
| 2 Recompilación de prueba | **Probado (x86-64 y ARM64)** | 98 funciones reales pasan la prueba diferencial; en ARM64 real los 544 vectores comparables son idénticos bit a bit al host |
| 3 Runtime funcional | **Probado (arranque), Parcial (conjunto)** | Programa completo + `RenderOpenGL.dll` recompilados, enlazados y ejecutados desde el punto de entrada **en PC y en ARM64 real con resultado idéntico**: CRT → `WinMain` → ventana → carga del DLL del renderizador (`DllMain`, 28 imports contra las exportaciones reales del exe) → `Create` → contexto GL → sondeo de extensiones → DirectInput → ini/registro → DirectSound. Hechos: cargador PE multi-imagen, despacho global, 116/151 imports del exe y 105/105 de los del renderizador salvo 5, TIB, montón, handles, archivos, ventanas y cola de mensajes, DirectInput 8, registro. Falta: DirectSound/EAX, hilos, SEH/excepciones C++, bucle de mensajes con fuente de entrada, escenas |
| 4 Primer render | **Parcial** | La capa OpenGL de función fija (`renderer/`) implementa los 64 imports GL del renderizador original más multitextura/`blend_subtract` y es **ejercitada por el código original** (formato de píxel, contexto, 2 texturas, `glClear`). Backend nulo (cuenta, no dibuja). Pendiente: backend GLES 3 con superficie, primer fotograma real |
| 5 Carga de escenas | Pendiente | Solo reconocimiento del contenedor (ZIP sin cifrar, 13 tipos de fichero por escena) |
| 6 Integración Android | **Parcial** | APK de diagnóstico compilado, instalado y ejecutado en el dispositivo. Sin superficie gráfica, entrada, audio ni importación de datos |
| 7 Jugabilidad | Pendiente | |
| 8 Optimización | Pendiente | |

## Entregables (§17 del encargo)

| # | Entregable | Estado | Dónde / evidencia |
|---|---|---|---|
| 1 | Repositorio estructurado | Implementado | raíz; `git init` hecho, **sin commits** |
| 2 | `README.md` con instrucciones verificadas | Probado | cada comando del README se ejecutó en esta sesión |
| 3 | `AGENTS.md` no intrusivo | Implementado | `AGENTS.md` |
| 4 | Inventario de binarios | Probado | `analysis/pe_inventory.json`, `binary_versions.json`, `modules.json` |
| 5 | Informe de imports y dependencias | Probado | `analysis/imports_report.md`, `analysis/dependencies.md` |
| 6 | Mapa de arquitectura Glacier | Parcial | `analysis/glacier_map.md` (por rutas `__FILE__` y exports; sin asignar funciones a subsistemas) |
| 7 | Pipeline reproducible de PCRecomp | Parcial | `prepare_image` → `disasm32` → `lift_image` → `difftest_game`; solo `hitman2.exe`, falta `RenderOpenGL.dll` |
| 8 | Primeras funciones recompiladas | Probado | exe 21.336/21.336 y renderizador 1.706/1.706 entradas levantadas, 0 excepciones del lifter; `docs/evidence/lift_census_*_2026-11-10.json` |
| 9 | Pruebas diferenciales | Probado | `scripts/difftest_game.py`; repetida el 2026-11-10 tras cambiar el lifter: mismos resultados (`docs/evidence/difftest_2026-11-10_host_x64_and_arm64.json`) |
| 10 | Runtime inicial | **Probado (arranque)** | `runtime/` (memoria, despacho con diagnóstico, registro, parada controlada) + `platform/` (cargador PE multi-imagen, imports, montón, handles, archivos, KERNEL32, USER32, DirectInput, registro) + `input/`; `docs/evidence/boot_probe_2026-11-10.txt` |
| 11 | Adaptación de renderización | **Parcial** | `renderer/h2_gl.c` + backend nulo; decisión en `docs/RENDERER_DECISION.md`. Sin backend gráfico real |
| 12 | Ejecutable de prueba seguro para PC | Probado (consola) | `pc_test/boot_probe.c` vía `scripts/boot_probe.py`: sin ventana, prioridad baja, con límite de tiempo y de llamadas. No existe aún ejecutable con ventana |
| 13 | Proyecto Android compilable | **Compilado** | `android/` (Gradle 8.9, AGP 8.7.3, Kotlin 2.1.20, NDK 29, CMake 3.30.5) |
| 14 | APK de prueba | **Probado** | `docs/evidence/android_selftest_2026-11-09.txt`: 22/22 en SM-S938B. **No es el juego**: no contiene código ni datos de Hitman 2 |
| 15 | HUD táctil | **Parcial** | Assets visuales implementados en `android/app/src/main/assets/touch_hud/`; catálogo y equivalencias en `design/touch_hud/README.md`. La superposición, el multitáctil y la conexión con la entrada siguen pendientes. |
| 16 | Informe de compatibilidad | Parcial | `docs/RENDERER_DECISION.md` (matriz GL→GLES, todo "Pendiente") |
| 17 | Lista de problemas pendientes | Implementado | `docs/KNOWN_ISSUES.md` |
| 18 | Evidencia de pruebas | Probado | `docs/evidence/` |

## Objetivos (§18)

| Objetivo | Estado | Nota |
|---|---|---|
| A Analizar binarios | Parcial | `hitman2.exe` catalogado; DLL de render solo a nivel de imports/strings |
| B Recompilar y validar funciones | **Probado** | 98/165 muestreadas PASS, 1 divergencia conocida, 66 inconclusas, 0 fallos sin explicar |
| C Runtime portable | **Probado hasta la inicialización de sonido** | 104.926 llamadas internas y 8.554 llamadas Win32/GL/DirectInput ejecutadas, idénticas en x86-64 y ARM64; se detiene en `EAX.DLL!#6` |
| D Dependencias Windows/render | **Parcial, avanzado** | `tools/missing_imports.py`: 39 imports sin implementar en total (31 KERNEL32 del exe —archivos/hilos/SEH/atoms—, 3 USER32, DirectSound, y 5 KERNEL32 del renderizador). OpenGL 64/64, USER32 y GDI32 del renderizador completos |
| E Código nativo ARM64 | **Probado para la muestra, Compilado para la imagen completa** | muestra ejecutada en SM-S938B; las 72 unidades de `hitman2.exe` compilan (27 MB de objetos) pero no se han enlazado ni ejecutado |
| F–J | Pendiente | |

## Qué significan exactamente los números

### Prueba diferencial
- Muestra: 165 funciones de `hitman2.exe` (40 por clase: enteras, x87, cadenas, `switch`; 5 con llamadas directas), 8 estados de máquina pseudoaleatorios cada una, semilla 1.
- Referencia: los bytes originales bajo Unicorn (x86-32). Candidato: C generado por `lift32_cpu` + overrides del proyecto, compilado con clang, con memoria invitada en una reserva de 4 GiB.
- Se compara: los 8 registros (ESP incluido), cada byte de memoria modificado, profundidad x87 y ST(0). EFLAGS se compara aparte (20 vectores PASS difieren solo en flags muertos al retorno).
- ARM64: el mismo trabajo ejecutado en el teléfono por adb. 539 PASS, 1 FAIL (la divergencia conocida de `fprem`), 4 inconclusos; 544/544 idénticos al resultado del host.
- **INCONCLUSA no es un aprobado**: la referencia no pudo completar ninguno de los vectores (punteros aleatorios inválidos, entradas x87 no suministradas). 66 funciones siguen sin veredicto.
- No cubre: llamadas indirectas/virtuales, acceso `fs:` (SEH), imports, hilos. 6.286 + 1.679 funciones quedaron fuera de la muestra por eso.
- La muestra es el 0,8 % de las funciones del ejecutable.

### Levantado de la imagen completa (2026-11-10)
| Módulo | Entradas | Líneas de C | `RECOMP_TODO` | Destinos sin cuerpo | Compila host / ARM64 |
|---|--:|--:|--:|--:|---|
| `hitman2.exe` | 21.336 | 1.061.774 | 29 sitios en 23 funciones (`int` 16, `fpu` 6, `x87` 5, `int1` 2) | 195 de 9.021 | 72/72 · 72/72 |
| `RenderOpenGL.dll` | 1.706 | 122.185 | 0 | 11 de 864 | 6/6 · 6/6 |

- Antes del decodificador por flujo de control el exe tenía **505** sitios sin traducir (298 tras añadir `pushal`…): la mayoría eran **datos incrustados en el código** (tablas de saltos del `memmove`/`memcpy` del CRT) decodificados como instrucciones. Ahora son 29; se confirmó la sospecha de la sesión anterior.
- Una instrucción sin traducir que se alcanza detiene el invitado con su dirección; no devuelve éxito.
- Destinos sin cuerpo: huecos del catálogo (se ven como `unresolved-dispatch` si se alcanzan).
- "Compila" es una afirmación sobre la traducción, no sobre la corrección.

### Sonda de arranque
- Qué es: el C levantado de `hitman2.exe` completo, enlazado con `runtime/` y `platform/`, ejecutado desde el punto de entrada original. El `.exe` del usuario se carga como datos en la memoria invitada.
- Qué demuestra: que el código original recompilado inicializa su CRT, entra en `WinMain`, lee su configuración (`Hitman2.ini` del perfil del puerto), carga y arranca el DLL del renderizador, inicializa OpenGL a través de la capa del proyecto y sondea DirectInput, igual en x86-64 y en ARM64 real (mismo punto de parada, mismos contadores). También que el sondeo de extensiones del renderizador original funciona contra lo que la capa anuncia.
- Qué no demuestra: nada más allá de ese punto. Se detiene en `EAX.DLL!#6`: no hay audio, bucle de mensajes con entrada, carga de escenas ni un solo píxel dibujado. 104.926 llamadas es una fracción pequeña del programa.
- Cada parada nombra su causa (import sin implementar, instrucción sin traducir, destino sin cuerpo, fallo de memoria con dirección invitada). Ninguna función devuelve éxito sin hacer su trabajo.

### APK
- Lo que demuestra: la cadena Gradle + NDK + JNI funciona, `libh2ae.so` carga en ARM64 y el runtime portable pasa sus 22 comprobaciones en el dispositivo.
- Lo que no demuestra: nada sobre el juego. Los botones de importar e iniciar están deshabilitados y rotulados como pendientes.
