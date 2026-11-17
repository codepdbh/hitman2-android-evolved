package com.h2ae.android

import org.junit.Assert.assertEquals
import org.junit.Test

class GamePointerTest {
    @Test fun mapsThePictureRatherThanTheWholePhone() {
        assertEquals(0 to 0, GamePointer.point(210f, 0f, 210f, 0f, 1920f, 1080f))
        assertEquals(400 to 225, GamePointer.point(1170f, 540f, 210f, 0f, 1920f, 1080f))
        assertEquals(799 to 449, GamePointer.point(2130f, 1080f, 210f, 0f, 1920f, 1080f))
    }
    @Test fun clampsDragPositionsOutsideThePicture() {
        assertEquals(0 to 449, GamePointer.point(-100f, 2000f, 210f, 0f, 1920f, 1080f))
    }
}
