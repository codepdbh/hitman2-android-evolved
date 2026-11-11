package com.h2ae.android

/**
 * The only door between Kotlin and the native runtime (libh2ae.so).
 * Game logic never lives on this side; the Activity only asks and displays.
 */
object NativeBridge {
    /** Null when the library loaded; otherwise why it did not. */
    val loadError: String? = try {
        System.loadLibrary("h2ae")
        null
    } catch (e: UnsatisfiedLinkError) {
        e.message ?: "UnsatisfiedLinkError"
    }

    /** "arm64-v8a, 64-bit pointers, built …" — facts about the loaded library. */
    external fun buildInfo(): String

    /**
     * Runs runtime/h2_selftest.c. Returns the report; its last line is
     * "N passed, M failed". No game code is involved.
     */
    external fun runSelfTest(): String

    /** Null when libh2game.so (the recompiled game) loaded; otherwise why not. */
    val gameLoadError: String? = try { System.loadLibrary("h2game"); null } catch (e: UnsatisfiedLinkError) { e.message ?: "UnsatisfiedLinkError" }

    /** Starts the game on a native thread drawing into [surface]. False if it could not start. */
    external fun startGame(surface: android.view.Surface, gameDir: String, userDir: String): Boolean
    /** Asks the game to stop and waits for it. */
    external fun stopGame()
    external fun gameRunning(): Boolean

    // Input into the game's InputManager (DirectInput scan codes; mouse relative + absolute cursor in game pixels).
    external fun inputKey(dik: Int, down: Boolean)
    external fun inputMouseMove(dx: Int, dy: Int)
    external fun inputMouseButton(index: Int, down: Boolean)
    external fun inputWheel(dz: Int)
    external fun inputCursor(x: Int, y: Int)
    external fun inputReset()

    /** What the game is showing: 0 starting, 1 intro / logos, 2 menu, 3 mission (from the scene it loaded last). */
    external fun gameContext(): Int
}
