# Mapa del motor Glacier en `hitman2.exe` (build 1.0.0.265)

Solo hechos leídos del binario. Las interpretaciones están en la sección "Hipótesis".

## Módulos y contrato entre ellos (verificado)

```
hitman2.exe  ── importa ──> P5DLL.dll (guante P5), EAX.DLL (#6), DSOUND.dll (#2), DINPUT8.dll,
   │                        KERNEL32 (106), USER32 (33), ADVAPI32 (4, registro), GDI32 (1)
   │  CRT enlazada estáticamente (no importa msvcrt.dll)
   │
   ├── exporta 677 símbolos C++ decorados
   │       ZEngineDataBase (154), ConstrainedParticleSystem (60), ZSaveGame (20), ZLoadGame (16),
   │       y ~424 globales/funciones libres (g_pSysInterface, g_pSysMem, g_pSysFile, g_pSysCom, …)
   │
   └── carga por configuración (`DrawDll` en Hitman2.ini) ──> RenderD3D.dll | RenderOpenGL.dll
            cada una exporta solo `Create` e importa los mismos 28 símbolos de hitman2.exe
```

- `jpegdll.dll` (libjpeg, 103 exports `__fastcall`) y `xmlparse.dll` (expat, 54 exports) están en la carpeta pero no aparecen en ninguna tabla de imports estática: se cargan por nombre en tiempo de ejecución o no se usan.
- `Hitman2.ini` nombra `SoundEngine.dll`, `SDL_Engine.dll` y `ScriptInterfaces.dll`. No existen en la instalación ni en su `INSTALL.LOG`; sus rutas de fuente (`Z:\code\engine\Audio\soundengine\`, `…\SDL_Engine\Glacier\scriptdll.cpp`, `…\ScriptInterfaces\`) están dentro de `hitman2.exe`. En esta build están enlazados estáticamente.

## Árbol de fuentes que el binario nombra (134 cadenas `__FILE__`)

| Directorio | Ficheros nombrados |
|---|--:|
| `engine\eventsbase` (action, actorcollision, checkinside, checkitem, checkvisible, …) | 25 |
| `engine\geomsextend` | 12 |
| `engine\geomsbase` | 7 |
| `engine\scriptinterfaces` (actor_if, dialog_if, engine_if, item_if, list_if, tracklink_if, zgeom_if) | 7 |
| `engine\camera` (basecamera, cameracontrol, mouseconcam, newdialogcamera) | 4 |
| `engine\pathfinder` | 3 |
| `engine\shared\vehicles` | 3 |
| `engine\audio\soundengine` (eventcontroller, sound) | 2 |
| `engine\dynamics` (particle3, rigidbody) | 2 |
| `engine\drawing`, `engine\enginedata`, `engine\sdl_engine\glacier`, `engine\system\_wintel`, `engine\zstdlib` | 1 c/u |
| `hitman2\characters` | 30 |
| `hitman2\gui` | 12 |
| `hitman2\background` | 11 |
| `hitman2\camera` | 7 |
| `hitman2\_hm2_interfaces` | 4 |

Lista completa en `analysis/dependencies.md`. Solo aparecen los ficheros que usan una macro de aserción/registro; el árbol real es mayor.

## Código

- `.text` 0x00401000–0x00635000 (2,31 MB). Entrada 0x0059F54E: prólogo SEH de `WinMainCRTStartup` de MSVC 6.
- `disasm32`: 21.336 entradas (12.951 inicios de función, 8.385 alias), 2.612.047 instrucciones, 95,0 % de los bytes.
- CRT identificada por patrón: `_ftol` en 0x0059DD9C; `strrchr` en 0x0059D7D0.

## Datos del juego (verificado)

- 85 tríos `X.ZIP` + `X.WHD` + `X.WAV` bajo `Scenes/`, más `streams.wav` (112 MB).
- Los ZIP no están cifrados. `C1-1__MAIN.ZIP` contiene 13 ficheros, uno por tipo:
  `.GMS .PRM .TEX .ANM .SND .LOC .OCT .BUF .SGP .RMC .RMI .ZGF .SUP`.
- `.WHD` empieza con dos offsets y contiene rutas `Sound\…\*.wav` seguidas de campos numéricos (cabeceras de las ondas del `.WAV` hermano).
- `keyboard.cfg` es XML: `<Mapping Action="…"><Key SCode="K_W" Device="Keyboard"/>`. Las acciones son la lista real de controles del juego (base para el HUD táctil).

## Hipótesis (sin verificar)

- `ZSysInterface`/`ZSysMem`/`ZSysFile`/`ZSysCom` son la capa de abstracción de plataforma del motor (`engine\system\_wintel`); sustituirlos podría ahorrar parte del shim Win32.
- `GMS` = escena/grafo de geometría, `PRM` = primitivas/mallas, `TEX` = texturas, `ANM` = animaciones, `LOC` = textos localizados, `OCT` = octree de colisión, `SND` = definiciones de sonido.
- El motor de sonido tiene un backend OpenAL (`EnableOpenAL`, `openal32.dll` entre las cadenas) además de DirectSound; sería la vía natural en Android.
- Existe soporte de mando (`UseGameController`).
- `UseTryCatchMainLoop` sugiere que el bucle principal puede ejecutarse sin el manejador de excepciones C++ global.
