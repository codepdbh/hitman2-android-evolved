package com.h2ae.android

import org.junit.Assert.*
import org.junit.Test

class EnglishUiTest {
    @Test fun translatesControlsAndLauncher() {
        assertEquals("⋯ GEAR", EnglishUi.translate("⋯ EQUIPO"))
        assertEquals("Camera sensitivity", EnglishUi.translate("Sensibilidad de cámara"))
        assertEquals("Continue mission  →", EnglishUi.translate("Continuar misión  →"))
        assertEquals("Crouch", EnglishUi.translate("Agacharse"))
        assertEquals("Close", EnglishUi.translate("Cerrar"))
    }
    @Test fun translatesDynamicFileStateAndKeepsPathsUntouched() {
        assertEquals("3 essential files missing", EnglishUi.translate("Faltan 3 archivos esenciales"))
        assertEquals("Missing:\nScenes/MainMenu.ZIP", EnglishUi.translate("Faltan:\nScenes/MainMenu.ZIP"))
        assertEquals("/sdcard/h2androidevolved", EnglishUi.translate("/sdcard/h2androidevolved"))
    }
    @Test fun explainsMissingEnglishGameAssets() {
        val text = EnglishUi.translate("Tu copia contiene el juego en español. Para jugar en inglés hacen falta los datos ingleses; el selector no los incluye.")
        assertTrue(text.contains("requires English game data"))
    }
}
