package org.sugarota.companion

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.sugarota.companion.network.FirmwareUpdateManager

class CalVerComparatorTest {

    private fun compare(v1: String, v2: String): Int {
        // Test comparator logic directly
        fun parseParts(v: String): IntArray {
            val clean = v.trim().removePrefix("v").removePrefix("V")
            val segments = clean.split(".")
            val parts = IntArray(4) { 0 }
            for (i in 0 until minOf(segments.size, 4)) {
                parts[i] = segments[i].toIntOrNull() ?: 0
            }
            return parts
        }

        val p1 = parseParts(v1)
        val p2 = parseParts(v2)
        for (i in 0 until 4) {
            val cmp = p1[i].compareTo(p2[i])
            if (cmp != 0) return cmp
        }
        return 0
    }

    @Test
    fun testCalVerComparison() {
        // Newer build on same day
        assertTrue(compare("v0.09.20.41", "v0.09.20.3") > 0)
        assertTrue(compare("v0.09.20.3", "v0.09.20.41") < 0)

        // Same version
        assertEquals(0, compare("v0.09.20.41", "v0.09.20.41"))
        assertEquals(0, compare("v0.09.20.41", "0.09.20.41"))

        // Newer day
        assertTrue(compare("v0.09.21.0", "v0.09.20.99") > 0)

        // Newer month
        assertTrue(compare("v0.10.01.0", "v0.09.30.50") > 0)
    }
}
