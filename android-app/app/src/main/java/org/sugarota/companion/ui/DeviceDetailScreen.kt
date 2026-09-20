package org.sugarota.companion.ui

import androidx.activity.compose.BackHandler
import androidx.compose.animation.AnimatedVisibility
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
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.foundation.text.selection.SelectionContainer
import androidx.compose.ui.platform.LocalClipboardManager
import androidx.compose.ui.text.AnnotatedString
import androidx.compose.ui.text.font.FontFamily
import kotlinx.coroutines.launch
import org.sugarota.companion.DeviceConfigScreen
import org.sugarota.companion.TrendArrowIcon
import org.sugarota.companion.getGlucoseColor
import org.sugarota.companion.model.SugarotaDevice
import org.sugarota.companion.service.SugarotaBleService
import org.sugarota.companion.ui.components.*
import org.sugarota.companion.ui.theme.ShadcnTheme

enum class DeviceScreenTab {
    CHART,
    CONFIG,
    LOGS
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun DeviceDetailScreen(
    deviceAddress: String,
    initialTab: DeviceScreenTab = DeviceScreenTab.CHART,
    service: SugarotaBleService?,
    onDismiss: () -> Unit
) {
    BackHandler(onBack = onDismiss)

    val devicesMap by service?.devices?.collectAsState() ?: remember { mutableStateOf(emptyMap()) }
    val device = devicesMap[deviceAddress] ?: SugarotaDevice(
        name = service?.getDefaultDeviceName(deviceAddress) ?: "Sugarota Device",
        address = deviceAddress
    )

    // Return to main screen automatically when the device is disconnected (e.g. rebooting or powering off)
    LaunchedEffect(device.isConnected) {
        if (!device.isConnected) {
            onDismiss()
        }
    }

    val lastReading by service?.lastReading?.collectAsState() ?: remember { mutableStateOf(null) }
    var currentTab by remember { mutableStateOf(initialTab) }

    val colors = ShadcnTheme.colors
    val typography = ShadcnTheme.typography

    // Custom device name rename dialog
    var customName by remember { mutableStateOf(device.name) }
    var showRenameDialog by remember { mutableStateOf(false) }
    var showFirmwareUpdate by remember { mutableStateOf(false) }
    var editNameText by remember { mutableStateOf(customName) }

    if (showRenameDialog) {
        androidx.compose.ui.window.Dialog(
            onDismissRequest = { showRenameDialog = false },
            properties = androidx.compose.ui.window.DialogProperties(usePlatformDefaultWidth = false)
        ) {
            Box(
                modifier = Modifier
                    .fillMaxSize()
                    .background(Color.Black.copy(alpha = 0.75f))
                    .padding(24.dp),
                contentAlignment = Alignment.Center
            ) {
                Column(
                    modifier = Modifier
                        .fillMaxWidth()
                        .clip(RoundedCornerShape(ShadcnTheme.shapes.radiusLarge))
                        .background(colors.card)
                        .border(
                            BorderStroke(1.dp, colors.border),
                            RoundedCornerShape(ShadcnTheme.shapes.radiusLarge)
                        )
                        .padding(20.dp)
                ) {
                    Text(
                        text = "Rename Device",
                        style = typography.h2,
                        color = colors.foreground
                    )
                    Spacer(modifier = Modifier.height(16.dp))

                    val defaultName = service?.getDefaultDeviceName(deviceAddress) ?: "Sugarota"
                    ShadcnInput(
                        value = editNameText,
                        onValueChange = { editNameText = it },
                        label = "Device Name",
                        placeholder = defaultName
                    )

                    Spacer(modifier = Modifier.height(20.dp))

                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.End,
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        ShadcnButton(
                            onClick = { showRenameDialog = false },
                            variant = ShadcnButtonVariant.GHOST
                        ) {
                            Text("Cancel", style = typography.body, color = colors.mutedForeground)
                        }
                        Spacer(modifier = Modifier.width(8.dp))
                        ShadcnButton(
                            onClick = {
                                val trimmed = editNameText.trim()
                                val defaultDeviceName = service?.getDefaultDeviceName(deviceAddress) ?: "Sugarota"
                                val finalName = if (trimmed.isBlank()) defaultDeviceName else trimmed
                                customName = finalName
                                service?.setDeviceCustomName(deviceAddress, if (trimmed.isBlank()) "" else finalName)
                                showRenameDialog = false
                            },
                            variant = ShadcnButtonVariant.DEFAULT
                        ) {
                            Text(
                                text = "Save",
                                style = typography.body,
                                fontWeight = FontWeight.Bold,
                                color = colors.primaryForeground
                            )
                        }
                    }
                }
            }
        }
    }

    if (showFirmwareUpdate) {
        FirmwareUpdateScreen(
            device = device,
            service = service,
            onDismiss = { showFirmwareUpdate = false }
        )
        return
    }

    Scaffold(
        containerColor = colors.background,
        topBar = {
            TopAppBar(
                title = {
                    Column {
                        Row(verticalAlignment = Alignment.CenterVertically) {
                            Text(
                                text = customName.ifBlank { device.name },
                                style = typography.h2,
                                color = colors.foreground
                            )
                            Spacer(modifier = Modifier.width(6.dp))
                            IconButton(
                                onClick = {
                                    editNameText = customName.ifBlank { device.name }
                                    showRenameDialog = true
                                },
                                modifier = Modifier.size(28.dp)
                            ) {
                                Icon(
                                    imageVector = Icons.Default.Edit,
                                    contentDescription = "Rename Device",
                                    tint = colors.mutedForeground,
                                    modifier = Modifier.size(17.dp)
                                )
                            }
                        }
                        Text(
                            text = deviceAddress,
                            style = typography.caption,
                            color = colors.mutedForeground
                        )
                    }
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
                actions = {},
                colors = TopAppBarDefaults.topAppBarColors(
                    containerColor = colors.card,
                    titleContentColor = colors.foreground,
                    navigationIconContentColor = colors.foreground
                )
            )
        },
        bottomBar = {
            // Persistent bottom switcher between Chart and Config screens
            Surface(
                color = colors.background,
                tonalElevation = 0.dp,
                shadowElevation = 0.dp
            ) {
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(horizontal = 16.dp, vertical = 12.dp),
                    horizontalArrangement = Arrangement.Center
                ) {
                    Box(
                        modifier = Modifier
                            .fillMaxWidth()
                            .height(42.dp)
                            .clip(RoundedCornerShape(ShadcnTheme.shapes.radiusMedium))
                            .background(colors.card) // zinc-900 surface
                            .border(
                                width = 1.dp,
                                color = colors.border, // zinc-800 subtle outline
                                shape = RoundedCornerShape(ShadcnTheme.shapes.radiusMedium)
                            )
                            .padding(3.dp)
                    ) {
                        Row(modifier = Modifier.fillMaxSize()) {
                            // Chart Tab Button
                            val isChartSelected = currentTab == DeviceScreenTab.CHART
                            Box(
                                modifier = Modifier
                                    .weight(1f)
                                    .fillMaxHeight()
                                    .clip(RoundedCornerShape(6.dp))
                                    .background(if (isChartSelected) colors.secondary else Color.Transparent)
                                    .clickable { currentTab = DeviceScreenTab.CHART },
                                contentAlignment = Alignment.Center
                            ) {
                                Row(verticalAlignment = Alignment.CenterVertically) {
                                    Icon(
                                        imageVector = Icons.Default.ShowChart,
                                        contentDescription = null,
                                        tint = if (isChartSelected) colors.foreground else colors.mutedForeground,
                                        modifier = Modifier.size(16.dp)
                                    )
                                    Spacer(modifier = Modifier.width(6.dp))
                                    Text(
                                        text = "Chart",
                                        style = typography.caption.copy(
                                            fontWeight = if (isChartSelected) FontWeight.SemiBold else FontWeight.Normal
                                        ),
                                        color = if (isChartSelected) colors.foreground else colors.mutedForeground
                                    )
                                }
                            }

                            // Config Tab Button
                            val isConfigSelected = currentTab == DeviceScreenTab.CONFIG
                            Box(
                                modifier = Modifier
                                    .weight(1f)
                                    .fillMaxHeight()
                                    .clip(RoundedCornerShape(6.dp))
                                    .background(if (isConfigSelected) colors.secondary else Color.Transparent)
                                    .clickable { currentTab = DeviceScreenTab.CONFIG },
                                contentAlignment = Alignment.Center
                            ) {
                                Row(verticalAlignment = Alignment.CenterVertically) {
                                    Icon(
                                        imageVector = Icons.Default.Settings,
                                        contentDescription = null,
                                        tint = if (isConfigSelected) colors.foreground else colors.mutedForeground,
                                        modifier = Modifier.size(16.dp)
                                    )
                                    Spacer(modifier = Modifier.width(6.dp))
                                    Text(
                                        text = "Config",
                                        style = typography.caption.copy(
                                            fontWeight = if (isConfigSelected) FontWeight.SemiBold else FontWeight.Normal
                                        ),
                                        color = if (isConfigSelected) colors.foreground else colors.mutedForeground
                                    )
                                }
                            }

                            // Logs Tab Button (Terminal / Verbose debug logs)
                            val isLogsSelected = currentTab == DeviceScreenTab.LOGS
                            Box(
                                modifier = Modifier
                                    .weight(1f)
                                    .fillMaxHeight()
                                    .clip(RoundedCornerShape(6.dp))
                                    .background(if (isLogsSelected) colors.secondary else Color.Transparent)
                                    .clickable { currentTab = DeviceScreenTab.LOGS },
                                contentAlignment = Alignment.Center
                            ) {
                                Row(verticalAlignment = Alignment.CenterVertically) {
                                    Icon(
                                        imageVector = Icons.Default.Code,
                                        contentDescription = null,
                                        tint = if (isLogsSelected) colors.foreground else colors.mutedForeground,
                                        modifier = Modifier.size(16.dp)
                                    )
                                    Spacer(modifier = Modifier.width(6.dp))
                                    Text(
                                        text = "Logs",
                                        style = typography.caption.copy(
                                            fontWeight = if (isLogsSelected) FontWeight.SemiBold else FontWeight.Normal
                                        ),
                                        color = if (isLogsSelected) colors.foreground else colors.mutedForeground
                                    )
                                }
                            }
                        }
                    }
                }
            }
        }
    ) { padding ->
        Box(
            modifier = Modifier
                .fillMaxSize()
                .padding(padding)
        ) {
            when (currentTab) {
                DeviceScreenTab.CHART -> {
                    DeviceChartContent(
                        device = device,
                        lastReading = lastReading,
                        service = service,
                        onOpenFirmwareUpdate = { showFirmwareUpdate = true }
                    )
                }
                DeviceScreenTab.CONFIG -> {
                    DeviceConfigScreen(
                        deviceAddress = deviceAddress,
                        deviceName = customName.ifBlank { device.name },
                        service = service,
                        showHeader = false,
                        onOpenFirmwareUpdate = { showFirmwareUpdate = true },
                        onDismiss = onDismiss
                    )
                }
                DeviceScreenTab.LOGS -> {
                    DeviceLogsScreen(
                        device = device,
                        service = service
                    )
                }
            }
        }
    }
}

@Composable
fun DeviceChartContent(
    device: SugarotaDevice,
    lastReading: org.sugarota.companion.model.GlucoseData?,
    service: SugarotaBleService?,
    onOpenFirmwareUpdate: () -> Unit = {}
) {
    val colors = ShadcnTheme.colors
    val typography = ShadcnTheme.typography

    // Readings list for the chart: use history from lastReading, or fallback with the current single reading
    val historyData = remember(lastReading) {
        if (lastReading != null) {
            if (lastReading.history.isNotEmpty()) {
                lastReading.history
            } else {
                listOf(lastReading)
            }
        } else {
            emptyList()
        }
    }

    val units = lastReading?.units ?: "mg/dL"

    Column(
        modifier = Modifier
            .fillMaxSize()
            .padding(16.dp)
    ) {
        // Glucose Value One-liner above the chart
        if (lastReading != null) {
            val deltaFormatted = "${if (lastReading.delta > 0) "+" else ""}${lastReading.delta}"
            val bgCol = getGlucoseColor(lastReading.sgv)
            val timeStr = java.text.SimpleDateFormat("HH:mm", java.util.Locale.getDefault())
                .format(java.util.Date(lastReading.timestamp * 1000))
            val minsAgo = ((System.currentTimeMillis() / 1000 - lastReading.timestamp) / 60).coerceAtLeast(0)

            Row(
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text(
                    text = "${lastReading.sgv}",
                    fontSize = 28.sp,
                    fontWeight = FontWeight.Bold,
                    color = bgCol
                )
                Spacer(modifier = Modifier.width(6.dp))
                TrendArrowIcon(
                    direction = lastReading.direction,
                    tint = bgCol
                )
                Spacer(modifier = Modifier.width(12.dp))
                Text(
                    text = "$deltaFormatted ${lastReading.units}",
                    fontSize = 20.sp,
                    fontWeight = FontWeight.SemiBold,
                    color = colors.foreground
                )
                Spacer(modifier = Modifier.weight(1f))
                Text(
                    text = "$timeStr ($minsAgo min ago)",
                    style = typography.caption,
                    color = colors.mutedForeground
                )
            }
            Spacer(modifier = Modifier.height(10.dp))
        }

        // Upper Part: Glucose Chart Area (30% of vertical height)
        Box(
            modifier = Modifier
                .fillMaxWidth()
                .weight(0.30f)
        ) {
            GlucoseChartView(
                history = historyData,
                units = units,
                isDarkTheme = true,
                modifier = Modifier.fillMaxSize()
            )
        }

        Spacer(modifier = Modifier.height(14.dp))

        // Lower Part: Controls Area (70% of vertical height)
        Box(
            modifier = Modifier
                .fillMaxWidth()
                .weight(0.70f)
        ) {
            ShadcnCard(
                modifier = Modifier.fillMaxSize()
            ) {
                // Confirmation Dialog States
                var showResetDialog by remember { mutableStateOf(false) }
                var showPowerOffDialog by remember { mutableStateOf(false) }
                var isFindingDevice by remember { mutableStateOf(false) }
                val coroutineScope = rememberCoroutineScope()

                // Reset Confirmation Dialog
                if (showResetDialog) {
                    androidx.compose.ui.window.Dialog(
                        onDismissRequest = { showResetDialog = false },
                        properties = androidx.compose.ui.window.DialogProperties(usePlatformDefaultWidth = false)
                    ) {
                        Box(
                            modifier = Modifier
                                .fillMaxSize()
                                .background(Color.Black.copy(alpha = 0.75f))
                                .padding(24.dp),
                            contentAlignment = Alignment.Center
                        ) {
                            Column(
                                modifier = Modifier
                                    .fillMaxWidth()
                                    .clip(RoundedCornerShape(ShadcnTheme.shapes.radiusLarge))
                                    .background(colors.card)
                                    .border(
                                        BorderStroke(1.dp, colors.border),
                                        RoundedCornerShape(ShadcnTheme.shapes.radiusLarge)
                                    )
                                    .padding(20.dp)
                            ) {
                                Text(
                                    text = "Restart Device",
                                    style = typography.h2,
                                    color = colors.foreground
                                )
                                Spacer(modifier = Modifier.height(10.dp))
                                Text(
                                    text = "Are you sure you want to reboot ${device.name}? The device will restart and disconnect momentarily.",
                                    style = typography.body,
                                    color = colors.mutedForeground
                                )
                                Spacer(modifier = Modifier.height(20.dp))
                                Row(
                                    modifier = Modifier.fillMaxWidth(),
                                    horizontalArrangement = Arrangement.End,
                                    verticalAlignment = Alignment.CenterVertically
                                ) {
                                    ShadcnButton(
                                        onClick = { showResetDialog = false },
                                        variant = ShadcnButtonVariant.GHOST
                                    ) {
                                        Text("Cancel", style = typography.body, color = colors.mutedForeground)
                                    }
                                    Spacer(modifier = Modifier.width(8.dp))
                                    ShadcnButton(
                                        onClick = {
                                            showResetDialog = false
                                            service?.rebootDevice(device.address)
                                        },
                                        variant = ShadcnButtonVariant.SECONDARY
                                    ) {
                                        Text(
                                            text = "Restart",
                                            style = typography.body,
                                            fontWeight = FontWeight.Bold,
                                            color = colors.foreground
                                        )
                                    }
                                }
                            }
                        }
                    }
                }

                // Power Off Confirmation Dialog
                if (showPowerOffDialog) {
                    androidx.compose.ui.window.Dialog(
                        onDismissRequest = { showPowerOffDialog = false },
                        properties = androidx.compose.ui.window.DialogProperties(usePlatformDefaultWidth = false)
                    ) {
                        Box(
                            modifier = Modifier
                                .fillMaxSize()
                                .background(Color.Black.copy(alpha = 0.75f))
                                .padding(24.dp),
                            contentAlignment = Alignment.Center
                        ) {
                            Column(
                                modifier = Modifier
                                    .fillMaxWidth()
                                    .clip(RoundedCornerShape(ShadcnTheme.shapes.radiusLarge))
                                    .background(colors.card)
                                    .border(
                                        BorderStroke(1.dp, colors.destructive.copy(alpha = 0.4f)),
                                        RoundedCornerShape(ShadcnTheme.shapes.radiusLarge)
                                    )
                                    .padding(20.dp)
                            ) {
                                Text(
                                    text = "Turn Off Device",
                                    style = typography.h2,
                                    color = colors.destructive
                                )
                                Spacer(modifier = Modifier.height(10.dp))
                                Text(
                                    text = "Are you sure you want to shut down ${device.name}? To turn it back on, press and hold the hardware power button.",
                                    style = typography.body,
                                    color = colors.mutedForeground
                                )
                                Spacer(modifier = Modifier.height(20.dp))
                                Row(
                                    modifier = Modifier.fillMaxWidth(),
                                    horizontalArrangement = Arrangement.End,
                                    verticalAlignment = Alignment.CenterVertically
                                ) {
                                    ShadcnButton(
                                        onClick = { showPowerOffDialog = false },
                                        variant = ShadcnButtonVariant.GHOST
                                    ) {
                                        Text("Cancel", style = typography.body, color = colors.mutedForeground)
                                    }
                                    Spacer(modifier = Modifier.width(8.dp))
                                    ShadcnButton(
                                        onClick = {
                                            showPowerOffDialog = false
                                            service?.powerOffDevice(device.address)
                                        },
                                        variant = ShadcnButtonVariant.DESTRUCTIVE
                                    ) {
                                        Text(
                                            text = "Turn Off",
                                            style = typography.body,
                                            fontWeight = FontWeight.Bold,
                                            color = colors.destructiveForeground
                                        )
                                    }
                                }
                            }
                        }
                    }
                }

                // Main Controls Column
                Column(
                    modifier = Modifier
                        .fillMaxSize()
                        .verticalScroll(rememberScrollState())
                ) {
                    // Header with device info badges
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceBetween,
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Row(verticalAlignment = Alignment.CenterVertically) {
                            Icon(
                                imageVector = Icons.Default.Tune,
                                contentDescription = null,
                                tint = colors.primary,
                                modifier = Modifier.size(18.dp)
                            )
                            Spacer(modifier = Modifier.width(8.dp))
                            Text(
                                text = "Device Controls",
                                style = typography.h3,
                                color = colors.foreground,
                                fontWeight = FontWeight.SemiBold
                            )
                        }

                        Row(
                            horizontalArrangement = Arrangement.spacedBy(8.dp),
                            verticalAlignment = Alignment.CenterVertically
                        ) {
                            ShadcnBadge(
                                text = "${device.status.batteryPct}%${if (device.status.isCharging) "(+)" else ""}",
                                variant = ShadcnButtonVariant.SECONDARY
                            )
                            ShadcnBadge(
                                text = device.status.version,
                                variant = ShadcnButtonVariant.SECONDARY,
                                modifier = Modifier.clickable(onClick = onOpenFirmwareUpdate)
                            )
                        }
                    }

                    Spacer(modifier = Modifier.height(16.dp))

                    // 1. Brightness Section (levels 76, 153, 204, 255)
                    val brightnessLevels = listOf(
                        Pair(76, "30%"),
                        Pair(153, "60%"),
                        Pair(204, "80%"),
                        Pair(255, "100%")
                    )
                    val currentBrightness = device.status.brightness

                    Text(
                        text = "Brightness",
                        style = typography.caption.copy(fontWeight = FontWeight.Medium),
                        color = colors.mutedForeground
                    )
                    Spacer(modifier = Modifier.height(8.dp))
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.spacedBy(8.dp)
                    ) {
                        brightnessLevels.forEach { (level, label) ->
                            val isSelected = currentBrightness == level
                            Box(
                                modifier = Modifier
                                    .weight(1f)
                                    .height(38.dp)
                                    .clip(RoundedCornerShape(ShadcnTheme.shapes.radiusMedium))
                                    .background(if (isSelected) colors.primary.copy(alpha = 0.15f) else colors.secondary)
                                    .border(
                                        BorderStroke(
                                            1.dp,
                                            if (isSelected) colors.primary else colors.border
                                        ),
                                        RoundedCornerShape(ShadcnTheme.shapes.radiusMedium)
                                    )
                                    .clickable {
                                        service?.setDeviceBrightness(device.address, level)
                                    },
                                contentAlignment = Alignment.Center
                            ) {
                                Row(
                                    verticalAlignment = Alignment.CenterVertically,
                                    horizontalArrangement = Arrangement.Center
                                ) {
                                    Icon(
                                        imageVector = Icons.Default.BrightnessMedium,
                                        contentDescription = null,
                                        tint = if (isSelected) colors.primary else colors.mutedForeground,
                                        modifier = Modifier.size(14.dp)
                                    )
                                    Spacer(modifier = Modifier.width(4.dp))
                                    Text(
                                        text = label,
                                        style = typography.caption.copy(
                                            fontWeight = if (isSelected) FontWeight.Bold else FontWeight.Normal
                                        ),
                                        color = if (isSelected) colors.primary else colors.foreground
                                    )
                                }
                            }
                        }
                    }

                    Spacer(modifier = Modifier.height(16.dp))

                    // 2. Theme Selection Section (Dark / Light)
                    val isDark = device.status.isDarkTheme
                    Text(
                        text = "Display Theme",
                        style = typography.caption.copy(fontWeight = FontWeight.Medium),
                        color = colors.mutedForeground
                    )
                    Spacer(modifier = Modifier.height(8.dp))
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.spacedBy(8.dp)
                    ) {
                        // Dark Theme Pill
                        Box(
                            modifier = Modifier
                                .weight(1f)
                                .height(38.dp)
                                .clip(RoundedCornerShape(ShadcnTheme.shapes.radiusMedium))
                                .background(if (isDark) colors.primary.copy(alpha = 0.15f) else colors.secondary)
                                .border(
                                    BorderStroke(
                                        1.dp,
                                        if (isDark) colors.primary else colors.border
                                    ),
                                    RoundedCornerShape(ShadcnTheme.shapes.radiusMedium)
                                )
                                .clickable {
                                    service?.setDeviceTheme(device.address, true)
                                },
                            contentAlignment = Alignment.Center
                        ) {
                            Row(verticalAlignment = Alignment.CenterVertically) {
                                Icon(
                                    imageVector = Icons.Default.DarkMode,
                                    contentDescription = null,
                                    tint = if (isDark) colors.primary else colors.mutedForeground,
                                    modifier = Modifier.size(15.dp)
                                )
                                Spacer(modifier = Modifier.width(6.dp))
                                Text(
                                    text = "Dark Theme",
                                    style = typography.caption.copy(
                                        fontWeight = if (isDark) FontWeight.Bold else FontWeight.Normal
                                    ),
                                    color = if (isDark) colors.primary else colors.foreground
                                )
                            }
                        }

                        // Light Theme Pill
                        Box(
                            modifier = Modifier
                                .weight(1f)
                                .height(38.dp)
                                .clip(RoundedCornerShape(ShadcnTheme.shapes.radiusMedium))
                                .background(if (!isDark) colors.primary.copy(alpha = 0.15f) else colors.secondary)
                                .border(
                                    BorderStroke(
                                        1.dp,
                                        if (!isDark) colors.primary else colors.border
                                    ),
                                    RoundedCornerShape(ShadcnTheme.shapes.radiusMedium)
                                )
                                .clickable {
                                    service?.setDeviceTheme(device.address, false)
                                },
                            contentAlignment = Alignment.Center
                        ) {
                            Row(verticalAlignment = Alignment.CenterVertically) {
                                Icon(
                                    imageVector = Icons.Default.LightMode,
                                    contentDescription = null,
                                    tint = if (!isDark) colors.primary else colors.mutedForeground,
                                    modifier = Modifier.size(15.dp)
                                )
                                Spacer(modifier = Modifier.width(6.dp))
                                Text(
                                    text = "Light Theme",
                                    style = typography.caption.copy(
                                        fontWeight = if (!isDark) FontWeight.Bold else FontWeight.Normal
                                    ),
                                    color = if (!isDark) colors.primary else colors.foreground
                                )
                            }
                        }
                    }

                    Spacer(modifier = Modifier.height(16.dp))

                    // 5. Find Device Action
                    Text(
                        text = "Device Finder",
                        style = typography.caption.copy(fontWeight = FontWeight.Medium),
                        color = colors.mutedForeground
                    )
                    Spacer(modifier = Modifier.height(8.dp))
                    ShadcnButton(
                        onClick = {
                            if (!isFindingDevice) {
                                isFindingDevice = true
                                service?.findDevice(device.address)
                                coroutineScope.launch {
                                    // Visual feedback active during the pattern (~8 seconds)
                                    kotlinx.coroutines.delay(8000L)
                                    isFindingDevice = false
                                }
                            }
                        },
                        variant = if (isFindingDevice) ShadcnButtonVariant.SECONDARY else ShadcnButtonVariant.OUTLINE,
                        modifier = Modifier.fillMaxWidth()
                    ) {
                        Icon(
                            imageVector = if (isFindingDevice) Icons.Default.VolumeUp else Icons.Default.Search,
                            contentDescription = null,
                            tint = if (isFindingDevice) colors.primary else colors.foreground,
                            modifier = Modifier.size(16.dp)
                        )
                        Spacer(modifier = Modifier.width(8.dp))
                        Text(
                            text = if (isFindingDevice) "Playing Alert Sound..." else "Find Device",
                            style = typography.body,
                            color = if (isFindingDevice) colors.primary else colors.foreground,
                            fontWeight = FontWeight.SemiBold
                        )
                    }

                    Spacer(modifier = Modifier.height(20.dp))

                    // 3 & 4. Device Reset and Turn Off Actions
                    Text(
                        text = "Power & System",
                        style = typography.caption.copy(fontWeight = FontWeight.Medium),
                        color = colors.mutedForeground
                    )
                    Spacer(modifier = Modifier.height(8.dp))
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.spacedBy(8.dp)
                    ) {
                        // Reset Device
                        ShadcnButton(
                            onClick = { showResetDialog = true },
                            variant = ShadcnButtonVariant.OUTLINE,
                            modifier = Modifier.weight(1f)
                        ) {
                            Icon(
                                imageVector = Icons.Default.Refresh,
                                contentDescription = null,
                                tint = colors.foreground,
                                modifier = Modifier.size(15.dp)
                            )
                            Spacer(modifier = Modifier.width(6.dp))
                            Text(
                                text = "Restart",
                                style = typography.body,
                                color = colors.foreground,
                                fontWeight = FontWeight.Medium
                            )
                        }

                        // Turn Off Device
                        ShadcnButton(
                            onClick = { showPowerOffDialog = true },
                            variant = ShadcnButtonVariant.DESTRUCTIVE,
                            modifier = Modifier.weight(1f)
                        ) {
                            Icon(
                                imageVector = Icons.Default.PowerSettingsNew,
                                contentDescription = null,
                                tint = colors.destructiveForeground,
                                modifier = Modifier.size(15.dp)
                            )
                            Spacer(modifier = Modifier.width(6.dp))
                            Text(
                                text = "Turn Off",
                                style = typography.body,
                                color = colors.destructiveForeground,
                                fontWeight = FontWeight.SemiBold
                            )
                        }
                    }
                    Spacer(modifier = Modifier.height(8.dp))
                }
            }
        }
    }
}

@Composable
fun DeviceLogsScreen(
    device: SugarotaDevice,
    service: SugarotaBleService?
) {
    val colors = ShadcnTheme.colors
    val typography = ShadcnTheme.typography
    val clipboardManager = LocalClipboardManager.current
    val logsMap by service?.deviceLogs?.collectAsState() ?: remember { mutableStateOf(emptyMap()) }
    val logs = logsMap[device.address] ?: emptyList()

    // Local debugMode state synced with device status
    var isDebugEnabled by remember(device.status.isDebugMode) {
        mutableStateOf(device.status.isDebugMode)
    }

    val scrollState = rememberScrollState()

    // Auto scroll to bottom when new logs arrive
    LaunchedEffect(logs.size) {
        if (logs.isNotEmpty()) {
            scrollState.animateScrollTo(scrollState.maxValue)
        }
    }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .padding(16.dp)
    ) {
        // Debug Mode Switch Header Card
        Surface(
            modifier = Modifier.fillMaxWidth(),
            shape = RoundedCornerShape(ShadcnTheme.shapes.radiusMedium),
            color = colors.card,
            border = BorderStroke(1.dp, colors.border)
        ) {
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(horizontal = 16.dp, vertical = 14.dp),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Column(modifier = Modifier.weight(1f)) {
                    Text(
                        text = "Debug Mode",
                        style = typography.body,
                        fontWeight = FontWeight.SemiBold,
                        color = colors.foreground
                    )
                    Spacer(modifier = Modifier.height(2.dp))
                    Text(
                        text = if (isDebugEnabled) "Verbose device output active" else "Enable verbose live logs from device",
                        style = typography.caption,
                        color = colors.mutedForeground
                    )
                }
                Switch(
                    checked = isDebugEnabled,
                    onCheckedChange = { checked ->
                        isDebugEnabled = checked
                        service?.setDeviceDebugMode(device.address, checked)
                    },
                    colors = SwitchDefaults.colors(
                        checkedThumbColor = colors.primaryForeground,
                        checkedTrackColor = colors.primary,
                        uncheckedThumbColor = colors.mutedForeground,
                        uncheckedTrackColor = colors.secondary
                    )
                )
            }
        }

        // When switcher is ON, window with logs appears below it, taking rest of visible area
        AnimatedVisibility(
            visible = isDebugEnabled,
            modifier = Modifier.weight(1f)
        ) {
            Column(
                modifier = Modifier
                    .fillMaxSize()
                    .padding(top = 12.dp)
            ) {
                // Terminal Container Window
                Surface(
                    modifier = Modifier.fillMaxSize(),
                    shape = RoundedCornerShape(ShadcnTheme.shapes.radiusMedium),
                    color = Color(0xFF040407), // Deep terminal black
                    border = BorderStroke(1.dp, colors.border)
                ) {
                    Column(modifier = Modifier.fillMaxSize()) {
                        // Terminal Title Bar & Action Buttons
                        Row(
                            modifier = Modifier
                                .fillMaxWidth()
                                .background(Color(0xFF0C0D12))
                                .padding(horizontal = 12.dp, vertical = 8.dp),
                            horizontalArrangement = Arrangement.SpaceBetween,
                            verticalAlignment = Alignment.CenterVertically
                        ) {
                            Row(verticalAlignment = Alignment.CenterVertically) {
                                Box(
                                    modifier = Modifier
                                        .size(8.dp)
                                        .clip(RoundedCornerShape(4.dp))
                                        .background(Color(0xFF22C55E))
                                )
                                Spacer(modifier = Modifier.width(8.dp))
                                Text(
                                    text = "TERMINAL OUTPUT (${logs.size})",
                                    style = typography.caption.copy(
                                        fontFamily = FontFamily.Monospace,
                                        fontWeight = FontWeight.Bold,
                                        letterSpacing = 0.5.sp
                                    ),
                                    color = Color(0xFF94A3B8)
                                )
                            }

                            Row(horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                                // Copy All Logs Button
                                IconButton(
                                    onClick = {
                                        val fullText = logs.joinToString("\n")
                                        clipboardManager.setText(AnnotatedString(fullText))
                                    },
                                    modifier = Modifier.size(28.dp)
                                ) {
                                    Icon(
                                        imageVector = Icons.Default.ContentCopy,
                                        contentDescription = "Copy Logs",
                                        tint = Color(0xFF94A3B8),
                                        modifier = Modifier.size(15.dp)
                                    )
                                }

                                // Clear Logs Button
                                IconButton(
                                    onClick = {
                                        service?.clearDeviceLogs(device.address)
                                    },
                                    modifier = Modifier.size(28.dp)
                                ) {
                                    Icon(
                                        imageVector = Icons.Default.DeleteOutline,
                                        contentDescription = "Clear Logs",
                                        tint = Color(0xFF94A3B8),
                                        modifier = Modifier.size(15.dp)
                                    )
                                }
                            }
                        }

                        // Terminal Log Content Area (Wrapped in SelectionContainer so user can select text)
                        Box(
                            modifier = Modifier
                                .weight(1f)
                                .fillMaxWidth()
                                .padding(12.dp)
                        ) {
                            SelectionContainer {
                                Column(
                                    modifier = Modifier
                                        .fillMaxSize()
                                        .verticalScroll(scrollState)
                                ) {
                                    if (logs.isEmpty()) {
                                        Text(
                                            text = "> Debug mode active. Waiting for device log output...",
                                            style = typography.caption.copy(
                                                fontFamily = FontFamily.Monospace,
                                                fontSize = 12.sp,
                                                lineHeight = 18.sp
                                            ),
                                            color = Color(0xFF64748B)
                                        )
                                    } else {
                                        logs.forEach { line ->
                                            Text(
                                                text = line,
                                                style = typography.caption.copy(
                                                    fontFamily = FontFamily.Monospace,
                                                    fontSize = 11.5.sp,
                                                    lineHeight = 16.sp
                                                ),
                                                color = when {
                                                    line.contains("error", ignoreCase = true) || line.contains("failed", ignoreCase = true) -> Color(0xFFF87171)
                                                    line.contains("Connected", ignoreCase = true) -> Color(0xFF4ADE80)
                                                    line.contains("write", ignoreCase = true) -> Color(0xFF38BDF8)
                                                    line.contains("Status", ignoreCase = true) -> Color(0xFFFBBF24)
                                                    else -> Color(0xFFCBD5E1)
                                                }
                                            )
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}


