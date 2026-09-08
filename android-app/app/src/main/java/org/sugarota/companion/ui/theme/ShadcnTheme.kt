package org.sugarota.companion.ui.theme

import androidx.compose.runtime.Composable
import androidx.compose.runtime.staticCompositionLocalOf
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

/**
 * Shadcn-inspired design tokens for Jetpack Compose (Zinc / Dark aesthetic).
 */
data class ShadcnColors(
    val background: Color = Color(0xFF09090B),    // zinc-950
    val foreground: Color = Color(0xFFFAFAFA),    // zinc-50
    val card: Color = Color(0xFF18181B),          // zinc-900
    val cardForeground: Color = Color(0xFFFAFAFA),
    val popover: Color = Color(0xFF18181B),
    val popoverForeground: Color = Color(0xFFFAFAFA),
    val primary: Color = Color(0xFF00E676),       // Emerald green accent
    val primaryForeground: Color = Color(0xFF000000),
    val secondary: Color = Color(0xFF27272A),     // zinc-800
    val secondaryForeground: Color = Color(0xFFFAFAFA),
    val muted: Color = Color(0xFF27272A),
    val mutedForeground: Color = Color(0xFFA1A1AA),// zinc-400
    val accent: Color = Color(0xFF27272A),
    val accentForeground: Color = Color(0xFFFAFAFA),
    val destructive: Color = Color(0xFFEF4444),   // red-500
    val destructiveForeground: Color = Color(0xFFFAFAFA),
    val border: Color = Color(0xFF27272A),        // zinc-800
    val inputBorder: Color = Color(0xFF3F3F46),   // zinc-700
    val ring: Color = Color(0xFF00E676)
)

data class ShadcnTypography(
    val h1: TextStyle = TextStyle(fontSize = 24.sp, fontWeight = FontWeight.Bold, color = Color(0xFFFAFAFA)),
    val h2: TextStyle = TextStyle(fontSize = 18.sp, fontWeight = FontWeight.SemiBold, color = Color(0xFFFAFAFA)),
    val h3: TextStyle = TextStyle(fontSize = 15.sp, fontWeight = FontWeight.SemiBold, color = Color(0xFFFAFAFA)),
    val body: TextStyle = TextStyle(fontSize = 14.sp, fontWeight = FontWeight.Normal, color = Color(0xFFFAFAFA)),
    val bodyMuted: TextStyle = TextStyle(fontSize = 13.sp, fontWeight = FontWeight.Normal, color = Color(0xFFA1A1AA)),
    val caption: TextStyle = TextStyle(fontSize = 12.sp, fontWeight = FontWeight.Medium, color = Color(0xFFA1A1AA)),
    val code: TextStyle = TextStyle(fontFamily = FontFamily.Monospace, fontSize = 12.sp, color = Color(0xFFFAFAFA))
)

data class ShadcnShapes(
    val radiusSmall: Dp = 6.dp,
    val radiusMedium: Dp = 8.dp,
    val radiusLarge: Dp = 12.dp
)

val LocalShadcnColors = staticCompositionLocalOf { ShadcnColors() }
val LocalShadcnTypography = staticCompositionLocalOf { ShadcnTypography() }
val LocalShadcnShapes = staticCompositionLocalOf { ShadcnShapes() }

object ShadcnTheme {
    val colors: ShadcnColors
        @Composable
        get() = LocalShadcnColors.current

    val typography: ShadcnTypography
        @Composable
        get() = LocalShadcnTypography.current

    val shapes: ShadcnShapes
        @Composable
        get() = LocalShadcnShapes.current
}
