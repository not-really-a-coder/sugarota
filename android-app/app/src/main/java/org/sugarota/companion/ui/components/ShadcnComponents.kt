package org.sugarota.companion.ui.components

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.VisualTransformation
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import org.sugarota.companion.ui.theme.ShadcnTheme

enum class ShadcnButtonVariant {
    DEFAULT,
    SECONDARY,
    OUTLINE,
    GHOST,
    DESTRUCTIVE
}

@Composable
fun ShadcnCard(
    modifier: Modifier = Modifier,
    content: @Composable ColumnScope.() -> Unit
) {
    val colors = ShadcnTheme.colors
    val shapes = ShadcnTheme.shapes
    Column(
        modifier = modifier
            .clip(RoundedCornerShape(shapes.radiusLarge))
            .background(colors.card)
            .border(BorderStroke(1.dp, colors.border), RoundedCornerShape(shapes.radiusLarge))
            .padding(16.dp),
        content = content
    )
}

@Composable
fun ShadcnButton(
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    variant: ShadcnButtonVariant = ShadcnButtonVariant.DEFAULT,
    enabled: Boolean = true,
    isLoading: Boolean = false,
    content: @Composable RowScope.() -> Unit
) {
    val colors = ShadcnTheme.colors
    val shapes = ShadcnTheme.shapes

    val (bgColor, contentColor, borderStroke) = when (variant) {
        ShadcnButtonVariant.DEFAULT -> Triple(colors.primary, colors.primaryForeground, null)
        ShadcnButtonVariant.SECONDARY -> Triple(colors.secondary, colors.secondaryForeground, null)
        ShadcnButtonVariant.OUTLINE -> Triple(Color.Transparent, colors.foreground, BorderStroke(1.dp, colors.border))
        ShadcnButtonVariant.GHOST -> Triple(Color.Transparent, colors.foreground, null)
        ShadcnButtonVariant.DESTRUCTIVE -> Triple(colors.destructive, colors.destructiveForeground, null)
    }

    val actualBg = if (!enabled) bgColor.copy(alpha = 0.5f) else bgColor
    val actualContent = if (!enabled) contentColor.copy(alpha = 0.5f) else contentColor

    val borderModifier = if (borderStroke != null) {
        Modifier.border(borderStroke, RoundedCornerShape(shapes.radiusMedium))
    } else Modifier

    Row(
        modifier = modifier
            .clip(RoundedCornerShape(shapes.radiusMedium))
            .background(actualBg)
            .then(borderModifier)
            .clickable(enabled = enabled && !isLoading, onClick = onClick)
            .padding(horizontal = 14.dp, vertical = 9.dp),
        horizontalArrangement = Arrangement.Center,
        verticalAlignment = Alignment.CenterVertically
    ) {
        if (isLoading) {
            CircularProgressIndicator(
                modifier = Modifier.size(16.dp),
                color = actualContent,
                strokeWidth = 2.dp
            )
            Spacer(modifier = Modifier.width(8.dp))
        }
        content()
    }
}

@Composable
fun ShadcnBadge(
    text: String,
    modifier: Modifier = Modifier,
    variant: ShadcnButtonVariant = ShadcnButtonVariant.DEFAULT
) {
    val colors = ShadcnTheme.colors
    val shapes = ShadcnTheme.shapes

    val (bgColor, contentColor, borderStroke) = when (variant) {
        ShadcnButtonVariant.DEFAULT -> Triple(colors.primary.copy(alpha = 0.15f), colors.primary, BorderStroke(1.dp, colors.primary.copy(alpha = 0.3f)))
        ShadcnButtonVariant.SECONDARY -> Triple(colors.secondary, colors.mutedForeground, BorderStroke(1.dp, colors.border))
        ShadcnButtonVariant.DESTRUCTIVE -> Triple(colors.destructive.copy(alpha = 0.15f), colors.destructive, BorderStroke(1.dp, colors.destructive.copy(alpha = 0.3f)))
        else -> Triple(colors.card, colors.foreground, BorderStroke(1.dp, colors.border))
    }

    Box(
        modifier = modifier
            .clip(RoundedCornerShape(shapes.radiusSmall))
            .background(bgColor)
            .border(borderStroke, RoundedCornerShape(shapes.radiusSmall))
            .padding(horizontal = 8.dp, vertical = 3.dp),
        contentAlignment = Alignment.Center
    ) {
        Text(
            text = text,
            color = contentColor,
            fontSize = 11.sp,
            fontWeight = FontWeight.SemiBold
        )
    }
}

@Composable
fun ShadcnInput(
    value: String,
    onValueChange: (String) -> Unit,
    label: String,
    modifier: Modifier = Modifier,
    placeholder: String = "",
    readOnly: Boolean = false,
    isMonospace: Boolean = false,
    minHeight: androidx.compose.ui.unit.Dp = 42.dp,
    visualTransformation: VisualTransformation = VisualTransformation.None,
    trailingIcon: @Composable (() -> Unit)? = null
) {
    val colors = ShadcnTheme.colors
    val shapes = ShadcnTheme.shapes

    Column(modifier = modifier) {
        if (label.isNotBlank()) {
            Text(
                text = label,
                fontSize = 12.sp,
                fontWeight = FontWeight.Medium,
                color = colors.mutedForeground
            )
            Spacer(modifier = Modifier.height(4.dp))
        }
        Box(
            modifier = Modifier
                .fillMaxWidth()
                .heightIn(min = minHeight)
                .clip(RoundedCornerShape(shapes.radiusMedium))
                .background(colors.background)
                .border(BorderStroke(1.dp, colors.inputBorder), RoundedCornerShape(shapes.radiusMedium))
                .padding(start = 12.dp, end = if (trailingIcon != null) 4.dp else 12.dp, top = 2.dp, bottom = 2.dp),
            contentAlignment = Alignment.CenterStart
        ) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically
            ) {
                Box(
                    modifier = Modifier.weight(1f),
                    contentAlignment = Alignment.CenterStart
                ) {
                    if (value.isEmpty() && placeholder.isNotEmpty()) {
                        Text(
                            text = placeholder,
                            color = colors.mutedForeground.copy(alpha = 0.6f),
                            fontSize = 13.sp
                        )
                    }
                    BasicTextField(
                        value = value,
                        onValueChange = onValueChange,
                        readOnly = readOnly,
                        cursorBrush = SolidColor(colors.primary),
                        visualTransformation = visualTransformation,
                        textStyle = if (isMonospace) {
                            ShadcnTheme.typography.code.copy(color = colors.foreground, fontSize = 12.sp)
                        } else {
                            TextStyle(color = colors.foreground, fontSize = 13.sp)
                        },
                        modifier = Modifier
                            .fillMaxWidth()
                            .padding(vertical = 8.dp)
                    )
                }
                if (trailingIcon != null) {
                    trailingIcon()
                }
            }
        }
    }
}

@Composable
fun ShadcnChip(
    selected: Boolean,
    onClick: () -> Unit,
    label: String,
    modifier: Modifier = Modifier
) {
    val colors = ShadcnTheme.colors
    val shapes = ShadcnTheme.shapes

    val (bgColor, textColor, borderStroke) = if (selected) {
        Triple(colors.primary.copy(alpha = 0.15f), colors.primary, BorderStroke(1.dp, colors.primary))
    } else {
        Triple(colors.secondary, colors.mutedForeground, BorderStroke(1.dp, colors.border))
    }

    Box(
        modifier = modifier
            .clip(RoundedCornerShape(shapes.radiusMedium))
            .background(bgColor)
            .border(borderStroke, RoundedCornerShape(shapes.radiusMedium))
            .clickable(onClick = onClick)
            .padding(horizontal = 14.dp, vertical = 7.dp),
        contentAlignment = Alignment.Center
    ) {
        Text(
            text = label,
            color = textColor,
            fontSize = 13.sp,
            fontWeight = if (selected) FontWeight.Bold else FontWeight.Medium
        )
    }
}
