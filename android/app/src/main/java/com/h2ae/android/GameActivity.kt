package com.h2ae.android

import android.app.Activity
import android.content.Intent
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.Environment
import android.provider.Settings
import android.util.Log
import android.graphics.RectF
import android.view.Gravity
import android.widget.FrameLayout
import android.view.SurfaceHolder
import android.view.SurfaceView
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
    private val gameDir = File(Environment.getExternalStorageDirectory(), "h2androidevolved")

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        val sv = SurfaceView(this)
        sv.holder.setFixedSize(800, 450)
        sv.holder.addCallback(this)
        hud = TouchHudView(this)
        val root = FrameLayout(this)
        root.setBackgroundColor(0xFF000000.toInt())
        root.addView(sv, FrameLayout.LayoutParams(1, 1, Gravity.CENTER))
        root.addView(hud, FrameLayout.LayoutParams(FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT))
        // Keep the original 16:9 picture undistorted, centred, with black bars; the HUD covers the whole screen.
        root.addOnLayoutChangeListener { _, l, t, r, b, _, _, _, _ ->
            val w = r - l; val h = b - t
            if (w <= 0 || h <= 0) return@addOnLayoutChangeListener
            val gw = minOf(w.toFloat(), h * 16f / 9f); val gh = gw * 9f / 16f
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
    private fun writeProfileIni(user: File) {
        user.mkdirs()
        val seen = HashSet<String>()
        val out = ArrayList<String>()
        for (line in File(gameDir, "Hitman2.ini").readLines(Charsets.ISO_8859_1)) {
            val key = line.trim().split(Regex("\\s+")).firstOrNull() ?: ""
            val l = when (key) {
                "DrawDll" -> "DrawDll RenderOpenGL.dll"
                "Window" -> "Window 1"
                "Resolution" -> "Resolution 800x450"
                else -> line
            }
            seen.add(key); out.add(l)
        }
        for ((k, v) in listOf("DrawDll" to "RenderOpenGL.dll", "Window" to "1", "Resolution" to "800x450"))
            if (k !in seen) out.add("$k $v")
        File(user, "Hitman2.ini").writeText(out.joinToString("\r\n") + "\r\n", Charsets.ISO_8859_1)
    }

    override fun surfaceCreated(holder: SurfaceHolder) {
        if (Build.VERSION.SDK_INT >= 30 && !Environment.isExternalStorageManager()) {
            Log.e(TAG, "no access to /sdcard: opening the all-files-access settings")
            startActivity(Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION, Uri.parse("package:$packageName")))
            return
        }
        if (!File(gameDir, "hitman2.exe").exists()) { Log.e(TAG, "game data not found in $gameDir"); return }
        if (NativeBridge.gameLoadError != null) { Log.e(TAG, "libh2game.so: ${NativeBridge.gameLoadError}"); return }
        val user = File(filesDir, "user")
        writeProfileIni(user)
        val ok = NativeBridge.startGame(holder.surface, gameDir.absolutePath, user.absolutePath)
        Log.i(TAG, "startGame -> $ok")
    }

    override fun onPause() { super.onPause(); hud.releaseAll() }

    override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {}
    override fun surfaceDestroyed(holder: SurfaceHolder) { if (NativeBridge.gameLoadError == null) NativeBridge.stopGame() }

    private companion object { const val TAG = "H2AE" }
}
