package com.h2ae.android

import android.content.Context
import android.os.Environment
import java.io.File

internal class PortOptions(context: Context) {
    private val metrics = context.resources.displayMetrics
    val prefs = context.getSharedPreferences("port_options", Context.MODE_PRIVATE)
    val touch = context.getSharedPreferences("touch_hud", Context.MODE_PRIVATE)
    val defaultPath get() = File(Environment.getExternalStorageDirectory(), "h2androidevolved").absolutePath
    val language get() = prefs.getString("language", "es") ?: "es"
    val gameDir get() = File(prefs.getString("data_path", defaultPath) ?: defaultPath)
    /**
     * Internal resolution "WxH". Presets are 16:9; "screenNNN" means NNN lines high and as wide as this phone's screen
     * (so the picture fills it, with no side bars). The game then draws a wider view: whether it widens the field of
     * view or crops is up to its own projection (see docs/RENDER_REVIEW.md).
     */
    val resolution: String get() {
        val v = prefs.getString("resolution", "screen720") ?: "screen720"
        if (v.startsWith("screen")) {
            val h = (v.removePrefix("screen").toIntOrNull() ?: 720).coerceIn(360, 1440)
            val long = maxOf(metrics.widthPixels, metrics.heightPixels).toFloat(); val short = minOf(metrics.widthPixels, metrics.heightPixels).toFloat()
            val w = (Math.round(h * long / short / 2f) * 2).coerceIn(h, 3200)
            return "${w}x$h"
        }
        return v.takeIf { Regex("\\d{3,4}x\\d{3,4}").matches(it) } ?: "800x450"
    }
    val resolutionSize get() = resolution.split("x").let { it[0].toInt() to it[1].toInt() }

    fun profileOverrides(scene: String?, originalScene: String): Map<String, String> = linkedMapOf(
        "DefaultScene" to (scene ?: if (prefs.getBoolean("skip_intro", true)) "MainMenu.gms" else originalScene),
        "DrawDll" to "RenderOpenGL.dll", "Window" to "1", "Resolution" to resolution,
        "SpeechVolume" to prefs.getInt("speech_volume", 100).coerceIn(0, 100).toString(),
        "MusicVolume" to prefs.getInt("music_volume", 100).coerceIn(0, 100).toString(),
        "SoundEffectsVolume" to prefs.getInt("effects_volume", 100).coerceIn(0, 100).toString(),
        "Subtitles" to if (prefs.getBoolean("subtitles", true)) "1" else "0",
        "WeatherEffects" to if (prefs.getBoolean("weather", true)) "1" else "0"
    )
    fun missingFiles(): List<String> = listOf("hitman2.exe", "RenderOpenGL.dll", "Hitman2.ini", "keyboard.cfg",
        "streams.wav", "Scenes/MainMenu.ZIP", "Scenes/MainMenu.WHD", "Scenes/MainMenu.WAV")
        .filter { !File(gameDir, it).isFile }
}
