package com.h2ae.android

import org.junit.Assert.*
import org.junit.Test

class TouchHudPolicyTest {
    @Test fun slowCameraDragsDoNotLoseFractionalMotion() {
        val motion = CameraMotion()
        val result = (0 until 100).map { motion.consume(0.25f, -0.25f, 1f) }
        assertEquals(25, result.sumOf { it.first })
        assertEquals(-25, result.sumOf { it.second })
    }

    @Test fun cameraGainAndSamplingAreConsistent() {
        val fine = CameraMotion()
        val dx = (0 until 120).sumOf { fine.consume(0.5f, 0f, 2f * TouchHudPolicy.DEFAULT_CAMERA_SENSITIVITY).first }
        val coarse = CameraMotion().consume(60f, 0f, 2f * TouchHudPolicy.DEFAULT_CAMERA_SENSITIVITY).first
        assertEquals(coarse, dx)
        assertEquals(360, dx)
    }

    @Test fun aNewCameraGestureDoesNotInheritRemainder() {
        val motion = CameraMotion()
        motion.consume(0.75f, -0.75f, 1f)
        motion.reset()
        assertEquals(0 to 0, motion.consume(0.5f, -0.5f, 1f))
    }

    @Test fun compactDefaultKeepsEssentialActions() {
        assertEquals(setOf("fire", "action", "crouch", "pause"), TouchHudPolicy.compact)
        assertFalse("run" in TouchHudPolicy.compact)
        assertFalse("inventory" in TouchHudPolicy.compact)
    }

    @Test fun migrationPreservesCustomLayouts() {
        assertTrue(TouchHudPolicy.migrateDefault(TouchHudPolicy.legacy, false))
        assertFalse(TouchHudPolicy.migrateDefault(TouchHudPolicy.legacy, true))
        assertFalse(TouchHudPolicy.migrateDefault(TouchHudPolicy.legacy + "map", false))
        assertFalse(TouchHudPolicy.migrateDefault(TouchHudPolicy.legacy - "run", false))
    }

    @Test fun runningRequiresDeliberateForwardOvertravel() {
        assertFalse(TouchHudPolicy.running(0f, -1f, false, true))
        assertTrue(TouchHudPolicy.running(0f, -1.2f, false, true))
        assertTrue(TouchHudPolicy.running(0f, -1f, true, true))
        assertFalse(TouchHudPolicy.running(0f, -0.9f, true, true))
        assertFalse(TouchHudPolicy.running(1.3f, 0f, false, true))
        assertFalse(TouchHudPolicy.running(0f, 1.3f, false, true))
        assertFalse(TouchHudPolicy.running(0f, -1.3f, true, false))
        assertFalse(TouchHudPolicy.running(0f, 0f, true, true))
    }

    @Test fun drawerPaginationKeepsEveryActionReachable() {
        for (w in listOf(280f, 360f, 464f)) for (h in listOf(72f, 144f, 240f)) {
            val grid = TouchHudPolicy.grid(w, h, 23)
            assertTrue(w / grid.columns >= 104f)
            assertTrue(h / grid.rows >= 72f)
            val capacity = grid.columns * grid.rows
            val visited = (0 until grid.pages).flatMap { page ->
                (0 until 23).drop(page * capacity).take(capacity)
            }
            assertEquals((0 until 23).toList(), visited)
        }
    }

    @Test fun categoriesArePredictable() {
        assertEquals(0, TouchHudPolicy.group("inventory"))
        assertEquals(0, TouchHudPolicy.group("pistol"))
        assertEquals(1, TouchHudPolicy.group("reload"))
        assertEquals(1, TouchHudPolicy.group("lean_left"))
        assertEquals(2, TouchHudPolicy.group("map"))
        assertEquals(2, TouchHudPolicy.group("zoom_out"))
        assertEquals(1, TouchHudPolicy.grid(464f, 216f, 0).pages)
    }
}
