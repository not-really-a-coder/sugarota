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
    SETTINGS
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun DeviceDetailScreen(
    deviceAddress: String,
    initialTab: DeviceScreenTab = DeviceScreenTab.CHART,
    service: SugarotaBleService?,
    onEnterPip: (() -> Unit)? = null,
    onDismiss: () -> Unit
) {
    var showLogsSubPage by remember { mutableStateOf(false) }

    BackHandler(enabled = showLogsSubPage) {
        showLogsSubPage = false
    }
    BackHandler(enabled = !showLogsSubPage, onBack = onDismiss)

    val devicesMap by service?.devices?.collectAsState() ?: remember { mutableStateOf(emptyMap()) }
    val device = devicesMap[deviceAddress] ?: SugarotaDevice(
        name = service?.getDefaultDeviceName(deviceAddress) ?: "Sugarota Device",
        address = deviceAddress
    )

    val lastReading by service?.lastReading?.collectAsState() ?: remember { mutableStateOf(null) }
    val deviceReadings by service?.deviceReadings?.collectAsState() ?: remember { mutableStateOf(emptyMap()) }
    val deviceConfigured by service?.deviceConfigured?.collectAsState() ?: remember { mutableStateOf(emptyMap()) }
    val deviceConfigsMap by service?.deviceConfigsFlow?.collectAsState() ?: remember { mutableStateOf(emptyMap()) }
    val deviceReading = deviceReadings[deviceAddress] ?: lastReading
    val isConfigured = deviceConfigured[deviceAddress] ?: true
    var currentTab by remember { mutableStateOf(initialTab) }

    // Fallback tab to CHART when device is offline
    LaunchedEffect(device.isConnected) {
        if (!device.isConnected && currentTab != DeviceScreenTab.CHART) {
            currentTab = DeviceScreenTab.CHART
            showLogsSubPage = false
        }
    }

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
                actions = {
                    if (onEnterPip != null && android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.O) {
                        IconButton(onClick = onEnterPip) {
                            Icon(
                                imageVector = Icons.Default.PictureInPictureAlt,
                                contentDescription = "Picture in Picture",
                                tint = colors.primary
                            )
                        }
                    }
                },
                colors = TopAppBarDefaults.topAppBarColors(
                    containerColor = colors.card,
                    titleContentColor = colors.foreground,
                    navigationIconContentColor = colors.foreground
                )
            )
        },
        bottomBar = {
            if (!showLogsSubPage) {
                // Persistent bottom switcher between Chart and Settings screens
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

                                // Settings Tab Button
                                val isSettingsSelected = currentTab == DeviceScreenTab.SETTINGS
                                val isSettingsEnabled = device.isConnected
                                Box(
                                    modifier = Modifier
                                        .weight(1f)
                                        .fillMaxHeight()
                                        .clip(RoundedCornerShape(6.dp))
                                        .background(if (isSettingsSelected) colors.secondary else Color.Transparent)
                                        .then(
                                            if (isSettingsEnabled) {
                                                Modifier.clickable { currentTab = DeviceScreenTab.SETTINGS }
                                            } else {
                                                Modifier
                                            }
                                        ),
                                    contentAlignment = Alignment.Center
                                ) {
                                    Row(verticalAlignment = Alignment.CenterVertically) {
                                        Icon(
                                            imageVector = Icons.Default.Settings,
                                            contentDescription = null,
                                            tint = if (!isSettingsEnabled) {
                                                colors.mutedForeground.copy(alpha = 0.35f)
                                            } else if (isSettingsSelected) {
                                                colors.foreground
                                            } else {
                                                colors.mutedForeground
                                            },
                                            modifier = Modifier.size(16.dp)
                                        )
                                        Spacer(modifier = Modifier.width(6.dp))
                                        Text(
                                            text = "Settings",
                                            style = typography.caption.copy(
                                                fontWeight = if (isSettingsSelected) FontWeight.SemiBold else FontWeight.Normal
                                            ),
                                            color = if (!isSettingsEnabled) {
                                                colors.mutedForeground.copy(alpha = 0.35f)
                                            } else if (isSettingsSelected) {
                                                colors.foreground
                                            } else {
                                                colors.mutedForeground
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
    ) { padding ->
        Box(
            modifier = Modifier
                .fillMaxSize()
                .padding(padding)
        ) {
            if (showLogsSubPage) {
                DeviceLogsScreen(
                    device = device,
                    service = service,
                    onDismiss = { showLogsSubPage = false }
                )
            } else {
                when (currentTab) {
                    DeviceScreenTab.CHART -> {
                        val configuredUnits = deviceConfigsMap[device.address]?.let { cfg ->
                            try {
                                org.json.JSONObject(cfg).optString("units", "")
                            } catch (_: Exception) {
                                ""
                            }
                        }?.takeIf { it.isNotBlank() } ?: service?.getDeviceUnits(device.address) ?: "mg/dL"
                        DeviceChartContent(
                            device = device,
                            lastReading = deviceReading,
                            configuredUnits = configuredUnits,
                            isConfigured = isConfigured,
                            service = service,
                            onNavigateToConfig = { currentTab = DeviceScreenTab.SETTINGS },
                            onOpenFirmwareUpdate = { showFirmwareUpdate = true }
                        )
                    }

                    DeviceScreenTab.SETTINGS -> {
                        DeviceConfigScreen(
                            deviceAddress = deviceAddress,
                            deviceName = customName.ifBlank { device.name },
                            service = service,
                            showHeader = false,
                            onOpenFirmwareUpdate = { showFirmwareUpdate = true },
                            onOpenLogs = { showLogsSubPage = true },
                            onDismiss = onDismiss
                        )
                    }
                }
            }
        }
    }
}

@Composable
fun DeviceChartContent(
    device: SugarotaDevice,
    lastReading: org.sugarota.companion.model.GlucoseData?,
    configuredUnits: String = "mg/dL",
    isConfigured: Boolean = true,
    service: SugarotaBleService?,
    onNavigateToConfig: () -> Unit = {},
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

    val units = configuredUnits.takeIf { it.isNotBlank() } ?: service?.getDeviceUnits(device.address)
    ?: lastReading?.units?.takeIf { it.isNotBlank() } ?: "mg/dL"
    val isMmol = units.equals("mmol/l", ignoreCase = true)

    Column(
        modifier = Modifier
            .fillMaxSize()
            .padding(16.dp)
    ) {
        // If not configured, show prominent callout to start configuration
        if (!isConfigured) {
            Row(
                verticalAlignment = Alignment.CenterVertically,
                modifier = Modifier
                    .fillMaxWidth()
                    .clip(RoundedCornerShape(ShadcnTheme.shapes.radiusMedium))
                    .background(Color(0xFFF59E0B).copy(alpha = 0.15f))
                    .border(
                        width = 1.dp,
                        color = Color(0xFFF59E0B).copy(alpha = 0.4f),
                        shape = RoundedCornerShape(ShadcnTheme.shapes.radiusMedium)
                    )
                    .clickable(onClick = onNavigateToConfig)
                    .padding(horizontal = 14.dp, vertical = 12.dp)
            ) {
                Icon(
                    imageVector = Icons.Default.Settings,
                    contentDescription = "Configure Account",
                    tint = Color(0xFFF59E0B),
                    modifier = Modifier.size(20.dp)
                )
                Spacer(modifier = Modifier.width(10.dp))
                Column(modifier = Modifier.weight(1f)) {
                    Text(
                        text = "Account Details Not Configured",
                        style = typography.body.copy(fontWeight = FontWeight.Bold, fontSize = 14.sp),
                        color = Color(0xFFF59E0B)
                    )
                    Spacer(modifier = Modifier.height(2.dp))
                    Text(
                        text = "Tap to set up Nightscout URL or Dexcom credentials for this device.",
                        style = typography.caption.copy(fontSize = 12.sp),
                        color = colors.mutedForeground
                    )
                }
                Icon(
                    imageVector = Icons.Default.ChevronRight,
                    contentDescription = null,
                    tint = Color(0xFFF59E0B),
                    modifier = Modifier.size(20.dp)
                )
            }
            Spacer(modifier = Modifier.height(12.dp))
        }

        // Glucose Value One-liner above the chart
        if (lastReading != null) {
            val formattedSgv = if (isMmol) {
                String.format(java.util.Locale.US, "%.1f", lastReading.sgv / 18.0182f)
            } else {
                "${lastReading.sgv}"
            }
            val deltaFormatted = if (isMmol) {
                val mmolVal = lastReading.delta / 18.0182f
                if (lastReading.delta == 0) "+0.0" else String.format(
                    java.util.Locale.US,
                    "%s%.1f",
                    if (lastReading.delta > 0) "+" else "",
                    mmolVal
                )
            } else {
                "${if (lastReading.delta > 0) "+" else ""}${lastReading.delta}"
            }
            val bgCol = getGlucoseColor(lastReading.sgv)
            val timeStr = java.text.SimpleDateFormat("HH:mm", java.util.Locale.getDefault())
                .format(java.util.Date(lastReading.timestamp * 1000))
            val minsAgo = ((System.currentTimeMillis() / 1000 - lastReading.timestamp) / 60).coerceAtLeast(0)
            val agoText = if (minsAgo == 0L) "now" else "$minsAgo min ago"

            val syncRotation = remember { androidx.compose.animation.core.Animatable(0f) }
            val syncScope = rememberCoroutineScope()

            Row(
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
                    direction = lastReading.direction,
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
                Spacer(modifier = Modifier.width(4.dp))
                IconButton(
                    onClick = {
                        syncScope.launch {
                            syncRotation.snapTo(0f)
                            syncRotation.animateTo(
                                targetValue = 360f,
                                animationSpec = androidx.compose.animation.core.tween(
                                    durationMillis = 700,
                                    easing = androidx.compose.animation.core.FastOutSlowInEasing
                                )
                            )
                        }
                        service?.triggerManualSync()
                    },
                    modifier = Modifier.size(32.dp)
                ) {
                    Icon(
                        imageVector = Icons.Default.Refresh,
                        contentDescription = "Force Refresh",
                        tint = colors.primary,
                        modifier = Modifier
                            .size(18.dp)
                            .graphicsLayer {
                                rotationZ = syncRotation.value
                            }
                    )
                }
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
            if (!device.isConnected) {
                ShadcnCard(
                    modifier = Modifier.fillMaxSize()
                ) {
                    Box(
                        modifier = Modifier.fillMaxSize(),
                        contentAlignment = Alignment.Center
                    ) {
                        Text(
                            text = "Device is offline",
                            style = typography.body.copy(fontSize = 15.sp, fontWeight = FontWeight.Medium),
                            color = colors.mutedForeground
                        )
                    }
                }
            } else {
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
                                    imageVector = Icons.Default.Bolt,
                                    contentDescription = null,
                                    tint = colors.primary,
                                    modifier = Modifier.size(18.dp)
                                )
                                Spacer(modifier = Modifier.width(8.dp))
                                Text(
                                    text = "Quick Actions",
                                    style = typography.h3,
                                    color = colors.foreground,
                                    fontWeight = FontWeight.SemiBold
                                )
                            }

                            Row(
                                horizontalArrangement = Arrangement.spacedBy(8.dp),
                                verticalAlignment = Alignment.CenterVertically
                            ) {
                                val voltText = if (device.status.batteryVoltage > 0.0f) " (${String.format(java.util.Locale.US, "%.2fV", device.status.batteryVoltage)})" else ""
                                ShadcnBadge(
                                    text = "${device.status.batteryPct}%$voltText${if (device.status.isCharging) " (+)" else ""}",
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
                            text = "Screen Brightness",
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

                        // 2. Speaker Volume Section (levels 0: Off, 1: 35%, 2: 70%, 3: 100%)
                        val volumeLevels = listOf(
                            Pair(0, "Off"),
                            Pair(1, "35%"),
                            Pair(2, "70%"),
                            Pair(3, "100%")
                        )
                        val currentVolume = device.status.volume

                        Text(
                            text = "Speaker Volume",
                            style = typography.caption.copy(fontWeight = FontWeight.Medium),
                            color = colors.mutedForeground
                        )
                        Spacer(modifier = Modifier.height(8.dp))
                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            horizontalArrangement = Arrangement.spacedBy(8.dp)
                        ) {
                            volumeLevels.forEach { (level, label) ->
                                val isSelected = currentVolume == level
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
                                            service?.setDeviceVolume(device.address, level)
                                        },
                                    contentAlignment = Alignment.Center
                                ) {
                                    Row(
                                        verticalAlignment = Alignment.CenterVertically,
                                        horizontalArrangement = Arrangement.Center
                                    ) {
                                        Icon(
                                            imageVector = if (level == 0) Icons.Default.VolumeOff else Icons.Default.VolumeUp,
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

                        // 3. Toggles Section: Night Mode & Light Theme in unified cards
                        Surface(
                            modifier = Modifier.fillMaxWidth(),
                            shape = RoundedCornerShape(ShadcnTheme.shapes.radiusMedium),
                            color = colors.card,
                            border = BorderStroke(1.dp, colors.border)
                        ) {
                            Column {
                                // Night Mode row
                                Row(
                                    modifier = Modifier
                                        .fillMaxWidth()
                                        .padding(horizontal = 14.dp, vertical = 12.dp),
                                    horizontalArrangement = Arrangement.SpaceBetween,
                                    verticalAlignment = Alignment.CenterVertically
                                ) {
                                    Row(
                                        verticalAlignment = Alignment.CenterVertically,
                                        modifier = Modifier.weight(1f)
                                    ) {
                                        Icon(
                                            imageVector = Icons.Default.Bedtime,
                                            contentDescription = null,
                                            tint = if (device.status.isNightMode) colors.primary else colors.mutedForeground,
                                            modifier = Modifier.size(20.dp)
                                        )
                                        Spacer(modifier = Modifier.width(12.dp))
                                        Column {
                                            Text(
                                                text = "Enable Night Mode",
                                                style = typography.body,
                                                fontWeight = FontWeight.Medium,
                                                color = colors.foreground
                                            )
                                            Text(
                                                text = "Auto screen off after 30s on new reading. Tap to wake. Active 22:00-07:00",
                                                style = typography.caption,
                                                color = colors.mutedForeground
                                            )
                                        }
                                    }
                                    Switch(
                                        checked = device.status.isNightMode,
                                        onCheckedChange = { checked ->
                                            service?.setDeviceNightMode(device.address, checked)
                                        },
                                        colors = SwitchDefaults.colors(
                                            checkedThumbColor = colors.primaryForeground,
                                            checkedTrackColor = colors.primary,
                                            uncheckedThumbColor = colors.mutedForeground,
                                            uncheckedTrackColor = colors.secondary
                                        )
                                    )
                                }

                                HorizontalDivider(
                                    modifier = Modifier.fillMaxWidth(),
                                    thickness = 1.dp,
                                    color = colors.border
                                )

                                // Light Theme row (Off = Dark theme active)
                                val isLightTheme = !device.status.isDarkTheme
                                Row(
                                    modifier = Modifier
                                        .fillMaxWidth()
                                        .padding(horizontal = 14.dp, vertical = 12.dp),
                                    horizontalArrangement = Arrangement.SpaceBetween,
                                    verticalAlignment = Alignment.CenterVertically
                                ) {
                                    Row(
                                        verticalAlignment = Alignment.CenterVertically,
                                        modifier = Modifier.weight(1f)
                                    ) {
                                        Icon(
                                            imageVector = Icons.Default.WbSunny,
                                            contentDescription = null,
                                            tint = if (isLightTheme) colors.primary else colors.mutedForeground,
                                            modifier = Modifier.size(20.dp)
                                        )
                                        Spacer(modifier = Modifier.width(12.dp))
                                        Column {
                                            Text(
                                                text = "Enable Light Theme",
                                                style = typography.body,
                                                fontWeight = FontWeight.Medium,
                                                color = colors.foreground
                                            )
                                            Text(
                                                text = if (isLightTheme) "White high-contrast background" else "Dark theme active when disabled",
                                                style = typography.caption,
                                                color = colors.mutedForeground
                                            )
                                        }
                                    }
                                    Switch(
                                        checked = isLightTheme,
                                        onCheckedChange = { lightChecked ->
                                            // When light is ON, darkTheme is false
                                            service?.setDeviceTheme(device.address, !lightChecked)
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
                        }

                        Spacer(modifier = Modifier.height(16.dp))

                        // 4. Combined Action Row: Find Device (flex width) + Power Menu (icon button)
                        var showPowerMenu by remember { mutableStateOf(false) }

                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            horizontalArrangement = Arrangement.spacedBy(8.dp),
                            verticalAlignment = Alignment.CenterVertically
                        ) {
                            // Find Device Button
                            ShadcnButton(
                                onClick = {
                                    if (!isFindingDevice) {
                                        isFindingDevice = true
                                        service?.findDevice(device.address)
                                        coroutineScope.launch {
                                            kotlinx.coroutines.delay(8000L)
                                            isFindingDevice = false
                                        }
                                    }
                                },
                                variant = if (isFindingDevice) ShadcnButtonVariant.SECONDARY else ShadcnButtonVariant.OUTLINE,
                                modifier = Modifier
                                    .weight(1f)
                                    .height(42.dp)
                            ) {
                                Icon(
                                    imageVector = if (isFindingDevice) Icons.Default.VolumeUp else Icons.Default.Search,
                                    contentDescription = null,
                                    tint = if (isFindingDevice) colors.primary else colors.foreground,
                                    modifier = Modifier.size(15.dp)
                                )
                                Spacer(modifier = Modifier.width(6.dp))
                                Text(
                                    text = if (isFindingDevice) "Playing Sound..." else "Find Device",
                                    style = typography.body,
                                    color = if (isFindingDevice) colors.primary else colors.foreground,
                                    fontWeight = FontWeight.SemiBold
                                )
                            }

                            // Power Dropdown Button (like Windows start menu power button)
                            Box {
                                Box(
                                    modifier = Modifier
                                        .size(42.dp)
                                        .clip(RoundedCornerShape(ShadcnTheme.shapes.radiusMedium))
                                        .background(colors.card)
                                        .border(
                                            BorderStroke(1.dp, colors.border),
                                            RoundedCornerShape(ShadcnTheme.shapes.radiusMedium)
                                        )
                                        .clickable { showPowerMenu = true },
                                    contentAlignment = Alignment.Center
                                ) {
                                    Icon(
                                        imageVector = Icons.Default.PowerSettingsNew,
                                        contentDescription = "Power Options",
                                        tint = colors.foreground,
                                        modifier = Modifier.size(18.dp)
                                    )
                                }

                                DropdownMenu(
                                    expanded = showPowerMenu,
                                    onDismissRequest = { showPowerMenu = false },
                                    modifier = Modifier
                                        .background(colors.card)
                                        .border(1.dp, colors.border)
                                ) {
                                    DropdownMenuItem(
                                        text = {
                                            Text(
                                                text = "Restart",
                                                color = colors.foreground,
                                                fontWeight = FontWeight.Medium
                                            )
                                        },
                                        leadingIcon = {
                                            Icon(
                                                imageVector = Icons.Default.Refresh,
                                                contentDescription = null,
                                                tint = colors.foreground,
                                                modifier = Modifier.size(18.dp)
                                            )
                                        },
                                        onClick = {
                                            showPowerMenu = false
                                            showResetDialog = true
                                        }
                                    )
                                    HorizontalDivider(color = colors.border)
                                    DropdownMenuItem(
                                        text = {
                                            Text(
                                                text = "Turn Off",
                                                color = colors.destructive,
                                                fontWeight = FontWeight.SemiBold
                                            )
                                        },
                                        leadingIcon = {
                                            Icon(
                                                imageVector = Icons.Default.PowerSettingsNew,
                                                contentDescription = null,
                                                tint = colors.destructive,
                                                modifier = Modifier.size(18.dp)
                                            )
                                        },
                                        onClick = {
                                            showPowerMenu = false
                                            showPowerOffDialog = true
                                        }
                                    )
                                }
                            }
                        }
                        Spacer(modifier = Modifier.height(12.dp))
                    }
                }
            }
        }
    }
}


@Composable
fun DeviceLogsScreen(
    device: SugarotaDevice,
    service: SugarotaBleService?,
    onDismiss: (() -> Unit)? = null
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

    // Terminal controls matching Web Console
    var filterQuery by remember { mutableStateOf("") }
    var isAutoScrollEnabled by remember { mutableStateOf(true) }
    var isBatteryLogView by remember { mutableStateOf(false) }

    val scrollState = rememberScrollState()

    // Filter entries based on search query (minimum 3 chars, matching web console) and battery view toggle
    val filteredLogs = remember(logs, filterQuery, isBatteryLogView) {
        logs.filter { line ->
            val matchesBattery = !isBatteryLogView || line.contains("Battery", ignoreCase = true)
            val matchesQuery = filterQuery.length < 3 || line.contains(filterQuery, ignoreCase = true)
            matchesBattery && matchesQuery
        }
    }

    // Auto scroll to bottom when new logs arrive (if auto-scroll is enabled)
    LaunchedEffect(filteredLogs.size, isAutoScrollEnabled) {
        if (isAutoScrollEnabled && filteredLogs.isNotEmpty()) {
            scrollState.animateScrollTo(scrollState.maxValue)
        }
    }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .padding(16.dp)
    ) {
        if (onDismiss != null) {
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(bottom = 12.dp),
                verticalAlignment = Alignment.CenterVertically
            ) {
                IconButton(
                    onClick = onDismiss,
                    modifier = Modifier.size(36.dp)
                ) {
                    Icon(
                        imageVector = Icons.AutoMirrored.Filled.ArrowBack,
                        contentDescription = "Back to Settings",
                        tint = colors.foreground
                    )
                }
                Spacer(modifier = Modifier.width(8.dp))
                Text(
                    text = "Device & BLE Logs",
                    style = typography.h2,
                    color = colors.foreground
                )
            }
        }
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
                        // Terminal Title Bar & Action Buttons (Aligned with Web Console)
                        Column(
                            modifier = Modifier
                                .fillMaxWidth()
                                .background(Color(0xFF0C0D12))
                                .padding(horizontal = 12.dp, vertical = 8.dp)
                        ) {
                            Row(
                                modifier = Modifier.fillMaxWidth(),
                                horizontalArrangement = Arrangement.SpaceBetween,
                                verticalAlignment = Alignment.CenterVertically
                            ) {
                                Row(
                                    verticalAlignment = Alignment.CenterVertically,
                                    modifier = Modifier.weight(1f, fill = false)
                                ) {
                                    Box(
                                        modifier = Modifier
                                            .size(8.dp)
                                            .clip(RoundedCornerShape(4.dp))
                                            .background(if (isBatteryLogView) Color(0xFF34D399) else Color(0xFF22C55E))
                                    )
                                    Spacer(modifier = Modifier.width(8.dp))
                                    Text(
                                        text = if (isBatteryLogView) "BATTERY TELEMETRY (${filteredLogs.size})" else "CONSOLE OUTPUT (${filteredLogs.size})",
                                        style = typography.caption.copy(
                                            fontFamily = FontFamily.Monospace,
                                            fontWeight = FontWeight.Bold,
                                            letterSpacing = 0.5.sp
                                        ),
                                        color = if (isBatteryLogView) Color(0xFF34D399) else Color(0xFF94A3B8),
                                        maxLines = 1
                                    )
                                }

                                Row(
                                    horizontalArrangement = Arrangement.spacedBy(2.dp),
                                    verticalAlignment = Alignment.CenterVertically
                                ) {
                                    // Battery Log View Toggle Button
                                    IconButton(
                                        onClick = { isBatteryLogView = !isBatteryLogView },
                                        modifier = Modifier.size(28.dp)
                                    ) {
                                        Icon(
                                            imageVector = Icons.Default.BatteryChargingFull,
                                            contentDescription = "Toggle Battery Telemetry",
                                            tint = if (isBatteryLogView) Color(0xFF34D399) else Color(0xFF94A3B8),
                                            modifier = Modifier.size(16.dp)
                                        )
                                    }

                                    // Pause Auto-Scroll Toggle Button
                                    IconButton(
                                        onClick = { isAutoScrollEnabled = !isAutoScrollEnabled },
                                        modifier = Modifier.size(28.dp)
                                    ) {
                                        Icon(
                                            imageVector = if (isAutoScrollEnabled) Icons.Default.ArrowDownward else Icons.Default.Pause,
                                            contentDescription = if (isAutoScrollEnabled) "Auto-scroll Enabled" else "Auto-scroll Paused",
                                            tint = if (!isAutoScrollEnabled) Color(0xFFFBBF24) else Color(0xFF94A3B8),
                                            modifier = Modifier.size(16.dp)
                                        )
                                    }

                                    // Copy All Logs Button
                                    IconButton(
                                        onClick = {
                                            val fullText = filteredLogs.joinToString("\n")
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

                            // Filter input search bar matching web console
                            Spacer(modifier = Modifier.height(6.dp))
                            Row(
                                modifier = Modifier
                                    .fillMaxWidth()
                                    .clip(RoundedCornerShape(6.dp))
                                    .background(Color(0xFF040407))
                                    .border(1.dp, Color(0xFF1E293B), RoundedCornerShape(6.dp))
                                    .padding(horizontal = 8.dp, vertical = 4.dp),
                                verticalAlignment = Alignment.CenterVertically
                            ) {
                                Icon(
                                    imageVector = Icons.Default.Search,
                                    contentDescription = null,
                                    tint = Color(0xFF64748B),
                                    modifier = Modifier.size(14.dp)
                                )
                                Spacer(modifier = Modifier.width(6.dp))
                                androidx.compose.foundation.text.BasicTextField(
                                    value = filterQuery,
                                    onValueChange = { filterQuery = it },
                                    modifier = Modifier.weight(1f),
                                    singleLine = true,
                                    textStyle = typography.caption.copy(
                                        color = Color(0xFFF1F5F9),
                                        fontFamily = FontFamily.Monospace,
                                        fontSize = 11.5.sp
                                    ),
                                    decorationBox = { innerTextField ->
                                        if (filterQuery.isEmpty()) {
                                            Text(
                                                text = "Filter logs (min 3 chars)...",
                                                style = typography.caption.copy(
                                                    color = Color(0xFF64748B),
                                                    fontSize = 11.5.sp
                                                )
                                            )
                                        }
                                        innerTextField()
                                    }
                                )
                                if (filterQuery.isNotEmpty()) {
                                    Icon(
                                        imageVector = Icons.Default.Close,
                                        contentDescription = "Clear search",
                                        tint = Color(0xFF94A3B8),
                                        modifier = Modifier
                                            .size(14.dp)
                                            .clickable { filterQuery = "" }
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
                                    if (filteredLogs.isEmpty()) {
                                        Text(
                                            text = if (logs.isEmpty()) {
                                                "> Debug mode active. Waiting for device log output..."
                                            } else {
                                                "> No log entries match the active filter."
                                            },
                                            style = typography.caption.copy(
                                                fontFamily = FontFamily.Monospace,
                                                fontSize = 12.sp,
                                                lineHeight = 18.sp
                                            ),
                                            color = Color(0xFF64748B)
                                        )
                                    } else {
                                        filteredLogs.forEach { line ->
                                            Text(
                                                text = line,
                                                style = typography.caption.copy(
                                                    fontFamily = FontFamily.Monospace,
                                                    fontSize = 11.5.sp,
                                                    lineHeight = 16.sp
                                                ),
                                                color = when {
                                                    line.contains("error", ignoreCase = true) || line.contains(
                                                        "failed",
                                                        ignoreCase = true
                                                    ) -> Color(0xFFF87171) // Red (var(--accent-red))

                                                    line.contains("Connected", ignoreCase = true) -> Color(0xFF00FF66) // Neon Green (var(--accent-green))
                                                    line.contains("write", ignoreCase = true) -> Color(0xFF00F0FF) // Neon Cyan (var(--accent-blue))
                                                    line.contains("Status", ignoreCase = true) -> Color(0xFFFFB700) // Amber Yellow (var(--accent-yellow))
                                                    line.contains("Battery", ignoreCase = true) -> Color(0xFF34D399) // Emerald Green
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


