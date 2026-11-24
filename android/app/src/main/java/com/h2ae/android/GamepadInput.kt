package com.h2ae.android

import android.os.Handler
import android.os.Looper
import android.view.InputDevice
import android.view.KeyEvent
import android.view.MotionEvent
import kotlin.math.abs

/**
 * Android gamepads -> the game's inputs (DirectInput scan codes and mouse, through NativeBridge.input*).
 * Same targets as the touch controls: the bindings of the install's keyboard.cfg.
 *
 *   mission : left stick = W A S D, right stick = camera, R2 / R1 = fire, L2 = run, A = action (hold), B = crouch,
 *             X = reload, Y = inventory (hold), L1 = sneak, Start = pause, Select = map, thumb L = toggle run,
 *             thumb R = camera view, D-pad up/down = zoom, left/right = lean
 *   menu    : D-pad or left stick = arrows, A = accept, B = back (Esc), Start = Esc
 *   intro   : A / Start = skip
 *
 * Only what Android reports as a gamepad / joystick source is handled. Layout follows Android's standard
 * mapping (Xbox / PlayStation / Switch style pads); other pads that report differently may need a mapping
 * that is not implemented. Verified only by reading the code and compiling: no pad was connected when written.
 */
internal class GamepadInput {
    private val ui = Handler(Looper.getMainLooper())
    private val down = HashSet<Int>()                 // key codes currently held (to release on pause / context change)
    private var lx = 0f; private var ly = 0f; private var rx = 0f; private var ry = 0f
    private var lt = 0f; private var rt = 0f; private var hatX = 0f; private var hatY = 0f
    private val moveKeys = BooleanArray(4)            // W A S D / arrows in menus
    private var firing = false; private var running = false
    private var ticking = false
    var cameraSensitivity = 1.0f
    /** Mouse units per tick at full right-stick deflection (one tick = 16 ms). */
    private val cameraSpeed = 18f

    private companion object {
        const val W = 0x11; const val A = 0x1E; const val S = 0x1F; const val D = 0x20
        const val ESC = 0x01; const val ENTER = 0x1C; const val LCTRL = 0x1D; const val LSHIFT = 0x2A; const val SPACE = 0x39
        const val CAPS = 0x3A; const val F1 = 0x3B; const val Q = 0x10; const val E = 0x12; const val M = 0x32
        const val UP = 0xC8; const val LEFT = 0xCB; const val RIGHT = 0xCD; const val DOWN = 0xD0
        const val NUM1 = 0x4F; const val NUM3 = 0x51
        const val DEAD = 0.28f
    }

    fun isGamepad(source: Int) = (source and InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD ||
        (source and InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK

    private fun ctx() = if (NativeBridge.gameLoadError == null) NativeBridge.gameContext() else 0

    private fun key(dik: Int, on: Boolean) = NativeBridge.inputKey(dik, on)

    /** Returns true if the event was a gamepad button and has been consumed. */
    fun onKey(e: KeyEvent): Boolean {
        if (!isGamepad(e.source)) return false
        val on = e.action == KeyEvent.ACTION_DOWN
        if (e.action != KeyEvent.ACTION_DOWN && e.action != KeyEvent.ACTION_UP) return false
        if (on && e.repeatCount > 0) return true                       // holds are tracked by down/up, not by key repeat
        val c = ctx()
        if (on) down.add(e.keyCode) else down.remove(e.keyCode)
        when (c) {
            1 -> when (e.keyCode) {                                      // intro: A or Start skips (Esc + click, as the SKIP button)
                KeyEvent.KEYCODE_BUTTON_A, KeyEvent.KEYCODE_BUTTON_START -> { key(ESC, on); NativeBridge.inputMouseButton(0, on) }
                else -> return false
            }
            2 -> when (e.keyCode) {
                KeyEvent.KEYCODE_BUTTON_A -> { key(ENTER, on); key(E, on) }
                KeyEvent.KEYCODE_BUTTON_B, KeyEvent.KEYCODE_BUTTON_START -> key(ESC, on)
                KeyEvent.KEYCODE_DPAD_UP -> key(UP, on); KeyEvent.KEYCODE_DPAD_DOWN -> key(DOWN, on)
                KeyEvent.KEYCODE_DPAD_LEFT -> key(LEFT, on); KeyEvent.KEYCODE_DPAD_RIGHT -> key(RIGHT, on)
                else -> return false
            }
            3 -> when (e.keyCode) {
                KeyEvent.KEYCODE_BUTTON_A -> key(E, on)
                KeyEvent.KEYCODE_BUTTON_B -> key(LCTRL, on)
                KeyEvent.KEYCODE_BUTTON_X -> NativeBridge.inputMouseButton(1, on)
                KeyEvent.KEYCODE_BUTTON_Y -> key(Q, on)
                KeyEvent.KEYCODE_BUTTON_L1 -> key(SPACE, on)
                KeyEvent.KEYCODE_BUTTON_R1 -> NativeBridge.inputMouseButton(0, on)
                KeyEvent.KEYCODE_BUTTON_START -> key(ESC, on)
                KeyEvent.KEYCODE_BUTTON_SELECT -> key(M, on)
                KeyEvent.KEYCODE_BUTTON_THUMBL -> key(CAPS, on)
                KeyEvent.KEYCODE_BUTTON_THUMBR -> key(F1, on)
                KeyEvent.KEYCODE_DPAD_UP -> key(UP, on); KeyEvent.KEYCODE_DPAD_DOWN -> key(DOWN, on)
                KeyEvent.KEYCODE_DPAD_LEFT -> key(NUM1, on); KeyEvent.KEYCODE_DPAD_RIGHT -> key(NUM3, on)
                else -> return false
            }
            else -> return false
        }
        return true
    }

    /** Sticks, triggers and a D-pad reported as a hat. Returns true if the event came from a gamepad. */
    fun onMotion(e: MotionEvent): Boolean {
        if (!isGamepad(e.source) || e.action != MotionEvent.ACTION_MOVE) return false
        lx = e.getAxisValue(MotionEvent.AXIS_X); ly = e.getAxisValue(MotionEvent.AXIS_Y)
        rx = e.getAxisValue(MotionEvent.AXIS_Z); ry = e.getAxisValue(MotionEvent.AXIS_RZ)
        if (rx == 0f && ry == 0f) { rx = e.getAxisValue(MotionEvent.AXIS_RX); ry = e.getAxisValue(MotionEvent.AXIS_RY) }
        lt = maxOf(e.getAxisValue(MotionEvent.AXIS_LTRIGGER), e.getAxisValue(MotionEvent.AXIS_BRAKE))
        rt = maxOf(e.getAxisValue(MotionEvent.AXIS_RTRIGGER), e.getAxisValue(MotionEvent.AXIS_GAS))
        hatX = e.getAxisValue(MotionEvent.AXIS_HAT_X); hatY = e.getAxisValue(MotionEvent.AXIS_HAT_Y)
        apply()
        return true
    }

    private fun apply() {
        val c = ctx()
        // Left stick / D-pad hat: movement in a mission, arrows in a menu.
        val wantMove = booleanArrayOf(ly < -DEAD || (c == 2 && hatY < -0.5f), lx < -DEAD || (c == 2 && hatX < -0.5f),
                                      ly > DEAD || (c == 2 && hatY > 0.5f), lx > DEAD || (c == 2 && hatX > 0.5f))
        val codes = if (c == 2) intArrayOf(UP, LEFT, DOWN, RIGHT) else intArrayOf(W, A, S, D)
        if (c == 2 || c == 3) for (i in 0..3) if (wantMove[i] != moveKeys[i]) { moveKeys[i] = wantMove[i]; key(codes[i], wantMove[i]) }
        if (c == 3) {                                                       // triggers: fire and run
            val f = rt > 0.5f; if (f != firing) { firing = f; NativeBridge.inputMouseButton(0, f) }
            val r = lt > 0.5f; if (r != running) { running = r; key(LSHIFT, r) }
        }
        val camera = c == 3 && (abs(rx) > DEAD || abs(ry) > DEAD)
        if (camera && !ticking) { ticking = true; ui.postDelayed(tick, 16) }
    }

    private val tick = object : Runnable {
        override fun run() {
            if (ctx() != 3 || (abs(rx) <= DEAD && abs(ry) <= DEAD)) { ticking = false; return }
            fun curve(v: Float) = if (abs(v) <= DEAD) 0f else (v - DEAD * Math.signum(v)) / (1f - DEAD) * abs(v)
            NativeBridge.inputMouseMove((curve(rx) * cameraSpeed * cameraSensitivity).toInt(), (curve(ry) * cameraSpeed * cameraSensitivity).toInt())
            ui.postDelayed(this, 16)
        }
    }

    /** Releases everything that is held (pause, context change, a pad disconnecting). */
    fun releaseAll() {
        ui.removeCallbacks(tick); ticking = false
        for (i in 0..3) moveKeys[i] = false
        if (firing) { firing = false }
        if (running) { running = false }
        down.clear()
        lx = 0f; ly = 0f; rx = 0f; ry = 0f; lt = 0f; rt = 0f; hatX = 0f; hatY = 0f
        NativeBridge.inputReset()
    }
}
