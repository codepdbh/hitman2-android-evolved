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
import android.view.MotionEvent
import android.view.View
import android.widget.LinearLayout
import android.widget.SeekBar
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
 *   mission                : a short default set, a "..." panel with the rest, the settings gear
 * The default screen set is deliberately small. The gear opens the layout settings: which buttons stay on
 * screen (the others go to the "..." panel), moving them by dragging, size, opacity, restore defaults. The
 * layout is saved in SharedPreferences.
 *
 * Any touch on empty screen drags the camera (relative mouse); in menus a tap moves the game's cursor there and
 * clicks. Cancelled touches and a paused activity release everything held ([releaseAll]).
 *
 * Layout and feel have only been checked on one phone; see docs/STATUS.md.
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
        const val GAME_W = 800; const val GAME_H = 450
        const val MOUSE_SPEED = 1.356307f     // Hitman2.ini MouseSpeed
    }

    private val buttons = listOf(
        // on screen by default (few)
        Btn("fire", "Disparar", Kind.HOLD, mouse = 0, main = true, fx = 0.90f, fy = 0.74f, size = 100f),
        Btn("action", "Acción", Kind.HOLD, intArrayOf(E), main = true, fx = 0.77f, fy = 0.87f, size = 72f),
        Btn("crouch", "Agacharse", Kind.HOLD, intArrayOf(LCTRL), main = true, fx = 0.66f, fy = 0.87f, size = 58f),
        Btn("run", "Correr", Kind.HOLD, intArrayOf(LSHIFT), main = true, fx = 0.78f, fy = 0.64f, size = 58f),
        Btn("inventory", "Inventario", Kind.HOLD, intArrayOf(Q), main = true, fx = 0.90f, fy = 0.46f, size = 54f),
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
        Btn("accept", "Aceptar", Kind.TAP, intArrayOf(ENTER), menu = true),
    )
    // "holster" exists as an asset but has no binding in this install's keyboard.cfg, so it is not offered.

    private val skipRect = RectF()
    private val gearRect = RectF()
    private val moreRect = RectF()
    private val doneRect = RectF()
    private var panelOpen = false
    private var panelEnabled = true
    private var editMode = false
    private var editPointer = -1
    private var editBtn: Btn? = null
    private var userScale = 1.0f
    var opacity = 0.70f

    private var joyBase: Bitmap? = null
    private var joyKnob: Bitmap? = null
    private val joyRect = RectF()
    private var joyPointer = -1
    private var joyDx = 0f; private var joyDy = 0f
    private val joyKeysDown = booleanArrayOf(false, false, false, false)       // W A S D
    private var lookPointer = -1
    private var lookX = 0f; private var lookY = 0f; private var lookDownT = 0L; private var lookMoved = 0f
    private var skipPointer = -1; private var gearPointer = -1; private var morePointer = -1; private var donePointer = -1
    /** The game's picture rectangle on screen, for mapping touches to game pixels. */
    var gameRect = RectF(0f, 0f, 1f, 1f)

    /** 0 starting, 1 intro / logos, 2 menu, 3 mission (NativeBridge.gameContext). */
    private var ctx = 0
    private val poll = object : Runnable {
        override fun run() {
            val now = if (NativeBridge.gameLoadError == null) NativeBridge.gameContext() else 0
            if (now != ctx) { releaseAll(); ctx = now; panelOpen = false; editMode = false; layout(); invalidate() }
            ui.postDelayed(this, 200)
        }
    }
    override fun onAttachedToWindow() { super.onAttachedToWindow(); ui.post(poll) }
    override fun onDetachedFromWindow() { ui.removeCallbacks(poll); super.onDetachedFromWindow() }

    init { loadAssets(); loadLayout() }

    // ---------------- assets and saved layout ----------------
    private fun bmp(name: String): Bitmap? = try { context.assets.open("touch_hud/$name.png").use { BitmapFactory.decodeStream(it) } } catch (e: Exception) { null }

    private fun loadAssets() {
        for (b in buttons) { b.normal = bmp("${b.name}_normal"); b.pressed = bmp("${b.name}_pressed") }
        joyBase = bmp("joystick_base"); joyKnob = bmp("joystick_knob")
    }

    private fun loadLayout() {
        opacity = prefs.getFloat("opacity", 0.70f); userScale = prefs.getFloat("scale", 1.0f); panelEnabled = prefs.getBoolean("panel", true)
        for (b in buttons) {
            if (b.menu) continue
            b.onScreen = prefs.getBoolean("on_${b.name}", b.main)
            b.x = prefs.getFloat("x_${b.name}", b.fx); b.y = prefs.getFloat("y_${b.name}", b.fy)
        }
    }

    private fun saveLayout() {
        val e = prefs.edit()
        e.putFloat("opacity", opacity).putFloat("scale", userScale).putBoolean("panel", panelEnabled)
        for (b in buttons) if (!b.menu) e.putBoolean("on_${b.name}", b.onScreen).putFloat("x_${b.name}", b.x).putFloat("y_${b.name}", b.y)
        e.apply()
    }

    private fun resetLayout() {
        opacity = 0.70f; userScale = 1.0f; panelEnabled = true
        for (b in buttons) { b.onScreen = b.main; b.x = b.fx; b.y = b.fy }
        saveLayout(); layout(); invalidate()
    }

    // ---------------- layout ----------------
    private fun scaled(v: Float) = v * dp * userScale

    private fun layout() {
        val w = width.toFloat(); val h = height.toFloat()
        if (w <= 0f || h <= 0f) return
        val m = 20 * dp
        val js = scaled(150f)
        joyRect.set(m + 16 * dp, h - js - m, m + 16 * dp + js, h - m)
        for (b in buttons) {
            if (b.menu) continue
            val sz = scaled(b.size)
            b.rect.set(b.x * w - sz / 2, b.y * h - sz / 2, b.x * w + sz / 2, b.y * h + sz / 2)
        }
        val gs = 44 * dp
        gearRect.set(m, m, m + gs, m + gs)
        moreRect.set(w - m - 46 * dp, h * 0.10f + 40 * dp, w - m, h * 0.10f + 40 * dp + 46 * dp)
        skipRect.set(w - m - 190 * dp, h - m - 64 * dp, w - m, h - m)
        doneRect.set(w / 2 - 80 * dp, m, w / 2 + 80 * dp, m + 48 * dp)
        // menu layout: bottom-right d-pad + accept
        val u = 72 * dp; val cx = w - m - 3.2f * u; val cy = h - m - 1.6f * u
        fun at(n: String, x: Float, y: Float, size: Float) { buttons.first { it.name == n }.menuRect.set(x - size / 2, y - size / 2, x + size / 2, y + size / 2) }
        at("nav_up", cx, cy - u, 64 * dp); at("nav_down", cx, cy + u, 64 * dp); at("nav_left", cx - u, cy, 64 * dp); at("nav_right", cx + u, cy, 64 * dp)
        at("accept", w - m - 1.0f * u, h - m - 1.1f * u, 96 * dp)
        // panel: cells for every button that is not on screen
        val cell = 58 * dp; val cols = 6
        val inPanel = buttons.filter { !it.menu && !it.onScreen }
        val rows = ((inPanel.size + cols - 1) / cols).coerceAtLeast(1)
        val x0 = (w - cols * cell) / 2; val y0 = (h - rows * cell) / 2
        inPanel.forEachIndexed { i, b ->
            val px = x0 + (i % cols) * cell; val py = y0 + (i / cols) * cell
            b.menuRect.set(px + 4 * dp, py + 4 * dp, px + cell - 4 * dp, py + cell - 4 * dp)
        }
    }

    override fun onSizeChanged(w: Int, h: Int, ow: Int, oh: Int) { layout() }

    private fun inPanel(b: Btn) = !b.menu && !b.onScreen
    private fun visible(b: Btn): Boolean = when (ctx) {
        3 -> when {
            b.menu -> false
            b.name == "pause" -> true
            b.onScreen -> true
            else -> panelOpen && panelEnabled
        }
        2 -> b.menu || b.name == "pause"
        else -> false
    }
    /** Where a button is drawn and hit-tested: its panel cell, its menu position, or its own place. */
    private fun rectOf(b: Btn): RectF = when {
        ctx == 2 && b.menu -> b.menuRect
        inPanel(b) -> b.menuRect
        else -> b.rect
    }

    // ---------------- drawing ----------------
    private fun drawLabel(c: Canvas, text: String, r: RectF, bg: Int, size: Float) {
        paint.style = Paint.Style.FILL
        paint.color = bg; paint.alpha = (opacity * 255).toInt().coerceAtLeast(110)
        c.drawRoundRect(r, r.height() / 2, r.height() / 2, paint)
        paint.color = 0xFFEEF2F0.toInt(); paint.alpha = 255
        paint.textSize = size; paint.textAlign = Paint.Align.CENTER; paint.isFakeBoldText = true
        c.drawText(text, r.centerX(), r.centerY() + size * 0.35f, paint)
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
            val img = (if (b.pointer >= 0) b.pressed else b.normal) ?: continue
            c.drawBitmap(img, null, rectOf(b), paint)
            if (editMode && b === editBtn) {
                paint.style = Paint.Style.STROKE; paint.strokeWidth = 3 * dp; paint.color = 0xFFFFD040.toInt()
                c.drawCircle(rectOf(b).centerX(), rectOf(b).centerY(), rectOf(b).width() / 2 + 4 * dp, paint)
                paint.style = Paint.Style.FILL; paint.color = 0xFFFFFFFF.toInt(); paint.alpha = (opacity * 255).toInt()
            }
        }
        if (editMode) { drawLabel(c, "LISTO", doneRect, 0xFF2E7D32.toInt(), 20 * dp); return }
        if (ctx == 3 && panelEnabled) drawLabel(c, "⋯", moreRect, if (panelOpen) 0xFFB02020.toInt() else 0xFF202426.toInt(), 22 * dp)
        drawLabel(c, "⚙", gearRect, 0xFF202426.toInt(), 22 * dp)
    }

    // ---------------- input helpers ----------------
    private fun press(b: Btn, down: Boolean) {
        for (k in b.keys) NativeBridge.inputKey(k, down)
        if (b.mouse >= 0) NativeBridge.inputMouseButton(b.mouse, down)
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
     * The game's menus use DirectInput relative mouse motion only, so a tap must move the game's own cursor: push it
     * into the top-left corner, wait for that motion to be consumed, move by (target / MouseSpeed), then click.
     * The game's cursor model (position += delta * speed, clamped) is an assumption not yet confirmed on a menu.
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
        if (skipPointer >= 0) { skipPointer = -1; skip(false) }
        lookPointer = -1; gearPointer = -1; morePointer = -1; donePointer = -1
        NativeBridge.inputReset()
        invalidate()
    }

    private fun buttonAt(x: Float, y: Float): Btn? = buttons.lastOrNull { visible(it) && it.pointer < 0 && rectOf(it).contains(x, y) }

    // ---------------- settings dialogs ----------------
    private fun openSettings() {
        releaseAll()
        val items = arrayOf("Elegir botones en pantalla…", "Mover botones", "Tamaño…", "Transparencia…",
            if (panelEnabled) "Ocultar el panel «⋯»" else "Mostrar el panel «⋯»", "Restaurar valores por defecto")
        AlertDialog.Builder(context).setTitle("Controles táctiles").setItems(items) { _, which ->
            when (which) {
                0 -> chooseButtons()
                1 -> { if (ctx == 3) { editMode = true; panelOpen = false; invalidate() } }
                2 -> slider("Tamaño de los botones", (userScale * 100).toInt() - 60, 100) { userScale = (it + 60) / 100f; layout(); saveLayout(); invalidate() }
                3 -> slider("Transparencia (a la derecha, más visibles)", (opacity * 100).toInt() - 20, 80) { opacity = (it + 20) / 100f; saveLayout(); invalidate() }
                4 -> { panelEnabled = !panelEnabled; panelOpen = false; saveLayout(); invalidate() }
                5 -> resetLayout()
            }
        }.setNegativeButton("Cerrar", null).show()
    }

    private fun chooseButtons() {
        val list = buttons.filter { !it.menu && it.name != "pause" }
        val labels = list.map { it.label }.toTypedArray()
        val checked = BooleanArray(list.size) { list[it].onScreen }
        AlertDialog.Builder(context).setTitle("Botones en pantalla (los demás van al panel «⋯»)")
            .setMultiChoiceItems(labels, checked) { _, i, on -> checked[i] = on }
            .setPositiveButton("Aceptar") { _, _ ->
                list.forEachIndexed { i, b ->
                    if (checked[i] && !b.onScreen) { b.x = 0.5f; b.y = 0.5f }    // newly pinned: starts in the middle, then drag it with "Mover botones"
                    b.onScreen = checked[i]
                }
                saveLayout(); layout(); invalidate()
                if (list.any { it.onScreen && !it.main && it.x == 0.5f && it.y == 0.5f }) { editMode = ctx == 3; panelOpen = false }
            }.setNegativeButton("Cancelar", null).show()
    }

    private fun slider(title: String, value: Int, max: Int, onChange: (Int) -> Unit) {
        val bar = SeekBar(context); bar.max = max; bar.progress = value.coerceIn(0, max)
        val pad = (20 * dp).toInt()
        val box = LinearLayout(context); box.setPadding(pad, pad, pad, pad); box.addView(bar, LinearLayout.LayoutParams(-1, -2))
        bar.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(s: SeekBar?, p: Int, u: Boolean) { onChange(p) }
            override fun onStartTrackingTouch(s: SeekBar?) {}
            override fun onStopTrackingTouch(s: SeekBar?) {}
        })
        AlertDialog.Builder(context).setTitle(title).setView(box).setPositiveButton("Listo", null).show()
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
                if ((ctx == 2 || ctx == 3) && gearRect.contains(x, y)) { gearPointer = id; return true }
                if (ctx == 3 && panelEnabled && moreRect.contains(x, y)) { morePointer = id; panelOpen = !panelOpen; invalidate(); return true }
                val b = buttonAt(x, y)
                if (b != null) { buttonDown(b, id); return true }
                if (ctx == 3 && joyPointer < 0 && joyRect.contains(x, y)) { joyPointer = id; updateJoy(x, y); return true }
                if (lookPointer < 0) {
                    lookPointer = id; lookX = x; lookY = y; lookDownT = System.currentTimeMillis(); lookMoved = 0f
                    if (ctx == 3 && panelOpen) { panelOpen = false; invalidate() }                // a tap outside closes the panel
                }
            }
            MotionEvent.ACTION_MOVE -> {
                for (i in 0 until e.pointerCount) {
                    val id = e.getPointerId(i); val x = e.getX(i); val y = e.getY(i)
                    if (editMode && id == editPointer) {
                        editBtn?.let { it.x = (x / width).coerceIn(0.03f, 0.97f); it.y = (y / height).coerceIn(0.05f, 0.95f); layout(); invalidate() }
                    } else if (id == joyPointer) updateJoy(x, y)
                    else if (id == lookPointer) {
                        val dx = x - lookX; val dy = y - lookY
                        lookX = x; lookY = y; lookMoved += abs(dx) + abs(dy)
                        if (ctx == 3) { val k = GAME_W / gameRect.width(); NativeBridge.inputMouseMove((dx * k * 1.6f).toInt(), (dy * k * 1.6f).toInt()) }
                    }
                }
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP -> {
                val i = e.actionIndex; val id = e.getPointerId(i)
                if (id == donePointer) { donePointer = -1; editMode = false; editBtn = null; saveLayout(); invalidate() }
                if (id == editPointer) { editPointer = -1; saveLayout() }
                if (id == skipPointer) { skipPointer = -1; skip(false); invalidate() }
                if (id == gearPointer) { gearPointer = -1; openSettings() }
                if (id == morePointer) morePointer = -1
                buttons.firstOrNull { it.pointer == id }?.let { buttonUp(it) }
                if (id == joyPointer) releaseJoy()
                if (id == lookPointer) {
                    lookPointer = -1
                    if (System.currentTimeMillis() - lookDownT < 250 && lookMoved < 24 * dp && ctx == 2) {      // a tap in a menu = click there
                        val (gx, gy) = toGame(lookX, lookY); clickGameAt(gx, gy)
                    }
                }
            }
            MotionEvent.ACTION_CANCEL -> releaseAll()
        }
        return true
    }
}
