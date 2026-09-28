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

/**
 * Picture-in-Picture Glucose Chart Content:
 * - Direct copy of the Device Details screen chart area (upper portion) without interactive elements (no force refresh, no scrubber).
 * - Fixed reference canvas (e.g. 400x225 dp, 16:9) scaled uniformly via BoxWithConstraints so elements resize proportionally like a picture across all PiP window sizes.
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

    // Reference canvas design dimensions (16:9 ratio)
    val baseWidthDp = 400.dp
    val baseHeightDp = 225.dp

    BoxWithConstraints(
        modifier = Modifier
            .fillMaxSize()
            .background(colors.background),
        contentAlignment = Alignment.Center
    ) {
        val scaleX = maxWidth / baseWidthDp
        val scaleY = maxHeight / baseHeightDp
        val uniformScale = minOf(scaleX, scaleY).coerceAtLeast(0.1f)

        Box(
            modifier = Modifier
                .size(width = baseWidthDp, height = baseHeightDp)
                .scale(uniformScale)
                .padding(horizontal = 14.dp, vertical = 10.dp)
        ) {
            Column(
                modifier = Modifier.fillMaxSize()
            ) {
                // Glucose Value One-liner above the chart (same as DeviceDetailScreen, without refresh button)
                if (reading != null) {
                    val formattedSgv = if (isMmol) {
                        String.format(Locale.US, "%.1f", reading.sgv / 18.0182f)
                    } else {
                        "${reading.sgv}"
                    }
                    val deltaFormatted = if (isMmol) {
                        val mmolVal = reading.delta / 18.0182f
                        if (reading.delta == 0) "+0.0" else String.format(Locale.US, "%s%.1f", if (reading.delta > 0) "+" else "", mmolVal)
                    } else {
                        "${if (reading.delta > 0) "+" else ""}${reading.delta}"
                    }
                    val bgCol = getGlucoseColor(reading.sgv)
                    val timeStr = SimpleDateFormat("HH:mm", Locale.getDefault())
                        .format(Date(reading.timestamp * 1000))
                    val minsAgo = ((System.currentTimeMillis() / 1000 - reading.timestamp) / 60).coerceAtLeast(0)
                    val agoText = if (minsAgo == 0L) "now" else "$minsAgo min ago"

                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Text(
                            text = formattedSgv,
                            fontSize = 28.sp,
                            fontWeight = FontWeight.Bold,
                            color = bgCol
                        )
                        Spacer(modifier = Modifier.width(6.dp))
                        TrendArrowIcon(
                            direction = reading.direction,
                            tint = bgCol
                        )
                        Spacer(modifier = Modifier.width(12.dp))
                        Text(
                            text = "$deltaFormatted $units",
                            fontSize = 20.sp,
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
                    Spacer(modifier = Modifier.height(10.dp))
                }

                // Glucose Chart View (Device details version, without scrubber interaction)
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
                        modifier = Modifier.fillMaxSize()
                    )
                }
            }
        }
    }
}
