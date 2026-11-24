# Hitman 2: Android Evolved

Port experimental nativo ARM64 de **Hitman 2: Silent Assassin** (PC, 2002, motor Glacier), mediante recompilación estática con [PCRecomp](https://github.com/sp00nznet/pcrecomp). Sin Wine, Winlator ni streaming.

**Estado: Parcial / preview — 24 de noviembre de 2026.** El juego arranca, muestra el menú principal y la primera misión (el tutorial del cobertizo) con gráficos, audio, controles táctiles y mando, **comprobado en dos teléfonos**: Samsung Galaxy S25 Ultra (Adreno 830, 120 fps estables) y Xiaomi Redmi Note 8 (Adreno 610, 52–60 fps según la resolución). No se ha validado el juego completo: faltan guardar/cargar de extremo a extremo, el resto de misiones y probar el mando con un mando real.

**[Descargar APK v0.3.0-preview.1](https://github.com/codepdbh/hitman2-android-evolved/releases/tag/v0.3.0-preview.1)** · [Todas las releases](https://github.com/codepdbh/hitman2-android-evolved/releases) · [Estado y evidencia](docs/STATUS.md) · [Qué seguirá mejorando](#qué-seguirá-mejorando)

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

1. Abre la [release](https://github.com/codepdbh/hitman2-android-evolved/releases/tag/v0.3.0-preview.1) y descarga el archivo `Hitman2-AndroidEvolved-v0.3.0-preview.1-arm64.apk` de **Assets**. Los archivos «Source code» son el código fuente, no la app.
2. Abre el APK en el teléfono. Si Android lo solicita, permite instalar aplicaciones desde el navegador o gestor de archivos que estés usando.
3. Si ya tienes una compilación anterior firmada con la misma clave, instala encima para conservar los datos. **Desinstalar borra el perfil privado y las partidas de la app.**

Es un APK de preview firmado con la clave de depuración pública de Android (la misma de v0.2.0-preview.1, así que se instala encima de aquella); no es una publicación de producción en Google Play. La release incluye `SHA256SUMS.txt` para comprobar la descarga.

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

### Dónde están esos archivos en tu PC

Necesitas **la carpeta de instalación del juego**, la que contiene `hitman2.exe`. Según cómo lo tengas:

| Tu copia | Cómo llegar a la carpeta |
|---|---|
| **Steam** | Biblioteca → clic derecho en *Hitman 2: Silent Assassin* → **Administrar → Explorar archivos locales** |
| **GOG** | GOG Galaxy → el juego → **Administrar instalación → Mostrar carpeta** (o la carpeta que elegiste en el instalador) |
| **CD/DVD original** | Instálalo en el PC y usa la carpeta de instalación (la que eligió el instalador). Copiar directamente el disco **no** sirve |
| **Ya lo tenías instalado** | Busca `hitman2.exe` con el buscador de Windows; la carpeta donde está es la que buscas |

Qué copiar y qué dejar:

| Copiar al teléfono | No hace falta copiar |
|---|---|
| `hitman2.exe`, `RenderOpenGL.dll`, `Hitman2.ini`, `keyboard.cfg`, `streams.wav` | `config.exe`, el desinstalador, `RenderD3D.dll` y las demás DLL (`eax.dll`, `msvcrt.dll`…): el port aporta sus propias versiones |
| la carpeta **`Scenes`** completa (unos 400 MB) | `Save` (las partidas de PC no se han probado importar), capturas, `Readme.rtf`, `INSTALL.LOG` |

La edición de referencia ocupa unos **510 MB** en total. Formas de pasarlos al teléfono:

1. **Cable USB** (lo más simple): conecta el teléfono, elige *Transferencia de archivos* y arrastra los archivos a `h2androidevolved` desde el explorador de Windows.
2. **Nube o red local** (Drive, OneDrive, un recurso compartido…): sube la carpeta, descárgala en el teléfono con la app de archivos y colócala en `h2androidevolved`. Comprueba que `Scenes` conserve sus subcarpetas y que los `.ZIP` no se hayan descomprimido ni renombrado.
3. **ADB** (para desarrollo): ver la alternativa más abajo.

Después, en el launcher: **Sistema → Verificar archivos**. Debe decir **Listo para jugar**. Si tu copia es de otra edición, compara los hashes de la sección «Binarios de referencia»: si no coinciden, puede no funcionar.

### 3. Autoriza los archivos y entra al juego

1. Abre **Hitman 2 AE**.
2. Pulsa **Permitir acceso** o entra en **Sistema → Permiso de acceso a archivos**. En los ajustes de Android habilita **Permitir acceso para administrar todos los archivos** para esta app y vuelve al launcher.
3. Pulsa **Sistema → Verificar archivos**. Si faltan archivos, revisa la ruta y termina de copiar `Scenes`.
4. Cuando aparezca **Listo para jugar**, pulsa **Jugar**. Por defecto se omiten los logos y se abre el menú principal.
5. Usa la cruceta y **Aceptar** para navegar. En el menú del juego puedes empezar una partida o cargar una guardada por el port. Tocar un elemento del menú mueve el cursor del juego hasta él y pulsa.

El port lee los originales del teléfono y escribe el perfil y las partidas en el almacenamiento privado de la app. No modifica tu instalación de PC.

### Alternativa: copiar mediante ADB

Es opcional; la transferencia USB anterior no necesita depuración USB. Si ya tienes ADB instalado y el teléfono autorizado, estos comandos de PowerShell copian los mismos archivos. Ajusta `$gameCopy` a tu instalación y usa `adb -s SERIE` si hay varios dispositivos:

```powershell
$gameCopy = 'C:\Juegos\Hitman 2 Silent Assassin'
adb devices
adb install -r '.\Hitman2-AndroidEvolved-v0.3.0-preview.1-arm64.apk'
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

**Resolución y rendimiento:** **Audio / GPU → Resolución interna**. Por defecto es *Ajustada a la pantalla* (720 líneas, p. ej. 1560×720): llena toda la pantalla sin bordes negros y el juego dibuja una vista más ancha. Medido en la misión 1: Galaxy S25 Ultra 120 fps en 800×450 y 1280×720 y 104–120 en 1560×720; **Redmi Note 8: 60 fps a 800×450, ~53 a 1280×720 y ~52 a 1560×720**. Si tu teléfono va por debajo de 60, baja la resolución. La imagen se dibuja con suavizado 4x y con la rampa de gamma del propio juego.

**Gráficos:** empieza con **Audio / GPU → Perfil gráfico → Automático**. Los perfiles Mali y Unisoc son experimentales; Snapdragon/Adreno también tiene un perfil explícito. Todos usan el **driver OpenGL ES de Android**: no instalan Turnip ni otros drivers externos. El modo de menor profundidad puede reducir la precisión visual. La información del GPU y driver aparece después de iniciar el juego.

**Mandos:** hay mapeo para mandos que Android reconozca como gamepad (stick izquierdo mover, derecho cámara, R2/R1 disparar, L2 correr, A acción, B agacharse, X recargar, Y inventario, L1 sigilo, Start pausa, Select mapa; en menús cruceta, A aceptar, B atrás). **Está implementado pero no se ha probado con un mando real.**

Más detalles: [launcher](docs/LAUNCHER.md) · [controles táctiles](docs/TOUCH_CONTROLS.md).

## Si no arranca

| Síntoma | Qué revisar |
|---|---|
| «Faltan archivos» | Carpeta seleccionada, cinco archivos de raíz y `Scenes` completa; sin carpetas adicionales ni ZIP extraídos |
| «Falta permiso» | Habilitar el acceso a todos los archivos para **Hitman 2 AE** y volver al launcher |
| Error al cargar la biblioteca | Teléfono ARM64; APK ARM64; revisar el mensaje mostrado y los requisitos de GLES 3 |
| Pantalla negra o parada del motor | Esta edición del juego puede ser distinta; comparar hashes. Cerrar la app por completo antes de volver a iniciar un motor detenido |
| El puntero del menú no cae donde tocas | Usar la cruceta y Aceptar mientras tanto; avisa en qué pantalla pasa |
| Se ve mal con un perfil GPU | Volver a Automático y arrancar de nuevo el motor; Mali y Unisoc aún no tienen validación en hardware |

La suspensión y reentrada conservando la superficie se comprobaron en un caso del S25. No hay recuperación automática de una misión si Android termina el proceso. Tampoco se han validado todas las misiones, guardar/cargar de extremo a extremo ni el rendimiento sostenido. Consulta [problemas conocidos](docs/KNOWN_ISSUES.md) y [evidencia de esta versión](docs/evidence/launcher_2026-11-17.md).

## Qué seguirá mejorando

Lo que no está hecho o no está comprobado, en lenguaje llano (el detalle técnico está en [problemas conocidos](docs/KNOWN_ISSUES.md) y [estado](docs/STATUS.md)):

- **Guardar y cargar partidas:** las llamadas del sistema que hacen falta existen, pero no se ha probado guardar y cargar una partida completa.
- **Más misiones:** solo se ha jugado el tutorial. Las demás cargan por la misma ruta, pero no se han recorrido.
- **Mandos:** implementados, sin probar con un mando físico.
- **Más teléfonos y GPU:** probado solo en Adreno 830 y Adreno 610. Hay perfiles para Mali y Unisoc y una escalera de sombreadores de respaldo, sin validar en esos chips.
- **Rendimiento:** a 60 Hz en gama baja va justo; se puede agrupar el dibujado y optimizar el código recompilado.
- **Sonido:** hay mezclador con atenuación y panorama por distancia, pero sin Doppler ni los efectos ambientales EAX del original. No se ha comparado el audio con el de PC.
- **Imagen:** se aplica la gamma del juego; falta comparar con capturas del original y revisar las sombras y reflejos.
- **Excepciones del programa original:** el manejo estructurado de excepciones (SEH/C++) no está implementado; si el juego lo necesita, se detiene indicando qué falta.
- **Idioma:** el launcher está en español e inglés; el idioma del juego depende de los datos que copies.
- **Reanudar:** al volver a la app se conserva la sesión, pero no se recupera una misión si Android mata el proceso.
- **Otras ediciones del juego:** solo la edición de referencia está verificada.

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
