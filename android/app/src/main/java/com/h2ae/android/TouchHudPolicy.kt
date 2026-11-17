package com.h2ae.android

import kotlin.math.hypot

/** Android-independent rules shared by rendering, hit testing and headless tests. */
internal object TouchHudPolicy {
    const val DEFAULT_CAMERA_SENSITIVITY = 3f
    val compact = setOf("fire", "action", "crouch", "pause")
    val legacy = compact + setOf("run", "inventory")
    val tabs = listOf("Equipo", "Acciones", "Vista")

    fun group(name: String): Int = when (name) {
        "inventory", "quick_inventory", "melee", "pistol", "smg", "rifle",
        "binoculars", "night_vision", "lockpick" -> 0
        "map", "briefing", "camera", "zoom_in", "zoom_out" -> 2
        else -> 1
    }

    /** Only migrate the untouched old default; never discard a custom arrangement. */
    fun migrateDefault(pinned: Set<String>, moved: Boolean) = pinned == legacy && !moved

    /** Deliberate outer travel, with hysteresis. Walking remains possible near the centre. */
    fun running(dx: Float, dy: Float, wasRunning: Boolean, enabled: Boolean): Boolean =
        enabled && dy < -0.28f && hypot(dx, dy) >= if (wasRunning) 0.96f else 1.18f

    data class Grid(val columns: Int, val rows: Int, val pages: Int)
    fun grid(widthDp: Float, heightDp: Float, count: Int): Grid {
        val columns = (widthDp / 104f).toInt().coerceIn(1, 4)
        val rows = (heightDp / 72f).toInt().coerceIn(1, 3)
        return Grid(columns, rows, ((count + columns * rows - 1) / (columns * rows)).coerceAtLeast(1))
    }
}

/** Keep sub-pixel mouse motion; slow drags must not vanish when events are frequent. */
internal class CameraMotion {
    private var remainderX = 0f
    private var remainderY = 0f
    fun reset() { remainderX = 0f; remainderY = 0f }
    fun consume(dx: Float, dy: Float, gain: Float): Pair<Int, Int> {
        val x = dx * gain + remainderX
        val y = dy * gain + remainderY
        val ix = x.toInt(); val iy = y.toInt()
        remainderX = x - ix; remainderY = y - iy
        return ix to iy
    }
}

internal object GamePointer {
    fun point(x: Float, y: Float, left: Float, top: Float, width: Float, height: Float): Pair<Int, Int> =
        ((x - left) * 800f / width.coerceAtLeast(1f)).toInt().coerceIn(0, 799) to
            ((y - top) * 450f / height.coerceAtLeast(1f)).toInt().coerceIn(0, 449)
}
