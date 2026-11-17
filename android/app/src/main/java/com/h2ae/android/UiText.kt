package com.h2ae.android

import android.content.Context

internal object UiText {
    fun t(context: Context, source: String): String = if (
        context.getSharedPreferences("port_options", Context.MODE_PRIVATE).getString("language", "es") == "en"
    ) EnglishUi.translate(source) else source
}

/** App-authored text only. Original game localization belongs to the user's game data. */
internal object EnglishUi {
    private val words = mapOf(
        "Audio / GPU" to "Audio / GPU", "Gráficos y audio" to "Graphics and audio",
        "Audio, gráficos, subtítulos y clima se aplican al próximo arranque del motor." to "Audio, graphics, subtitles and weather apply at the next engine start.",
        "Perfil gráfico" to "Graphics profile", "Automático (recomendado)" to "Automatic (recommended)",
        "Mali · compatibilidad de 24 bits" to "Mali · 24-bit compatibility", "Unisoc · compatibilidad de 16 bits" to "Unisoc · 16-bit compatibility",
        "Se usa el driver OpenGL ES de Android. Los perfiles Mali y Unisoc son experimentales; el modo de 16 bits puede reducir la precisión de profundidad." to "Uses the Android OpenGL ES driver. Mali and Unisoc profiles are experimental; 16-bit mode may reduce depth precision.",
        "Dispositivo gráfico" to "Graphics device", "GPU y driver activos" to "Active GPU and driver",
        "GPU information available after starting the game." to "GPU information available after starting the game.",
        "Información del GPU disponible después de iniciar el juego." to "GPU information available after starting the game.",
        "No se pudo aplicar el perfil gráfico. Cierra la app por completo y vuelve a abrirla." to "Could not apply the graphics profile. Fully close and reopen the app.",
        "Inicio" to "Home", "Controles" to "Controls", "Audio / pantalla" to "Audio / display", "Sistema" to "System",
        "Tu próxima misión,\na un toque." to "Your next mission,\none tap away.",
        "Comprobando instalación…" to "Checking installation…", "Jugar  →" to "Play  →",
        "●  Misión en curso" to "●  Mission in progress", "Motor no disponible" to "Engine unavailable",
        "El motor se ha detenido" to "The engine has stopped", "Falta permiso de archivos" to "Storage permission required",
        "●  Listo para jugar" to "●  Ready to play", "Continuar misión  →" to "Continue mission  →",
        "Permitir acceso  →" to "Allow access  →", "Revisar archivos  →" to "Check files  →",
        "Cierra la app por completo y vuelve a abrirla. No se perderán las partidas guardadas." to "Fully close and reopen the app. Saved games will be preserved.",
        "Retoma la sesión actual. Los datos originales permanecen intactos." to "Resume your current session. Original game files remain unchanged.",
        "Autoriza el acceso a tu copia del juego." to "Allow access to your game files.",
        "Tu copia local · Sin descargas ni cuenta" to "Your local copy · No downloads or account",
        "Preparado para la misión" to "Ready for the mission",
        "Inicia el juego desde aquí o continúa la sesión que dejaste abierta." to "Start the game here or resume your open session.",
        "Omitir logos e introducción" to "Skip logos and introduction",
        "Se aplica al próximo arranque. Las partidas se cargan desde el menú del juego." to "Applies at the next engine start. Load saves from the game menu.",
        "Controles compactos" to "Compact controls",
        "Cinco botones, joystick y acciones por categorías. Cámara más rápida y ajustable." to "Five buttons, a joystick and grouped actions. Faster, adjustable camera.",
        "Personalizar controles  →" to "Customize controls  →", "Mandos" to "Controllers",
        "A tu manera" to "Make it yours", "Estos ajustes también se aplican al reanudar la misión." to "These settings also apply when resuming a mission.",
        "Sensibilidad de cámara" to "Camera sensitivity", "Tamaño de botones" to "Button size", "Visibilidad de botones" to "Button visibility",
        "Correr al estirar el joystick" to "Run beyond the joystick edge", "Respuesta háptica al pulsar" to "Haptic feedback",
        "Para mover o elegir botones: EQUIPO → Ajustes dentro de la misión." to "Move or pin buttons in a mission: GEAR → Settings.",
        "Audio y pantalla" to "Audio and display", "Audio, subtítulos y clima se aplican al próximo arranque del motor." to "Audio, subtitles and weather apply at the next engine start.",
        "Voces" to "Speech", "Música" to "Music", "Efectos" to "Effects", "Subtítulos" to "Subtitles",
        "Efectos de clima" to "Weather effects", "Mantener pantalla encendida" to "Keep screen awake",
        "Imagen sin deformaciones" to "Undistorted picture",
        "800 × 450 internos · Proporción 16:9 · OpenGL ES 3. No modifica la resolución de Android." to "800 × 450 internal · 16:9 aspect ratio · OpenGL ES 3. Android resolution is unchanged.",
        "Tu instalación" to "Your installation", "Usa tu propia copia del juego. El launcher no incluye ni descarga sus archivos." to "Use your own game copy. This launcher does not include or download game files.",
        "Carpeta de datos" to "Game data folder", "Cambiar carpeta" to "Change folder", "Permiso de acceso a archivos" to "Storage access permission",
        "Verificar archivos" to "Check files", "Comprobación" to "File check", "Pendiente: autoriza el acceso a archivos." to "Pending: allow storage access.",
        "Archivos esenciales encontrados. Esto no verifica todas las misiones ni la integridad completa de los datos." to "Essential files found. This does not verify every mission or full file integrity.",
        "Autoprueba del runtime" to "Runtime self-test",
        "La autoprueba comprueba el runtime; no valida la jugabilidad. Solo disponible antes de iniciar una misión." to "This test checks the runtime, not gameplay. Available only before starting a mission.",
        "Soporte en el juego: pendiente." to "In-game support: not implemented.", "Android no detecta ningún mando conectado." to "Android detects no connected controllers.",
        "Acceso disponible en esta versión de Android." to "Storage access is available on this Android version.",
        "Carpeta de tu copia del juego" to "Your game copy folder", "Introduce la carpeta que contiene hitman2.exe, Hitman2.ini y Scenes." to "Enter the folder containing hitman2.exe, Hitman2.ini and Scenes.",
        "Introduce una carpeta existente con permiso de lectura." to "Enter an existing folder with read access.",
        "Guardar" to "Save", "Cancelar" to "Cancel", "Cerrar" to "Close", "Resultado de autoprueba" to "Self-test results",
        "Idioma" to "Language", "Idioma del launcher y los controles" to "Launcher and controls language",
        "El cambio de interfaz es inmediato. Los textos y voces del juego dependen de los archivos instalados." to "Interface changes apply immediately. Game text and speech depend on installed game files.",
        "Tu copia contiene el juego en español. Para jugar en inglés hacen falta los datos ingleses; el selector no los incluye." to "Your game copy is Spanish. Playing in English requires English game data, which this selector does not include.",
        "Disparar" to "Fire", "Acción" to "Action", "Agacharse" to "Crouch", "Correr" to "Run", "Inventario" to "Inventory", "Pausa" to "Pause",
        "Recargar" to "Reload", "Sigilo" to "Sneak", "Inclinar izq." to "Lean left", "Inclinar der." to "Lean right", "Inv. rápido" to "Quick inventory",
        "Cuerpo a cuerpo" to "Melee", "Pistolas" to "Pistols", "Subfusiles" to "SMGs", "Rifles" to "Rifles", "Prismáticos" to "Binoculars",
        "Visión nocturna" to "Night vision", "Ganzúa" to "Lockpick", "Soltar" to "Drop", "Mapa" to "Map", "Informe" to "Briefing", "Cámara" to "Camera",
        "Correr (alternar)" to "Toggle run", "Acción anterior" to "Previous action", "Acción siguiente" to "Next action",
        "Arriba" to "Up", "Abajo" to "Down", "Izquierda" to "Left", "Derecha" to "Right", "Aceptar" to "Accept",
        "Equipo" to "Gear", "Acciones" to "Actions", "Vista" to "View", "SALTAR  ▶▶" to "SKIP  ▶▶", "LISTO" to "DONE",
        "⋯ EQUIPO" to "⋯ GEAR", "CORRER" to "RUN", "ESTIRA PARA CORRER" to "EXTEND TO RUN",
        "CONTROLES · tocar fuera para cerrar" to "CONTROLS · tap outside to close", "⚙ Ajustes" to "⚙ Settings",
        "Elegir botones en pantalla…" to "Choose on-screen buttons…", "Mover botones" to "Move buttons", "Tamaño…" to "Size…", "Transparencia…" to "Opacity…",
        "Correr con joystick: activado" to "Joystick running: on", "Correr con joystick: desactivado" to "Joystick running: off",
        "Restaurar diseño compacto" to "Reset compact layout", "Ayuda de controles" to "Controls help", "Sensibilidad de cámara…" to "Camera sensitivity…",
        "Controles táctiles" to "Touch controls", "Tamaño de los botones" to "Button size",
        "Transparencia (a la derecha, más visibles)" to "Opacity (more visible to the right)", "Entendido" to "Got it", "Listo" to "Done",
        "Botones en pantalla (los demás van al panel «⋯»)" to "On-screen buttons (others appear in the drawer)",
        "No se pudo conectar la superficie de Android." to "Could not attach the Android surface.",
        "No se pudo iniciar el hilo del juego." to "Could not start the game thread.",
        "El motor del juego se ha detenido." to "The game engine has stopped.",
        "Cierra la app por completo y vuelve a abrirla." to "Fully close and reopen the app.",
        "Mueve el joystick para caminar. Arrastra hacia delante más allá del borde para correr; vuelve al centro para caminar. Puedes desactivarlo y fijar Correr en pantalla.\n\nArrastra por la zona libre para mirar. Disparar, Acción y Agacharse funcionan mientras mantienes el dedo.\n\nEQUIPO abre todas las acciones por categorías. Las acciones mantenidas, como Inventario, se sueltan al levantar el dedo. Toca fuera para cerrar. Pausa abre el menú del juego.\n\nLos ajustes permiten fijar acciones, moverlas y cambiar tamaño y transparencia." to
            "Move the joystick to walk. Drag forwards beyond the edge to run; move back towards the centre to walk. You can disable this and pin Run instead.\n\nDrag an empty area to look around. Hold Fire, Action or Crouch to use them.\n\nGEAR opens grouped actions. Held actions such as Inventory release when you lift your finger. Tap outside to close. Pause opens the game menu.\n\nSettings let you pin, move and resize controls and adjust opacity."
    )
    fun translate(source: String): String {
        words[source]?.let { return it }
        Regex("Faltan (\\d+) archivos esenciales").matchEntire(source)?.let { return "${it.groupValues[1]} essential files missing" }
        val prefixes = mapOf("Faltan:\n" to "Missing:\n", "Android detecta: " to "Android detects: ",
            "Autoprueba fallida: " to "Self-test failed: ", "No se pudo cargar el motor: " to "Could not load the engine: ",
            "No se pudo leer o escribir el perfil: " to "Could not read or write the profile: ")
        prefixes.entries.firstOrNull { source.startsWith(it.key) }?.let {
            return it.value + source.removePrefix(it.key).replace(". Aún no se traducen sus botones ni sticks al juego.", ". Its buttons and sticks are not mapped to the game yet.")
        }
        if (source.contains('\n')) return source.split('\n').joinToString("\n") { translate(it) }
        return source
    }
}
