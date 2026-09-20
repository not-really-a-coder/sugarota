package org.sugarota.companion.ui

import androidx.activity.compose.BackHandler
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.automirrored.filled.ArrowForward
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.AnnotatedString
import androidx.compose.ui.text.SpanStyle
import androidx.compose.ui.text.buildAnnotatedString
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import org.json.JSONObject
import org.sugarota.companion.model.FirmwareReleaseInfo
import org.sugarota.companion.model.SugarotaDevice
import org.sugarota.companion.network.FirmwareUpdateManager
import org.sugarota.companion.service.SugarotaBleService
import org.sugarota.companion.ui.components.*
import org.sugarota.companion.ui.theme.ShadcnTheme
import java.io.File

enum class UpdateScreenState {
    CHECKING,
    UP_TO_DATE,
    READY_TO_DOWNLOAD,
    DOWNLOADING,
    CHECKING_WIFI,
    WIFI_MISMATCH,
    READY_TO_FLASH,
    FLASHING,
    COMPLETE,
    ERROR
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun FirmwareUpdateScreen(
    device: SugarotaDevice,
    service: SugarotaBleService?,
    onDismiss: () -> Unit
) {
    BackHandler(onBack = onDismiss)

    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    val updateManager = remember { FirmwareUpdateManager(context) }

    val colors = ShadcnTheme.colors
    val typography = ShadcnTheme.typography

    var screenState by remember { mutableStateOf(UpdateScreenState.CHECKING) }
    var releaseInfo by remember { mutableStateOf<FirmwareReleaseInfo?>(null) }
    var errorMessage by remember { mutableStateOf<String?>(null) }
    var downloadProgress by remember { mutableIntStateOf(0) }
    var flashProgress by remember { mutableIntStateOf(0) }
    var flashStatusText by remember { mutableStateOf("") }
    var downloadedFile by remember { mutableStateOf<File?>(null) }
    var wifiErrorReason by remember { mutableStateOf("") }
    var activeDeviceIp by remember { mutableStateOf("") }

    val deviceCurrentVersion = device.status.version

    // 1. Initial check for updates
    LaunchedEffect(Unit) {
        screenState = UpdateScreenState.CHECKING
        val info = updateManager.checkForUpdates(deviceCurrentVersion)
        if (info == null) {
            // No update or failed to reach
            releaseInfo = FirmwareReleaseInfo(
                version = deviceCurrentVersion,
                changelog = "Unable to fetch online release notes. Ensure internet connection is active.",
                downloadUrl = ""
            )
            screenState = UpdateScreenState.UP_TO_DATE
        } else {
            releaseInfo = info
            val cmp = updateManager.compareCalVer(info.version, deviceCurrentVersion)
            if (cmp > 0) {
                screenState = UpdateScreenState.READY_TO_DOWNLOAD
            } else {
                screenState = UpdateScreenState.UP_TO_DATE
            }
        }
    }

    // Function to verify Wi-Fi and device readiness
    fun checkWifiAndPrepare() {
        screenState = UpdateScreenState.CHECKING_WIFI
        wifiErrorReason = ""

        // Extract configured Wi-Fi SSID from device config
        val rawConfig = service?.getCachedConfig(device.address) ?: ""
        var targetSsid = ""
        var targetSecSsid = ""
        if (rawConfig.isNotBlank()) {
            try {
                val root = JSONObject(rawConfig)
                val wifiObj = root.optJSONObject("wifi")
                targetSsid = wifiObj?.optString("primary_ssid", "") ?: ""
                targetSecSsid = wifiObj?.optString("secondary_ssid", "") ?: ""
            } catch (e: Exception) {
                // Ignore
            }
        }

        val phoneSsid = updateManager.getCurrentWifiSSID()
        val isPhoneWifi = updateManager.isPhoneOnWifi()

        if (!isPhoneWifi) {
            wifiErrorReason = "Phone is not connected to Wi-Fi. Please connect phone to '${targetSsid.ifBlank { "your Wi-Fi" }}'."
            screenState = UpdateScreenState.WIFI_MISMATCH
            return
        }

        // If phone SSID can be read and doesn't match primary or secondary SSID
        if (phoneSsid.isNotBlank() && targetSsid.isNotBlank() && !phoneSsid.equals(targetSsid, ignoreCase = true) && !phoneSsid.equals(targetSecSsid, ignoreCase = true)) {
            wifiErrorReason = "Phone is connected to '$phoneSsid', but Sugarota is configured for '$targetSsid'. Both devices must be on the same Wi-Fi network."
            screenState = UpdateScreenState.WIFI_MISMATCH
            return
        }

        // Verify BLE connection is active before sending command
        val currentDevState = service?.devices?.value?.get(device.address)
        if (currentDevState == null || !currentDevState.isConnected) {
            wifiErrorReason = "Sugarota is not connected over Bluetooth. Please ensure the device is powered on and within Bluetooth range."
            screenState = UpdateScreenState.WIFI_MISMATCH
            return
        }

        // Request device to start Wi-Fi OTA server
        service.startWifiOta(device.address) { writeInitiated ->
            if (!writeInitiated) {
                scope.launch {
                    wifiErrorReason = "Failed to send OTA activation command to Sugarota over Bluetooth. Please try again."
                    screenState = UpdateScreenState.WIFI_MISMATCH
                }
            }
        }

        // Poll for device OTA ready state or direct subnet response
        scope.launch {
            var attempts = 0
            var deviceIp = ""
            while (attempts < 20) {
                delay(1000)
                attempts++

                val currentDev = service?.devices?.value?.get(device.address)
                if (currentDev != null && currentDev.wifiOta.isReady) {
                    deviceIp = currentDev.wifiOta.ip
                    break
                }
                if (currentDev != null && currentDev.wifiOta.isFailed) {
                    wifiErrorReason = "Sugarota failed to connect to Wi-Fi '$targetSsid'. Check Wi-Fi password in Device Config."
                    screenState = UpdateScreenState.WIFI_MISMATCH
                    return@launch
                }

                // Probe mDNS / IP if available
                if (updateManager.probeDeviceSubnet("sugarota.local")) {
                    deviceIp = "sugarota.local"
                    break
                }
            }

            if (deviceIp.isBlank()) {
                // One last try: test if probe works
                if (updateManager.probeDeviceSubnet("sugarota.local")) {
                    deviceIp = "sugarota.local"
                } else {
                    wifiErrorReason = "Timed out connecting Sugarota to Wi-Fi. Make sure the screen is within Wi-Fi range and both phone & display share the same network."
                    screenState = UpdateScreenState.WIFI_MISMATCH
                    return@launch
                }
            }

            // Both phone and Sugarota are confirmed on same subnet!
            activeDeviceIp = deviceIp
            screenState = UpdateScreenState.READY_TO_FLASH
        }
    }

    Scaffold(
        containerColor = colors.background,
        topBar = {
            TopAppBar(
                title = {
                    Column {
                        Text(
                            text = "Firmware Update",
                            style = typography.h2,
                            color = colors.foreground
                        )
                        val subText = if (device.name.isNotBlank() && device.address.isNotBlank()) {
                            "${device.name} • ${device.address}"
                        } else {
                            device.name.ifBlank { device.address }
                        }
                        if (subText.isNotBlank()) {
                            Text(
                                text = subText,
                                style = typography.caption,
                                color = colors.mutedForeground
                            )
                        }
                    }
                },
                navigationIcon = {
                    IconButton(
                        onClick = onDismiss,
                        enabled = screenState != UpdateScreenState.FLASHING
                    ) {
                        Icon(
                            imageVector = Icons.AutoMirrored.Filled.ArrowBack,
                            contentDescription = "Back",
                            tint = if (screenState == UpdateScreenState.FLASHING) colors.mutedForeground else colors.foreground
                        )
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
            Surface(
                color = colors.background,
                tonalElevation = 0.dp,
                shadowElevation = 0.dp
            ) {
                Box(
                    modifier = Modifier
                        .fillMaxWidth()
                        .background(colors.background)
                        .padding(horizontal = 16.dp, vertical = 14.dp)
                ) {
                    when (screenState) {
                        UpdateScreenState.CHECKING -> {
                            ShadcnButton(
                                onClick = {},
                                enabled = false,
                                isLoading = true,
                                modifier = Modifier.fillMaxWidth()
                            ) {
                                Text("Checking for updates...", style = typography.body, color = colors.primaryForeground)
                            }
                        }
                        UpdateScreenState.UP_TO_DATE -> {
                            ShadcnButton(
                                onClick = {},
                                enabled = false,
                                variant = ShadcnButtonVariant.SECONDARY,
                                modifier = Modifier.fillMaxWidth()
                            ) {
                                Icon(
                                    imageVector = Icons.Default.CheckCircle,
                                    contentDescription = null,
                                    tint = Color(0xFF00E676),
                                    modifier = Modifier.size(18.dp)
                                )
                                Spacer(modifier = Modifier.width(8.dp))
                                Text(
                                    text = "The firmware is up to date",
                                    style = typography.body,
                                    fontWeight = FontWeight.Bold,
                                    color = colors.foreground
                                )
                            }
                        }
                        UpdateScreenState.READY_TO_DOWNLOAD -> {
                            ShadcnButton(
                                onClick = {
                                    val rel = releaseInfo
                                    if (rel != null && rel.downloadUrl.isNotBlank()) {
                                        screenState = UpdateScreenState.DOWNLOADING
                                        downloadProgress = 0
                                        scope.launch {
                                            val cacheFile = File(context.cacheDir, "sugarota_update_${rel.version}.bin")
                                            val ok = updateManager.downloadFirmware(rel.downloadUrl, cacheFile) { pct, _, _ ->
                                                downloadProgress = pct
                                            }
                                            if (ok) {
                                                downloadedFile = cacheFile
                                                checkWifiAndPrepare()
                                            } else {
                                                errorMessage = "Failed to download firmware binary."
                                                screenState = UpdateScreenState.ERROR
                                            }
                                        }
                                    }
                                },
                                variant = ShadcnButtonVariant.DEFAULT,
                                modifier = Modifier.fillMaxWidth()
                            ) {
                                Icon(
                                    imageVector = Icons.Default.Download,
                                    contentDescription = null,
                                    tint = colors.primaryForeground,
                                    modifier = Modifier.size(18.dp)
                                )
                                Spacer(modifier = Modifier.width(8.dp))
                                Text(
                                    text = "Download new firmware",
                                    style = typography.body,
                                    fontWeight = FontWeight.Bold,
                                    color = colors.primaryForeground
                                )
                            }
                        }
                        UpdateScreenState.DOWNLOADING -> {
                            ShadcnButton(
                                onClick = {},
                                enabled = false,
                                isLoading = true,
                                modifier = Modifier.fillMaxWidth()
                            ) {
                                Text(
                                    text = "Downloading ($downloadProgress%)...",
                                    style = typography.body,
                                    color = colors.primaryForeground
                                )
                            }
                        }
                        UpdateScreenState.CHECKING_WIFI -> {
                            ShadcnButton(
                                onClick = {},
                                enabled = false,
                                isLoading = true,
                                modifier = Modifier.fillMaxWidth()
                            ) {
                                Text(
                                    text = "Connecting to device Wi-Fi...",
                                    style = typography.body,
                                    color = colors.primaryForeground
                                )
                            }
                        }
                        UpdateScreenState.WIFI_MISMATCH -> {
                            ShadcnButton(
                                onClick = { checkWifiAndPrepare() },
                                variant = ShadcnButtonVariant.DEFAULT,
                                modifier = Modifier.fillMaxWidth()
                            ) {
                                Icon(
                                    imageVector = Icons.Default.Refresh,
                                    contentDescription = null,
                                    tint = colors.primaryForeground,
                                    modifier = Modifier.size(18.dp)
                                )
                                Spacer(modifier = Modifier.width(8.dp))
                                Text(
                                    text = "Retry Wi-Fi Connection",
                                    style = typography.body,
                                    fontWeight = FontWeight.Bold,
                                    color = colors.primaryForeground
                                )
                            }
                        }
                        UpdateScreenState.READY_TO_FLASH -> {
                            ShadcnButton(
                                onClick = {
                                    val file = downloadedFile
                                    val devIp = activeDeviceIp.ifBlank { device.wifiOta.ip.ifBlank { "sugarota.local" } }
                                    if (file != null && file.exists()) {
                                        screenState = UpdateScreenState.FLASHING
                                        flashProgress = 0
                                        flashStatusText = "Uploading firmware..."
                                        scope.launch {
                                            val res = updateManager.uploadFirmwareToDevice(devIp, file) { pct ->
                                                flashProgress = pct
                                                flashStatusText = if (pct >= 100) "Verifying & Rebooting..." else "Flashing ($pct%)..."
                                            }
                                            if (res.isSuccess) {
                                                screenState = UpdateScreenState.COMPLETE
                                            } else {
                                                errorMessage = res.exceptionOrNull()?.message ?: "Flashing failed."
                                                screenState = UpdateScreenState.ERROR
                                            }
                                        }
                                    }
                                },
                                variant = ShadcnButtonVariant.DEFAULT,
                                modifier = Modifier.fillMaxWidth()
                            ) {
                                Icon(
                                    imageVector = Icons.Default.Bolt,
                                    contentDescription = null,
                                    tint = colors.primaryForeground,
                                    modifier = Modifier.size(20.dp)
                                )
                                Spacer(modifier = Modifier.width(8.dp))
                                Text(
                                    text = "Start OTA flashing",
                                    style = typography.body,
                                    fontWeight = FontWeight.Bold,
                                    color = colors.primaryForeground
                                )
                            }
                        }
                        UpdateScreenState.FLASHING -> {
                            ShadcnButton(
                                onClick = {},
                                enabled = false,
                                isLoading = true,
                                modifier = Modifier.fillMaxWidth()
                            ) {
                                Text(
                                    text = "Flashing in progress ($flashProgress%)...",
                                    style = typography.body,
                                    color = colors.primaryForeground
                                )
                            }
                        }
                        UpdateScreenState.COMPLETE -> {
                            ShadcnButton(
                                onClick = onDismiss,
                                variant = ShadcnButtonVariant.DEFAULT,
                                modifier = Modifier.fillMaxWidth()
                            ) {
                                Text(
                                    text = "Done",
                                    style = typography.body,
                                    fontWeight = FontWeight.Bold,
                                    color = colors.primaryForeground
                                )
                            }
                        }
                        UpdateScreenState.ERROR -> {
                            ShadcnButton(
                                onClick = { checkWifiAndPrepare() },
                                variant = ShadcnButtonVariant.DESTRUCTIVE,
                                modifier = Modifier.fillMaxWidth()
                            ) {
                                Text(
                                    text = "Retry",
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
    ) { padding ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(padding)
                .verticalScroll(rememberScrollState())
                .padding(16.dp)
        ) {
            // Top Version Summary Card
            ShadcnCard(modifier = Modifier.fillMaxWidth()) {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Column {
                        Text(
                            text = "CURRENT VERSION",
                            style = typography.caption,
                            fontWeight = FontWeight.Bold,
                            color = colors.mutedForeground
                        )
                        Spacer(modifier = Modifier.height(4.dp))
                        Text(
                            text = deviceCurrentVersion,
                            style = typography.h3,
                            color = colors.foreground,
                            fontFamily = FontFamily.Monospace
                        )
                    }

                    Icon(
                        imageVector = Icons.AutoMirrored.Filled.ArrowForward,
                        contentDescription = null,
                        tint = colors.mutedForeground,
                        modifier = Modifier.size(24.dp)
                    )

                    Column(horizontalAlignment = Alignment.End) {
                        Text(
                            text = "LATEST VERSION",
                            style = typography.caption,
                            fontWeight = FontWeight.Bold,
                            color = colors.mutedForeground
                        )
                        Spacer(modifier = Modifier.height(4.dp))
                        val targetVer = releaseInfo?.version ?: "—"
                        val isNewer = releaseInfo != null && updateManager.compareCalVer(releaseInfo!!.version, deviceCurrentVersion) > 0
                        Text(
                            text = targetVer,
                            style = typography.h3,
                            color = if (isNewer) Color(0xFF00E676) else colors.foreground,
                            fontFamily = FontFamily.Monospace
                        )
                    }
                }
            }

            Spacer(modifier = Modifier.height(16.dp))

            // Warning or Wi-Fi Notification Alerts
            AnimatedVisibility(visible = screenState == UpdateScreenState.WIFI_MISMATCH) {
                Column(
                    modifier = Modifier
                        .fillMaxWidth()
                        .clip(RoundedCornerShape(ShadcnTheme.shapes.radiusMedium))
                        .background(Color(0xFFEF4444).copy(alpha = 0.15f))
                        .border(
                            BorderStroke(1.dp, Color(0xFFEF4444).copy(alpha = 0.4f)),
                            RoundedCornerShape(ShadcnTheme.shapes.radiusMedium)
                        )
                        .padding(14.dp)
                ) {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Icon(
                            imageVector = Icons.Default.WifiOff,
                            contentDescription = null,
                            tint = Color(0xFFEF4444),
                            modifier = Modifier.size(22.dp)
                        )
                        Spacer(modifier = Modifier.width(10.dp))
                        Text(
                            text = "Same Wi-Fi Required",
                            style = typography.body,
                            fontWeight = FontWeight.Bold,
                            color = Color(0xFFEF4444)
                        )
                    }
                    Spacer(modifier = Modifier.height(6.dp))
                    Text(
                        text = wifiErrorReason.ifBlank { "For flashing it is required to be connected to the same Wi-Fi network as Sugarota." },
                        style = typography.caption,
                        color = colors.foreground
                    )
                }
            }

            // Flashing / Download Progress Indicator
            AnimatedVisibility(visible = screenState == UpdateScreenState.DOWNLOADING || screenState == UpdateScreenState.FLASHING) {
                Column(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(vertical = 12.dp)
                ) {
                    val pct = if (screenState == UpdateScreenState.DOWNLOADING) downloadProgress else flashProgress
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceBetween
                    ) {
                        Text(
                            text = if (screenState == UpdateScreenState.DOWNLOADING) "Downloading Binary..." else flashStatusText,
                            style = typography.caption,
                            fontWeight = FontWeight.Bold,
                            color = colors.foreground
                        )
                        Text(
                            text = "$pct%",
                            style = typography.caption,
                            fontWeight = FontWeight.Bold,
                            color = colors.primary
                        )
                    }
                    Spacer(modifier = Modifier.height(8.dp))
                    LinearProgressIndicator(
                        progress = { pct / 100f },
                        modifier = Modifier
                            .fillMaxWidth()
                            .height(8.dp)
                            .clip(RoundedCornerShape(4.dp)),
                        color = colors.primary,
                        trackColor = colors.secondary
                    )
                    if (screenState == UpdateScreenState.FLASHING) {
                        Spacer(modifier = Modifier.height(6.dp))
                        Text(
                            text = "⚡ Sugarota screen is displaying update progress. Please keep devices on and close.",
                            style = typography.caption,
                            color = colors.mutedForeground
                        )
                    }
                }
            }

            // Success Card
            AnimatedVisibility(visible = screenState == UpdateScreenState.COMPLETE) {
                Column(
                    modifier = Modifier
                        .fillMaxWidth()
                        .clip(RoundedCornerShape(ShadcnTheme.shapes.radiusMedium))
                        .background(Color(0xFF00E676).copy(alpha = 0.12f))
                        .border(
                            BorderStroke(1.dp, Color(0xFF00E676).copy(alpha = 0.35f)),
                            RoundedCornerShape(ShadcnTheme.shapes.radiusMedium)
                        )
                        .padding(16.dp)
                ) {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Icon(
                            imageVector = Icons.Default.CheckCircle,
                            contentDescription = null,
                            tint = Color(0xFF00E676),
                            modifier = Modifier.size(24.dp)
                        )
                        Spacer(modifier = Modifier.width(10.dp))
                        Text(
                            text = "Update Complete!",
                            style = typography.body,
                            fontWeight = FontWeight.Bold,
                            color = Color(0xFF00E676)
                        )
                    }
                    Spacer(modifier = Modifier.height(6.dp))
                    Text(
                        text = "The new firmware was written successfully. Sugarota is now rebooting.",
                        style = typography.caption,
                        color = colors.foreground
                    )
                }
            }

            // Error message banner
            errorMessage?.let { err ->
                Spacer(modifier = Modifier.height(12.dp))
                ShadcnBadge(
                    text = err,
                    variant = ShadcnButtonVariant.DESTRUCTIVE,
                    modifier = Modifier.fillMaxWidth()
                )
            }

            Spacer(modifier = Modifier.height(16.dp))

            // Changelog Section
            Text(
                text = "CHANGELOG",
                style = typography.caption,
                fontWeight = FontWeight.Bold,
                color = colors.foreground
            )
            Spacer(modifier = Modifier.height(8.dp))

            ShadcnCard(
                modifier = Modifier.fillMaxWidth()
            ) {
                val notes = releaseInfo?.changelog ?: "Loading release notes..."
                MarkdownChangelogView(markdown = notes)
            }

            Spacer(modifier = Modifier.height(16.dp))
        }
    }
}

/**
 * Renders Markdown-formatted changelog notes with Shadcn typography,
 * handling headers (H1, H2, H3), horizontal dividers, bullet lists, bold text, and inline code.
 */
@Composable
fun MarkdownChangelogView(
    markdown: String,
    modifier: Modifier = Modifier
) {
    val colors = ShadcnTheme.colors
    val typography = ShadcnTheme.typography

    Column(
        modifier = modifier.fillMaxWidth(),
        verticalArrangement = Arrangement.spacedBy(4.dp)
    ) {
        val lines = markdown.lines()
        for (line in lines) {
            val trimmed = line.trim()
            when {
                trimmed.isBlank() -> {
                    Spacer(modifier = Modifier.height(4.dp))
                }
                trimmed == "---" || trimmed == "***" || trimmed == "___" -> {
                    HorizontalDivider(
                        modifier = Modifier
                            .fillMaxWidth()
                            .padding(vertical = 6.dp),
                        color = colors.border,
                        thickness = 1.dp
                    )
                }
                trimmed.startsWith("# ") -> {
                    Text(
                        text = trimmed.removePrefix("# ").trim(),
                        style = typography.h2,
                        color = colors.foreground,
                        fontWeight = FontWeight.Bold,
                        modifier = Modifier.padding(top = 4.dp, bottom = 2.dp)
                    )
                }
                trimmed.startsWith("## ") -> {
                    Text(
                        text = trimmed.removePrefix("## ").trim(),
                        style = typography.h3,
                        color = colors.primary,
                        fontWeight = FontWeight.Bold,
                        modifier = Modifier.padding(top = 8.dp, bottom = 2.dp)
                    )
                }
                trimmed.startsWith("### ") -> {
                    Text(
                        text = trimmed.removePrefix("### ").trim(),
                        style = typography.body,
                        color = colors.foreground,
                        fontWeight = FontWeight.SemiBold,
                        modifier = Modifier.padding(top = 6.dp, bottom = 2.dp)
                    )
                }
                trimmed.startsWith("- ") || trimmed.startsWith("* ") || trimmed.startsWith("• ") -> {
                    val content = trimmed.substring(2).trim()
                    Row(
                        modifier = Modifier
                            .fillMaxWidth()
                            .padding(vertical = 1.dp),
                        verticalAlignment = Alignment.Top
                    ) {
                        Text(
                            text = "•",
                            style = typography.body,
                            color = colors.primary,
                            modifier = Modifier.padding(end = 8.dp, start = 2.dp)
                        )
                        Text(
                            text = parseInlineMarkdown(content, colors.mutedForeground, colors.foreground),
                            style = typography.caption.copy(lineHeight = 18.sp),
                            color = colors.mutedForeground,
                            modifier = Modifier.weight(1f)
                        )
                    }
                }
                else -> {
                    Text(
                        text = parseInlineMarkdown(trimmed, colors.mutedForeground, colors.foreground),
                        style = typography.caption.copy(lineHeight = 18.sp),
                        color = colors.mutedForeground
                    )
                }
            }
        }
    }
}

/**
 * Parses bold `**text**` and inline `` `code` `` spans into an AnnotatedString.
 */
private fun parseInlineMarkdown(
    text: String,
    normalColor: Color,
    highlightColor: Color
): AnnotatedString {
    return buildAnnotatedString {
        var i = 0
        while (i < text.length) {
            // Check bold **bold**
            if (i + 1 < text.length && text[i] == '*' && text[i + 1] == '*') {
                val end = text.indexOf("**", i + 2)
                if (end != -1) {
                    val boldText = text.substring(i + 2, end)
                    pushStyle(SpanStyle(fontWeight = FontWeight.Bold, color = highlightColor))
                    append(boldText)
                    pop()
                    i = end + 2
                    continue
                }
            }
            // Check inline code `code`
            if (text[i] == '`') {
                val end = text.indexOf('`', i + 1)
                if (end != -1) {
                    val codeText = text.substring(i + 1, end)
                    pushStyle(SpanStyle(fontFamily = FontFamily.Monospace, color = highlightColor, background = highlightColor.copy(alpha = 0.1f)))
                    append(" $codeText ")
                    pop()
                    i = end + 1
                    continue
                }
            }
            append(text[i])
            i++
        }
    }
}
