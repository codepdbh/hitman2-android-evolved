# Launcher y configuración

Estado: **Parcial**. Entrada desde el launcher y cambio de idioma de su interfaz comprobados en S25; las últimas correcciones del puntero y los perfiles GPU requieren prueba en dispositivo. El propietario aplazó las pruebas porque estaba usando el teléfono.

## Inicio

El launcher sustituye la pantalla antigua de diagnóstico, cuyo botón de inicio estaba desactivado. Ahora ofrece **Jugar** y **Continuar misión**, comprueba los archivos esenciales y permite revisar la carpeta y el permiso de almacenamiento. Por defecto entra directamente al menú principal; se puede conservar la introducción.

Una sesión suspendida se reanuda sin reinicializar el motor. Si el motor ha terminado, el launcher lo muestra; es necesario cerrar la app por completo antes de arrancarlo otra vez. La reinicialización del runtime global dentro del mismo proceso sigue sin implementarse.

## Ajustes

| Grupo | Opciones | Aplicación |
|---|---|---|
| Controles | Sensibilidad 0,5–8×, tamaño, visibilidad, correr con joystick, respuesta háptica | Al volver a la misión |
| Audio | Voces, música y efectos, 0–100 % | Próximo arranque del motor |
| Pantalla | Subtítulos, clima, mantener pantalla encendida | Subtítulos/clima al arrancar; pantalla al reanudar |
| GPU | Automático, Mali, Unisoc, Snapdragon/Adreno; información del driver activo | Perfil al arrancar |
| Sistema | Carpeta, permiso de lectura, verificación de archivos, autoprueba del runtime | Según la acción |

El perfil del juego se escribe solo en los archivos privados de la app. Se conservan las opciones del usuario que el launcher no administra y se eliminan duplicados de las opciones administradas. La resolución interna permanece en 800 × 450. La autoprueba del runtime no está disponible mientras hay una sesión iniciada, para no alterar su memoria.

## Idiomas

**Español / English** cambia inmediatamente launcher, botones, panel táctil y ayuda. La elección se guarda y se aplica también al reanudar una sesión.

La copia disponible del propietario contiene textos españoles; el propietario confirmó que no tiene datos ingleses. El idioma del juego depende de esos recursos. No se presenta el selector de interfaz como una traducción del juego: el launcher explica que el juego seguirá en español. Estado del juego en inglés: **Bloqueado (faltan datos ingleses)**.

## GPU y drivers

Se utiliza el **driver OpenGL ES del sistema Android**. Los perfiles no instalan ni cargan paquetes de drivers externos. Requieren un dispositivo con OpenGL ES 3.

| Perfil | Configuraciones de superficie solicitadas, por orden |
|---|---|
| Automático / Snapdragon-Adreno | Profundidad 24 + stencil 8 → profundidad 24 sin stencil → profundidad 16 sin stencil |
| Mali, experimental | Profundidad 24 sin stencil → profundidad 16 sin stencil |
| Unisoc, experimental | Profundidad 16 sin stencil |

Son preferencias de compatibilidad, no garantías por fabricante. EGL puede devolver una profundidad mayor que la mínima solicitada; la información de GPU muestra los valores reales. Una menor profundidad puede reducir la precisión y producir artefactos. Se recomienda **Automático**.

Al crear el contexto se registran vendor, renderer, versión del driver, profundidad/stencil reales, tamaño máximo de textura y unidades de textura. Un error de la API EGL no se oculta intentando otra configuración: se registra y falla la inicialización. La pérdida de contexto tampoco se simula como un resultado correcto.

Solo existe evidencia física previa para el Adreno del S25. Los perfiles nuevos no están comprobados en Mali/Unisoc ni en el S25 después del último cambio. No hay soporte implementado para cargar Turnip, ANGLE u otros drivers externos.

## Ratón y mandos

El menú original consulta `GetCursorPos` y luego `ScreenToClient`. Se sustituyó el desplazamiento relativo supuesto por coordenadas absolutas en el rectángulo real de la imagen. Se mantienen los clics breves al menos 60 ms; los movimientos permiten arrastrar y se atiende el hover de ratón Android. La prueba física de esta corrección está aplazada.

Los mandos USB/Bluetooth **todavía no tienen mapeo al juego**. El launcher distingue un mando detectado por Android del soporte del juego; no anuncia compatibilidad por el hecho de detectar el dispositivo.

Evidencia y límites: [launcher_2026-11-17.md](evidence/launcher_2026-11-17.md).
