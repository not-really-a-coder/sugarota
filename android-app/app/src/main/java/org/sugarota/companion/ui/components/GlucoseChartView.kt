package org.sugarota.companion.ui.components

import android.graphics.Paint
import android.graphics.Typeface
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.nativeCanvas
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import org.sugarota.companion.model.GlucoseData
import org.sugarota.companion.ui.theme.ShadcnTheme
import java.text.SimpleDateFormat
import java.util.*
import kotlin.math.abs

@Composable
fun GlucoseChartView(
    history: List<GlucoseData>,
    modifier: Modifier = Modifier,
    units: String = "mg/dL",
    isDarkTheme: Boolean = true
) {
    val colors = ShadcnTheme.colors
    val typography = ShadcnTheme.typography

    // Prepare list sorted descending (latest first) to match firmware bgHistory[]
    val sortedHistory = remember(history) {
        history.sortedByDescending { it.timestamp }
    }

    var isTouching by remember { mutableStateOf(false) }
    var touchX by remember { mutableFloatStateOf(-1f) }

    if (sortedHistory.isEmpty()) {
        Box(
            modifier = modifier
                .clip(RoundedCornerShape(ShadcnTheme.shapes.radiusLarge))
                .background(colors.card)
                .border(
                    androidx.compose.foundation.BorderStroke(1.dp, colors.border),
                    RoundedCornerShape(ShadcnTheme.shapes.radiusLarge)
                )
                .padding(16.dp),
            contentAlignment = Alignment.Center
        ) {
            Text(
                text = "No history data available",
                style = typography.bodyMuted,
                color = colors.mutedForeground
            )
        }
        return
    }

    // Chart container
    Box(
        modifier = modifier
            .clip(RoundedCornerShape(ShadcnTheme.shapes.radiusLarge))
            .background(colors.card)
            .border(
                androidx.compose.foundation.BorderStroke(1.dp, colors.border),
                RoundedCornerShape(ShadcnTheme.shapes.radiusLarge)
            )
            .padding(top = 16.dp, bottom = 12.dp, start = 8.dp, end = 12.dp)
    ) {
        Canvas(
            modifier = Modifier
                .fillMaxSize()
                .pointerInput(sortedHistory) {
                    detectTapGestures(
                        onPress = { offset ->
                            isTouching = true
                            touchX = offset.x
                            tryAwaitRelease()
                            isTouching = false
                        }
                    )
                }
                .pointerInput(sortedHistory) {
                    detectDragGestures(
                        onDragStart = { offset ->
                            isTouching = true
                            touchX = offset.x
                        },
                        onDragEnd = {
                            isTouching = false
                        },
                        onDragCancel = {
                            isTouching = false
                        },
                        onDrag = { change, _ ->
                            change.consume()
                            isTouching = true
                            touchX = change.position.x
                        }
                    )
                }
        ) {
            val totalW = size.width
            val totalH = size.height

            // Layout metrics
            val yAxisLabelWidth = 44.dp.toPx()
            val xAxisLabelHeight = 22.dp.toPx()
            val chartTop = 10.dp.toPx()
            val chartBottom = totalH - xAxisLabelHeight
            val chartHeight = chartBottom - chartTop
            val chartLeft = yAxisLabelWidth
            val chartRight = totalW - 8.dp.toPx()
            val chartWidth = chartRight - chartLeft

            if (chartWidth <= 0 || chartHeight <= 0) return@Canvas

            // 1. Calculate dynamic BG min / max according to firmware logic:
            var maxBG = 0
            var minBG = 400
            for (item in sortedHistory) {
                if (item.sgv > maxBG) maxBG = item.sgv
                if (item.sgv < minBG) minBG = item.sgv
            }
            if (maxBG < 200) maxBG = 200
            if (minBG > 60) minBG = 60
            maxBG += 20
            minBG -= 20

            fun getY(bg: Int): Float {
                val clamped = bg.coerceIn(minBG, maxBG)
                val ratio = (clamped - minBG).toFloat() / (maxBG - minBG).toFloat()
                return chartBottom - (ratio * chartHeight)
            }

            // 2. Compute visual indices exactly matching firmware ui.cpp:
            val count = sortedHistory.size
            val visualIndex = IntArray(count)
            visualIndex[0] = 0
            for (i in 1 until count) {
                val diff = sortedHistory[i - 1].timestamp - sortedHistory[i].timestamp
                if (diff > 360) {
                    visualIndex[i] = visualIndex[i - 1] + 3
                } else {
                    visualIndex[i] = visualIndex[i - 1] + 1
                }
            }

            val maxHistory = 48
            val barWidth = chartWidth / maxHistory.toFloat()

            fun getPointX(idx: Int): Float {
                return chartRight - (visualIndex[idx] * barWidth)
            }

            // 3. Draw Target Range Band (70 - 180 mg/dL)
            val y180 = getY(180)
            val y70 = getY(70)
            val oldestX = getPointX(count - 1).coerceAtLeast(chartLeft)
            val targetBandColor = if (isDarkTheme) Color(0xFF1E293B).copy(alpha = 0.5f) else Color(0xFFF1F5F9)

            drawRect(
                color = targetBandColor,
                topLeft = Offset(oldestX, y180),
                size = Size((chartRight - oldestX), y70 - y180)
            )

            // Target threshold boundary lines (70 & 180)
            val gridLineColor = Color(0xFF27272A).copy(alpha = 0.8f)
            drawLine(
                color = gridLineColor,
                start = Offset(chartLeft, y180),
                end = Offset(chartRight, y180),
                strokeWidth = 1.dp.toPx()
            )
            drawLine(
                color = gridLineColor,
                start = Offset(chartLeft, y70),
                end = Offset(chartRight, y70),
                strokeWidth = 1.dp.toPx()
            )

            // Y-Axis Labels (Text Paint)
            val textPaint = Paint().apply {
                color = android.graphics.Color.parseColor("#A1A1AA")
                textSize = 10.sp.toPx()
                typeface = Typeface.create(Typeface.DEFAULT, Typeface.NORMAL)
                isAntiAlias = true
            }

            fun formatValueForY(bg: Int): String {
                return if (units.equals("mmol/l", ignoreCase = true)) {
                    String.format(Locale.US, "%.1f", bg / 18.0182f)
                } else {
                    bg.toString()
                }
            }

            val nativeCanvas = drawContext.canvas.nativeCanvas

            // 180 line label
            nativeCanvas.drawText(
                formatValueForY(180),
                8.dp.toPx(),
                y180 + 3.dp.toPx(),
                textPaint
            )

            // 70 line label
            nativeCanvas.drawText(
                formatValueForY(70),
                8.dp.toPx(),
                y70 + 3.dp.toPx(),
                textPaint
            )

            // Max line label (top)
            val yMaxTop = getY(maxBG)
            nativeCanvas.drawText(
                formatValueForY(maxBG - 20),
                8.dp.toPx(),
                yMaxTop + 10.dp.toPx(),
                textPaint
            )

            // 4. Data Gap indicators (when timeDiff > 360 sec)
            // Rendered as two vertical zigzag lines with width equal to 1 data point interval,
            // matching firmware ui.cpp.
            val zigzagColor = Color(0xFF71717A) // Muted gray
            val insideBgColor = if (isDarkTheme) colors.card else colors.card

            class GapRegion(
                val x1: Float,
                val x2: Float,
                val gapIdx: Int,
                val timeDiff: Long
            )
            val gapRegions = mutableListOf<GapRegion>()

            val numSegments = 10
            val segH = chartHeight / numSegments.toFloat()
            val amp = 3.dp.toPx()

            for (i in 0 until count - 1) {
                val timeDiff = sortedHistory[i].timestamp - sortedHistory[i + 1].timestamp
                if (timeDiff > 360) {
                    val xRight = getPointX(i)
                    val xLeft = getPointX(i + 1)
                    if (xRight < chartLeft || xLeft < chartLeft) continue

                    val xMid = (xLeft + xRight) / 2f
                    var gapW = barWidth
                    val minGapW = 6.dp.toPx()
                    if (gapW < minGapW) gapW = minGapW
                    val gx1 = xMid - gapW / 2f
                    val gx2 = gx1 + gapW

                    gapRegions.add(GapRegion(gx1, gx2, i, timeDiff))

                    // Fill interior between the two zigzag lines to canvas background
                    for (s in 0 until numSegments) {
                        val sy1 = chartTop + s * segH
                        val sy2 = if (s == numSegments - 1) chartBottom else (sy1 + segH)
                        val dxStart = if (s % 2 == 0) -amp else amp
                        val dxEnd = if (s % 2 == 0) amp else -amp

                        val segPath = Path().apply {
                            moveTo(gx1 + dxStart, sy1)
                            lineTo(gx2 + dxStart, sy1)
                            lineTo(gx2 + dxEnd, sy2)
                            lineTo(gx1 + dxEnd, sy2)
                            close()
                        }
                        drawPath(segPath, insideBgColor)
                    }

                    // Draw the two vertical zigzag boundary lines
                    val line1Path = Path().apply {
                        moveTo(gx1 + (if (0 % 2 == 0) -amp else amp), chartTop)
                        for (s in 0 until numSegments) {
                            val sy2 = if (s == numSegments - 1) chartBottom else (chartTop + (s + 1) * segH)
                            val dxEnd = if (s % 2 == 0) amp else -amp
                            lineTo(gx1 + dxEnd, sy2)
                        }
                    }
                    val line2Path = Path().apply {
                        moveTo(gx2 + (if (0 % 2 == 0) -amp else amp), chartTop)
                        for (s in 0 until numSegments) {
                            val sy2 = if (s == numSegments - 1) chartBottom else (chartTop + (s + 1) * segH)
                            val dxEnd = if (s % 2 == 0) amp else -amp
                            lineTo(gx2 + dxEnd, sy2)
                        }
                    }

                    drawPath(line1Path, zigzagColor, style = Stroke(width = 1.dp.toPx()))
                    drawPath(line2Path, zigzagColor, style = Stroke(width = 1.dp.toPx()))
                }
            }

            // 5. Connecting lines between consecutive readings (unless gap > 360)
            for (i in 0 until count - 1) {
                if (sortedHistory[i].timestamp - sortedHistory[i + 1].timestamp > 360) continue

                var x1 = getPointX(i)
                var x2 = getPointX(i + 1)
                if (x1 < chartLeft && x2 < chartLeft) continue

                var y1 = getY(sortedHistory[i].sgv)
                var y2 = getY(sortedHistory[i + 1].sgv)

                if (x2 < chartLeft) {
                    if (x1 != x2) {
                        y2 = y1 + (y2 - y1) * (chartLeft - x1) / (x2 - x1)
                    }
                    x2 = chartLeft
                }

                val avgSgv = (sortedHistory[i].sgv + sortedHistory[i + 1].sgv) / 2
                val lineColor = if (avgSgv in 70..180) Color(0xFF00E676) else Color(0xFFF97316)

                drawLine(
                    color = lineColor,
                    start = Offset(x1, y1),
                    end = Offset(x2, y2),
                    strokeWidth = 2.5.dp.toPx()
                )
            }

            // 6. Data circles
            for (i in 0 until count) {
                val x = getPointX(i)
                if (x < chartLeft) continue
                val y = getY(sortedHistory[i].sgv)
                val dotColor = if (sortedHistory[i].sgv in 70..180) Color(0xFF00E676) else Color(0xFFF97316)

                drawCircle(
                    color = dotColor,
                    radius = 3.dp.toPx(),
                    center = Offset(x, y)
                )
            }

            // 7. X-Axis Time Labels (Absolute hours: e.g. 14:00, 13:00, 12:00, 11:00 or current reading time)
            val xAxisPaint = Paint().apply {
                color = android.graphics.Color.parseColor("#71717A")
                textSize = 10.sp.toPx()
                isAntiAlias = true
            }

            val latestTs = sortedHistory.first().timestamp
            val timeSdf = SimpleDateFormat("HH:mm", Locale.getDefault())
            val timeIntervals = listOf(
                Pair(0f, timeSdf.format(Date(latestTs * 1000L))),
                Pair(16 * barWidth, timeSdf.format(Date((latestTs - 3600L) * 1000L))),
                Pair(32 * barWidth, timeSdf.format(Date((latestTs - 7200L) * 1000L))),
                Pair(48 * barWidth, timeSdf.format(Date((latestTs - 10800L) * 1000L)))
            )
            for ((offsetFromRight, label) in timeIntervals) {
                val lx = chartRight - offsetFromRight
                if (lx >= chartLeft) {
                    val textW = xAxisPaint.measureText(label)
                    nativeCanvas.drawText(
                        label,
                        lx - (textW / 2f).coerceAtMost(lx - chartLeft),
                        totalH - 4.dp.toPx(),
                        xAxisPaint
                    )
                }
            }

            // 8. Interactive Scrubber / Tooltip (supports both regular reading & gap scrubber)
            if (isTouching && touchX in chartLeft..chartRight) {
                // Check if scrubber cursor falls inside or near a gap
                val touchTolerance = 6.dp.toPx()
                val touchedGap = gapRegions.firstOrNull { gap ->
                    touchX >= (gap.x1 - touchTolerance) && touchX <= (gap.x2 + touchTolerance)
                }

                if (touchedGap != null) {
                    // Scrubber over Gap: display gap duration and start/end times
                    val gxMid = (touchedGap.x1 + touchedGap.x2) / 2f
                    drawLine(
                        color = Color.White.copy(alpha = 0.8f),
                        start = Offset(gxMid, chartTop),
                        end = Offset(gxMid, chartBottom),
                        strokeWidth = 1.5.dp.toPx()
                    )

                    val boxW = 120.dp.toPx()
                    val boxH = 44.dp.toPx()
                    var boxX = gxMid - (boxW / 2f)
                    if (boxX < chartLeft) boxX = chartLeft
                    if (boxX + boxW > chartRight) boxX = chartRight - boxW
                    val boxY = (chartTop + 10.dp.toPx()).coerceAtLeast(chartTop)

                    drawRoundRect(
                        color = Color(0xFF27272A),
                        topLeft = Offset(boxX, boxY),
                        size = Size(boxW, boxH),
                        cornerRadius = androidx.compose.ui.geometry.CornerRadius(6.dp.toPx())
                    )
                    drawRoundRect(
                        color = Color(0xFF3F3F46),
                        topLeft = Offset(boxX, boxY),
                        size = Size(boxW, boxH),
                        cornerRadius = androidx.compose.ui.geometry.CornerRadius(6.dp.toPx()),
                        style = Stroke(width = 1.dp.toPx())
                    )

                    val gapTitlePaint = Paint().apply {
                        color = android.graphics.Color.WHITE
                        textSize = 13.sp.toPx()
                        typeface = Typeface.create(Typeface.DEFAULT, Typeface.BOLD)
                        isAntiAlias = true
                    }
                    val gapSubPaint = Paint().apply {
                        color = android.graphics.Color.parseColor("#A1A1AA")
                        textSize = 10.sp.toPx()
                        isAntiAlias = true
                    }

                    val gapMins = touchedGap.timeDiff / 60
                    val idx = touchedGap.gapIdx
                    val tStart = sortedHistory[idx + 1].timestamp
                    val tEnd = sortedHistory[idx].timestamp
                    val sStart = timeSdf.format(Date(tStart * 1000L))
                    val sEnd = timeSdf.format(Date(tEnd * 1000L))

                    nativeCanvas.drawText(
                        "GAP $gapMins min",
                        boxX + 10.dp.toPx(),
                        boxY + 18.dp.toPx(),
                        gapTitlePaint
                    )
                    nativeCanvas.drawText(
                        "$sStart - $sEnd",
                        boxX + 10.dp.toPx(),
                        boxY + 34.dp.toPx(),
                        gapSubPaint
                    )
                } else {
                    // Regular reading scrubber
                    var closestIdx = 0
                    var minDiff = Float.MAX_VALUE
                    for (i in 0 until count) {
                        val px = getPointX(i)
                        if (px < chartLeft) continue
                        val diff = abs(touchX - px)
                        if (diff < minDiff) {
                            minDiff = diff
                            closestIdx = i
                        }
                    }

                    val curReading = sortedHistory[closestIdx]
                    val curX = getPointX(closestIdx)
                    val curY = getY(curReading.sgv)

                    // Vertical cursor line
                    drawLine(
                        color = Color.White.copy(alpha = 0.8f),
                        start = Offset(curX, chartTop),
                        end = Offset(curX, chartBottom),
                        strokeWidth = 1.5.dp.toPx()
                    )

                    // Dot selection indicator
                    drawCircle(
                        color = Color.White,
                        radius = 5.dp.toPx(),
                        center = Offset(curX, curY)
                    )
                    drawCircle(
                        color = Color.Black,
                        radius = 3.5.dp.toPx(),
                        center = Offset(curX, curY)
                    )

                    // Tooltip Callout Box
                    val boxW = 84.dp.toPx()
                    val boxH = 44.dp.toPx()
                    var boxX = curX - (boxW / 2f)
                    if (boxX < chartLeft) boxX = chartLeft
                    if (boxX + boxW > chartRight) boxX = chartRight - boxW
                    val boxY = (curY - boxH - 10.dp.toPx()).coerceAtLeast(chartTop)

                    // Box background
                    drawRoundRect(
                        color = Color(0xFF27272A),
                        topLeft = Offset(boxX, boxY),
                        size = Size(boxW, boxH),
                        cornerRadius = androidx.compose.ui.geometry.CornerRadius(6.dp.toPx())
                    )
                    drawRoundRect(
                        color = Color(0xFF3F3F46),
                        topLeft = Offset(boxX, boxY),
                        size = Size(boxW, boxH),
                        cornerRadius = androidx.compose.ui.geometry.CornerRadius(6.dp.toPx()),
                        style = Stroke(width = 1.dp.toPx())
                    )

                    // Tooltip text
                    val valPaint = Paint().apply {
                        color = android.graphics.Color.WHITE
                        textSize = 14.sp.toPx()
                        typeface = Typeface.create(Typeface.DEFAULT, Typeface.BOLD)
                        isAntiAlias = true
                    }
                    val timePaint = Paint().apply {
                        color = android.graphics.Color.parseColor("#A1A1AA")
                        textSize = 10.sp.toPx()
                        isAntiAlias = true
                    }

                    val formattedBg = if (units.equals("mmol/l", ignoreCase = true)) {
                        String.format(Locale.US, "%.1f", curReading.sgv / 18.0182f)
                    } else {
                        curReading.sgv.toString()
                    }
                    val deltaStr = if (curReading.delta != 0) {
                        val sign = if (curReading.delta > 0) "+" else ""
                        " ($sign${curReading.delta})"
                    } else ""
                    val fullValStr = "$formattedBg$deltaStr"

                    val timeStr = timeSdf.format(Date(curReading.timestamp * 1000L))

                    nativeCanvas.drawText(
                        fullValStr,
                        boxX + 10.dp.toPx(),
                        boxY + 18.dp.toPx(),
                        valPaint
                    )
                    nativeCanvas.drawText(
                        "$timeStr · $units",
                        boxX + 10.dp.toPx(),
                        boxY + 34.dp.toPx(),
                        timePaint
                    )
                }
            }
        }
    }
}
