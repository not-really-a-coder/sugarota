package org.sugarota.companion.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.scale
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.TransformOrigin
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import org.sugarota.companion.TrendArrowIcon
import org.sugarota.companion.getGlucoseColor
import org.sugarota.companion.model.GlucoseData
import org.sugarota.companion.service.SugarotaBleService
import org.sugarota.companion.ui.components.GlucoseChartView
import org.sugarota.companion.ui.theme.ShadcnTheme
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

import androidx.compose.foundation.Canvas
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.drawscope.Stroke

/**
 * Picture-in-Picture Glucose Chart Content:
 * - Adaptable layout based on window width:
 *   - >= 320.dp: Full chart layout with labels and values, with padding from edges.
 *   - 240.dp..319.dp: Full canvas chart without labels, padding from edges, compact header with data age as "X min ago".
 *   - < 240.dp: Actual Glucose Value, trend arrow, 2-line delta & units, and a 2hr smoothed sparkline.
 */
@Composable
fun PipGlucoseChartContent(
    reading: GlucoseData?,
    units: String,
    service: SugarotaBleService? = null
) {
    val historyData = remember(reading) {
        if (reading != null) {
            if (reading.history.isNotEmpty()) {
                reading.history
            } else {
                listOf(reading)
            }
        } else {
            emptyList()
        }
    }

    val isMmol = units.equals("mmol/l", ignoreCase = true)
    val colors = ShadcnTheme.colors
    val typography = ShadcnTheme.typography

    BoxWithConstraints(
        modifier = Modifier
            .fillMaxSize()
            .background(colors.background)
    ) {
        val pipWidthDp = maxWidth
        val pipHeightDp = maxHeight

        val formattedSgv = if (reading != null) {
            if (isMmol) {
                String.format(Locale.US, "%.1f", reading.sgv / 18.0182f)
            } else {
                "${reading.sgv}"
            }
        } else "--"

        val deltaFormatted = if (reading != null) {
            if (isMmol) {
                val mmolVal = reading.delta / 18.0182f
                if (reading.delta == 0) "+0.0" else String.format(Locale.US, "%s%.1f", if (reading.delta > 0) "+" else "", mmolVal)
            } else {
                "${if (reading.delta > 0) "+" else ""}${reading.delta}"
            }
        } else ""

        val bgCol = if (reading != null) getGlucoseColor(reading.sgv) else colors.foreground

        when {
            // Mode 3: < 240.dp width
            // Glucose Value, Trend Arrow, 2-line Delta+Units, plus small 2hr smoothed sparkline
            pipWidthDp < 240.dp -> {
                val baseRefWidth = 200.dp
                val baseRefHeight = 112.dp
                val scale = minOf(pipWidthDp / baseRefWidth, pipHeightDp / baseRefHeight).coerceAtLeast(0.1f)

                Box(
                    modifier = Modifier.fillMaxSize(),
                    contentAlignment = Alignment.Center
                ) {
                    Box(
                        modifier = Modifier
                            .size(width = baseRefWidth, height = baseRefHeight)
                            .scale(scale)
                            .padding(horizontal = 8.dp, vertical = 4.dp),
                        contentAlignment = Alignment.Center
                    ) {
                        Column(
                            modifier = Modifier.fillMaxSize(),
                            verticalArrangement = Arrangement.Center,
                            horizontalAlignment = Alignment.CenterHorizontally
                        ) {
                            // Actual Glucose Value string
                            Row(
                                verticalAlignment = Alignment.CenterVertically,
                                horizontalArrangement = Arrangement.Center
                            ) {
                                Text(
                                    text = formattedSgv,
                                    fontSize = 32.sp,
                                    fontWeight = FontWeight.Bold,
                                    color = bgCol
                                )
                                if (reading != null) {
                                    Spacer(modifier = Modifier.width(4.dp))
                                    TrendArrowIcon(
                                        direction = reading.direction,
                                        tint = bgCol,
                                        modifier = Modifier.size(24.dp)
                                    )
                                }
                                Spacer(modifier = Modifier.width(6.dp))
                                Column(
                                    verticalArrangement = Arrangement.Center
                                ) {
                                    if (deltaFormatted.isNotBlank()) {
                                        Text(
                                            text = deltaFormatted,
                                            fontSize = 11.sp,
                                            fontWeight = FontWeight.SemiBold,
                                            color = colors.foreground,
                                            lineHeight = 12.sp
                                        )
                                    }
                                    Text(
                                        text = units,
                                        fontSize = 10.sp,
                                        fontWeight = FontWeight.Normal,
                                        color = colors.mutedForeground,
                                        lineHeight = 11.sp
                                    )
                                }
                            }

                            Spacer(modifier = Modifier.height(4.dp))

                            // 2-hour smoothed sparkline
                            PipGlucoseSparkline(
                                reading = reading,
                                history = historyData,
                                modifier = Modifier
                                    .fillMaxWidth()
                                    .height(34.dp)
                            )
                        }
                    }
                }
            }

            // Mode 2: 240.dp .. 319.dp width
            // Chart without labels, edge paddings increased, slightly reduced header elements, data age "X min ago" / "now"
            pipWidthDp < 320.dp -> {
                Column(
                    modifier = Modifier
                        .fillMaxSize()
                        .padding(horizontal = 14.dp, vertical = 10.dp)
                ) {
                    if (reading != null) {
                        val minsAgo = ((System.currentTimeMillis() / 1000 - reading.timestamp) / 60).coerceAtLeast(0)
                        val agoText = if (minsAgo == 0L) "now" else "$minsAgo min ago"

                        Row(
                            modifier = Modifier
                                .fillMaxWidth()
                                .padding(horizontal = 4.dp, vertical = 2.dp),
                            verticalAlignment = Alignment.CenterVertically
                        ) {
                            Text(
                                text = formattedSgv,
                                fontSize = 21.sp,
                                fontWeight = FontWeight.Bold,
                                color = bgCol
                            )
                            Spacer(modifier = Modifier.width(4.dp))
                            TrendArrowIcon(
                                direction = reading.direction,
                                tint = bgCol,
                                modifier = Modifier.size(17.dp)
                            )
                            Spacer(modifier = Modifier.width(5.dp))
                            Text(
                                text = "$deltaFormatted $units",
                                fontSize = 14.sp,
                                fontWeight = FontWeight.SemiBold,
                                color = colors.foreground
                            )
                            Spacer(modifier = Modifier.weight(1f))
                            Text(
                                text = agoText,
                                style = typography.caption,
                                fontSize = 11.sp,
                                color = colors.mutedForeground
                            )
                        }
                    }

                    Spacer(modifier = Modifier.height(4.dp))

                    // Chart without labels taking remaining space
                    Box(
                        modifier = Modifier
                            .fillMaxWidth()
                            .weight(1f)
                    ) {
                        GlucoseChartView(
                            history = historyData,
                            units = units,
                            isDarkTheme = true,
                            enableInteraction = false,
                            showLabels = false,
                            showCardFrame = false,
                            modifier = Modifier.fillMaxSize()
                        )
                    }
                }
            }

            // Mode 1: >= 320.dp width
            // Direct responsive column taking 100% with moderate edge padding (eliminating double/aspect-ratio canvas letterboxing)
            else -> {
                Column(
                    modifier = Modifier
                        .fillMaxSize()
                        .padding(start = 12.dp, end = 12.dp, top = 8.dp, bottom = 6.dp)
                ) {
                    if (reading != null) {
                        val timeStr = SimpleDateFormat("HH:mm", Locale.getDefault())
                            .format(Date(reading.timestamp * 1000))
                        val minsAgo = ((System.currentTimeMillis() / 1000 - reading.timestamp) / 60).coerceAtLeast(0)
                        val agoText = if (minsAgo == 0L) "now" else "$minsAgo min ago"

                        Row(
                            modifier = Modifier
                                .fillMaxWidth()
                                .padding(horizontal = 4.dp, vertical = 2.dp),
                            verticalAlignment = Alignment.CenterVertically
                        ) {
                            Text(
                                text = formattedSgv,
                                fontSize = 26.sp,
                                fontWeight = FontWeight.Bold,
                                color = bgCol
                            )
                            Spacer(modifier = Modifier.width(6.dp))
                            TrendArrowIcon(
                                direction = reading.direction,
                                tint = bgCol,
                                modifier = Modifier.size(22.dp)
                            )
                            Spacer(modifier = Modifier.width(10.dp))
                            Text(
                                text = "$deltaFormatted $units",
                                fontSize = 18.sp,
                                fontWeight = FontWeight.SemiBold,
                                color = colors.foreground
                            )
                            Spacer(modifier = Modifier.weight(1f))
                            Text(
                                text = "$timeStr ($agoText)",
                                style = typography.caption,
                                color = colors.mutedForeground
                            )
                        }
                        Spacer(modifier = Modifier.height(4.dp))
                    }

                    Box(
                        modifier = Modifier
                            .fillMaxWidth()
                            .weight(1f)
                    ) {
                        GlucoseChartView(
                            history = historyData,
                            units = units,
                            isDarkTheme = true,
                            enableInteraction = false,
                            showLabels = true,
                            showCardFrame = false,
                            modifier = Modifier.fillMaxSize()
                        )
                    }
                }
            }
        }
    }
}

/**
 * Clean 2-hour smoothed sparkline with subtle target range background band (70-180 mg/dL).
 * Gaps between readings (> 360s / 6 min) are shown as disconnected curve segments with individual point dots.
 */
@Composable
fun PipGlucoseSparkline(
    reading: GlucoseData?,
    history: List<GlucoseData>,
    modifier: Modifier = Modifier
) {
    if (reading == null || history.isEmpty()) {
        Box(modifier = modifier)
        return
    }

    val twoHoursSec = 2 * 3600L
    val nowTs = reading.timestamp
    val cutoffTs = nowTs - twoHoursSec

    val points = remember(reading, history) {
        val all = mutableListOf<GlucoseData>()
        all.add(reading)
        all.addAll(history)
        all.filter { it.timestamp >= cutoffTs }
            .distinctBy { it.timestamp }
            .sortedBy { it.timestamp } // Ascending (oldest to newest)
    }

    if (points.isEmpty()) {
        Box(modifier = modifier)
        return
    }

    Canvas(modifier = modifier) {
        val w = size.width
        val h = size.height
        if (w <= 0f || h <= 0f) return@Canvas

        val maxSgv = maxOf(200, (points.maxOfOrNull { it.sgv } ?: reading.sgv) + 15)
        val minSgv = minOf(60, (points.minOfOrNull { it.sgv } ?: reading.sgv) - 15)

        fun getY(sgv: Int): Float {
            val clamped = sgv.coerceIn(minSgv, maxSgv)
            val ratio = (clamped - minSgv).toFloat() / (maxSgv - minSgv).toFloat()
            return h - (ratio * h)
        }

        fun getX(ts: Long): Float {
            val offset = (ts - cutoffTs).coerceIn(0L, twoHoursSec)
            return (offset.toFloat() / twoHoursSec.toFloat()) * w
        }

        // Draw Target Range Band (70 - 180 mg/dL)
        val y180 = getY(180)
        val y70 = getY(70)
        val bandTop = minOf(y180, y70)
        val bandHeight = kotlin.math.abs(y70 - y180)
        drawRect(
            color = Color(0xFF1E293B).copy(alpha = 0.5f),
            topLeft = Offset(0f, bandTop),
            size = Size(w, bandHeight)
        )

        // Target range reference lines (70 & 180)
        val gridLineColor = Color(0xFF27272A).copy(alpha = 0.6f)
        drawLine(
            color = gridLineColor,
            start = Offset(0f, y180),
            end = Offset(w, y180),
            strokeWidth = 1.dp.toPx()
        )
        drawLine(
            color = gridLineColor,
            start = Offset(0f, y70),
            end = Offset(w, y70),
            strokeWidth = 1.dp.toPx()
        )

        val coords = points.map { Offset(getX(it.timestamp), getY(it.sgv)) }

        // Partition points into continuous segments where gap <= 360s (6 min)
        val segments = mutableListOf<MutableList<Int>>()
        var currentSegment = mutableListOf(0)

        for (i in 1 until points.size) {
            val gap = points[i].timestamp - points[i - 1].timestamp
            if (gap > 360) {
                segments.add(currentSegment)
                currentSegment = mutableListOf(i)
            } else {
                currentSegment.add(i)
            }
        }
        segments.add(currentSegment)

        // Draw continuous Bezier smoothed curves for segments with >= 2 points
        for (seg in segments) {
            if (seg.size < 2) continue

            val path = Path().apply {
                val startIdx = seg.first()
                moveTo(coords[startIdx].x, coords[startIdx].y)
                for (k in 0 until seg.size - 1) {
                    val idx0 = seg[k]
                    val idx1 = seg[k + 1]
                    val p0 = coords[idx0]
                    val p1 = coords[idx1]
                    val controlX = (p0.x + p1.x) / 2f
                    cubicTo(
                        x1 = controlX, y1 = p0.y,
                        x2 = controlX, y2 = p1.y,
                        x3 = p1.x, y3 = p1.y
                    )
                }
            }

            val lastSegIdx = seg.last()
            val segSgv = points[lastSegIdx].sgv
            val segLineColor = if (segSgv in 70..180) Color(0xFF00E676) else Color(0xFFF97316)

            drawPath(
                path = path,
                color = segLineColor,
                style = Stroke(
                    width = 2.dp.toPx(),
                    cap = androidx.compose.ui.graphics.StrokeCap.Round
                )
            )
        }

        // Draw data points as dots (shows isolated dots during gaps and points along the curve)
        for (i in points.indices) {
            val pt = points[i]
            val dotColor = if (pt.sgv in 70..180) Color(0xFF00E676) else Color(0xFFF97316)
            val radius = if (i == points.size - 1) 2.5.dp.toPx() else 1.5.dp.toPx()
            drawCircle(
                color = dotColor,
                radius = radius,
                center = coords[i]
            )
        }
    }
}
