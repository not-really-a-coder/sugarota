package org.sugarota.companion.ui

import androidx.activity.compose.BackHandler
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.Check
import androidx.compose.material.icons.filled.Info
import androidx.compose.material.icons.filled.Notifications
import androidx.compose.material.icons.filled.Visibility
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import org.sugarota.companion.data.AppNotificationSettings
import org.sugarota.companion.data.AppSettingsPreferences
import org.sugarota.companion.data.NotificationImportanceLevel
import org.sugarota.companion.data.NotificationLockScreenVisibility
import org.sugarota.companion.service.SugarotaBleService
import org.sugarota.companion.ui.theme.ShadcnTheme

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun AppSettingsScreen(
    service: SugarotaBleService?,
    onDismiss: () -> Unit
) {
    BackHandler(onBack = onDismiss)

    val context = LocalContext.current
    val appSettingsPrefs = remember(context) { AppSettingsPreferences(context) }
    var settings by remember { mutableStateOf(appSettingsPrefs.loadSettings()) }

    val colors = ShadcnTheme.colors
    val typography = ShadcnTheme.typography

    Scaffold(
        containerColor = colors.background,
        topBar = {
            TopAppBar(
                title = {
                    Text(
                        text = "App Settings",
                        style = typography.h2,
                        color = colors.foreground
                    )
                },
                navigationIcon = {
                    IconButton(onClick = onDismiss) {
                        Icon(
                            imageVector = Icons.AutoMirrored.Filled.ArrowBack,
                            contentDescription = "Back",
                            tint = colors.foreground
                        )
                    }
                },
                colors = TopAppBarDefaults.topAppBarColors(
                    containerColor = colors.background,
                    titleContentColor = colors.foreground
                )
            )
        }
    ) { padding ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(padding)
                .verticalScroll(rememberScrollState())
                .padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(20.dp)
        ) {
            // Section: Lock Screen Expansion Hint / Explanation
            Card(
                shape = RoundedCornerShape(ShadcnTheme.shapes.radiusMedium),
                colors = CardDefaults.cardColors(containerColor = colors.card),
                border = BorderStroke(1.dp, colors.border),
                modifier = Modifier.fillMaxWidth()
            ) {
                Row(
                    modifier = Modifier.padding(14.dp),
                    verticalAlignment = Alignment.Top
                ) {
                    Icon(
                        imageVector = Icons.Default.Info,
                        contentDescription = null,
                        tint = colors.primary,
                        modifier = Modifier.size(20.dp).padding(top = 2.dp)
                    )
                    Spacer(modifier = Modifier.width(10.dp))
                    Text(
                        text = "For Android to expand the notification chart automatically on the lock screen, set visibility to 'Show all content' and importance to 'High'. Note that Android expands only the top-most notification on the lock screen.",
                        style = typography.caption,
                        color = colors.mutedForeground,
                        lineHeight = 18.sp
                    )
                }
            }

            // Section 1: Lock Screen Visibility
            Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Icon(
                        imageVector = Icons.Default.Visibility,
                        contentDescription = null,
                        tint = colors.primary,
                        modifier = Modifier.size(18.dp)
                    )
                    Spacer(modifier = Modifier.width(8.dp))
                    Text(
                        text = "Lock Screen Visibility",
                        style = typography.h3,
                        color = colors.foreground
                    )
                }
                Text(
                    text = "Controls how much information is shown when the device is locked.",
                    style = typography.caption,
                    color = colors.mutedForeground
                )

                Spacer(modifier = Modifier.height(4.dp))

                NotificationLockScreenVisibility.values().forEach { option ->
                    val isSelected = settings.visibility == option
                    Card(
                        shape = RoundedCornerShape(ShadcnTheme.shapes.radiusMedium),
                        colors = CardDefaults.cardColors(
                            containerColor = if (isSelected) colors.card else colors.background
                        ),
                        border = BorderStroke(
                            1.dp,
                            if (isSelected) colors.primary else colors.border
                        ),
                        modifier = Modifier
                            .fillMaxWidth()
                            .clickable {
                                val updated = settings.copy(visibility = option)
                                settings = updated
                                appSettingsPrefs.saveSettings(updated)
                                service?.applyNotificationSettings(updated)
                            }
                    ) {
                        Row(
                            modifier = Modifier
                                .fillMaxWidth()
                                .padding(14.dp),
                            verticalAlignment = Alignment.CenterVertically,
                            horizontalArrangement = Arrangement.SpaceBetween
                        ) {
                            Column(modifier = Modifier.weight(1f)) {
                                Text(
                                    text = option.title,
                                    style = typography.body.copy(fontWeight = FontWeight.SemiBold),
                                    color = colors.foreground
                                )
                                Spacer(modifier = Modifier.height(2.dp))
                                Text(
                                    text = option.description,
                                    style = typography.caption,
                                    color = colors.mutedForeground
                                )
                            }
                            if (isSelected) {
                                Icon(
                                    imageVector = Icons.Default.Check,
                                    contentDescription = "Selected",
                                    tint = colors.primary,
                                    modifier = Modifier.size(20.dp)
                                )
                            }
                        }
                    }
                }
            }

            // Section 2: Notification Importance / Priority
            Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Icon(
                        imageVector = Icons.Default.Notifications,
                        contentDescription = null,
                        tint = colors.primary,
                        modifier = Modifier.size(18.dp)
                    )
                    Spacer(modifier = Modifier.width(8.dp))
                    Text(
                        text = "Notification Importance",
                        style = typography.h3,
                        color = colors.foreground
                    )
                }
                Text(
                    text = "High importance ranks the notification at the top of the lock screen without playing sound.",
                    style = typography.caption,
                    color = colors.mutedForeground
                )

                Spacer(modifier = Modifier.height(4.dp))

                NotificationImportanceLevel.values().forEach { option ->
                    val isSelected = settings.importance == option
                    Card(
                        shape = RoundedCornerShape(ShadcnTheme.shapes.radiusMedium),
                        colors = CardDefaults.cardColors(
                            containerColor = if (isSelected) colors.card else colors.background
                        ),
                        border = BorderStroke(
                            1.dp,
                            if (isSelected) colors.primary else colors.border
                        ),
                        modifier = Modifier
                            .fillMaxWidth()
                            .clickable {
                                val updated = settings.copy(importance = option)
                                settings = updated
                                appSettingsPrefs.saveSettings(updated)
                                service?.applyNotificationSettings(updated)
                            }
                    ) {
                        Row(
                            modifier = Modifier
                                .fillMaxWidth()
                                .padding(14.dp),
                            verticalAlignment = Alignment.CenterVertically,
                            horizontalArrangement = Arrangement.SpaceBetween
                        ) {
                            Column(modifier = Modifier.weight(1f)) {
                                Text(
                                    text = option.title,
                                    style = typography.body.copy(fontWeight = FontWeight.SemiBold),
                                    color = colors.foreground
                                )
                                Spacer(modifier = Modifier.height(2.dp))
                                Text(
                                    text = option.description,
                                    style = typography.caption,
                                    color = colors.mutedForeground
                                )
                            }
                            if (isSelected) {
                                Icon(
                                    imageVector = Icons.Default.Check,
                                    contentDescription = "Selected",
                                    tint = colors.primary,
                                    modifier = Modifier.size(20.dp)
                                )
                            }
                        }
                    }
                }
            }
        }
    }
}
