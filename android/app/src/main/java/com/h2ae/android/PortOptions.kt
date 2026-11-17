package com.h2ae.android

import android.content.Context
import android.os.Environment
import java.io.File

internal class PortOptions(context: Context) {
    val prefs = context.getSharedPreferences("port_options", Context.MODE_PRIVATE)
    val touch = context.getSharedPreferences("touch_hud", Context.MODE_PRIVATE)
    val defaultPath get() = File(Environment.getExternalStorageDirectory(), "h2androidevolved").absolutePath
    val language get() = prefs.getString("language", "es") ?: "es"
    val gameDir get() = File(prefs.getString("data_path", defaultPath) ?: defaultPath)

    fun profileOverrides(scene: String?, originalScene: String): Map<String, String> = linkedMapOf(
        "DefaultScene" to (scene ?: if (prefs.getBoolean("skip_intro", true)) "MainMenu.gms" else originalScene),
        "DrawDll" to "RenderOpenGL.dll", "Window" to "1", "Resolution" to "800x450",
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
