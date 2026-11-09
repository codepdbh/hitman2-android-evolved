# Estado del proyecto

Última actualización: 2026-11-09 (sesión 1, bloque 2).
Estados: Implementado · Compilado · Probado · Parcial · Bloqueado · Pendiente.
"Probado" exige una prueba ejecutada con evidencia en `docs/evidence/` o un comando reproducible.

**En una frase:** el código original se levanta entero a C, compila para ARM64 y una muestra de él se
comporta igual que el x86 original tanto en PC como en un teléfono real; el juego todavía no arranca
porque no existen el cargador, la capa Win32, el renderizador, el audio ni los controles.

## Fases

| Fase | Estado | Resumen |
|---|---|---|
| 0 Preparación | **Probado** | Entorno inventariado, PCRecomp fijado, `WORK_SAFE_MODE` con autoprueba |
| 1 Análisis de ejecutables | **Parcial** | Inventario PE, imports, catálogo de 21.336 funciones. Falta: DLL de render, vtables, grafo por subsistema, Ghidra |
| 2 Recompilación de prueba | **Probado (x86-64 y ARM64)** | 98 funciones reales pasan la prueba diferencial; en ARM64 real los 544 vectores comparables son idénticos bit a bit al host |
| 3 Runtime funcional | **Probado (arranque), Parcial (conjunto)** | El programa recompilado completo se enlaza y **arranca desde su punto de entrada en PC y en ARM64 real**: CRT → `WinMain` → hasta el registro de la clase de ventana, con resultado idéntico en ambos. Hechos: cargador PE, despacho global, 73/151 imports, TIB, montón, handles, sistema de archivos. Falta: ventanas/mensajes, hilos, SEH/excepciones C++, carga de DLL del juego, registro |
| 4 Primer render | Pendiente | Decisión de backend tomada con evidencia estática (`docs/RENDERER_DECISION.md`) |
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
| 8 | Primeras funciones recompiladas | Probado | imagen completa: 21.336 entradas levantadas, 0 excepciones; `docs/evidence/lift_census_2026-11-09.json` |
| 9 | Pruebas diferenciales | Probado | `scripts/difftest_game.py`; `docs/evidence/difftest_2026-11-09_host_x64_and_arm64.json` |
| 10 | Runtime inicial | **Probado (arranque)** | `runtime/` (memoria, despacho, registro, parada controlada) + `platform/` (cargador PE, imports, montón, handles, archivos, KERNEL32); `docs/evidence/boot_probe_2026-11-09.txt` |
| 11 | Adaptación de renderización | Pendiente | solo decisión documentada |
| 12 | Ejecutable de prueba seguro para PC | Probado (consola) | `pc_test/boot_probe.c` vía `scripts/boot_probe.py`: sin ventana, prioridad baja, con límite de tiempo y de llamadas. No existe aún ejecutable con ventana |
| 13 | Proyecto Android compilable | **Compilado** | `android/` (Gradle 8.9, AGP 8.7.3, Kotlin 2.1.20, NDK 29, CMake 3.30.5) |
| 14 | APK de prueba | **Probado** | `docs/evidence/android_selftest_2026-11-09.txt`: 22/22 en SM-S938B. **No es el juego**: no contiene código ni datos de Hitman 2 |
| 15 | HUD táctil | Pendiente | |
| 16 | Informe de compatibilidad | Parcial | `docs/RENDERER_DECISION.md` (matriz GL→GLES, todo "Pendiente") |
| 17 | Lista de problemas pendientes | Implementado | `docs/KNOWN_ISSUES.md` |
| 18 | Evidencia de pruebas | Probado | `docs/evidence/` |

## Objetivos (§18)

| Objetivo | Estado | Nota |
|---|---|---|
| A Analizar binarios | Parcial | `hitman2.exe` catalogado; DLL de render solo a nivel de imports/strings |
| B Recompilar y validar funciones | **Probado** | 98/165 muestreadas PASS, 1 divergencia conocida, 66 inconclusas, 0 fallos sin explicar |
| C Runtime portable | **Probado hasta `WinMain`** | 3.736 llamadas internas y 583 llamadas Win32 ejecutadas, idénticas en x86-64 y ARM64; se detiene en el primer import de ventanas |
| D Dependencias Windows/render | Parcial | 73 de 151 imports del exe implementados (KERNEL32 de arranque, handles, archivos); USER32/GDI/DirectInput/DirectSound/registro y las 64+19 funciones GL, pendientes |
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

### Levantado de la imagen completa
- 21.336 entradas → 1.209.940 líneas de C en 72 unidades; 0 excepciones del lifter.
- 505 instrucciones sin traducir (`RECOMP_TODO`) en 112 funciones. La mezcla (`pushal` 114, `out` 76, `in` 64, `das` 33, `xlatb` 33, `iretd` 20…) sugiere que la mayoría son datos catalogados como código, **sin verificar**. Si se alcanzan en ejecución, abortan con la dirección; no devuelven éxito.
- 233 de 9.428 destinos de salto/llamada estáticos no tienen cuerpo levantado: huecos del catálogo por cerrar.
- "Compila" es una afirmación sobre la traducción, no sobre la corrección.

### Sonda de arranque
- Qué es: el C levantado de `hitman2.exe` completo, enlazado con `runtime/` y `platform/`, ejecutado desde el punto de entrada original. El `.exe` del usuario se carga como datos en la memoria invitada.
- Qué demuestra: que el código original recompilado inicializa su CRT, ejecuta sus inicializadores C++ y entra en `WinMain`, igual en x86-64 y en ARM64 real (mismo punto de parada, mismos contadores).
- Qué no demuestra: nada más allá de ese punto. Se detiene en `USER32!GetClassInfoExA`: no hay ventana, mensajes, render, audio ni entrada. 3.736 llamadas es una fracción mínima del programa.
- Cada parada nombra su causa (import sin implementar, instrucción sin traducir, destino sin cuerpo, fallo de memoria con dirección invitada). Ninguna función devuelve éxito sin hacer su trabajo.

### APK
- Lo que demuestra: la cadena Gradle + NDK + JNI funciona, `libh2ae.so` carga en ARM64 y el runtime portable pasa sus 22 comprobaciones en el dispositivo.
- Lo que no demuestra: nada sobre el juego. Los botones de importar e iniciar están deshabilitados y rotulados como pendientes.
