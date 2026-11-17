# Hitman 2: Android Evolved

Port experimental nativo ARM64 de **Hitman 2: Silent Assassin** (PC, 2002, motor Glacier), mediante recompilación estática con [PCRecomp](https://github.com/sp00nznet/pcrecomp). Sin Wine, Winlator ni streaming.

**Estado: Parcial / preview — 17 de noviembre de 2026.** Se ha llegado al menú principal y al tutorial con gráficos, audio y entrada táctil en un Samsung Galaxy S25 Ultra (SM-S938B). No se ha validado el juego completo. La corrección más reciente del cursor y los perfiles gráficos nuevos están compilados, pero su prueba en dispositivo sigue pendiente.

**[Descargar APK v0.2.0-preview.1](https://github.com/codepdbh/hitman2-android-evolved/releases/tag/v0.2.0-preview.1)** · [Todas las releases](https://github.com/codepdbh/hitman2-android-evolved/releases) · [Estado y evidencia](docs/STATUS.md)

## Antes de empezar

- Necesitas **tu propia copia legítima de la versión de PC**. No sirve una edición de consola ni otro juego de la serie.
- Para esta guía: **Android 11 o superior**, CPU **ARM64** y GPU con **OpenGL ES 3.0 o superior**. No se incluye una versión de 32 bits. La compatibilidad física solo se ha comprobado en el S25 indicado.
- Necesitas espacio para el APK y todos los datos de tu copia. Copia la carpeta `Scenes` completa para disponer de las escenas de esa instalación.
- Esta preview se recompiló para unos binarios concretos. **Otras ediciones o ejecutables pueden fallar**, aunque se llamen igual. La comprobación de archivos del launcher no comprueba todavía sus hashes ni todas las misiones.

El APK incluye el programa recompilado y las capas del port; **no incluye los datos, escenas ni audio originales**. Los originales de PC también se necesitan en el teléfono: el cargador lee sus imágenes y datos. El repositorio fuente excluye los archivos comerciales, el C generado y los catálogos locales.

### Binarios de referencia de esta preview

`hitman2.exe`: versión de archivo **1.0.0.265**. SHA-256:

```text
0d7742c85e328886e91152036ae6615ca4c98bbb97c7a36d8f9e150235e57560
```

`RenderOpenGL.dll`, SHA-256:

```text
5d16962fd33268739abad0a8202d6c515f41ef1983f0d9f697d43000e4802c17
```

Puedes comparar los tuyos en PowerShell con `Get-FileHash -Algorithm SHA256 -LiteralPath 'ruta\hitman2.exe'` y lo mismo para el DLL. Si no coinciden, la compatibilidad de esta preview con tu edición está **sin verificar**. Los detalles del inventario están en [analysis/binary_versions.json](analysis/binary_versions.json). No se proporciona ningún método para eliminar protección de copia.

## Instalar y copiar tu juego al teléfono

### 1. Instala el APK

1. Abre la [release](https://github.com/codepdbh/hitman2-android-evolved/releases/tag/v0.2.0-preview.1) y descarga el archivo `Hitman2-AndroidEvolved-v0.2.0-preview.1-arm64.apk` de **Assets**. Los archivos «Source code» son el código fuente, no la app.
2. Abre el APK en el teléfono. Si Android lo solicita, permite instalar aplicaciones desde el navegador o gestor de archivos que estés usando.
3. Si ya tienes una compilación anterior firmada con la misma clave, instala encima para conservar los datos. **Desinstalar borra el perfil privado y las partidas de la app.**

Es un APK de desarrollo firmado para esta preview; no es una publicación de producción en Google Play. La release incluye `SHA256SUMS.txt` para comprobar la descarga.

### 2. Copia los archivos de tu instalación de PC

1. Conecta el teléfono por USB, desbloquéalo y elige **Transferencia de archivos** en la conexión USB.
2. En el almacenamiento interno del teléfono crea una carpeta llamada **`h2androidevolved`**. Debe quedar en la raíz del almacenamiento compartido, no dentro de `Android/data`.
3. Desde tu instalación de PC copia estos **cinco archivos** y la carpeta **`Scenes` completa**, con todas sus subcarpetas y archivos:

```text
Almacenamiento interno/
└── h2androidevolved/
    ├── hitman2.exe
    ├── RenderOpenGL.dll
    ├── Hitman2.ini
    ├── keyboard.cfg
    ├── streams.wav
    └── Scenes/
        ├── MainMenu.ZIP
        ├── MainMenu.WHD
        ├── MainMenu.WAV
        ├── AllLevels/
        ├── C0-1/
        └── … todas las demás carpetas y archivos de Scenes
```

Conserva los nombres y la estructura; **no extraigas los ZIP del juego**. Si tu gestor muestra `Keyboard.cfg` con otra capitalización, usa el nombre `keyboard.cfg` en el destino para la comprobación del launcher.

La ruta predeterminada en Android es:

```text
/sdcard/h2androidevolved
```

Los archivos deben estar directamente ahí: evita una carpeta adicional como `h2androidevolved/Hitman 2 Silent Assassin/`. También puedes elegir una carpeta existente distinta desde **Sistema → Cambiar carpeta** en el launcher.

### 3. Autoriza los archivos y entra al juego

1. Abre **Hitman 2 AE**.
2. Pulsa **Permitir acceso** o entra en **Sistema → Permiso de acceso a archivos**. En los ajustes de Android habilita **Permitir acceso para administrar todos los archivos** para esta app y vuelve al launcher.
3. Pulsa **Sistema → Verificar archivos**. Si faltan archivos, revisa la ruta y termina de copiar `Scenes`.
4. Cuando aparezca **Listo para jugar**, pulsa **Jugar**. Por defecto se omiten los logos y se abre el menú principal.
5. Usa la cruceta y **Aceptar** para navegar. En el menú del juego puedes empezar una partida o cargar una guardada por el port. La selección directa con el puntero tiene una corrección nueva pendiente de validar.

El port lee los originales del teléfono y escribe el perfil y las partidas en el almacenamiento privado de la app. No modifica tu instalación de PC.

### Alternativa: copiar mediante ADB

Es opcional; la transferencia USB anterior no necesita depuración USB. Si ya tienes ADB instalado y el teléfono autorizado, estos comandos de PowerShell copian los mismos archivos. Ajusta `$gameCopy` a tu instalación y usa `adb -s SERIE` si hay varios dispositivos:

```powershell
$gameCopy = 'C:\Juegos\Hitman 2 Silent Assassin'
adb devices
adb install -r '.\Hitman2-AndroidEvolved-v0.2.0-preview.1-arm64.apk'
adb shell mkdir -p /sdcard/h2androidevolved
adb push "$gameCopy\hitman2.exe" "$gameCopy\RenderOpenGL.dll" "$gameCopy\Hitman2.ini" "$gameCopy\keyboard.cfg" "$gameCopy\streams.wav" /sdcard/h2androidevolved/
adb push "$gameCopy\Scenes" /sdcard/h2androidevolved/
adb shell appops set com.h2ae.android MANAGE_EXTERNAL_STORAGE allow
adb shell am start -n com.h2ae.android/.MainActivity
```

## Controles y configuración

- **Joystick izquierdo:** caminar; estirar hacia delante más allá del borde para correr.
- **Zona libre:** arrastrar para mover la cámara.
- **Disparar, Acción y Agacharse:** mantener pulsado.
- **EQUIPO:** inventario, recarga, armas y otras acciones por categorías; incluye ajustes para fijar y mover botones.
- **Pausa:** menú del juego. Guarda desde el menú del juego antes de cerrar la app.

En el launcher puedes ajustar sensibilidad de cámara, tamaño/visibilidad de botones, correr con joystick y respuesta háptica. Se aplican también al reanudar. Voces, música, efectos, subtítulos, clima y perfil gráfico se aplican al próximo arranque del motor; reanudar una sesión ya cargada no reinicia ese motor.

**Idiomas:** Español / English cambia launcher, botones y ayuda. El idioma del juego depende de sus datos: con la copia española disponible, el juego sigue en español. Esta preview no incluye los recursos ingleses.

**Gráficos:** empieza con **Audio / GPU → Perfil gráfico → Automático**. Los perfiles Mali y Unisoc son experimentales; Snapdragon/Adreno también tiene un perfil explícito. Todos usan el **driver OpenGL ES de Android**: no instalan Turnip ni otros drivers externos. El modo de menor profundidad puede reducir la precisión visual. La información del GPU y driver aparece después de iniciar el juego.

**Mandos:** todavía no hay mapeo jugable de mandos USB/Bluetooth. Que Android detecte un mando no significa que el juego lo admita.

Más detalles: [launcher](docs/LAUNCHER.md) · [controles táctiles](docs/TOUCH_CONTROLS.md).

## Si no arranca

| Síntoma | Qué revisar |
|---|---|
| «Faltan archivos» | Carpeta seleccionada, cinco archivos de raíz y `Scenes` completa; sin carpetas adicionales ni ZIP extraídos |
| «Falta permiso» | Habilitar el acceso a todos los archivos para **Hitman 2 AE** y volver al launcher |
| Error al cargar la biblioteca | Teléfono ARM64; APK ARM64; revisar el mensaje mostrado y los requisitos de GLES 3 |
| Pantalla negra o parada del motor | Esta edición del juego puede ser distinta; comparar hashes. Cerrar la app por completo antes de volver a iniciar un motor detenido |
| El puntero del menú no selecciona | Usar cruceta y Aceptar; la corrección de clic/arrastre más reciente sigue pendiente de prueba física |
| Se ve mal con un perfil GPU | Volver a Automático y arrancar de nuevo el motor; Mali y Unisoc aún no tienen validación en hardware |

La suspensión y reentrada conservando la superficie se comprobaron en un caso del S25. No hay recuperación automática de una misión si Android termina el proceso. Tampoco se han validado todas las misiones, guardar/cargar de extremo a extremo ni el rendimiento sostenido. Consulta [problemas conocidos](docs/KNOWN_ISSUES.md) y [evidencia de esta versión](docs/evidence/launcher_2026-11-17.md).

## Desarrollo y comprobaciones

El proyecto usa Kotlin/JNI, C11, Android SDK 35 y NDK `29.0.14206865`. La compilación local se hizo en Windows con JBR de Android Studio, CMake 3.30.5, Python, LLVM y las herramientas MSVC/Windows SDK. Las pruebas diferenciales requieren además `capstone`, `pefile` y `unicorn`.

Los datos originales van en `Hitman 2 Silent Assassin/`, ignorada por Git. PCRecomp permanece sin modificar en `third_party/pcrecomp`, fijado por [config/dependencies.json](config/dependencies.json). Preparar los catálogos y levantar el código de **tu propia copia** es un paso local; esos resultados no se descargan del repositorio ni se suben a Git.

```powershell
git clone https://github.com/sp00nznet/pcrecomp third_party/pcrecomp
git -C third_party/pcrecomp checkout 2adc02fec0702933325801aa7b29a7646321857f
python scripts/worksafe.py --selftest
python scripts/toolchain.py
python tools/pe_inventory.py
python tools/prepare_image.py 'Hitman 2 Silent Assassin/hitman2.exe'
New-Item -ItemType Directory -Force recomp/catalog | Out-Null
python third_party/pcrecomp/tools/disasm/disasm32.py build/work/hitman2.norm.exe --output recomp/catalog/hitman2.functions.json
python third_party/pcrecomp/tools/disasm/disasm32.py 'Hitman 2 Silent Assassin/RenderOpenGL.dll' --output recomp/catalog/renderopengl.functions.json
python scripts/lift_image.py lift
python scripts/lift_image.py compile --target arm64
python scripts/test_graphics_config.py
python scripts/audit_repo.py
```

Después configura `android/local.properties` con la ruta local del SDK (no se versiona) y `JAVA_HOME` con tu JDK. Para compilar y ejecutar las pruebas desde la raíz del repositorio, sin abrir ventanas:

```powershell
(Get-Process -Id $PID).PriorityClass = 'BelowNormal'
@'
import os
from scripts.worksafe import run, max_jobs
run([
    os.path.join(os.environ['JAVA_HOME'], 'bin', 'java.exe'),
    '-classpath', 'android/gradle/wrapper/gradle-wrapper.jar',
    'org.gradle.wrapper.GradleWrapperMain', '-p', 'android',
    ':app:assembleDebug', ':app:testDebugUnitTest', '--console=plain',
    '--max-workers', str(min(2, max_jobs())),
    '-Pkotlin.compiler.execution.strategy=in-process'
], timeout=900, check=True)
'@ | python -
```

El APK queda en `android/app/build/outputs/apk/debug/app-debug.apk`. Los artefactos y logs de compilación no se suben a Git.

La preview tiene **16 pruebas unitarias de la app** y **nueve casos del selector EGL** aprobados. Estas pruebas no sustituyen una prueba del juego en cada GPU ni una comparación diferencial del código recompilado. El historial de pruebas diferenciales y sus límites está en [docs/STATUS.md](docs/STATUS.md).

Las reglas de trabajo no intrusivo están en [AGENTS.md](AGENTS.md): no ejecutar los originales en Windows, no alterar la pantalla ni capturar entradas globales, respetar los límites de recursos y auditar antes de cada commit.

## Licencias y alcance

PCRecomp se usa según su licencia MIT. *Hitman 2: Silent Assassin*, su código, marcas y recursos pertenecen a sus respectivos titulares; este es un proyecto no oficial y experimental. El repositorio no concede derechos sobre el juego ni contiene una copia de sus datos. Debes aportar una copia legítima compatible. No se incluye funcionalidad para eludir protección de copia.
