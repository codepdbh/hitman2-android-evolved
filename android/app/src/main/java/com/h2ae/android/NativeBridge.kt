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
}
