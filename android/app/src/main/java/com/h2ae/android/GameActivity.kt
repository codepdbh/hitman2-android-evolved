package com.h2ae.android

import android.app.Activity
import android.content.Intent
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.Environment
import android.os.Handler
import android.os.Looper
import android.provider.Settings
import android.util.Log
import android.graphics.RectF
import android.view.Gravity
import android.widget.FrameLayout
import android.widget.TextView
import android.view.SurfaceHolder
import android.view.SurfaceView
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.View
import android.view.WindowManager
import java.io.File

/**
 * Hosts the recompiled game. The user's game data is read from internal storage, /sdcard/h2androidevolved
 * (the user copies it there; the app never ships or downloads it). The game draws into this SurfaceView at the
 * original 800x450 and the compositor scales it. Everything the game writes goes to the app's private files.
 */
class GameActivity : Activity(), SurfaceHolder.Callback {
    private lateinit var hud: TouchHudView
    private val pad = GamepadInput()
    private lateinit var gameStatus: TextView
    private val ui = Handler(Looper.getMainLooper())
    private val monitor = object : Runnable {
        override fun run() {
            if (NativeBridge.gameLoadError == null) NativeBridge.gameStopMessage()?.let { showGameError(it) }
            ui.postDelayed(this, 500)
        }
    }
    private val options by lazy { PortOptions(this) }
    private val gameDir get() = options.gameDir

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        val sv = SurfaceView(this)
        intent?.getStringExtra("res")?.takeIf { Regex("\\d{3,4}x\\d{3,4}|screen\\d{3,4}").matches(it) }?.let { options.prefs.edit().putString("resolution", it).apply() }   // tests: --es res 1280x720
        val (resW, resH) = options.resolutionSize
        sv.holder.setFixedSize(resW, resH)
        sv.holder.addCallback(this)
        hud = TouchHudView(this)
        hud.gameW = resW; hud.gameH = resH
        val root = FrameLayout(this)
        root.setBackgroundColor(0xFF000000.toInt())
        root.addView(sv, FrameLayout.LayoutParams(1, 1, Gravity.CENTER))
        root.addView(hud, FrameLayout.LayoutParams(FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT))
        gameStatus = TextView(this).apply {
            setTextColor(0xFFFFFFFF.toInt()); setBackgroundColor(0xEE181D20.toInt())
            textSize = 18f; gravity = Gravity.CENTER; visibility = View.GONE
            setPadding(32, 32, 32, 32)
        }
        root.addView(gameStatus, FrameLayout.LayoutParams(FrameLayout.LayoutParams.MATCH_PARENT,
            FrameLayout.LayoutParams.WRAP_CONTENT, Gravity.CENTER))
        // Keep the picture undistorted (its own aspect), centred, with black bars; the HUD covers the whole screen.
        root.addOnLayoutChangeListener { _, l, t, r, b, _, _, _, _ ->
            val w = r - l; val h = b - t
            if (w <= 0 || h <= 0) return@addOnLayoutChangeListener
            val aspect = resW.toFloat() / resH
            val gw = minOf(w.toFloat(), h * aspect); val gh = gw / aspect
            val lp = sv.layoutParams as FrameLayout.LayoutParams
            if (lp.width != gw.toInt() || lp.height != gh.toInt()) { lp.width = gw.toInt(); lp.height = gh.toInt(); sv.layoutParams = lp }
            hud.gameRect = RectF((w - gw) / 2, (h - gh) / 2, (w + gw) / 2, (h + gh) / 2)
        }
        setContentView(root)
        @Suppress("DEPRECATION")
        sv.systemUiVisibility = View.SYSTEM_UI_FLAG_FULLSCREEN or View.SYSTEM_UI_FLAG_HIDE_NAVIGATION or
            View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY or View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
    }

    /** Hitman2.ini with the three port overrides (same as scripts/boot_probe.py write_profile_ini). */
    /** Optional first scene, e.g. `adb shell am start -n com.h2ae.android/.GameActivity --es scene AllLevels/MainMenu.gms`. */
    private val startScene: String? get() = intent?.getStringExtra("scene")

    private fun writeProfileIni(user: File) {
        user.mkdirs()
        val original = File(gameDir, "Hitman2.ini").readLines(Charsets.ISO_8859_1)
        val originalScene = original.firstOrNull { it.trim().startsWith("DefaultScene=", true) }
            ?.substringAfter('=')?.trim() ?: "AllLevels/Logos.gms"
        val profile = File(user, "Hitman2.ini")
        val source = if (profile.isFile) profile.readLines(Charsets.ISO_8859_1) else original
        profile.writeText(GameProfile.render(source, options.profileOverrides(startScene, originalScene)), Charsets.ISO_8859_1)
    }

    override fun surfaceCreated(holder: SurfaceHolder) {
        if (NativeBridge.gameLoadError != null) { showGameError("No se pudo cargar el motor: ${NativeBridge.gameLoadError}"); return }
        if (NativeBridge.gameRunning()) {
            if (!NativeBridge.setGameSurface(holder.surface)) showGameError("No se pudo conectar la superficie de Android.")
            else { gameStatus.visibility = View.GONE; Log.i(TAG, "game surface reattached") }
            return
        }
        if (Build.VERSION.SDK_INT >= 30 && !Environment.isExternalStorageManager()) {
            Log.e(TAG, "no access to /sdcard: opening the all-files-access settings")
            startActivity(Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION, Uri.parse("package:$packageName")))
            return
        }
        if (!File(gameDir, "hitman2.exe").exists()) { showGameError("No se encuentran los datos en $gameDir. Revisa la carpeta desde el launcher."); return }
        try {
            val stopped = NativeBridge.gameStopMessage()
            if (stopped != null) { showGameError(stopped); return }
            val user = File(filesDir, "user")
            writeProfileIni(user)
            if (!NativeBridge.setGraphicsProfile(options.prefs.getInt("graphics_profile", 0))) {
                showGameError("No se pudo aplicar el perfil gráfico. Cierra la app por completo y vuelve a abrirla."); return
            }
            val ok = NativeBridge.startGame(holder.surface, gameDir.absolutePath, user.absolutePath)
            Log.i(TAG, "startGame -> $ok")
            if (!ok) showGameError("No se pudo iniciar el hilo del juego.")
        } catch (e: java.io.IOException) {
            showGameError("No se pudo leer o escribir el perfil: ${e.message}")
        }
    }

    private fun showGameError(message: String) {
        Log.e(TAG, "game unavailable: $message")
        gameStatus.text = UiText.t(this, "El motor del juego se ha detenido.\n$message\nCierra la app por completo y vuelve a abrirla.")
        gameStatus.visibility = View.VISIBLE
    }

    override fun onResume() {
        super.onResume()
        hud.reloadSettings()
        if (options.prefs.getBoolean("keep_awake", true)) window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        else window.clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        if (NativeBridge.gameLoadError == null) NativeBridge.setGamePaused(false)
        ui.post(monitor)
    }
    override fun dispatchKeyEvent(event: KeyEvent): Boolean = pad.onKey(event) || super.dispatchKeyEvent(event)
    override fun dispatchGenericMotionEvent(event: MotionEvent): Boolean = pad.onMotion(event) || super.dispatchGenericMotionEvent(event)

    override fun onPause() {
        hud.releaseAll(); pad.releaseAll()
        if (NativeBridge.gameLoadError == null) NativeBridge.setGamePaused(true)
        ui.removeCallbacks(monitor)
        super.onPause()
    }

    override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {}
    override fun surfaceDestroyed(holder: SurfaceHolder) {
        if (NativeBridge.gameLoadError == null) NativeBridge.setGameSurface(null)
    }

    private companion object { const val TAG = "H2AE" }
}
