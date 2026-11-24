package com.h2ae.android

import android.app.AlertDialog
import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.RectF
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.view.MotionEvent
import android.view.View
import android.view.HapticFeedbackConstants
import android.view.InputDevice
import android.widget.LinearLayout
import android.widget.SeekBar
import android.widget.TextView
import java.util.Locale
import kotlin.math.abs
import kotlin.math.hypot

/**
 * Touch controls over the game. Pure input: touches become DirectInput scan codes / mouse events for the game's
 * InputManager (NativeBridge.input*), using the bindings in the install's keyboard.cfg (not the manual's
 * defaults). Pictograms are the PNG files in assets/touch_hud.
 *
 * What is shown depends on what the game is showing (NativeBridge.gameContext):
 *   intro / logos / movies : one labelled SKIP button
 *   menus                  : d-pad + accept, pause (back), the settings gear
 *   mission                : fire, action, crouch, pause + a labelled, paged action drawer
 * Running uses deliberate outer joystick travel (configurable). Settings live inside the drawer.
 * The default screen set is deliberately small. Settings choose which buttons stay on
 * screen (the others go to the "..." panel), moving them by dragging, size, opacity, restore defaults. The
 * layout is saved in SharedPreferences.
 *
 * Any touch on empty screen drags the camera (relative mouse); in menus a tap moves the game's cursor there and
 * clicks. Cancelled touches and a paused activity release everything held ([releaseAll]).
 *
 * Compact layout: compiled and policy-tested headlessly; device ergonomics pending (docs/STATUS.md).
 */
class TouchHudView(context: Context) : View(context) {
    private enum class Kind { HOLD, TAP, WHEEL_UP, WHEEL_DOWN }

    /** One button. fx/fy: default centre as a fraction of the view; size in dp; main: on screen by default. */
    private class Btn(val name: String, val label: String, val kind: Kind, val keys: IntArray = IntArray(0), val mouse: Int = -1,
                      val main: Boolean = false, val fx: Float = 0.5f, val fy: Float = 0.5f, val size: Float = 56f, val menu: Boolean = false) {
        val rect = RectF()
        val menuRect = RectF()
        var pointer = -1
        var normal: Bitmap? = null
        var pressed: Bitmap? = null
        var onScreen = main
        var x = fx; var y = fy
    }

    private val dp = resources.displayMetrics.density
    private val ui = Handler(Looper.getMainLooper())
    private val paint = Paint(Paint.FILTER_BITMAP_FLAG or Paint.ANTI_ALIAS_FLAG)
    private val prefs = context.getSharedPreferences("touch_hud", Context.MODE_PRIVATE)

    private companion object {
        const val W = 0x11; const val A = 0x1E; const val S = 0x1F; const val D = 0x20
        const val ESC = 0x01; const val ENTER = 0x1C; const val LCTRL = 0x1D; const val LSHIFT = 0x2A; const val SPACE = 0x39
        const val CAPS = 0x3A; const val F1 = 0x3B; const val Q = 0x10; const val E = 0x12; const val F = 0x21; const val T = 0x14
        const val B = 0x30; const val M = 0x32; const val UP = 0xC8; const val LEFT = 0xCB; const val RIGHT = 0xCD; const val DOWN = 0xD0
        const val N1 = 0x02; const val N2 = 0x03; const val N3 = 0x04; const val N4 = 0x05; const val N5 = 0x06; const val N6 = 0x07; const val N7 = 0x08
        const val NUM1 = 0x4F; const val NUM3 = 0x51
        const val MIN_PRESS_MS = 60L          // a tap must last long enough for the game's per-frame polling to see it
        const val MENU_CLICK_DELAY = 80L            // after the cursor has arrived, so the hover is applied before the click
        const val MENU_SEEK_MS = 50L               // time between nudges: the sprite must have been redrawn
        const val MENU_SEEK_TOLERANCE = 5          // pixels
    }

    private val buttons = listOf(
        // on screen by default (few)
        Btn("fire", "Disparar", Kind.HOLD, mouse = 0, main = true, fx = 0.90f, fy = 0.74f, size = 100f),
        Btn("action", "Acción", Kind.HOLD, intArrayOf(E), main = true, fx = 0.77f, fy = 0.87f, size = 72f),
        Btn("crouch", "Agacharse", Kind.HOLD, intArrayOf(LCTRL), main = true, fx = 0.66f, fy = 0.87f, size = 58f),
        Btn("run", "Correr", Kind.HOLD, intArrayOf(LSHIFT), main = false, fx = 0.78f, fy = 0.64f, size = 58f),
        Btn("inventory", "Inventario", Kind.HOLD, intArrayOf(Q), main = false, fx = 0.90f, fy = 0.46f, size = 54f),
        Btn("pause", "Pausa", Kind.TAP, intArrayOf(ESC), main = true, fx = 0.955f, fy = 0.10f, size = 48f),
        // in the "..." panel by default (can be pinned to the screen)
        Btn("reload", "Recargar", Kind.TAP, mouse = 1),
        Btn("sneak", "Sigilo", Kind.TAP, intArrayOf(SPACE)),
        Btn("lean_left", "Inclinar izq.", Kind.HOLD, intArrayOf(NUM1)), Btn("lean_right", "Inclinar der.", Kind.HOLD, intArrayOf(NUM3)),
        Btn("quick_inventory", "Inv. rápido", Kind.HOLD, intArrayOf(F)),
        Btn("melee", "Cuerpo a cuerpo", Kind.TAP, intArrayOf(N1)), Btn("pistol", "Pistolas", Kind.TAP, intArrayOf(N2)),
        Btn("smg", "Subfusiles", Kind.TAP, intArrayOf(N3)), Btn("rifle", "Rifles", Kind.TAP, intArrayOf(N4)),
        Btn("binoculars", "Prismáticos", Kind.TAP, intArrayOf(N5)), Btn("night_vision", "Visión nocturna", Kind.TAP, intArrayOf(N6)),
        Btn("lockpick", "Ganzúa", Kind.TAP, intArrayOf(N7)), Btn("drop", "Soltar", Kind.HOLD, intArrayOf(T)),
        Btn("map", "Mapa", Kind.TAP, intArrayOf(M)), Btn("briefing", "Informe", Kind.TAP, intArrayOf(B)),
        Btn("camera", "Cámara", Kind.TAP, intArrayOf(F1)), Btn("run_toggle", "Correr (alternar)", Kind.TAP, intArrayOf(CAPS)),
        Btn("zoom_in", "Zoom +", Kind.WHEEL_UP, intArrayOf(UP)), Btn("zoom_out", "Zoom −", Kind.WHEEL_DOWN, intArrayOf(DOWN)),
        Btn("action_previous", "Acción anterior", Kind.WHEEL_UP), Btn("action_next", "Acción siguiente", Kind.WHEEL_DOWN),
        // menu navigation (shown in menus only; not part of the layout settings)
        Btn("nav_up", "Arriba", Kind.TAP, intArrayOf(UP), menu = true), Btn("nav_down", "Abajo", Kind.TAP, intArrayOf(DOWN), menu = true),
        Btn("nav_left", "Izquierda", Kind.TAP, intArrayOf(LEFT), menu = true), Btn("nav_right", "Derecha", Kind.TAP, intArrayOf(RIGHT), menu = true),
        Btn("accept", "Aceptar", Kind.TAP, intArrayOf(ENTER, E), menu = true),
    )
    // "holster" exists as an asset but has no binding in this install's keyboard.cfg, so it is not offered.

    private val skipRect = RectF()
    private val gearRect = RectF()
    private val moreRect = RectF()
    private val doneRect = RectF()
    private var panelOpen = false
    // The drawer is always reachable: hiding it could strand unpinned actions.
    private val panelRect = RectF()
    private val settingsRect = RectF()
    private val pageRect = RectF()
    private val tabRects = List(TouchHudPolicy.tabs.size) { RectF() }
    private var panelTab = 0
    private var panelPage = 0
    private var panelPages = 1
    private var panelButtons = emptyList<Btn>()
    private var runWithJoystick = true
    private var joyRunning = false
    private var editMode = false
    private var editPointer = -1
    private var editBtn: Btn? = null
    private var userScale = 1.0f
    private var cameraSensitivity = TouchHudPolicy.DEFAULT_CAMERA_SENSITIVITY
    private val cameraMotion = CameraMotion()
    var opacity = 0.70f

    private var joyBase: Bitmap? = null
    private var joyKnob: Bitmap? = null
    private val joyRect = RectF()
    private var joyPointer = -1
    private var joyDx = 0f; private var joyDy = 0f
    private val joyKeysDown = booleanArrayOf(false, false, false, false)       // W A S D
    private var lookPointer = -1
    private var lookX = 0f; private var lookY = 0f
    private var menuPointer = -1
    private var menuPressedAt = 0L
    private var menuRelease: Runnable? = null
    private var skipPointer = -1; private var gearPointer = -1; private var donePointer = -1
    /** The game's picture rectangle on screen, for mapping touches to game pixels. */
    var gameRect = RectF(0f, 0f, 1f, 1f)
    /** The game's internal resolution (the ini's Resolution): touch positions are mapped to it. */
    var gameW = 800
    var gameH = 450

    /** 0 starting, 1 intro / logos, 2 menu, 3 mission (NativeBridge.gameContext). */
    private var ctx = 0
    private val poll = object : Runnable {
        override fun run() {
            val now = if (NativeBridge.gameLoadError == null) NativeBridge.gameContext() else 0
            if (now != ctx) { releaseAll(); ctx = now; panelOpen = false; editMode = false; layout(); invalidate() }
            ui.removeCallbacks(this)
            ui.postDelayed(this, 200)
        }
    }
    override fun onAttachedToWindow() { super.onAttachedToWindow(); ui.post(poll) }
    override fun onDetachedFromWindow() { releaseAll(); ui.removeCallbacksAndMessages(null); super.onDetachedFromWindow() }

    init { loadAssets(); loadLayout() }

    fun reloadSettings() { releaseAll(); loadLayout(); layout(); invalidate() }

    // ---------------- assets and saved layout ----------------
    private fun bmp(name: String): Bitmap? = try { context.assets.open("touch_hud/$name.png").use { BitmapFactory.decodeStream(it) } } catch (e: Exception) { null }

    private fun loadAssets() {
        for (b in buttons) { b.normal = bmp("${b.name}_normal"); b.pressed = bmp("${b.name}_pressed") }
        joyBase = bmp("joystick_base"); joyKnob = bmp("joystick_knob")
    }

    private fun loadLayout() {
        opacity = prefs.getFloat("opacity", 0.60f).coerceIn(0.2f, 1f)
        userScale = prefs.getFloat("scale", 1.0f).coerceIn(0.6f, 1.6f)
        runWithJoystick = prefs.getBoolean("joystick_run", true)
        cameraSensitivity = prefs.getFloat("camera_sensitivity", TouchHudPolicy.DEFAULT_CAMERA_SENSITIVITY).coerceIn(0.5f, 8f)
        for (b in buttons) {
            if (b.menu) continue
            b.onScreen = prefs.getBoolean("on_${b.name}", b.main)
            b.x = prefs.getFloat("x_${b.name}", b.fx); b.y = prefs.getFloat("y_${b.name}", b.fy)
        }
        if (prefs.getInt("layout_version", 0) < 2) {
            val pinned = buttons.filter { !it.menu && it.onScreen }.map { it.name }.toSet()
            val moved = buttons.any { !it.menu && (it.x != it.fx || it.y != it.fy) }
            if (TouchHudPolicy.migrateDefault(pinned, moved)) {
                buttons.filter { !it.menu }.forEach { it.onScreen = it.name in TouchHudPolicy.compact }
            }
            saveLayout()
        }
    }

    private fun saveLayout() {
        val e = prefs.edit()
        e.putFloat("opacity", opacity).putFloat("scale", userScale)
            .putInt("layout_version", 2).putBoolean("joystick_run", runWithJoystick)
            .putFloat("camera_sensitivity", cameraSensitivity)
        for (b in buttons) if (!b.menu) e.putBoolean("on_${b.name}", b.onScreen).putFloat("x_${b.name}", b.x).putFloat("y_${b.name}", b.y)
        e.apply()
    }

    private fun resetLayout() {
        releaseAll(); panelOpen = false
        opacity = 0.60f; userScale = 1.0f; runWithJoystick = true
        cameraSensitivity = TouchHudPolicy.DEFAULT_CAMERA_SENSITIVITY
        for (b in buttons) { b.onScreen = b.main; b.x = b.fx; b.y = b.fy }
        saveLayout(); layout(); invalidate()
    }

    // ---------------- layout ----------------
    private fun tr(text: String) = UiText.t(context, text)

    private fun scaled(v: Float) = v * dp * userScale

    private fun layout() {
        val w = width.toFloat(); val h = height.toFloat()
        if (w <= 0f || h <= 0f) return
        val m = 20 * dp
        val js = minOf(scaled(132f), h * 0.42f)
        joyRect.set(m + 16 * dp, h - js - m, m + 16 * dp + js, h - m)
        for (b in buttons) {
            if (b.menu) continue
            val sz = minOf(scaled(b.size), h * 0.27f)
            val cx = (b.x * w).coerceIn(sz / 2, w - sz / 2)
            val cy = (b.y * h).coerceIn(sz / 2, h - sz / 2)
            b.rect.set(cx - sz / 2, cy - sz / 2, cx + sz / 2, cy + sz / 2)
        }
        val gs = 44 * dp
        gearRect.set(m, m, m + gs, m + gs)
        // Keep clear of the original health/suspicion HUD in the top-left of the 16:9 picture.
        moreRect.set(w / 2 - 50 * dp, m, w / 2 + 50 * dp, m + 48 * dp)
        skipRect.set(w - m - 190 * dp, h - m - 64 * dp, w - m, h - m)
        doneRect.set(w / 2 - 80 * dp, m, w / 2 + 80 * dp, m + 48 * dp)
        // menu layout: bottom-right d-pad + accept
        val u = 72 * dp; val cx = w - m - 3.2f * u; val cy = h - m - 1.6f * u
        fun at(n: String, x: Float, y: Float, size: Float) { buttons.first { it.name == n }.menuRect.set(x - size / 2, y - size / 2, x + size / 2, y + size / 2) }
        at("nav_up", cx, cy - u, 64 * dp); at("nav_down", cx, cy + u, 64 * dp); at("nav_left", cx - u, cy, 64 * dp); at("nav_right", cx + u, cy, 64 * dp)
        at("accept", w - m - 1.0f * u, h - m - 1.1f * u, 96 * dp)
        // A modal drawer with labelled cells. Pagination keeps targets usable on small screens.
        val pw = minOf(w - 2 * m, 480 * dp)
        panelRect.set((w - pw) / 2, m, (w + pw) / 2, h - m)
        val innerW = pw - 16 * dp
        val top = panelRect.top
        val tabW = innerW / tabRects.size
        tabRects.forEachIndexed { i, r ->
            r.set(panelRect.left + 8 * dp + i * tabW, top + 48 * dp,
                panelRect.left + 8 * dp + (i + 1) * tabW, top + 96 * dp)
        }
        settingsRect.set(panelRect.left + 8 * dp, panelRect.bottom - 48 * dp, panelRect.centerX(), panelRect.bottom)
        pageRect.set(panelRect.centerX(), panelRect.bottom - 48 * dp, panelRect.right - 8 * dp, panelRect.bottom)
        val all = buttons.filter { inPanel(it) && TouchHudPolicy.group(it.name) == panelTab }
        val availableH = (panelRect.height() - 144 * dp).coerceAtLeast(1f)
        val grid = TouchHudPolicy.grid(innerW / dp, availableH / dp, all.size)
        panelPages = grid.pages; panelPage = panelPage.coerceIn(0, panelPages - 1)
        panelButtons = all.drop(panelPage * grid.columns * grid.rows).take(grid.columns * grid.rows)
        val cw = innerW / grid.columns
        val ch = availableH / grid.rows
        panelButtons.forEachIndexed { i, b ->
            val px = panelRect.left + 8 * dp + (i % grid.columns) * cw
            val py = top + 96 * dp + (i / grid.columns) * ch
            b.menuRect.set(px + 3 * dp, py + 3 * dp, px + cw - 3 * dp, py + ch - 3 * dp)
        }
    }

    override fun onSizeChanged(w: Int, h: Int, ow: Int, oh: Int) { layout() }

    private fun inPanel(b: Btn) = !b.menu && !b.onScreen
    private fun visible(b: Btn): Boolean = when (ctx) {
        3 -> when {
            b.menu -> false
            b.name == "pause" -> true
            b.onScreen -> true
            else -> false
        }
        2 -> b.menu || b.name == "pause"
        else -> false
    }
    /** Where a button is drawn and hit-tested: its panel cell, its menu position, or its own place. */
    private fun rectOf(b: Btn): RectF = when {
        ctx == 2 && b.menu -> b.menuRect
        ctx == 3 && inPanel(b) -> b.menuRect
        else -> b.rect
    }

    // ---------------- drawing ----------------
    private fun drawLabel(c: Canvas, text: String, r: RectF, bg: Int, size: Float) {
        paint.style = Paint.Style.FILL
        paint.color = bg; paint.alpha = (opacity * 255).toInt().coerceAtLeast(110)
        c.drawRoundRect(r, r.height() / 2, r.height() / 2, paint)
        paint.color = 0xFFEEF2F0.toInt(); paint.alpha = 255
        paint.textSize = size; paint.textAlign = Paint.Align.CENTER; paint.isFakeBoldText = true
        c.drawText(tr(text), r.centerX(), r.centerY() + size * 0.35f, paint)
        paint.isFakeBoldText = false
        paint.color = 0xFFFFFFFF.toInt()
    }

    override fun onDraw(c: Canvas) {
        paint.alpha = (opacity * 255).toInt()
        if (editMode) {
            paint.color = 0x99000000.toInt(); c.drawRect(0f, 0f, width.toFloat(), height.toFloat(), paint)
            paint.color = 0xFFFFFFFF.toInt(); paint.alpha = (opacity * 255).toInt()
        }
        if (ctx == 1) {
            drawLabel(c, "SALTAR  ▶▶", skipRect, if (skipPointer >= 0) 0xFFB02020.toInt() else 0xFF2A2E30.toInt(), 20 * dp)
            return
        }
        val jb = joyBase; val jk = joyKnob
        if (ctx == 3 && jb != null) c.drawBitmap(jb, null, joyRect, paint)
        if (ctx == 3 && jk != null) {
            val k = joyRect.width() * 0.42f
            val cx = joyRect.centerX() + joyDx * joyRect.width() * 0.3f; val cy = joyRect.centerY() + joyDy * joyRect.width() * 0.3f
            c.drawBitmap(jk, null, RectF(cx - k / 2, cy - k / 2, cx + k / 2, cy + k / 2), paint)
        }
        for (b in buttons) {
            if (!visible(b)) continue
            val img = if (b.pointer >= 0) b.pressed else b.normal
            if (img != null) c.drawBitmap(img, null, rectOf(b), paint)
            else drawLabel(c, b.label, rectOf(b), 0xFF30383D.toInt(), 12 * dp)
            if (editMode && b === editBtn) {
                paint.style = Paint.Style.STROKE; paint.strokeWidth = 3 * dp; paint.color = 0xFFFFD040.toInt()
                c.drawCircle(rectOf(b).centerX(), rectOf(b).centerY(), rectOf(b).width() / 2 + 4 * dp, paint)
                paint.style = Paint.Style.FILL; paint.color = 0xFFFFFFFF.toInt(); paint.alpha = (opacity * 255).toInt()
            }
        }
        if (editMode) { drawLabel(c, "LISTO", doneRect, 0xFF2E7D32.toInt(), 20 * dp); return }
        if (ctx == 3) {
            drawLabel(c, "⋯ EQUIPO", moreRect, 0xFF202426.toInt(), 14 * dp)
            if (joyRunning) drawLabel(c, "CORRER", RectF(joyRect.left, joyRect.top - 28 * dp, joyRect.right, joyRect.top), 0xFF8C2525.toInt(), 12 * dp)
            else if (joyPointer >= 0 && runWithJoystick) drawLabel(c, "ESTIRA PARA CORRER", RectF(joyRect.left - 10 * dp, joyRect.top - 28 * dp, joyRect.right + 10 * dp, joyRect.top), 0xFF202426.toInt(), 10 * dp)
        } else if (ctx == 2) drawLabel(c, "⚙", gearRect, 0xFF202426.toInt(), 22 * dp)
        if (panelOpen) drawPanel(c)
    }

    private fun drawPanel(c: Canvas) {
        paint.color = 0xB0000000.toInt(); paint.alpha = 255
        c.drawRect(0f, 0f, width.toFloat(), height.toFloat(), paint)
        paint.color = 0xFF181D20.toInt()
        c.drawRoundRect(panelRect, 16 * dp, 16 * dp, paint)
        val title = RectF(panelRect.left, panelRect.top, panelRect.right, panelRect.top + 48 * dp)
        drawLabel(c, "CONTROLES · tocar fuera para cerrar", title, 0xFF181D20.toInt(), 13 * dp)
        tabRects.forEachIndexed { i, r ->
            drawLabel(c, TouchHudPolicy.tabs[i], r, if (i == panelTab) 0xFF8C2525.toInt() else 0xFF30383D.toInt(), 14 * dp)
        }
        for (b in panelButtons) {
            val r = b.menuRect
            paint.color = if (b.pointer >= 0) 0xFF8C2525.toInt() else 0xFF30383D.toInt(); paint.alpha = 255
            c.drawRoundRect(r, 8 * dp, 8 * dp, paint)
            val iconSize = minOf(36 * dp, r.height() * 0.53f)
            (if (b.pointer >= 0) b.pressed else b.normal)?.let {
                c.drawBitmap(it, null, RectF(r.centerX() - iconSize / 2, r.top + 2 * dp,
                    r.centerX() + iconSize / 2, r.top + 2 * dp + iconSize), paint)
            }
            paint.color = 0xFFF0F2F3.toInt(); paint.textAlign = Paint.Align.CENTER
            paint.textSize = 12 * dp
            while (paint.measureText(tr(b.label)) > r.width() - 6 * dp && paint.textSize > 8 * dp) paint.textSize -= dp
            c.drawText(tr(b.label), r.centerX(), r.bottom - 8 * dp, paint)
        }
        drawLabel(c, "⚙ Ajustes", settingsRect, 0xFF30383D.toInt(), 14 * dp)
        drawLabel(c, "${panelPage + 1}/$panelPages  ›", pageRect, 0xFF30383D.toInt(), 14 * dp)
    }

    private fun setPanel(open: Boolean) {
        releaseAll()
        panelOpen = open
        layout(); invalidate()
    }

    // ---------------- input helpers ----------------
    private fun press(b: Btn, down: Boolean) {
        for (k in b.keys) NativeBridge.inputKey(k, down || (k == LSHIFT && joyRunning))
        if (b.mouse >= 0) NativeBridge.inputMouseButton(b.mouse, down)
        if (down) when (b.kind) { Kind.WHEEL_UP -> NativeBridge.inputWheel(120); Kind.WHEEL_DOWN -> NativeBridge.inputWheel(-120); else -> {} }
    }

    private val pressedAt = HashMap<Btn, Long>()
    private fun buttonDown(b: Btn, pid: Int) {
        if (prefs.getBoolean("haptics", false)) performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY)
        pendingReleases.remove(b)?.let { ui.removeCallbacks(it) }
        b.pointer = pid; pressedAt[b] = SystemClock.uptimeMillis(); press(b, true)
        // Held actions must expose the world/inventory while the finger remains down.
        if (panelOpen && b.kind == Kind.HOLD) panelOpen = false
        invalidate()
    }
    private val pendingReleases = HashMap<Btn, Runnable>()
    private fun buttonUp(b: Btn) {
        val wait = MIN_PRESS_MS - (SystemClock.uptimeMillis() - (pressedAt[b] ?: 0L))
        b.pointer = -1
        if (wait > 0) {
            val release = Runnable { pendingReleases.remove(b); if (b.pointer < 0) press(b, false) }
            pendingReleases[b] = release; ui.postDelayed(release, wait)
        } else press(b, false)
        if (panelOpen && b.kind == Kind.TAP) panelOpen = false
        invalidate()
    }

    /** SKIP: Esc and a click. Logos and movies listen to the keyboard buffer; the click is for scenes that wait for the mouse. */
    private fun skip(down: Boolean) {
        NativeBridge.inputKey(ESC, down)
        NativeBridge.inputMouseButton(0, down)
    }

    private fun setJoy(idx: Int, down: Boolean) {
        if (joyKeysDown[idx] == down) return
        joyKeysDown[idx] = down
        NativeBridge.inputKey(intArrayOf(W, A, S, D)[idx], down)
    }
    private fun updateJoy(x: Float, y: Float) {
        val r = joyRect.width() / 2
        var dx = (x - joyRect.centerX()) / r; var dy = (y - joyRect.centerY()) / r
        val running = TouchHudPolicy.running(dx, dy, joyRunning, runWithJoystick)
        if (running != joyRunning) { joyRunning = running; updateRunKey() }
        val len = hypot(dx, dy); if (len > 1f) { dx /= len; dy /= len }
        joyDx = dx; joyDy = dy
        val dead = 0.28f
        setJoy(0, dy < -dead); setJoy(2, dy > dead); setJoy(1, dx < -dead); setJoy(3, dx > dead)
        invalidate()
    }
    private fun updateRunKey() {
        NativeBridge.inputKey(LSHIFT, joyRunning || buttons.any { it.name == "run" && it.pointer >= 0 })
    }
    private fun releaseJoy() { joyRunning = false; updateRunKey(); joyPointer = -1; joyDx = 0f; joyDy = 0f; for (i in 0..3) setJoy(i, false); invalidate() }

    private fun toGame(x: Float, y: Float): Pair<Int, Int> {
        return GamePointer.point(x, y, gameRect.left, gameRect.top, gameRect.width(), gameRect.height(), gameW, gameH)
    }

    /**
     * Menu cursor, closed loop. The menus read the mouse as RELATIVE DirectInput motion and the game applies its own
     * (non-linear) acceleration, so a position cannot be reached by computing how much to move. Instead the renderer
     * reports where the cursor sprite was drawn in the last frame (NativeBridge.menuCursorPos) and the cursor is
     * nudged toward the target, one small step per ~50 ms, until it is within a few pixels (or after 40 tries).
     */
    private var seekToken = 0
    private var lastSeekAt = 0L
    private fun seekCursor(gx: Int, gy: Int, thenClick: Boolean) {
        val token = ++seekToken
        lastSeekAt = SystemClock.uptimeMillis()
        var tries = 0
        val step = object : Runnable {
            override fun run() {
                if (token != seekToken) return
                val packed = NativeBridge.menuCursorPos()
                if (packed == -1) { if (thenClick) menuClick(); return }          // the cursor cannot be seen: click where it is
                val ex = gx - (packed ushr 16); val ey = gy - (packed and 0xFFFF)
                if ((abs(ex) <= MENU_SEEK_TOLERANCE && abs(ey) <= MENU_SEEK_TOLERANCE) || tries++ >= 40) { if (thenClick) menuClick(); return }
                fun units(e: Int): Int { val v = (e / 1.6f).toInt().coerceIn(-40, 40); return if (v == 0 && e != 0) Integer.signum(e) else v }
                NativeBridge.inputMouseMove(units(ex), units(ey))
                ui.postDelayed(this, MENU_SEEK_MS)
            }
        }
        ui.post(step)
    }
    private fun menuClick() {
        ui.postDelayed({ NativeBridge.inputMouseButton(0, true) }, MENU_CLICK_DELAY)
        ui.postDelayed({ NativeBridge.inputMouseButton(0, false) }, MENU_CLICK_DELAY + MIN_PRESS_MS + 20)
    }

    /** The original menu calls GetCursorPos followed by ScreenToClient (guest 0041707B / 00417098). */
    private fun menuCursor(x: Float, y: Float) {
        val (gx, gy) = toGame(x, y)
        if (SystemClock.uptimeMillis() - lastSeekAt > 120) seekCursor(gx, gy, false)    // hover while the finger moves
    }

    override fun onHoverEvent(event: MotionEvent): Boolean {
        if (NativeBridge.gameLoadError == null && ctx == 2 && event.isFromSource(InputDevice.SOURCE_MOUSE) &&
            gameRect.contains(event.x, event.y)) {
            menuCursor(event.x, event.y)
            return true
        }
        return super.onHoverEvent(event)
    }

    /** Releases every held input (cancelled touch, activity paused). */
    fun releaseAll() {
        // Cancel delayed taps/clicks as well: no stale event may fire after pause or a context change.
        ui.removeCallbacksAndMessages(null)
        if (isAttachedToWindow) ui.postDelayed(poll, 200)
        pressedAt.clear()
        pendingReleases.clear()
        menuPointer = -1; menuRelease = null; seekToken++
        editPointer = -1; editBtn = null
        cameraMotion.reset()
        if (NativeBridge.gameLoadError != null) return
        releaseJoy()
        for (b in buttons) if (b.pointer >= 0) { b.pointer = -1; press(b, false) }
        if (skipPointer >= 0) { skipPointer = -1; skip(false) }
        lookPointer = -1; gearPointer = -1; donePointer = -1
        NativeBridge.inputReset()
        invalidate()
    }

    private fun buttonAt(x: Float, y: Float): Btn? = buttons.lastOrNull { visible(it) && it.pointer < 0 && rectOf(it).contains(x, y) }

    // ---------------- settings dialogs ----------------
    private fun openSettings() {
        releaseAll()
        panelOpen = false; invalidate()
        val items = arrayOf("Elegir botones en pantalla…", "Mover botones", "Tamaño…", "Transparencia…",
            if (runWithJoystick) "Correr con joystick: activado" else "Correr con joystick: desactivado",
            "Restaurar diseño compacto", "Ayuda de controles", "Sensibilidad de cámara…")
        AlertDialog.Builder(context).setTitle(tr("Controles táctiles")).setItems(items.map { tr(it) }.toTypedArray()) { _, which ->
            when (which) {
                0 -> chooseButtons()
                1 -> { if (ctx == 3) { editMode = true; panelOpen = false; invalidate() } }
                2 -> slider("Tamaño de los botones", (userScale * 100).toInt() - 60, 100) { userScale = (it + 60) / 100f; layout(); saveLayout(); invalidate() }
                3 -> slider("Transparencia (a la derecha, más visibles)", (opacity * 100).toInt() - 20, 80) { opacity = (it + 20) / 100f; saveLayout(); invalidate() }
                4 -> { runWithJoystick = !runWithJoystick; saveLayout(); invalidate() }
                5 -> resetLayout()
                6 -> AlertDialog.Builder(context).setTitle(tr("Controles compactos"))
                    .setMessage(tr("Mueve el joystick para caminar. Arrastra hacia delante más allá del borde para correr; vuelve al centro para caminar. Puedes desactivarlo y fijar Correr en pantalla.\n\nArrastra por la zona libre para mirar. Disparar, Acción y Agacharse funcionan mientras mantienes el dedo.\n\nEQUIPO abre todas las acciones por categorías. Las acciones mantenidas, como Inventario, se sueltan al levantar el dedo. Toca fuera para cerrar. Pausa abre el menú del juego.\n\nLos ajustes permiten fijar acciones, moverlas y cambiar tamaño y transparencia."))
                    .setPositiveButton(tr("Entendido"), null).show()
                7 -> slider("Sensibilidad de cámara", (cameraSensitivity * 100).toInt() - 50, 750,
                    formatter = { String.format(Locale.ROOT, "%.1f×", (it + 50) / 100f) }) {
                    cameraSensitivity = (it + 50) / 100f
                    cameraMotion.reset(); saveLayout()
                }
            }
        }.setNegativeButton(tr("Cerrar"), null).show()
    }

    private fun chooseButtons() {
        val list = buttons.filter { !it.menu && it.name != "pause" }
        val labels = list.map { tr(it.label) }.toTypedArray()
        val checked = BooleanArray(list.size) { list[it].onScreen }
        AlertDialog.Builder(context).setTitle(tr("Botones en pantalla (los demás van al panel «⋯»)"))
            .setMultiChoiceItems(labels, checked) { _, i, on -> checked[i] = on }
            .setPositiveButton("Aceptar") { _, _ ->
                var added = 0
                list.forEachIndexed { i, b ->
                    if (checked[i] && !b.onScreen) {
                        // Give newly pinned controls separate starting slots, then allow dragging.
                        b.x = 0.40f + (added % 5) * 0.11f
                        b.y = 0.28f + (added / 5) * 0.14f
                        added++
                    }
                    b.onScreen = checked[i]
                }
                if (added > 0) { editMode = ctx == 3; panelOpen = false }
                saveLayout(); layout(); invalidate()
            }.setNegativeButton(tr("Cancelar"), null).show()
    }

    private fun slider(title: String, value: Int, max: Int, formatter: ((Int) -> String)? = null, onChange: (Int) -> Unit) {
        val bar = SeekBar(context); bar.max = max; bar.progress = value.coerceIn(0, max)
        val pad = (20 * dp).toInt()
        val box = LinearLayout(context); box.orientation = LinearLayout.VERTICAL; box.setPadding(pad, pad, pad, pad)
        val readout = TextView(context)
        if (formatter != null) { readout.text = formatter(bar.progress); box.addView(readout) }
        box.addView(bar, LinearLayout.LayoutParams(-1, -2))
        bar.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(s: SeekBar?, p: Int, u: Boolean) {
                if (formatter != null) readout.text = formatter(p)
                onChange(p)
            }
            override fun onStartTrackingTouch(s: SeekBar?) {}
            override fun onStopTrackingTouch(s: SeekBar?) {}
        })
        AlertDialog.Builder(context).setTitle(tr(title)).setView(box).setPositiveButton(tr("Listo"), null).show()
    }

    // ---------------- touch ----------------
    override fun onTouchEvent(e: MotionEvent): Boolean {
        if (NativeBridge.gameLoadError != null) return false
        when (e.actionMasked) {
            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                val i = e.actionIndex; val id = e.getPointerId(i); val x = e.getX(i); val y = e.getY(i)
                if (editMode) {                                              // drag buttons; LISTO ends
                    if (doneRect.contains(x, y)) { donePointer = id; return true }
                    val b = buttons.lastOrNull { !it.menu && it.onScreen && it.rect.contains(x, y) }
                    if (b != null) { editBtn = b; editPointer = id; invalidate() }
                    return true
                }
                if (ctx == 1) {
                    if (skipRect.contains(x, y)) { skipPointer = id; skip(true); invalidate() }
                    return true
                }
                if (ctx == 3 && panelOpen) {
                    if (!panelRect.contains(x, y)) { setPanel(false); return true }
                    val tab = tabRects.indexOfFirst { it.contains(x, y) }
                    if (tab >= 0) { releaseAll(); panelTab = tab; panelPage = 0; layout(); invalidate(); return true }
                    if (settingsRect.contains(x, y)) { openSettings(); return true }
                    if (pageRect.contains(x, y)) { releaseAll(); panelPage = (panelPage + 1) % panelPages; layout(); invalidate(); return true }
                    panelButtons.lastOrNull { it.pointer < 0 && it.menuRect.contains(x, y) }?.let { buttonDown(it, id) }
                    return true
                }
                if (ctx == 2 && gearRect.contains(x, y)) { gearPointer = id; return true }
                if (ctx == 3 && moreRect.contains(x, y)) { setPanel(true); return true }
                val b = buttonAt(x, y)
                if (b != null) { buttonDown(b, id); return true }
                if (ctx == 2) {
                    if (menuPointer < 0 && gameRect.contains(x, y)) {
                        menuRelease?.let { ui.removeCallbacks(it) }; menuRelease = null
                        menuPointer = id; menuPressedAt = SystemClock.uptimeMillis()
                        val (gx, gy) = toGame(x, y); seekCursor(gx, gy, false)
                    }
                    return true
                }
                if (ctx == 3 && joyPointer < 0 && joyRect.contains(x, y)) { joyPointer = id; updateJoy(x, y); return true }
                if (lookPointer < 0) {
                    lookPointer = id; lookX = x; lookY = y
                    cameraMotion.reset()
                }
            }
            MotionEvent.ACTION_MOVE -> {
                for (i in 0 until e.pointerCount) {
                    val id = e.getPointerId(i); val x = e.getX(i); val y = e.getY(i)
                    if (editMode && id == editPointer) {
                        editBtn?.let { it.x = (x / width).coerceIn(0.03f, 0.97f); it.y = (y / height).coerceIn(0.05f, 0.95f); layout(); invalidate() }
                    } else if (id == menuPointer) menuCursor(x, y)
                    else if (id == joyPointer) updateJoy(x, y)
                    else if (id == lookPointer) {
                        val dx = x - lookX; val dy = y - lookY
                        lookX = x; lookY = y
                        if (ctx == 3) {
                            val gain = gameW / gameRect.width().coerceAtLeast(1f) * 1.6f * cameraSensitivity
                            val (mx, my) = cameraMotion.consume(dx, dy, gain)
                            if (mx != 0 || my != 0) NativeBridge.inputMouseMove(mx, my)
                        }
                    }
                }
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP -> {
                val i = e.actionIndex; val id = e.getPointerId(i)
                if (id == donePointer) { donePointer = -1; editMode = false; editBtn = null; saveLayout(); invalidate() }
                if (id == editPointer) { editPointer = -1; saveLayout() }
                if (id == skipPointer) { skipPointer = -1; skip(false); invalidate() }
                if (id == gearPointer) { gearPointer = -1; openSettings() }
                buttons.firstOrNull { it.pointer == id }?.let { buttonUp(it) }
                if (id == menuPointer) {
                    menuPointer = -1
                    val (gx, gy) = toGame(e.getX(i), e.getY(i))
                    seekCursor(gx, gy, true)                                       // go to the final position, then click
                }
                if (id == joyPointer) releaseJoy()
                if (id == lookPointer) {
                    lookPointer = -1
                }
            }
            MotionEvent.ACTION_CANCEL -> releaseAll()
        }
        return true
    }
}
