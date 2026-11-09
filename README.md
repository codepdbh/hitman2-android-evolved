# Hitman 2: Android Evolved

Port nativo ARM64 para Android de *Hitman 2: Silent Assassin* (PC, 2002, motor Glacier)
mediante recompilación estática con [PCRecomp](https://github.com/sp00nznet/pcrecomp).
Sin Wine, sin Winlator, sin emulación permanente de x86, sin streaming.

> **Estado real (2026-11-09): laboratorio, no juego.** El ejecutable original se levanta
> entero a C y compila para ARM64; una muestra de sus funciones se comporta igual que el
> x86 original en PC y en un teléfono real. El APK que existe es de diagnóstico: no
> contiene ni ejecuta el juego. No hay render, audio ni controles.
> El detalle está en [docs/STATUS.md](docs/STATUS.md).

Este repositorio no contiene ni contendrá el juego, sus recursos ni código derivado
de sus binarios. Necesitas tu propia copia legítima.

## Qué funciona hoy

- Inventario PE de los 11 módulos de la instalación, con hashes, imports y dependencias.
- Recuperación de funciones de `hitman2.exe`: 21.336 entradas, 95,0 % de `.text`.
- Lifting x86-32 → C con `lift32_cpu` de PCRecomp más correcciones propias
  ([recomp/h2_lifter.py](recomp/h2_lifter.py)).
- Memoria invitada independiente del ancho de puntero
  ([runtime/guest_mem.h](runtime/guest_mem.h)): reserva de 4 GiB, una dirección x86 nunca es un puntero host.
- Prueba diferencial de funciones reales contra Unicorn: **98 PASS, 1 divergencia conocida,
  66 inconclusas, 0 fallos sin explicar** sobre 165 funciones muestreadas.
- La misma prueba en ARM64 real (Galaxy S25 Ultra, por adb): 544/544 vectores idénticos al host.
- `hitman2.exe` completo: 21.336 entradas levantadas, 1,21 M de líneas de C, 72/72 unidades
  compiladas para `arm64-v8a`. Quedan 505 instrucciones sin traducir en 112 funciones.
- APK de diagnóstico (`android/`): carga `libh2ae.so` y pasa la autoprueba del runtime (22/22) en el dispositivo.

## Requisitos (los que se usaron)

Windows 11 x64 · Python 3.14 con `capstone`, `pefile`, `unicorn` · Git · LLVM/clang 22 ·
Visual Studio (librerías MSVC x64) + Windows SDK · Android NDK 29 (opcional, para ARM64).

`python scripts/toolchain.py` muestra qué encuentra.

## Reproducir

Todos los pasos son de consola, sin ventanas, a prioridad baja. La carpeta del juego
debe estar en `Hitman 2 Silent Assassin/` (ignorada por git).

```powershell
# 0. PCRecomp en la revisión fijada (config/dependencies.json)
git clone https://github.com/sp00nznet/pcrecomp third_party/pcrecomp
git -C third_party/pcrecomp checkout 2adc02fec0702933325801aa7b29a7646321857f

# 1. Comprobar que el modo no intrusivo se aplica de verdad
python scripts/worksafe.py --selftest

# 2. Inventario de binarios -> analysis/
python tools/pe_inventory.py

# 3. Copia de análisis con flags de sección corregidas (el original no se toca)
python tools/prepare_image.py "Hitman 2 Silent Assassin/hitman2.exe"

# 4. Catálogo de funciones (unos 3 minutos, un núcleo)
python third_party/pcrecomp/tools/disasm/disasm32.py build/work/hitman2.norm.exe `
       --output recomp/catalog/hitman2.functions.json

# 5. Lifting + compilación + prueba diferencial (unos 15 segundos)
python scripts/difftest_game.py --per-kind 40 --vectors 8 --seed 1
#    opcional, con un dispositivo en adb:
python scripts/difftest_game.py --android <serie>

# 6. Levantar el ejecutable completo y compilarlo para ARM64 (unos 10 s + 1 min)
python scripts/lift_image.py lift
python scripts/lift_image.py compile --target arm64

# 7. Autoprueba del runtime en el PC
python scripts/run_selftest.py

# 8. APK de diagnóstico (JDK 17-21; se usó el JBR de Android Studio)
python tools/make_icons.py
$env:JAVA_HOME = "C:\Program Files\Android\Android Studio\jbr"
cd android; .\gradlew.bat assembleDebug --console=plain; cd ..
adb install -r android/app/build/outputs/apk/debug/app-debug.apk
adb shell am start -n com.h2ae.android/.MainActivity --ez selftest true
adb logcat -d -s "H2AE:*"          # SELFTEST pass=22 fail=0

# 9. Antes de cualquier commit
python scripts/audit_repo.py
```

`android/local.properties` (ruta del SDK) es local y no se versiona.

El informe queda en `build/difftest/report.json`; una copia sin código derivado está en
[docs/evidence/](docs/evidence/).

## Estructura

| Ruta | Contenido |
|---|---|
| `AGENTS.md` | Reglas permanentes: no interrumpir el PC, honestidad de resultados, límites legales |
| `config/` | `work_safe.json` (perfil `WORK_SAFE_MODE`), dependencias fijadas |
| `scripts/` | `worksafe.py` (lanzador obligatorio), `toolchain.py`, `difftest_game.py`, `audit_repo.py` |
| `tools/` | `pe_inventory.py`, `prepare_image.py`, `gen_portable_cpu.py` |
| `recomp/` | `h2_lifter.py`; `catalog/` y `generated/` son locales y no se versionan |
| `runtime/` | Memoria invitada y ayudas x87 |
| `tests/difftest/` | Arnés C y lista de divergencias conocidas |
| `analysis/` | Informes de ingeniería inversa |
| `docs/` | Estado, problemas conocidos, decisión de renderizador, evidencia |
| `third_party/pcrecomp/` | PCRecomp sin modificar (no versionado aquí) |

| `android/` | App de diagnóstico (Kotlin + JNI + CMake); `branding/icono.png` es el icono fuente |

`platform/`, `renderer/`, `input/`, `audio/` y `pc_test/` se crearán cuando tengan contenido real.

## Documentos

- [docs/STATUS.md](docs/STATUS.md) — estado por fase, entregable y objetivo
- [docs/KNOWN_ISSUES.md](docs/KNOWN_ISSUES.md) — bloqueos y divergencias
- [docs/RENDERER_DECISION.md](docs/RENDERER_DECISION.md) — por qué OpenGL → OpenGL ES
- [analysis/glacier_map.md](analysis/glacier_map.md) — lo que el binario dice del motor

## Licencia y límites

PCRecomp es MIT. El C generado a partir del juego es obra derivada del juego y no se
distribuye. No se incluye ni se desarrolla nada para eludir protección anticopia.
