package org.sugarota.companion.ui.notification

import android.graphics.*
import org.sugarota.companion.model.GlucoseData
import java.util.Locale

object NotificationChartRenderer {

    private const val WIDTH = 640
    private const val HEIGHT = 200

    private const val MIN_BG = 40
    private const val MAX_BG = 300
    private const val TARGET_LOW = 70
    private const val TARGET_HIGH = 180

    // 2-hour window = 7200 seconds
    private const val TWO_HOURS_SEC = 7200L

    /**
     * Renders a clean 2-hour glucose preview sparkline bitmap:
     * - Dark sleek background matching Sugarota aesthetic (#121214)
     * - Left vertical axis labels:
     *     - Target boundaries: 70 & 180 (or mmol/L equivalents)
     *     - Peak / highest value if > 180
     *     - Lowest value if < 70
     * - Subtle target range band (70 - 180 mg/dL)
     * - Dotted/dashed target boundaries
     * - Connecting sparkline and dots colored in-range vs out-of-range
     */
    fun renderTwoHourChart(latestReading: GlucoseData, units: String = "mg/dL"): Bitmap {
        return renderChart(latestReading, units, windowSec = TWO_HOURS_SEC)
    }

    fun renderOneHourChart(latestReading: GlucoseData, units: String = "mg/dL"): Bitmap {
        return renderTwoHourChart(latestReading, units)
    }

    fun renderChart(latestReading: GlucoseData, units: String = "mg/dL", windowSec: Long = TWO_HOURS_SEC): Bitmap {
        val bitmap = Bitmap.createBitmap(WIDTH, HEIGHT, Bitmap.Config.ARGB_8888)
        val canvas = Canvas(bitmap)

        val bgPaint = Paint().apply {
            color = Color.parseColor("#121214")
            style = Paint.Style.FILL
            isAntiAlias = true
        }
        // Rounded rectangle background
        val rectF = RectF(0f, 0f, WIDTH.toFloat(), HEIGHT.toFloat())
        canvas.drawRoundRect(rectF, 16f, 16f, bgPaint)

        // Collect all readings in the window
        val allPoints = mutableListOf<GlucoseData>()
        allPoints.add(latestReading)
        allPoints.addAll(latestReading.history)

        val nowTs = latestReading.timestamp
        val cutoffTs = nowTs - windowSec

        val windowPoints = allPoints
            .filter { it.timestamp >= cutoffTs }
            .distinctBy { it.timestamp }
            .sortedBy { it.timestamp } // Ascending order (oldest to newest)

        val padLeft = 60f // Space reserved on the left for vertical axis labels
        val padRight = 24f
        val padTop = 24f
        val padBottom = 20f
        val chartW = WIDTH - padLeft - padRight
        val chartH = HEIGHT - padTop - padBottom

        // Compute dynamic min/max BG
        val pointMax = windowPoints.maxOfOrNull { it.sgv } ?: latestReading.sgv
        val pointMin = windowPoints.minOfOrNull { it.sgv } ?: latestReading.sgv

        var currentMaxBG = maxOf(TARGET_HIGH + 10, pointMax + 10)
        var currentMinBG = minOf(TARGET_LOW - 10, pointMin - 10)
        if (currentMinBG < 40) currentMinBG = 40

        fun getY(bg: Int): Float {
            val clamped = bg.coerceIn(currentMinBG, currentMaxBG)
            val ratio = (clamped - currentMinBG).toFloat() / (currentMaxBG - currentMinBG).toFloat()
            return (padTop + chartH) - (ratio * chartH)
        }

        // Draw Target Range Band (70 - 180 mg/dL)
        val y180 = getY(TARGET_HIGH)
        val y70 = getY(TARGET_LOW)

        val bandPaint = Paint().apply {
            color = Color.parseColor("#1A2E26") // Subtle emerald tinted dark band
            style = Paint.Style.FILL
            isAntiAlias = true
        }
        canvas.drawRect(padLeft, y180, padLeft + chartW, y70, bandPaint)

        // Target range border lines
        val linePaint = Paint().apply {
            color = Color.parseColor("#2D3748")
            strokeWidth = 2f
            style = Paint.Style.STROKE
            pathEffect = DashPathEffect(floatArrayOf(6f, 6f), 0f)
            isAntiAlias = true
        }
        canvas.drawLine(padLeft, y180, padLeft + chartW, y180, linePaint)
        canvas.drawLine(padLeft, y70, padLeft + chartW, y70, linePaint)

        // Helper to format values for Y-axis labels according to units
        fun formatValueForY(bg: Int): String {
            return if (units.equals("mmol/l", ignoreCase = true)) {
                String.format(Locale.US, "%.1f", bg / 18.0182f)
            } else {
                bg.toString()
            }
        }

        // Axis label paint
        val textPaint = Paint().apply {
            color = Color.parseColor("#94A3B8") // Muted slate gray
            textSize = 20f
            typeface = Typeface.create(Typeface.SANS_SERIF, Typeface.NORMAL)
            isAntiAlias = true
            textAlign = Paint.Align.RIGHT
        }

        val labelX = padLeft - 8f

        // Draw Target Boundary Labels (180 and 70)
        canvas.drawText(formatValueForY(TARGET_HIGH), labelX, y180 + 7f, textPaint)
        canvas.drawText(formatValueForY(TARGET_LOW), labelX, y70 + 7f, textPaint)

        // If highest point is outside target range (> 180), draw highest value label
        if (pointMax > TARGET_HIGH) {
            val yPeak = getY(pointMax)
            // Prevent text overlapping with y180
            if (y180 - yPeak > 22f) {
                val peakPaint = Paint(textPaint).apply {
                    color = Color.parseColor("#F97316") // Orange indicator for high
                }
                canvas.drawText(formatValueForY(pointMax), labelX, yPeak + 7f, peakPaint)
            }
        }

        // If lowest point is outside target range (< 70), draw lowest value label
        if (pointMin < TARGET_LOW) {
            val yLow = getY(pointMin)
            // Prevent text overlapping with y70
            if (yLow - y70 > 22f) {
                val lowPaint = Paint(textPaint).apply {
                    color = Color.parseColor("#EF4444") // Red indicator for low
                }
                canvas.drawText(formatValueForY(pointMin), labelX, yLow + 7f, lowPaint)
            }
        }

        if (windowPoints.isEmpty()) {
            return bitmap
        }

        fun getX(ts: Long): Float {
            val offsetSec = (ts - cutoffTs).coerceIn(0L, windowSec)
            val ratio = offsetSec.toFloat() / windowSec.toFloat()
            return padLeft + (ratio * chartW)
        }

        // Connecting lines
        val strokePaint = Paint().apply {
            strokeWidth = 4f
            style = Paint.Style.STROKE
            isAntiAlias = true
            strokeCap = Paint.Cap.ROUND
        }

        for (i in 0 until windowPoints.size - 1) {
            val p1 = windowPoints[i]
            val p2 = windowPoints[i + 1]

            // If gap between points is > 10 minutes (600s), don't draw connecting line
            if (p2.timestamp - p1.timestamp > 600) continue

            val x1 = getX(p1.timestamp)
            val y1 = getY(p1.sgv)
            val x2 = getX(p2.timestamp)
            val y2 = getY(p2.sgv)

            val avg = (p1.sgv + p2.sgv) / 2
            strokePaint.color = when {
                avg < TARGET_LOW -> Color.parseColor("#EF4444")
                avg > TARGET_HIGH -> Color.parseColor("#F97316")
                else -> Color.parseColor("#00E676")
            }
            canvas.drawLine(x1, y1, x2, y2, strokePaint)
        }

        // Data dots
        val dotPaint = Paint().apply {
            style = Paint.Style.FILL
            isAntiAlias = true
        }

        val dotStrokePaint = Paint().apply {
            color = Color.parseColor("#121214")
            strokeWidth = 3f
            style = Paint.Style.STROKE
            isAntiAlias = true
        }

        for (i in windowPoints.indices) {
            val pt = windowPoints[i]
            val x = getX(pt.timestamp)
            val y = getY(pt.sgv)

            val dotColor = when {
                pt.sgv < TARGET_LOW -> Color.parseColor("#EF4444")
                pt.sgv > TARGET_HIGH -> Color.parseColor("#F97316")
                else -> Color.parseColor("#00E676")
            }

            dotPaint.color = dotColor
            val isLatest = (i == windowPoints.size - 1)
            val radius = if (isLatest) 7f else 5f

            canvas.drawCircle(x, y, radius + 2f, dotStrokePaint)
            canvas.drawCircle(x, y, radius, dotPaint)
        }

        return bitmap
    }
}
