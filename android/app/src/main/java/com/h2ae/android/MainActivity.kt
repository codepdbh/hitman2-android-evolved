package com.h2ae.android

import android.app.Activity
import android.app.AlertDialog
import android.app.ActivityManager
import android.content.Intent
import android.graphics.Color
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.Environment
import android.provider.Settings
import android.view.Gravity
import android.view.InputDevice
import android.view.View
import android.widget.*
import java.io.File
import java.util.Locale
import kotlin.concurrent.thread

/** Native launcher. Preferences are consumed by GameActivity and TouchHudView. */
class MainActivity : Activity() {
    private lateinit var options: PortOptions
    private lateinit var status: TextView
    private lateinit var detail: TextView
    private lateinit var play: Button
    private lateinit var content: LinearLayout
    private var tab = 0
    private var testing = false
    private val navButtons = ArrayList<Button>()
    private val red = Color.rgb(202, 32, 52)
    private val ink = Color.rgb(239, 237, 230)
    private val muted = Color.rgb(156, 158, 162)
    private val dp get() = resources.displayMetrics.density
    private fun px(v: Int) = (v * dp).toInt()
    private fun tr(text: String) = UiText.t(this, text)

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        options = PortOptions(this)
        tab = savedInstanceState?.getInt("tab") ?: 0
        @Suppress("DEPRECATION")
        window.decorView.systemUiVisibility = View.SYSTEM_UI_FLAG_FULLSCREEN or View.SYSTEM_UI_FLAG_HIDE_NAVIGATION or View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
        buildLayout()
        if (intent?.getBooleanExtra("selftest", false) == true) runSelfTest()
    }
    override fun onResume() { super.onResume(); if (::play.isInitialized) { refreshStatus(); showTab(tab) } }
    override fun onSaveInstanceState(out: Bundle) { out.putInt("tab", tab); super.onSaveInstanceState(out) }

    private fun background(color: Int, radius: Int = 14, border: Int = Color.rgb(48, 49, 53)) = GradientDrawable().apply {
        setColor(color); cornerRadius = px(radius).toFloat(); setStroke(px(1), border)
    }
    private fun column() = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
    private fun label(text: String, size: Float = 14f, color: Int = ink) = TextView(this).apply {
        this.text = tr(text); textSize = size; setTextColor(color); setLineSpacing(px(2).toFloat(), 1f)
    }
    private fun LinearLayout.add(v: View, top: Int = 0) { addView(v, LinearLayout.LayoutParams(-1, -2).apply { topMargin = px(top) }) }
    private fun button(text: String, accent: Boolean = false, action: () -> Unit) = Button(this).apply {
        this.text = tr(text); isAllCaps = false; textSize = 14f; minHeight = px(48)
        setTextColor(ink); background = background(if (accent) red else Color.rgb(35, 36, 40), 10, if (accent) red else Color.rgb(57, 58, 62))
        setPadding(px(14), px(8), px(14), px(8)); setOnClickListener { action() }
    }

    private fun buildLayout() {
        navButtons.clear()
        val root = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL; setPadding(px(28), px(20), px(28), px(20))
            background = GradientDrawable(GradientDrawable.Orientation.TL_BR, intArrayOf(0xFF202125.toInt(), 0xFF0C0D0F.toInt(), 0xFF191012.toInt()))
        }
        val leftScroll = ScrollView(this).apply { isFillViewport = true }
        val left = column().apply { setPadding(0, 0, px(28), 0) }
        leftScroll.addView(left)
        root.addView(leftScroll, LinearLayout.LayoutParams(0, -1, 0.40f))
        left.add(label("H2  /  ANDROID EVOLVED", 11f, red).apply { letterSpacing = 0.13f })
        left.add(label("HITMAN 2", 36f).apply { typeface = Typeface.create("sans-serif-condensed", Typeface.BOLD); letterSpacing = 0.02f }, 16)
        left.add(label("SILENT ASSASSIN", 12f, muted).apply { letterSpacing = 0.18f })
        left.add(label("━━━━━━━━", 12f, red), 10)
        left.add(label("Tu próxima misión,\na un toque.", 23f).apply { typeface = Typeface.create("sans-serif-light", Typeface.NORMAL) }, 10)
        status = label("Comprobando instalación…", 13f)
        left.add(status, 18)
        play = button("Jugar  →", true) { launchGame() }
        left.add(play, 12)
        detail = label("", 11f, muted); left.add(detail, 8)
        left.add(label("ARM64 · OpenGL ES 3 · ${packageManager.getPackageInfo(packageName, 0).versionName}", 10f, muted), 18)

        val right = column().apply { background = background(0xFF18191D.toInt()); setPadding(px(18), px(12), px(18), px(12)) }
        root.addView(right, LinearLayout.LayoutParams(0, -1, 0.60f))
        val nav = LinearLayout(this)
        listOf("Inicio", "Controles", "Audio / GPU", "Sistema").forEachIndexed { i, text ->
            val item = button(text) { showTab(i) }.apply { textSize = 11f; setPadding(px(3), px(6), px(3), px(6)) }
            navButtons.add(item)
            nav.addView(item,
                LinearLayout.LayoutParams(0, px(48), 1f).apply { marginEnd = px(4) })
        }
        right.add(nav)
        val scroll = ScrollView(this)
        content = column().apply { setPadding(0, px(16), 0, px(8)) }
        scroll.addView(content); right.addView(scroll, LinearLayout.LayoutParams(-1, 0, 1f))
        setContentView(root); refreshStatus(); showTab(tab)
    }

    private fun storageAllowed() = Build.VERSION.SDK_INT < 30 || Environment.isExternalStorageManager()
    private fun running() = NativeBridge.gameLoadError == null && NativeBridge.gameRunning()
    private fun refreshStatus() {
        val live = running()
        val missing = if (storageAllowed()) options.missingFiles() else emptyList()
        val error = NativeBridge.gameLoadError
        val stopped = if (error == null) NativeBridge.gameStopMessage() else null
        status.text = tr(when {
            live -> "●  Misión en curso"
            error != null -> "Motor no disponible"
            stopped != null -> "El motor se ha detenido"
            !storageAllowed() -> "Falta permiso de archivos"
            missing.isNotEmpty() -> "Faltan ${missing.size} archivos esenciales"
            else -> "●  Listo para jugar"
        })
        status.setTextColor(if (live || (error == null && stopped == null && storageAllowed() && missing.isEmpty())) 0xFFB5D6BB.toInt() else 0xFFE7B6A5.toInt())
        play.text = tr(when { live -> "Continuar misión  →"; !storageAllowed() -> "Permitir acceso  →"; missing.isNotEmpty() -> "Revisar archivos  →"; else -> "Jugar  →" })
        play.isEnabled = error == null && stopped == null && !testing
        play.alpha = if (play.isEnabled) 1f else 0.45f
        detail.text = tr(when {
            error != null -> error
            stopped != null -> "Cierra la app por completo y vuelve a abrirla. No se perderán las partidas guardadas."
            live -> "Retoma la sesión actual. Los datos originales permanecen intactos."
            !storageAllowed() -> "Autoriza el acceso a tu copia del juego."
            missing.isNotEmpty() -> missing.take(3).joinToString("\n")
            else -> "Tu copia local · Sin descargas ni cuenta"
        })
    }
    private fun launchGame() {
        refreshStatus()
        if (!storageAllowed()) { requestStorage(); return }
        if (!running() && options.missingFiles().isNotEmpty()) { showTab(3); return }
        startActivity(Intent(this, GameActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_CLEAR_TOP or Intent.FLAG_ACTIVITY_SINGLE_TOP))
    }
    private fun heading(title: String, description: String) {
        content.add(label(title, 22f).apply { typeface = Typeface.DEFAULT_BOLD })
        content.add(label(description, 12f, muted), 6)
    }
    private fun showTab(index: Int) {
        tab = index; content.removeAllViews()
        navButtons.forEachIndexed { i, b ->
            b.isSelected = i == index
            b.background = background(if (i == index) 0xFF53222A.toInt() else 0xFF232428.toInt(), 10, if (i == index) red else 0xFF393A3E.toInt())
        }
        when (index) {
            0 -> {
                heading("Preparado para la misión", "Inicia el juego desde aquí o continúa la sesión que dejaste abierta.")
                content.add(label("Idioma del launcher y los controles", 13f), 14)
                val languages = LinearLayout(this)
                listOf("es" to "Español", "en" to "English").forEach { (code, name) ->
                    languages.addView(button(name, options.language == code) {
                        options.prefs.edit().putString("language", code).apply(); buildLayout()
                    }, LinearLayout.LayoutParams(0, px(48), 1f).apply { marginEnd = px(6) })
                }
                content.add(languages, 8)
                content.add(label("Tu copia contiene el juego en español. Para jugar en inglés hacen falta los datos ingleses; el selector no los incluye.", 11f, muted), 8)
                toggle("Omitir logos e introducción", options.prefs.getBoolean("skip_intro", true)) { options.prefs.edit().putBoolean("skip_intro", it).apply() }
                content.add(label("Se aplica al próximo arranque. Las partidas se cargan desde el menú del juego.", 11f, muted), 4)
                card("Controles compactos", "Cinco botones, joystick y acciones por categorías. Cámara más rápida y ajustable.")
                content.add(button("Personalizar controles  →") { showTab(1) }, 8)
                card("Mandos", controllerStatus())
            }
            1 -> {
                heading("A tu manera", "Estos ajustes también se aplican al reanudar la misión.")
                slider("Sensibilidad de cámara", 50, 800, (options.touch.getFloat("camera_sensitivity", 3f) * 100).toInt(),
                    { String.format(Locale.ROOT, "%.1f×", it / 100f) }) { options.touch.edit().putFloat("camera_sensitivity", it / 100f).apply() }
                slider("Tamaño de botones", 60, 160, (options.touch.getFloat("scale", 1f) * 100).toInt(), { "$it %" }) { options.touch.edit().putFloat("scale", it / 100f).apply() }
                slider("Visibilidad de botones", 20, 100, (options.touch.getFloat("opacity", 0.6f) * 100).toInt(), { "$it %" }) { options.touch.edit().putFloat("opacity", it / 100f).apply() }
                toggle("Correr al estirar el joystick", options.touch.getBoolean("joystick_run", true)) { options.touch.edit().putBoolean("joystick_run", it).apply() }
                toggle("Respuesta háptica al pulsar", options.touch.getBoolean("haptics", false)) { options.touch.edit().putBoolean("haptics", it).apply() }
                content.add(label("Para mover o elegir botones: EQUIPO → Ajustes dentro de la misión.", 11f, muted), 10)
            }
            2 -> {
                heading("Gráficos y audio", "Audio, gráficos, subtítulos y clima se aplican al próximo arranque del motor.")
                content.add(button("Perfil gráfico") { chooseGraphicsProfile() }, 12)
                content.add(button("Resolución interna") { chooseResolution() }, 12)
                content.add(label(options.resolution.replace("x", " × ") + if (options.resolution == "800x450") "  (original)" else "", 13f), 6)
                val profile = options.prefs.getInt("graphics_profile", 0).coerceIn(0, 3)
                content.add(label(tr(graphicsProfiles[profile]), 13f), 6)
                content.add(label("Se usa el driver OpenGL ES de Android. Los perfiles Mali y Unisoc son experimentales; el modo de 16 bits puede reducir la precisión de profundidad.", 11f, muted), 8)
                val config = (getSystemService(ACTIVITY_SERVICE) as ActivityManager).deviceConfigurationInfo
                val version = config.reqGlEsVersion
                card("Dispositivo gráfico", "${Build.MANUFACTURER} ${Build.MODEL}\nOpenGL ES ${(version ushr 16) and 65535}.${version and 65535}")
                if (NativeBridge.gameLoadError == null) {
                    val info = NativeBridge.graphicsInfo()
                    card("GPU y driver activos", if (info == "GPU information available after starting the game.")
                        "Información del GPU disponible después de iniciar el juego." else info)
                }
                listOf("Voces" to "speech_volume", "Música" to "music_volume", "Efectos" to "effects_volume").forEach { (name, key) ->
                    slider(name, 0, 100, options.prefs.getInt(key, 100), { "$it %" }) { options.prefs.edit().putInt(key, it).apply() }
                }
                toggle("Subtítulos", options.prefs.getBoolean("subtitles", true)) { options.prefs.edit().putBoolean("subtitles", it).apply() }
                toggle("Efectos de clima", options.prefs.getBoolean("weather", true)) { options.prefs.edit().putBoolean("weather", it).apply() }
                toggle("Mantener pantalla encendida", options.prefs.getBoolean("keep_awake", true)) { options.prefs.edit().putBoolean("keep_awake", it).apply() }
                card("Imagen sin deformaciones", "800 × 450 internos · Proporción 16:9 · OpenGL ES 3. No modifica la resolución de Android.")
            }
            3 -> {
                heading("Tu instalación", "Usa tu propia copia del juego. El launcher no incluye ni descarga sus archivos.")
                card("Carpeta de datos", options.gameDir.absolutePath)
                content.add(button("Cambiar carpeta") { chooseFolder() }, 8)
                content.add(button("Permiso de acceso a archivos") { requestStorage() }, 8)
                content.add(button("Verificar archivos") { refreshStatus(); showTab(3) }, 8)
                val missing = if (storageAllowed()) options.missingFiles() else emptyList()
                card("Comprobación", if (!storageAllowed()) "Pendiente: autoriza el acceso a archivos." else if (missing.isEmpty()) "Archivos esenciales encontrados. Esto no verifica todas las misiones ni la integridad completa de los datos." else "Faltan:\n${missing.joinToString("\n")}")
                card("Mandos", controllerStatus())
                content.add(button("Autoprueba del runtime") { runSelfTest() }.apply { isEnabled = !running() && !testing && NativeBridge.loadError == null && NativeBridge.gameLoadError == null && NativeBridge.gameStopMessage() == null }, 8)
                content.add(label("La autoprueba comprueba el runtime; no valida la jugabilidad. Solo disponible antes de iniciar una misión.", 11f, muted), 8)
                if (NativeBridge.loadError == null) content.add(label(NativeBridge.buildInfo(), 10f, muted), 10)
            }
        }
    }
    private fun card(title: String, text: String) {
        val box = column().apply { setPadding(px(14), px(12), px(14), px(12)); background = background(0xFF222328.toInt(), 10) }
        box.add(label(title, 14f).apply { typeface = Typeface.DEFAULT_BOLD }); box.add(label(text, 12f, muted), 4)
        content.add(box, 14)
    }
    private fun toggle(text: String, checked: Boolean, changed: (Boolean) -> Unit) {
        content.add(Switch(this).apply {
            this.text = tr(text); textSize = 13f; setTextColor(ink); minHeight = px(48)
            isChecked = checked; setOnCheckedChangeListener { _, on -> changed(on) }
        }, 8)
    }
    private fun slider(title: String, low: Int, high: Int, initial: Int, format: (Int) -> String, changed: (Int) -> Unit) {
        val caption = label("${tr(title)}   ${format(initial.coerceIn(low, high))}", 13f); content.add(caption, 18)
        content.add(SeekBar(this).apply {
            max = high - low; progress = initial.coerceIn(low, high) - low
            setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
                override fun onProgressChanged(bar: SeekBar?, value: Int, user: Boolean) {
                    caption.text = "${tr(title)}   ${format(value + low)}"; if (user) changed(value + low)
                }
                override fun onStartTrackingTouch(bar: SeekBar?) {}
                override fun onStopTrackingTouch(bar: SeekBar?) {}
            })
        })
    }
    private fun controllerStatus(): String {
        val devices = InputDevice.getDeviceIds().map { InputDevice.getDevice(it) }.filterNotNull().filter {
            it.supportsSource(InputDevice.SOURCE_GAMEPAD) || it.supportsSource(InputDevice.SOURCE_JOYSTICK)
        }
        return "Soporte en el juego: implementado (sin probar con un mando real).\n" + if (devices.isEmpty()) "Android no detecta ningún mando conectado." else "Android detecta: ${devices.joinToString { it.name }}.\nMisión: stick izq. = mover, stick der. = cámara, R2/R1 = disparar, L2 = correr, A = acción, B = agacharse, X = recargar, Y = inventario, L1 = sigilo, Start = pausa, Select = mapa. Menús: cruceta, A aceptar, B atrás."
    }
    private val graphicsProfiles = listOf("Automático (recomendado)", "Mali · compatibilidad de 24 bits", "Unisoc · compatibilidad de 16 bits", "Snapdragon / Adreno")
    private val resolutions = listOf("screen720" to "Ajustada a la pantalla, 720 líneas (sin bordes negros)", "screen900" to "Ajustada a la pantalla, 900 líneas", "800x450" to "800 × 450 (original, más rápido)", "1280x720" to "1280 × 720", "1600x900" to "1600 × 900", "1920x1080" to "1920 × 1080 (más nítido, más GPU)")
    private fun chooseResolution() {
        val current = resolutions.indexOfFirst { it.first == options.resolution }.coerceAtLeast(0)
        AlertDialog.Builder(this).setTitle(tr("Resolución interna"))
            .setSingleChoiceItems(resolutions.map { tr(it.second) }.toTypedArray(), current) { dialog, which ->
                options.prefs.edit().putString("resolution", resolutions[which].first).apply()
                dialog.dismiss(); showTab(2)
            }.setNegativeButton(tr("Cancelar"), null).show()
    }
    private fun chooseGraphicsProfile() {
        AlertDialog.Builder(this).setTitle(tr("Perfil gráfico"))
            .setSingleChoiceItems(graphicsProfiles.map { tr(it) }.toTypedArray(), options.prefs.getInt("graphics_profile", 0).coerceIn(0, 3)) { dialog, which ->
                options.prefs.edit().putInt("graphics_profile", which).apply()
                dialog.dismiss(); showTab(2)
            }.setNegativeButton(tr("Cancelar"), null).show()
    }
    private fun requestStorage() {
        if (Build.VERSION.SDK_INT >= 30) startActivity(Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION, Uri.parse("package:$packageName")))
        else Toast.makeText(this, "Acceso disponible en esta versión de Android.", Toast.LENGTH_LONG).show()
    }
    private fun chooseFolder() {
        val field = EditText(this).apply { setText(options.gameDir.absolutePath); setSingleLine() }
        val dialog = AlertDialog.Builder(this).setTitle(tr("Carpeta de tu copia del juego")).setMessage(tr("Introduce la carpeta que contiene hitman2.exe, Hitman2.ini y Scenes."))
            .setView(field).setPositiveButton(tr("Guardar"), null).setNegativeButton(tr("Cancelar"), null).create()
        dialog.setOnShowListener {
            dialog.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener {
                val path = File(field.text.toString().trim())
                if (!path.isAbsolute || !path.isDirectory) field.error = tr("Introduce una carpeta existente con permiso de lectura.")
                else { options.prefs.edit().putString("data_path", path.absolutePath).apply(); dialog.dismiss(); refreshStatus(); showTab(3) }
            }
        }
        dialog.show()
    }
    private fun runSelfTest() {
        if (testing || running() || NativeBridge.loadError != null || NativeBridge.gameLoadError != null || NativeBridge.gameStopMessage() != null) return
        testing = true; refreshStatus()
        thread(name = "h2ae-selftest") {
            val report = try { NativeBridge.runSelfTest() } catch (e: Exception) { "Autoprueba fallida: ${e.message}" }
            runOnUiThread {
                if (isDestroyed) return@runOnUiThread
                testing = false; refreshStatus(); showTab(3)
                AlertDialog.Builder(this).setTitle(tr("Resultado de autoprueba")).setMessage(report).setPositiveButton(tr("Cerrar"), null).show()
            }
        }
    }
}
