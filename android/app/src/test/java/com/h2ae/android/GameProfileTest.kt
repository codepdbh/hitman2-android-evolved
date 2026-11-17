package com.h2ae.android

import org.junit.Assert.*
import org.junit.Test

class GameProfileTest {
    @Test fun handlesBothIniSyntaxesAndRemovesDuplicateManagedKeys() {
        val result = GameProfile.render(listOf("MusicVolume 80", "musicvolume=50", "DefaultScene=old.gms", "CustomOption keep"),
            linkedMapOf("MusicVolume" to "25", "DefaultScene" to "MainMenu.gms", "Subtitles" to "1"))
        assertEquals("MusicVolume 25\r\nDefaultScene=MainMenu.gms\r\nCustomOption keep\r\nSubtitles 1\r\n", result)
    }
    @Test fun preservesUnmanagedSettingsAndComments() {
        val input = listOf("; owner profile", "MouseSpeed 1.356307", "", "GammaValue 0.632911", "  WeatherEffects 1")
        val result = GameProfile.render(input, mapOf("WeatherEffects" to "0"))
        assertTrue(result.contains("MouseSpeed 1.356307\r\n\r\nGammaValue 0.632911\r\n"))
        assertTrue(result.startsWith("; owner profile\r\n"))
        assertTrue(result.endsWith("WeatherEffects 0\r\n"))
    }
    @Test fun repeatedApplicationIsStable() {
        val settings = mapOf("DrawDll" to "RenderOpenGL.dll", "SpeechVolume" to "0")
        val once = GameProfile.render(listOf("SpeechVolume 100"), settings)
        val twice = GameProfile.render(once.trimEnd('\r', '\n').split("\r\n"), settings)
        assertEquals(once, twice)
    }
}
