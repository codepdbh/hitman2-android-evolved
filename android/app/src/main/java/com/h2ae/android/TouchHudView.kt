package com.h2ae.android

import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.RectF
import android.os.Handler
import android.os.Looper
import android.view.MotionEvent
import android.view.View
import kotlin.math.abs
import kotlin.math.hypot

/**
 * Touch controls over the game. Pure input: it turns touches into DirectInput scan codes / mouse events for
 * the game's InputManager (NativeBridge.input*), using the bindings found in the install's keyboard.cfg
 * (not the manual's defaults). Pictograms are the PNG files in assets/touch_hud.
 *
 * Layout: a joystick (W A S D) on the left; on the right the buttons used all the time; a "more" toggle opens a
 * grid with weapons, tools, map/briefing/camera, zoom, menu navigation. Any touch on empty screen drags the
 * camera (relative mouse) and moves the cursor; a short tap there is a left click (menus).
 * Cancelled touches and lost focus release everything that is held ([releaseAll]).
 *
 * Not verified on a real play session: sizes, positions and feel are a first layout, see docs/STATUS.md.
 */
class TouchHudView(context: Context) : View(context) {
    private enum class Kind { HOLD, TAP, WHEEL_UP, WHEEL_DOWN }

    /** One on-screen button. keys = DIK codes; mouse = mouse button index or -1. */
    private class Btn(val name: String, val kind: Kind, val keys: IntArray = IntArray(0), val mouse: Int = -1,
                      val panel: Boolean = false, val menu: Boolean = false) {
        val rect = RectF()
        val menuRect = RectF()               // position when the context is MENU (menu buttons only)
        var pointer = -1
        var normal: Bitmap? = null
        var pressed: Bitmap? = null
    }

    private val dp = resources.displayMetrics.density
    private val ui = Handler(Looper.getMainLooper())
    private val paint = Paint(Paint.FILTER_BITMAP_FLAG or Paint.ANTI_ALIAS_FLAG)

    // DirectInput scan codes (DIK_*)
    private companion object {
        const val W = 0x11; const val A = 0x1E; const val S = 0x1F; const val D = 0x20
        const val ESC = 0x01; const val ENTER = 0x1C; const val LCTRL = 0x1D; const val LSHIFT = 0x2A; const val SPACE = 0x39
        const val CAPS = 0x3A; const val F1 = 0x3B; const val Q = 0x10; const val E = 0x12; const val F = 0x21; const val T = 0x14
        const val B = 0x30; const val M = 0x32; const val UP = 0xC8; const val LEFT = 0xCB; const val RIGHT = 0xCD; const val DOWN = 0xD0
        const val N1 = 0x02; const val N2 = 0x03; const val N3 = 0x04; const val N4 = 0x05; const val N5 = 0x06; const val N6 = 0x07; const val N7 = 0x08
        const val NUM1 = 0x4F; const val NUM3 = 0x51
        const val MIN_PRESS_MS = 60L          // a tap must last long enough for the game's per-frame polling to see it
        const val GAME_W = 800; const val GAME_H = 450
        const val MOUSE_SPEED = 1.356307f
    }

    private val buttons = listOf(
        // always visible
        Btn("fire", Kind.HOLD, mouse = 0), Btn("action", Kind.HOLD, intArrayOf(E)), Btn("reload", Kind.TAP, mouse = 1),
        Btn("crouch", Kind.HOLD, intArrayOf(LCTRL)), Btn("sneak", Kind.TAP, intArrayOf(SPACE)), Btn("run", Kind.HOLD, intArrayOf(LSHIFT)),
        Btn("inventory", Kind.HOLD, intArrayOf(Q)), Btn("quick_inventory", Kind.HOLD, intArrayOf(F)),
        Btn("lean_left", Kind.HOLD, intArrayOf(NUM1)), Btn("lean_right", Kind.HOLD, intArrayOf(NUM3)),
        Btn("pause", Kind.TAP, intArrayOf(ESC)),
        // "more" panel
        Btn("melee", Kind.TAP, intArrayOf(N1), panel = true), Btn("pistol", Kind.TAP, intArrayOf(N2), panel = true),
        Btn("smg", Kind.TAP, intArrayOf(N3), panel = true), Btn("rifle", Kind.TAP, intArrayOf(N4), panel = true),
        Btn("binoculars", Kind.TAP, intArrayOf(N5), panel = true), Btn("night_vision", Kind.TAP, intArrayOf(N6), panel = true),
        Btn("lockpick", Kind.TAP, intArrayOf(N7), panel = true), Btn("drop", Kind.HOLD, intArrayOf(T), panel = true),
        Btn("map", Kind.TAP, intArrayOf(M), panel = true), Btn("briefing", Kind.TAP, intArrayOf(B), panel = true),
        Btn("camera", Kind.TAP, intArrayOf(F1), panel = true), Btn("run_toggle", Kind.TAP, intArrayOf(CAPS), panel = true),
        Btn("zoom_in", Kind.WHEEL_UP, intArrayOf(UP), panel = true), Btn("zoom_out", Kind.WHEEL_DOWN, intArrayOf(DOWN), panel = true),
        Btn("action_previous", Kind.WHEEL_UP, panel = true), Btn("action_next", Kind.WHEEL_DOWN, panel = true),
        Btn("nav_up", Kind.TAP, menu = true, keys = intArrayOf(UP), panel = true), Btn("nav_down", Kind.TAP, menu = true, keys = intArrayOf(DOWN), panel = true),
        Btn("nav_left", Kind.TAP, menu = true, keys = intArrayOf(LEFT), panel = true), Btn("nav_right", Kind.TAP, menu = true, keys = intArrayOf(RIGHT), panel = true),
        Btn("accept", Kind.TAP, menu = true, keys = intArrayOf(ENTER), panel = true),
    )
    // Assets present in the package but deliberately not wired: "holster" has no binding in this install's keyboard.cfg.

    private var panelOpen = false
    private val moreRect = RectF()
    private var morePointer = -1
    private var joyBase: Bitmap? = null
    private var joyKnob: Bitmap? = null
    private val joyRect = RectF()
    private var joyPointer = -1
    private var joyDx = 0f; private var joyDy = 0f
    private val joyKeysDown = booleanArrayOf(false, false, false, false)       // W A S D
    private var lookPointer = -1
    private var lookX = 0f; private var lookY = 0f; private var lookDownT = 0L; private var lookMoved = 0f
    /** The game's picture rectangle on screen, for mapping touches to game pixels. */
    var gameRect = RectF(0f, 0f, 1f, 1f)
    var opacity = 0.72f

    /** 0 starting, 1 intro / logos, 2 menu, 3 mission (NativeBridge.gameContext). Controls shown depend on it. */
    private var ctx = 0
    private val poll = object : Runnable {
        override fun run() {
            val now = if (NativeBridge.gameLoadError == null) NativeBridge.gameContext() else 0
            if (now != ctx) { releaseAll(); ctx = now; panelOpen = false; invalidate() }
            ui.postDelayed(this, 200)
        }
    }
    override fun onAttachedToWindow() { super.onAttachedToWindow(); ui.post(poll) }
    override fun onDetachedFromWindow() { ui.removeCallbacks(poll); super.onDetachedFromWindow() }

    init { loadAssets() }

    private fun bmp(name: String): Bitmap? = try { context.assets.open("touch_hud/$name.png").use { BitmapFactory.decodeStream(it) } } catch (e: Exception) { null }

    private fun loadAssets() {
        for (b in buttons) { b.normal = bmp("${b.name}_normal"); b.pressed = bmp("${b.name}_pressed") }
        joyBase = bmp("joystick_base"); joyKnob = bmp("joystick_knob")
    }

    override fun onSizeChanged(w: Int, h: Int, ow: Int, oh: Int) {
        val m = 20 * dp
        // joystick: bottom-left
        val js = 170 * dp
        joyRect.set(m + 20 * dp, h - js - m, m + 20 * dp + js, h - m)
        // right cluster (distances in dp from the bottom-right corner)
        fun place(n: String, cxFromRight: Float, cyFromBottom: Float, size: Float) {
            val b = buttons.first { it.name == n }
            val cx = w - cxFromRight * dp - m; val cy = h - cyFromBottom * dp - m
            b.rect.set(cx - size * dp / 2, cy - size * dp / 2, cx + size * dp / 2, cy + size * dp / 2)
        }
        place("fire", 70f, 70f, 110f)
        place("action", 170f, 40f, 76f); place("reload", 40f, 160f, 64f); place("run", 160f, 130f, 64f)
        place("crouch", 250f, 60f, 64f); place("sneak", 250f, 130f, 64f)
        place("inventory", 150f, 215f, 60f); place("quick_inventory", 80f, 235f, 60f)
        place("lean_left", 300f, 215f, 56f); place("lean_right", 235f, 215f, 56f)
        // pause: top-right
        buttons.first { it.name == "pause" }.rect.set(w - m - 56 * dp, m, w - m, m + 56 * dp)
        moreRect.set(w - m - 56 * dp, m + 66 * dp, w - m, m + 122 * dp)
        // menu layout (bottom-right d-pad + accept; pause acts as back)
        run {
            val u = 72 * dp; val cx = w - m - 3.2f * u; val cy = h - m - 1.6f * u
            fun at(n: String, x: Float, y: Float, size: Float) { buttons.first { it.name == n }.menuRect.set(x - size / 2, y - size / 2, x + size / 2, y + size / 2) }
            at("nav_up", cx, cy - u, 64 * dp); at("nav_down", cx, cy + u, 64 * dp)
            at("nav_left", cx - u, cy, 64 * dp); at("nav_right", cx + u, cy, 64 * dp)
            at("accept", w - m - 1.0f * u, h - m - 1.1f * u, 96 * dp)
        }
        // panel: 6 columns of 54dp cells, centred
        val cell = 58 * dp; val cols = 6
        val panelBtns = buttons.filter { it.panel }
        val rows = (panelBtns.size + cols - 1) / cols
        val x0 = (w - cols * cell) / 2; val y0 = (h - rows * cell) / 2
        panelBtns.forEachIndexed { i, b ->
            val cx = x0 + (i % cols) * cell; val cy = y0 + (i / cols) * cell
            b.rect.set(cx + 4 * dp, cy + 4 * dp, cx + cell - 4 * dp, cy + cell - 4 * dp)
        }
    }

    private fun visible(b: Btn) = when (ctx) {
        3 -> if (b.name == "pause") true else (!b.panel || panelOpen)
        2 -> b.menu || b.name == "pause"
        1 -> b.name == "pause"                   // intro / logos / cutscenes: Esc skips
        else -> false
    }
    private fun rectOf(b: Btn) = if (ctx == 2 && b.menu) b.menuRect else b.rect

    override fun onDraw(c: Canvas) {
        paint.alpha = (opacity * 255).toInt()
        val jb = joyBase; val jk = joyKnob
        if (ctx == 3 && jb != null) c.drawBitmap(jb, null, joyRect, paint)
        if (ctx == 3 && jk != null) {
            val k = joyRect.width() * 0.42f
            val cx = joyRect.centerX() + joyDx * joyRect.width() * 0.3f; val cy = joyRect.centerY() + joyDy * joyRect.width() * 0.3f
            c.drawBitmap(jk, null, RectF(cx - k / 2, cy - k / 2, cx + k / 2, cy + k / 2), paint)
        }
        for (b in buttons) {
            if (!visible(b)) continue
            val img = (if (b.pointer >= 0) b.pressed else b.normal) ?: continue
            c.drawBitmap(img, null, rectOf(b), paint)
        }
        if (ctx != 3) return
        // "more" toggle: drawn, there is no pictogram for it in the package
        paint.style = Paint.Style.FILL
        paint.color = if (panelOpen) 0xFFB02020.toInt() else 0xFF202426.toInt(); paint.alpha = (opacity * 255).toInt()
        c.drawCircle(moreRect.centerX(), moreRect.centerY(), moreRect.width() / 2, paint)
        paint.color = 0xFFEEF2F0.toInt(); paint.alpha = (opacity * 255).toInt()
        for (i in -1..1) c.drawCircle(moreRect.centerX() + i * moreRect.width() * 0.22f, moreRect.centerY(), moreRect.width() * 0.06f, paint)
        paint.color = 0xFFFFFFFF.toInt()
    }

    // ---- input helpers ----
    private fun press(b: Btn, down: Boolean) {
        for (k in b.keys) NativeBridge.inputKey(k, down)
        if (b.mouse >= 0) NativeBridge.inputMouseButton(b.mouse, down)
        // Logos and intro movies are skipped with the mouse button in this game (the keyboard is not polled there).
        if (ctx == 1 && b.name == "pause") NativeBridge.inputMouseButton(0, down)
        if (down) when (b.kind) { Kind.WHEEL_UP -> NativeBridge.inputWheel(120); Kind.WHEEL_DOWN -> NativeBridge.inputWheel(-120); else -> {} }
    }

    private val pressedAt = HashMap<Btn, Long>()
    private fun buttonDown(b: Btn, pid: Int) { b.pointer = pid; pressedAt[b] = System.currentTimeMillis(); press(b, true); invalidate() }
    private fun buttonUp(b: Btn) {
        val wait = MIN_PRESS_MS - (System.currentTimeMillis() - (pressedAt[b] ?: 0L))
        b.pointer = -1
        if (wait > 0) ui.postDelayed({ if (b.pointer < 0) press(b, false) }, wait) else press(b, false)
        invalidate()
    }

    private fun setJoy(idx: Int, down: Boolean) {
        if (joyKeysDown[idx] == down) return
        joyKeysDown[idx] = down
        NativeBridge.inputKey(intArrayOf(W, A, S, D)[idx], down)
    }
    private fun updateJoy(x: Float, y: Float) {
        val r = joyRect.width() / 2
        var dx = (x - joyRect.centerX()) / r; var dy = (y - joyRect.centerY()) / r
        val len = hypot(dx, dy); if (len > 1f) { dx /= len; dy /= len }
        joyDx = dx; joyDy = dy
        val dead = 0.28f
        setJoy(0, dy < -dead); setJoy(2, dy > dead); setJoy(1, dx < -dead); setJoy(3, dx > dead)
        invalidate()
    }
    private fun releaseJoy() { joyPointer = -1; joyDx = 0f; joyDy = 0f; for (i in 0..3) setJoy(i, false); invalidate() }

    private fun toGame(x: Float, y: Float): Pair<Int, Int> {
        val gx = ((x - gameRect.left) / gameRect.width() * GAME_W).toInt().coerceIn(0, GAME_W - 1)
        val gy = ((y - gameRect.top) / gameRect.height() * GAME_H).toInt().coerceIn(0, GAME_H - 1)
        return gx to gy
    }

    /**
     * The game's menus use DirectInput *relative* mouse motion only (no absolute cursor), so a tap must move the
     * game's own cursor to the touched point: push it into the top-left corner (the game clamps its cursor to the
     * screen), wait a few frames so that motion is consumed on its own, then move by (target / MouseSpeed) and click.
     * MouseSpeed is the value in Hitman2.ini (1.356307); the cursor model (position += delta * speed, clamped) is an
     * assumption that must be confirmed on a menu screen.
     */
    private fun clickGameAt(gx: Int, gy: Int) {
        NativeBridge.inputMouseMove(-4000, -4000)
        ui.postDelayed({ NativeBridge.inputMouseMove((gx / MOUSE_SPEED).toInt(), (gy / MOUSE_SPEED).toInt()) }, 90)
        ui.postDelayed({ NativeBridge.inputMouseButton(0, true) }, 180)
        ui.postDelayed({ NativeBridge.inputMouseButton(0, false) }, 260)
    }

    /** Releases every held input (cancelled touch, activity paused). */
    fun releaseAll() {
        releaseJoy()
        for (b in buttons) if (b.pointer >= 0) { b.pointer = -1; press(b, false) }
        if (lookPointer >= 0) { lookPointer = -1 }
        NativeBridge.inputReset()
        invalidate()
    }

    private fun buttonAt(x: Float, y: Float): Btn? = buttons.firstOrNull { visible(it) && it.pointer < 0 && rectOf(it).contains(x, y) }

    override fun onTouchEvent(e: MotionEvent): Boolean {
        if (NativeBridge.gameLoadError != null) return false
        when (e.actionMasked) {
            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                val i = e.actionIndex; val id = e.getPointerId(i); val x = e.getX(i); val y = e.getY(i)
                if (ctx == 3 && moreRect.contains(x, y)) { morePointer = id; panelOpen = !panelOpen; invalidate(); return true }
                val b = buttonAt(x, y)
                if (b != null) { buttonDown(b, id); return true }
                if (ctx == 3 && joyPointer < 0 && joyRect.contains(x, y)) { joyPointer = id; updateJoy(x, y); return true }
                if (lookPointer < 0) {
                    lookPointer = id; lookX = x; lookY = y; lookDownT = System.currentTimeMillis(); lookMoved = 0f
                    val (gx, gy) = toGame(x, y); NativeBridge.inputCursor(gx, gy)
                }
            }
            MotionEvent.ACTION_MOVE -> {
                for (i in 0 until e.pointerCount) {
                    val id = e.getPointerId(i); val x = e.getX(i); val y = e.getY(i)
                    if (id == joyPointer) updateJoy(x, y)
                    else if (id == lookPointer) {
                        val dx = x - lookX; val dy = y - lookY
                        lookX = x; lookY = y; lookMoved += abs(dx) + abs(dy)
                        val k = GAME_W / gameRect.width()
                        NativeBridge.inputMouseMove((dx * k * 1.6f).toInt(), (dy * k * 1.6f).toInt())
                        val (gx, gy) = toGame(x, y); NativeBridge.inputCursor(gx, gy)
                    }
                }
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP -> {
                val i = e.actionIndex; val id = e.getPointerId(i)
                if (id == morePointer) morePointer = -1
                buttons.firstOrNull { it.pointer == id }?.let { buttonUp(it) }
                if (id == joyPointer) releaseJoy()
                if (id == lookPointer) {
                    lookPointer = -1
                    if (System.currentTimeMillis() - lookDownT < 250 && lookMoved < 24 * dp) {     // a tap = left click
                        if (ctx == 2) { val (gx, gy) = toGame(lookX, lookY); clickGameAt(gx, gy) }
                        else { NativeBridge.inputMouseButton(0, true); ui.postDelayed({ NativeBridge.inputMouseButton(0, false) }, 80) }
                    }
                }
            }
            MotionEvent.ACTION_CANCEL -> releaseAll()
        }
        return true
    }
}
