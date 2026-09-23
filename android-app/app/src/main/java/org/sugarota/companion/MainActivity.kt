package org.sugarota.companion

import android.Manifest
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.ServiceConnection
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import android.os.IBinder
import androidx.activity.ComponentActivity
import androidx.activity.compose.BackHandler
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.core.*
import androidx.compose.animation.slideInHorizontally
import androidx.compose.animation.slideOutHorizontally
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.gestures.detectDragGesturesAfterLongPress
import androidx.compose.foundation.gestures.detectHorizontalDragGestures
import androidx.compose.foundation.layout.*
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.zIndex
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.material3.pulltorefresh.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.input.nestedscroll.NestedScrollConnection
import androidx.compose.ui.input.nestedscroll.NestedScrollSource
import androidx.compose.ui.input.nestedscroll.nestedScroll
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.text.input.VisualTransformation
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.core.content.ContextCompat
import kotlinx.coroutines.launch
import org.sugarota.companion.model.SugarotaDevice
import org.sugarota.companion.service.SugarotaBleScanReceiver
import org.sugarota.companion.service.SugarotaBleService
import org.sugarota.companion.ui.*
import org.sugarota.companion.ui.components.*
import org.sugarota.companion.ui.theme.*

class MainActivity : ComponentActivity() {

    private var bleService by mutableStateOf<SugarotaBleService?>(null)
    private var isBound by mutableStateOf(false)

    private val serviceConnection = object : ServiceConnection {
        override fun onServiceConnected(name: ComponentName?, service: IBinder?) {
            val binder = service as SugarotaBleService.LocalBinder
            bleService = binder.getService()
            isBound = true
            handleIncomingIntent(intent)
        }

        override fun onServiceDisconnected(name: ComponentName?) {
            bleService = null
            isBound = false
        }
    }

    private val permissionLauncher = registerForActivityResult(
        ActivityResultContracts.RequestMultiplePermissions()
    ) { _ ->
        startAndBindService()
    }

    private fun startAndBindService() {
        val serviceIntent = Intent(this, SugarotaBleService::class.java)
        ContextCompat.startForegroundService(this, serviceIntent)
        bindService(serviceIntent, serviceConnection, Context.BIND_AUTO_CREATE)
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        checkAndRequestPermissions()

        setContent {
            CompositionLocalProvider(
                LocalShadcnColors provides ShadcnColors(),
                LocalShadcnTypography provides ShadcnTypography(),
                LocalShadcnShapes provides ShadcnShapes()
            ) {
                MaterialTheme(
                    colorScheme = darkColorScheme(
                        primary = Color(0xFF00E676),
                        background = Color(0xFF09090B),
                        surface = Color(0xFF18181B),
                        onBackground = Color(0xFFFAFAFA),
                        onSurface = Color(0xFFFAFAFA)
                    )
                ) {
                    var showSplash by remember { mutableStateOf(true) }

                    LaunchedEffect(Unit) {
                        kotlinx.coroutines.delay(1000L)
                        showSplash = false
                    }

                    Surface(
                        modifier = Modifier.fillMaxSize(),
                        color = ShadcnTheme.colors.background
                    ) {
                        if (showSplash) {
                            SplashWelcomeScreen()
                        } else {
                            CompanionAppContent(bleService)
                        }
                    }
                }
            }
        }
    }

    private fun checkAndRequestPermissions() {
        val required = mutableListOf<String>()
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            required.add(Manifest.permission.BLUETOOTH_SCAN)
            required.add(Manifest.permission.BLUETOOTH_CONNECT)
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            required.add(Manifest.permission.POST_NOTIFICATIONS)
        }

        val allGranted = required.all {
            ContextCompat.checkSelfPermission(this, it) == PackageManager.PERMISSION_GRANTED
        }

        if (allGranted) {
            startAndBindService()
        } else {
            permissionLauncher.launch(required.toTypedArray())
        }
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        setIntent(intent)
        handleIncomingIntent(intent)
    }

    private fun handleIncomingIntent(intent: Intent?) {
        val address = intent?.getStringExtra(SugarotaBleScanReceiver.EXTRA_DEVICE_ADDRESS) ?: return
        SugarotaBleScanReceiver.clearNotificationForDevice(this, address)
        bleService?.connectDevice(address)
    }

    override fun onDestroy() {
        super.onDestroy()
        if (isBound) {
            unbindService(serviceConnection)
            isBound = false
        }
    }
}

@OptIn(ExperimentalMaterial3Api::class, androidx.compose.foundation.ExperimentalFoundationApi::class)
@Composable
fun CompanionAppContent(service: SugarotaBleService?) {
    val currentService by rememberUpdatedState(service)
    val devicesMap by service?.devices?.collectAsState() ?: remember { mutableStateOf(emptyMap()) }
    val deviceOrder by service?.deviceOrder?.collectAsState() ?: remember { mutableStateOf(emptyList()) }
    val deviceReadings by service?.deviceReadings?.collectAsState() ?: remember { mutableStateOf(emptyMap()) }
    val deviceConfigured by service?.deviceConfigured?.collectAsState() ?: remember { mutableStateOf(emptyMap()) }

    // Sort device list according to user custom order
    val sortedDeviceList = remember(devicesMap, deviceOrder) {
        val knownMap = devicesMap.toMutableMap()
        val result = mutableListOf<SugarotaDevice>()
        for (addr in deviceOrder) {
            knownMap.remove(addr)?.let { result.add(it) }
        }
        // Append any new devices not yet in order
        result.addAll(knownMap.values)
        result
    }

    val serviceScanning by service?.isScanning?.collectAsState() ?: remember { mutableStateOf(false) }
    val isScanning = serviceScanning

    var selectedDeviceAddress by remember { mutableStateOf<String?>(null) }
    var targetDeviceTab by remember { mutableStateOf(DeviceScreenTab.CHART) }
    val bridgeStatusText by service?.bridgeStatus?.collectAsState() ?: remember { mutableStateOf("Idle") }
    val lastReading by service?.lastReading?.collectAsState() ?: remember { mutableStateOf(null) }

    // Dialog state for confirming Forget scenario
    var deviceToForget by remember { mutableStateOf<SugarotaDevice?>(null) }

    val colors = ShadcnTheme.colors
    val typography = ShadcnTheme.typography

    // Rotation animation for the scan icon while scanning
    val infiniteTransition = rememberInfiniteTransition(label = "scan_spin")
    val rotation by infiniteTransition.animateFloat(
        initialValue = 0f,
        targetValue = 360f,
        animationSpec = infiniteRepeatable(
            animation = tween(900, easing = LinearEasing),
            repeatMode = RepeatMode.Restart
        ),
        label = "scan_rotation"
    )

    val density = androidx.compose.ui.platform.LocalDensity.current
    val scanThresholdPx = with(density) { 38.dp.toPx() }
    val maxPullPx = with(density) { 85.dp.toPx() }

    var pullDistancePx by remember { mutableFloatStateOf(0f) }
    var hasReachedThreshold by remember { mutableStateOf(false) }
    var isHandlingRelease by remember { mutableStateOf(false) }
    val coroutineScope = rememberCoroutineScope()
    val haptic = androidx.compose.ui.platform.LocalHapticFeedback.current

    // Provide haptic tick when threshold is crossed
    LaunchedEffect(hasReachedThreshold) {
        if (hasReachedThreshold && !isScanning) {
            haptic.performHapticFeedback(androidx.compose.ui.hapticfeedback.HapticFeedbackType.LongPress)
        }
    }

    val onPullRelease: () -> Unit = {
        val distance = pullDistancePx
        val shouldTrigger = hasReachedThreshold || (distance >= scanThresholdPx)
        val activeService = currentService
        if ((distance > 0f || shouldTrigger) && !isHandlingRelease) {
            android.util.Log.i("SugarotaPull", "onPullRelease: distance=$distance, threshold=$scanThresholdPx, shouldTrigger=$shouldTrigger, service=${if (activeService != null) "Ready" else "NULL"}")
            isHandlingRelease = true
            hasReachedThreshold = false
            if (shouldTrigger) {
                android.util.Log.i("SugarotaPull", "Calling activeService?.triggerScan()")
                activeService?.triggerScan()
            }
            coroutineScope.launch {
                try {
                    if (distance > 0f) {
                        androidx.compose.animation.core.animate(
                            initialValue = distance,
                            targetValue = 0f,
                            animationSpec = tween(200)
                        ) { value, _ ->
                            pullDistancePx = value
                        }
                    }
                } finally {
                    pullDistancePx = 0f
                    isHandlingRelease = false
                }
            }
        }
    }
    val currentOnPullRelease by rememberUpdatedState(onPullRelease)

    // When pulling down (drag fraction 0..1), rotate icon according to pull progress; when scanning, spin continuously
    val pullFraction = if (hasReachedThreshold) 1f else (pullDistancePx / scanThresholdPx).coerceIn(0f, 1f)
    val scanIconRotation = if (isScanning) rotation else (pullFraction * 180f)

    Scaffold(
        containerColor = colors.background,
        topBar = {
            TopAppBar(
                title = {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Text(
                            text = "Sugarota Companion",
                            style = typography.h2
                        )
                    }
                },
                actions = {
                    // Scan icon removed per requirement
                },
                colors = TopAppBarDefaults.topAppBarColors(
                    containerColor = colors.background,
                    titleContentColor = colors.foreground,
                    actionIconContentColor = colors.primary
                )
            )
        }
    ) { padding ->
        val nestedScrollConnection = remember(scanThresholdPx, maxPullPx) {
            object : androidx.compose.ui.input.nestedscroll.NestedScrollConnection {
                override fun onPreScroll(available: androidx.compose.ui.geometry.Offset, source: androidx.compose.ui.input.nestedscroll.NestedScrollSource): androidx.compose.ui.geometry.Offset {
                    // While pulling down, if dragging back upward before release
                    if (available.y < 0 && pullDistancePx > 0f) {
                        val consumed = (-available.y).coerceAtMost(pullDistancePx)
                        pullDistancePx -= consumed
                        if (pullDistancePx <= 10f) {
                            hasReachedThreshold = false
                        }
                        return androidx.compose.ui.geometry.Offset(0f, -consumed)
                    }
                    return androidx.compose.ui.geometry.Offset.Zero
                }

                override fun onPostScroll(consumed: androidx.compose.ui.geometry.Offset, available: androidx.compose.ui.geometry.Offset, source: androidx.compose.ui.input.nestedscroll.NestedScrollSource): androidx.compose.ui.geometry.Offset {
                    // When scrolled all the way to top and dragging further down
                    if (available.y > 0 && !isScanning) {
                        val newDistance = (pullDistancePx + available.y * 0.75f).coerceAtMost(maxPullPx)
                        pullDistancePx = newDistance
                        if (newDistance >= scanThresholdPx) {
                            hasReachedThreshold = true
                        }
                        return androidx.compose.ui.geometry.Offset(0f, available.y)
                    }
                    return androidx.compose.ui.geometry.Offset.Zero
                }

                override suspend fun onPreFling(available: androidx.compose.ui.unit.Velocity): androidx.compose.ui.unit.Velocity {
                    currentOnPullRelease()
                    return androidx.compose.ui.unit.Velocity.Zero
                }

                override suspend fun onPostFling(consumed: androidx.compose.ui.unit.Velocity, available: androidx.compose.ui.unit.Velocity): androidx.compose.ui.unit.Velocity {
                    currentOnPullRelease()
                    return androidx.compose.ui.unit.Velocity.Zero
                }
            }
        }

        // Pull-to-refresh disabled temporarily per user request to avoid gesture conflicts with drag-to-sort
        val enablePullToRefresh = false

        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(padding)
                .then(
                    if (enablePullToRefresh) {
                        Modifier
                            .nestedScroll(nestedScrollConnection)
                            .pointerInput(Unit) {
                                awaitEachGesture {
                                    awaitFirstDown(requireUnconsumed = false)
                                    do {
                                        val event = awaitPointerEvent(androidx.compose.ui.input.pointer.PointerEventPass.Initial)
                                    } while (event.changes.any { it.pressed })
                                    if (pullDistancePx > 0f || hasReachedThreshold) {
                                        currentOnPullRelease()
                                    }
                                }
                            }
                    } else {
                        Modifier
                    }
                )
                .padding(horizontal = 16.dp, vertical = 8.dp)
        ) {
            // Pull-to-refresh banner shows during drag and stays visible while active scanning
            val showBanner = enablePullToRefresh && (pullDistancePx > 5f || isScanning)
            androidx.compose.animation.AnimatedVisibility(
                visible = showBanner,
                enter = androidx.compose.animation.expandVertically() + androidx.compose.animation.fadeIn(),
                exit = androidx.compose.animation.shrinkVertically() + androidx.compose.animation.fadeOut()
            ) {
                Box(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(vertical = 8.dp),
                    contentAlignment = Alignment.Center
                ) {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        if (isScanning) {
                            CircularProgressIndicator(
                                modifier = Modifier.size(15.dp),
                                color = colors.primary,
                                strokeWidth = 2.dp
                            )
                        } else {
                            Icon(
                                imageVector = Icons.Default.Refresh,
                                contentDescription = null,
                                tint = colors.primary,
                                modifier = Modifier
                                    .size(15.dp)
                                    .graphicsLayer {
                                        rotationZ = scanIconRotation
                                    }
                            )
                        }
                        Spacer(modifier = Modifier.width(8.dp))
                        Text(
                            text = if (isScanning) {
                                "Scanning..."
                            } else if (hasReachedThreshold || pullDistancePx >= scanThresholdPx) {
                                "Release to scan"
                            } else {
                                "Pull down to scan"
                            },
                            style = typography.caption.copy(
                                fontWeight = FontWeight.Bold
                            ),
                            color = colors.primary
                        )
                    }
                }
            }

            Text(
                text = "Connected Displays",
                style = typography.h3,
                color = colors.mutedForeground
            )
            Spacer(modifier = Modifier.height(10.dp))

                if (sortedDeviceList.isEmpty()) {
                    val context = androidx.compose.ui.platform.LocalContext.current
                    val currentAppVersion = remember(context) {
                        try {
                            val pInfo = context.packageManager.getPackageInfo(context.packageName, 0)
                            pInfo.versionName ?: "v0.09.09.0"
                        } catch (e: Exception) {
                            "v0.09.09.0"
                        }
                    }
                    Box(
                        modifier = Modifier
                            .fillMaxWidth()
                            .weight(1f)
                            .verticalScroll(rememberScrollState()),
                        contentAlignment = Alignment.Center
                    ) {
                        Column(
                            horizontalAlignment = Alignment.CenterHorizontally,
                            modifier = Modifier.padding(horizontal = 24.dp, vertical = 16.dp)
                        ) {
                            androidx.compose.foundation.Image(
                                painter = androidx.compose.ui.res.painterResource(id = org.sugarota.companion.R.drawable.ic_sugarota_logo),
                                contentDescription = "Sugarota Logo",
                                modifier = Modifier
                                    .size(88.dp)
                                    .padding(bottom = 12.dp)
                            )
                            Text(
                                text = "Sugarota Companion",
                                style = typography.h3,
                                color = colors.foreground,
                                fontWeight = FontWeight.Bold
                            )
                            Spacer(modifier = Modifier.height(4.dp))
                            Text(
                                text = currentAppVersion,
                                style = typography.caption,
                                color = colors.mutedForeground.copy(alpha = 0.7f)
                            )
                            Spacer(modifier = Modifier.height(12.dp))
                            Text(
                                text = "Pull down or power on Sugarota to scan & pair.",
                                style = typography.caption,
                                color = colors.mutedForeground
                            )
                        }
                    }
                } else {
                    val lazyListState = androidx.compose.foundation.lazy.rememberLazyListState()
                    var draggedDeviceAddress by remember { mutableStateOf<String?>(null) }
                    var settlingDeviceAddress by remember { mutableStateOf<String?>(null) }
                    var dragDisplacementY by remember { mutableFloatStateOf(0f) }
                    val settleOffsetY = remember { Animatable(0f) }
                    // Single stable state instance across re-renders to prevent pointerInput closure from holding a stale delegate
                    var currentOrderList by remember { mutableStateOf(sortedDeviceList) }
                    var initialDragIndex by remember { mutableIntStateOf(-1) }
                    var initialItemCenters by remember { mutableStateOf<Map<Int, Float>>(emptyMap()) }
                    var initialItemOffsets by remember { mutableStateOf<Map<Int, Float>>(emptyMap()) }
                    val dragScope = rememberCoroutineScope()
                    val currentSortedDeviceList by rememberUpdatedState(sortedDeviceList)

                    // Sync when sortedDeviceList changes outside of active drag session
                    LaunchedEffect(sortedDeviceList) {
                        if (draggedDeviceAddress == null && settlingDeviceAddress == null) {
                            currentOrderList = sortedDeviceList
                        }
                    }

                    var swipedDeviceAddress by remember { mutableStateOf<String?>(null) }

                    LazyColumn(
                        state = lazyListState,
                        modifier = Modifier
                            .weight(1f)
                            .pointerInput(Unit) {
                                detectDragGesturesAfterLongPress(
                                    onDragStart = { offset ->
                                        val items = lazyListState.layoutInfo.visibleItemsInfo
                                        val hitItem = items.firstOrNull {
                                            offset.y.toInt() in it.offset..(it.offset + it.size)
                                        }
                                        if (hitItem != null && hitItem.index in currentOrderList.indices) {
                                            initialDragIndex = hitItem.index
                                            draggedDeviceAddress = currentOrderList.getOrNull(hitItem.index)?.address
                                            dragDisplacementY = 0f
                                            settlingDeviceAddress = null
                                            // Capture static initial slot geometry so we never read stale layoutInfo mid-drag
                                            initialItemCenters = items.associate { it.index to (it.offset + it.size / 2f) }
                                            initialItemOffsets = items.associate { it.index to it.offset.toFloat() }
                                        }
                                    },
                                    onDrag = { change, dragAmount ->
                                        change.consume()
                                        val currentAddr = draggedDeviceAddress ?: return@detectDragGesturesAfterLongPress
                                        dragDisplacementY += dragAmount.y

                                        val currentPos = currentOrderList.indexOfFirst { it.address == currentAddr }
                                        if (currentPos == -1 || initialDragIndex !in initialItemCenters) return@detectDragGesturesAfterLongPress

                                        val startCenter = initialItemCenters[initialDragIndex] ?: return@detectDragGesturesAfterLongPress
                                        val currentDraggedCenter = startCenter + dragDisplacementY

                                        // Slot swap check with directional hysteresis (requires crossing 60% towards the target slot, providing a 20% deadband)
                                        val nextCenter = initialItemCenters[currentPos + 1]
                                        val prevCenter = initialItemCenters[currentPos - 1]
                                        val curCenter = initialItemCenters[currentPos] ?: startCenter

                                        val targetIndex = when {
                                            nextCenter != null && currentDraggedCenter > curCenter + (nextCenter - curCenter) * 0.6f -> currentPos + 1
                                            prevCenter != null && currentDraggedCenter < curCenter - (curCenter - prevCenter) * 0.6f -> currentPos - 1
                                            else -> null
                                        }

                                        // Swap synchronously in state
                                        if (targetIndex != null && targetIndex in currentOrderList.indices) {
                                            val reordered = currentOrderList.toMutableList()
                                            val item = reordered.removeAt(currentPos)
                                            reordered.add(targetIndex, item)
                                            currentOrderList = reordered
                                        }
                                    },
                                    onDragEnd = {
                                        val currentAddr = draggedDeviceAddress
                                        if (currentAddr != null) {
                                            val currentPos = currentOrderList.indexOfFirst { it.address == currentAddr }
                                            val originOffset = initialItemOffsets[initialDragIndex] ?: 0f
                                            val currentSlotOffset = initialItemOffsets[currentPos] ?: originOffset
                                            val currentVisualTranslationY = dragDisplacementY - (currentSlotOffset - originOffset)

                                            draggedDeviceAddress = null
                                            settlingDeviceAddress = currentAddr

                                            dragScope.launch {
                                                settleOffsetY.snapTo(currentVisualTranslationY)
                                                settleOffsetY.animateTo(
                                                    0f,
                                                    spring(stiffness = Spring.StiffnessMediumLow, dampingRatio = Spring.DampingRatioLowBouncy)
                                                )
                                                settlingDeviceAddress = null
                                                val finalOrder = currentOrderList.map { it.address }
                                                currentService?.saveDeviceOrder(finalOrder)
                                            }
                                        } else {
                                            draggedDeviceAddress = null
                                        }
                                    },
                                    onDragCancel = {
                                        draggedDeviceAddress = null
                                        settlingDeviceAddress = null
                                        currentOrderList = currentSortedDeviceList
                                    }
                                )
                            },
                        verticalArrangement = Arrangement.spacedBy(10.dp)
                    ) {
                        items(
                            items = currentOrderList,
                            key = { it.address }
                        ) { device ->
                            val isDragging = draggedDeviceAddress == device.address
                            val isSettling = settlingDeviceAddress == device.address
                            val currentPos = currentOrderList.indexOfFirst { it.address == device.address }
                            val originOffset = initialItemOffsets[initialDragIndex] ?: 0f
                            val currentSlotOffset = initialItemOffsets[currentPos] ?: originOffset
                            val activeVisualTranslationY = if (isDragging) {
                                dragDisplacementY - (currentSlotOffset - originOffset)
                            } else if (isSettling) {
                                settleOffsetY.value
                            } else 0f

                            val devReading = deviceReadings[device.address] ?: if (device.address == currentOrderList.firstOrNull()?.address) lastReading else null
                            val isConfigured = deviceConfigured[device.address] ?: true

                            // Custom horizontal swipe-to-forget with snap threshold
                            val currentDensity = androidx.compose.ui.platform.LocalDensity.current
                            val maxSwipePx = with(currentDensity) { -90.dp.toPx() } // snap distance
                            val swipeOffsetX = remember(device.address) { androidx.compose.animation.core.Animatable(0f) }
                            val swipeScope = rememberCoroutineScope()

                            // Auto-close swipe when another card becomes active
                            LaunchedEffect(swipedDeviceAddress) {
                                if (swipedDeviceAddress != device.address && swipeOffsetX.value != 0f) {
                                    swipeOffsetX.animateTo(0f, spring(stiffness = Spring.StiffnessMediumLow))
                                }
                            }

                            Box(
                                modifier = Modifier
                                    .fillMaxWidth()
                                    .then(
                                        if (!isDragging && !isSettling) {
                                            Modifier.animateItemPlacement(
                                                animationSpec = spring(
                                                    stiffness = Spring.StiffnessHigh,
                                                    dampingRatio = Spring.DampingRatioNoBouncy
                                                )
                                            )
                                        } else {
                                            Modifier
                                        }
                                    )
                                    .zIndex(if (isDragging || isSettling) 10f else 1f)
                                    .graphicsLayer {
                                        if (isDragging || isSettling) {
                                            this.translationY = activeVisualTranslationY
                                            this.scaleX = 1.03f
                                            this.scaleY = 1.03f
                                            this.shadowElevation = 20f
                                        }
                                    }
                                    .clip(RoundedCornerShape(ShadcnTheme.shapes.radiusLarge))
                            ) {
                                // Background Forget action button, revealed only when swiping left
                                if (swipeOffsetX.value < -2f) {
                                    Box(
                                        modifier = Modifier
                                            .matchParentSize()
                                            .background(Color(0xFFDC2626)) // destructive red
                                            .clickable {
                                                deviceToForget = device
                                                swipedDeviceAddress = null
                                                swipeScope.launch {
                                                    swipeOffsetX.animateTo(0f, spring(stiffness = Spring.StiffnessMediumLow))
                                                }
                                            }
                                            .padding(end = 22.dp),
                                        contentAlignment = Alignment.CenterEnd
                                    ) {
                                        Column(
                                            horizontalAlignment = Alignment.CenterHorizontally,
                                            verticalArrangement = Arrangement.Center
                                        ) {
                                            Icon(
                                                imageVector = Icons.Default.DeleteOutline,
                                                contentDescription = "Forget",
                                                tint = Color.White,
                                                modifier = Modifier.size(24.dp)
                                            )
                                            Spacer(modifier = Modifier.height(2.dp))
                                            Text(
                                                text = "Forget",
                                                color = Color.White,
                                                fontWeight = FontWeight.Bold,
                                                style = typography.caption
                                            )
                                        }
                                    }
                                }

                                // Foreground Device Card with constrained swipe offset
                                Box(
                                    modifier = Modifier
                                        .fillMaxWidth()
                                        .offset { IntOffset(swipeOffsetX.value.toInt(), 0) }
                                        .pointerInput(isDragging) {
                                            if (!isDragging) {
                                                detectHorizontalDragGestures(
                                                    onDragEnd = {
                                                        swipeScope.launch {
                                                            // Snap to revealed state (-90dp) if dragged past 50% threshold, else snap back to 0
                                                            if (swipeOffsetX.value <= maxSwipePx * 0.5f) {
                                                                swipedDeviceAddress = device.address
                                                                swipeOffsetX.animateTo(maxSwipePx, spring(stiffness = Spring.StiffnessMediumLow))
                                                            } else {
                                                                if (swipedDeviceAddress == device.address) {
                                                                    swipedDeviceAddress = null
                                                                }
                                                                swipeOffsetX.animateTo(0f, spring(stiffness = Spring.StiffnessMediumLow))
                                                            }
                                                        }
                                                    },
                                                    onDragCancel = {
                                                        if (swipedDeviceAddress == device.address) {
                                                            swipedDeviceAddress = null
                                                        }
                                                        swipeScope.launch {
                                                            swipeOffsetX.animateTo(0f, spring(stiffness = Spring.StiffnessMediumLow))
                                                        }
                                                    },
                                                    onHorizontalDrag = { change, dragAmount ->
                                                        change.consume()
                                                        val newOffset = (swipeOffsetX.value + dragAmount).coerceIn(maxSwipePx, 0f)
                                                        if (dragAmount < 0f && swipedDeviceAddress != device.address) {
                                                            swipedDeviceAddress = device.address
                                                        }
                                                        swipeScope.launch {
                                                            swipeOffsetX.snapTo(newOffset)
                                                        }
                                                    }
                                                )
                                            }
                                        }
                                ) {
                                    val devUnits = devReading?.units?.takeIf { it.isNotBlank() } ?: service?.getDeviceUnits(device.address) ?: "mg/dL"
                                    DeviceCard(
                                        device = device,
                                        bridgeStatusText = bridgeStatusText,
                                        lastReading = devReading,
                                        units = devUnits,
                                        isConfigured = isConfigured,
                                        onClick = {
                                            if (swipeOffsetX.value < -5f) {
                                                swipeScope.launch {
                                                    swipeOffsetX.animateTo(0f, spring(stiffness = Spring.StiffnessMediumLow))
                                                }
                                            } else {
                                                targetDeviceTab = DeviceScreenTab.CHART
                                                selectedDeviceAddress = device.address
                                            }
                                        },
                                        onConfigureClick = {
                                            targetDeviceTab = DeviceScreenTab.CONFIG
                                            selectedDeviceAddress = device.address
                                        },
                                        onConnect = { service?.connectDevice(device.address) },
                                        onSync = { service?.triggerManualSync() },
                                        onDisconnect = { service?.disconnectDevice(device.address) }
                                    )
                                }
                            }
                        }
                    }
                }

                Spacer(modifier = Modifier.height(12.dp))
                val context = androidx.compose.ui.platform.LocalContext.current
                val appVersion = remember(context) {
                    try {
                        val pInfo = context.packageManager.getPackageInfo(context.packageName, 0)
                        pInfo.versionName ?: "v0.09.09.0"
                    } catch (e: Exception) {
                        "v0.09.09.0"
                    }
                }
                Text(
                    text = "Sugarota Companion $appVersion",
                    style = typography.caption,
                    color = colors.mutedForeground.copy(alpha = 0.6f),
                    modifier = Modifier
                        .align(Alignment.CenterHorizontally)
                        .padding(bottom = 6.dp)
                )
            }
        }

    // Confirmation Dialog for Forget Device scenario
    if (deviceToForget != null) {
        val targetDev = deviceToForget!!
        androidx.compose.ui.window.Dialog(
            onDismissRequest = { deviceToForget = null },
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
                            androidx.compose.foundation.BorderStroke(1.dp, colors.border),
                            RoundedCornerShape(ShadcnTheme.shapes.radiusLarge)
                        )
                        .padding(20.dp)
                ) {
                    Text(
                        text = "Forget ${targetDev.name}?",
                        style = typography.h2,
                        color = colors.foreground
                    )
                    Spacer(modifier = Modifier.height(10.dp))
                    Text(
                        text = "This will unpair the device from your phone and clear its connection. The Sugarota device will continue operating as normal, but you will need to re-pair it to connect again.",
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
                            onClick = { deviceToForget = null },
                            variant = ShadcnButtonVariant.GHOST
                        ) {
                            Text(
                                text = "Cancel",
                                style = typography.body,
                                color = colors.mutedForeground
                            )
                        }
                        Spacer(modifier = Modifier.width(8.dp))
                        ShadcnButton(
                            onClick = {
                                val addr = targetDev.address
                                deviceToForget = null
                                if (selectedDeviceAddress == addr) {
                                    selectedDeviceAddress = null
                                }
                                service?.forgetDevice(addr)
                            },
                            variant = ShadcnButtonVariant.DESTRUCTIVE
                        ) {
                            Text(
                                text = "Forget Device",
                                color = Color.White,
                                fontWeight = FontWeight.Bold
                            )
                        }
                    }
                }
            }
        }
    }

    // Device Detail Screen (animated full screen overlay with Chart & Config tabs)
    AnimatedVisibility(
        visible = selectedDeviceAddress != null,
        enter = slideInHorizontally(initialOffsetX = { it }) + fadeIn(),
        exit = slideOutHorizontally(targetOffsetX = { it }) + fadeOut()
    ) {
        selectedDeviceAddress?.let { targetAddress ->
            DeviceDetailScreen(
                deviceAddress = targetAddress,
                initialTab = targetDeviceTab,
                service = service,
                onDismiss = { selectedDeviceAddress = null }
            )
        }
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun DeviceConfigScreen(
    deviceAddress: String,
    deviceName: String,
    service: SugarotaBleService?,
    showHeader: Boolean = true,
    onOpenFirmwareUpdate: (() -> Unit)? = null,
    onDismiss: () -> Unit
) {
    BackHandler(onBack = onDismiss)

    var rawConfigText by remember { mutableStateOf("") }
    var isLoading by remember { mutableStateOf(true) }
    var isSaving by remember { mutableStateOf(false) }
    var statusMessage by remember { mutableStateOf<String?>(null) }
    var showRawJson by remember { mutableStateOf(false) }
    var showFirmwareUpdateScreen by remember { mutableStateOf(false) }
    val scope = rememberCoroutineScope()

    val colors = ShadcnTheme.colors
    val typography = ShadcnTheme.typography

    // Custom device name (in-app only)
    var customName by remember { mutableStateOf(deviceName) }
    var showRenameDialog by remember { mutableStateOf(false) }
    var editNameText by remember { mutableStateOf(customName) }

    // Form fields parsed from /config.json
    var provider by remember { mutableStateOf("NIGHTSCOUT") }
    var nsUrl by remember { mutableStateOf("") }
    var nsSecret by remember { mutableStateOf("") }
    var dexUser by remember { mutableStateOf("") }
    var dexPass by remember { mutableStateOf("") }
    var dexServer by remember { mutableStateOf("shareous1.dexcom.com") }
    var primarySsid by remember { mutableStateOf("") }
    var primaryPass by remember { mutableStateOf("") }
    var secondarySsid by remember { mutableStateOf("") }
    var secondaryPass by remember { mutableStateOf("") }
    var activeWifiTab by remember { mutableStateOf("PRIMARY") }
    var useSecondaryFirst by remember { mutableStateOf(false) }
    var units by remember { mutableStateOf("mg/dL") }
    var debugMode by remember { mutableStateOf(false) }
    var connectionMode by remember { mutableStateOf("AUTO") }
    var pollIntervalSec by remember { mutableIntStateOf(60) }

    // Password preview toggles (temporary reveal with eye icon)
    var showPrimaryPass by remember { mutableStateOf(false) }
    var showSecondaryPass by remember { mutableStateOf(false) }
    var showDexPass by remember { mutableStateOf(false) }
    var showNsSecret by remember { mutableStateOf(false) }

    fun populateFromText(text: String) {
        try {
            val root = org.json.JSONObject(text)
            provider = root.optString("provider", "NIGHTSCOUT").uppercase()
            units = root.optString("units", "mg/dL")
            debugMode = root.optBoolean("debug", false)
            connectionMode = root.optString("connection_mode", "AUTO").uppercase().ifBlank { "AUTO" }
            pollIntervalSec = root.optInt("poll_interval_sec", 60).coerceIn(30, 600)

            val ns = root.optJSONObject("nightscout")
            nsUrl = ns?.optString("url", "") ?: ""
            nsSecret = ns?.optString("secret", "") ?: ""

            val dex = root.optJSONObject("dexcom")
            dexUser = dex?.optString("user", "") ?: ""
            dexPass = dex?.optString("pass", "") ?: ""
            dexServer = dex?.optString("server", "shareous1.dexcom.com") ?: "shareous1.dexcom.com"

            val wifi = root.optJSONObject("wifi")
            primarySsid = wifi?.optString("primary_ssid", "") ?: ""
            primaryPass = wifi?.optString("primary_pass", "") ?: ""
            secondarySsid = wifi?.optString("secondary_ssid", "") ?: ""
            secondaryPass = wifi?.optString("secondary_pass", "") ?: ""
            useSecondaryFirst = wifi?.optBoolean("use_secondary_first", false) ?: false
        } catch (e: Exception) {
            e.printStackTrace()
        }
    }

    LaunchedEffect(deviceAddress) {
        service?.readConfig(deviceAddress) { rawJson ->
            rawConfigText = try {
                if (rawJson.trim().startsWith("{")) {
                    val formatted = org.json.JSONObject(rawJson).toString(2)
                    populateFromText(formatted)
                    formatted
                } else {
                    rawJson
                }
            } catch (e: Exception) {
                rawJson
            }
            isLoading = false
        }
    }

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
                        .background(colors.card) // zinc-900
                        .border(
                            androidx.compose.foundation.BorderStroke(1.dp, colors.border), // zinc-800
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

    if (showFirmwareUpdateScreen) {
        val devObj = service?.devices?.collectAsState()?.value?.get(deviceAddress) ?: SugarotaDevice(
            name = customName.ifBlank { deviceName },
            address = deviceAddress
        )
        FirmwareUpdateScreen(
            device = devObj,
            service = service,
            onDismiss = { showFirmwareUpdateScreen = false }
        )
        return
    }

    Scaffold(
        containerColor = colors.background,
        topBar = {
            if (showHeader) {
                TopAppBar(
                    title = {
                        Column {
                            Row(
                                verticalAlignment = Alignment.CenterVertically
                            ) {
                                Text(
                                    text = customName.ifBlank { deviceName },
                                    style = typography.h2,
                                    color = colors.foreground
                                )
                                Spacer(modifier = Modifier.width(6.dp))
                                IconButton(
                                    onClick = {
                                        editNameText = customName.ifBlank { deviceName }
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
                    colors = TopAppBarDefaults.topAppBarColors(
                        containerColor = colors.card,
                        titleContentColor = colors.foreground,
                        navigationIconContentColor = colors.foreground
                    )
                )
            }
        },
        bottomBar = {
            Surface(
                color = colors.background,
                tonalElevation = 0.dp,
                shadowElevation = 0.dp
            ) {
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .background(colors.background)
                        .padding(horizontal = 16.dp, vertical = 12.dp)
                ) {
                    ShadcnButton(
                        onClick = {
                            isSaving = true
                            statusMessage = "Writing configuration..."

                            // Ensure latest custom device name is saved in app locally
                            service?.setDeviceCustomName(deviceAddress, customName)

                            try {
                                val root = try {
                                     if (rawConfigText.trim().startsWith("{")) org.json.JSONObject(rawConfigText) else org.json.JSONObject()
                                } catch (e: Exception) {
                                     org.json.JSONObject()
                                }

                                root.put("provider", provider)
                                root.put("units", units)
                                root.put("debug", debugMode)
                                root.put("connection_mode", connectionMode)
                                root.put("poll_interval_sec", pollIntervalSec)

                                val nsObj = root.optJSONObject("nightscout") ?: org.json.JSONObject()
                                nsObj.put("url", nsUrl.trim())
                                nsObj.put("secret", nsSecret.trim())
                                root.put("nightscout", nsObj)

                                val dexObj = root.optJSONObject("dexcom") ?: org.json.JSONObject()
                                dexObj.put("user", dexUser.trim())
                                dexObj.put("pass", dexPass.trim())
                                dexObj.put("server", dexServer.trim())
                                root.put("dexcom", dexObj)

                                val wifiObj = root.optJSONObject("wifi") ?: org.json.JSONObject()
                                wifiObj.put("primary_ssid", primarySsid.trim())
                                wifiObj.put("primary_pass", primaryPass.trim())
                                wifiObj.put("secondary_ssid", secondarySsid.trim())
                                wifiObj.put("secondary_pass", secondaryPass.trim())
                                wifiObj.put("use_secondary_first", useSecondaryFirst)
                                root.put("wifi", wifiObj)

                                val finalJson = root.toString(2)
                                rawConfigText = finalJson

                                if (service == null) {
                                    isSaving = false
                                    statusMessage = "BLE Service unavailable"
                                    return@ShadcnButton
                                }

                                service.writeConfig(deviceAddress, finalJson) { success ->
                                     isSaving = false
                                     if (success) {
                                         statusMessage = "Saved! Device rebooting..."
                                         scope.launch {
                                             kotlinx.coroutines.delay(400)
                                             onDismiss()
                                         }
                                     } else {
                                         statusMessage = "Failed to write configuration"
                                     }
                                }
                            } catch (e: Exception) {
                                isSaving = false
                                statusMessage = "Error: ${e.localizedMessage ?: "Unknown error"}"
                            }
                        },
                        variant = ShadcnButtonVariant.DEFAULT,
                        enabled = !isLoading && !isSaving,
                        isLoading = isSaving,
                        modifier = Modifier.fillMaxWidth()
                    ) {
                        Text(
                            text = "Save & Reboot",
                            style = typography.body,
                            fontWeight = FontWeight.Bold,
                            color = colors.primaryForeground
                        )
                    }
                }
            }
        }
    ) { padding ->
        if (isLoading) {
            Box(
                modifier = Modifier
                    .fillMaxSize()
                    .padding(padding),
                contentAlignment = Alignment.Center
            ) {
                CircularProgressIndicator(color = colors.primary)
            }
        } else {
            Column(
                modifier = Modifier
                    .fillMaxSize()
                    .padding(padding)
                    .verticalScroll(rememberScrollState())
                    .padding(16.dp)
            ) {
                // Status banner if present
                statusMessage?.let { msg ->
                    ShadcnBadge(
                        text = msg,
                        variant = if (msg.contains("Saved", ignoreCase = true) || msg.contains("Success", ignoreCase = true)) ShadcnButtonVariant.DEFAULT else ShadcnButtonVariant.DESTRUCTIVE,
                        modifier = Modifier.fillMaxWidth()
                    )
                    Spacer(modifier = Modifier.height(14.dp))
                }

                // Data Provider Switcher
                Text(
                    text = "DATA PROVIDER",
                    style = typography.caption,
                    fontWeight = FontWeight.Bold,
                    color = colors.foreground
                )
                Spacer(modifier = Modifier.height(8.dp))
                Row(modifier = Modifier.fillMaxWidth()) {
                    ShadcnChip(
                        selected = provider == "NIGHTSCOUT",
                        onClick = { provider = "NIGHTSCOUT" },
                        label = "Nightscout",
                        modifier = Modifier.weight(1f)
                    )
                    Spacer(modifier = Modifier.width(8.dp))
                    ShadcnChip(
                        selected = provider == "DEXCOM",
                        onClick = { provider = "DEXCOM" },
                        label = "Dexcom Share",
                        modifier = Modifier.weight(1f)
                    )
                }

                Spacer(modifier = Modifier.height(14.dp))

                if (provider == "NIGHTSCOUT") {
                    ShadcnInput(
                        value = nsUrl,
                        onValueChange = { nsUrl = it },
                        label = "Nightscout URL",
                        placeholder = "https://my-nightscout.fly.dev"
                    )
                    Spacer(modifier = Modifier.height(10.dp))
                    ShadcnInput(
                        value = nsSecret,
                        onValueChange = { nsSecret = it },
                        label = "API Secret (optional)",
                        placeholder = "my-secret-token",
                        visualTransformation = if (showNsSecret) VisualTransformation.None else PasswordVisualTransformation(),
                        trailingIcon = {
                            IconButton(
                                onClick = { showNsSecret = !showNsSecret },
                                modifier = Modifier.size(32.dp)
                            ) {
                                EyeToggleIcon(visible = showNsSecret, tint = colors.mutedForeground)
                            }
                        }
                    )
                } else {
                    ShadcnInput(
                        value = dexUser,
                        onValueChange = { dexUser = it },
                        label = "Dexcom Username",
                        placeholder = "username"
                    )
                    Spacer(modifier = Modifier.height(10.dp))
                    ShadcnInput(
                        value = dexPass,
                        onValueChange = { dexPass = it },
                        label = "Dexcom Password",
                        placeholder = "Password",
                        visualTransformation = if (showDexPass) VisualTransformation.None else PasswordVisualTransformation(),
                        trailingIcon = {
                            IconButton(
                                onClick = { showDexPass = !showDexPass },
                                modifier = Modifier.size(32.dp)
                            ) {
                                EyeToggleIcon(visible = showDexPass, tint = colors.mutedForeground)
                            }
                        }
                    )
                    Spacer(modifier = Modifier.height(10.dp))
                    ShadcnInput(
                        value = dexServer,
                        onValueChange = { dexServer = it },
                        label = "Dexcom Server",
                        placeholder = "shareous1.dexcom.com"
                    )
                }

                Spacer(modifier = Modifier.height(20.dp))

                // Units Switcher
                Text(
                    text = "GLUCOSE UNITS",
                    style = typography.caption,
                    fontWeight = FontWeight.Bold,
                    color = colors.foreground
                )
                Spacer(modifier = Modifier.height(8.dp))
                Row(modifier = Modifier.fillMaxWidth()) {
                    ShadcnChip(
                        selected = units == "mg/dL",
                        onClick = { units = "mg/dL" },
                        label = "mg/dL",
                        modifier = Modifier.weight(1f)
                    )
                    Spacer(modifier = Modifier.width(8.dp))
                    ShadcnChip(
                        selected = units == "mmol/L",
                        onClick = { units = "mmol/L" },
                        label = "mmol/L",
                        modifier = Modifier.weight(1f)
                    )
                }

                Spacer(modifier = Modifier.height(20.dp))

                // Wi-Fi Configuration with Switcher
                Text(
                    text = "WI-FI CONFIGURATION",
                    style = typography.caption,
                    fontWeight = FontWeight.Bold,
                    color = colors.foreground
                )
                Spacer(modifier = Modifier.height(8.dp))
                Row(modifier = Modifier.fillMaxWidth()) {
                    ShadcnChip(
                        selected = activeWifiTab == "PRIMARY",
                        onClick = { activeWifiTab = "PRIMARY" },
                        label = "Primary Wi-Fi",
                        modifier = Modifier.weight(1f)
                    )
                    Spacer(modifier = Modifier.width(8.dp))
                    ShadcnChip(
                        selected = activeWifiTab == "SECONDARY",
                        onClick = { activeWifiTab = "SECONDARY" },
                        label = "Secondary Wi-Fi",
                        modifier = Modifier.weight(1f)
                    )
                }

                Spacer(modifier = Modifier.height(12.dp))

                if (activeWifiTab == "PRIMARY") {
                    ShadcnInput(
                        value = primarySsid,
                        onValueChange = { primarySsid = it },
                        label = "Primary SSID",
                        placeholder = "Network Name"
                    )
                    Spacer(modifier = Modifier.height(10.dp))
                    ShadcnInput(
                        value = primaryPass,
                        onValueChange = { primaryPass = it },
                        label = "Primary Password",
                        placeholder = "Password",
                        visualTransformation = if (showPrimaryPass) VisualTransformation.None else PasswordVisualTransformation(),
                        trailingIcon = {
                            IconButton(
                                onClick = { showPrimaryPass = !showPrimaryPass },
                                modifier = Modifier.size(32.dp)
                            ) {
                                EyeToggleIcon(visible = showPrimaryPass, tint = colors.mutedForeground)
                            }
                        }
                    )
                } else {
                    ShadcnInput(
                        value = secondarySsid,
                        onValueChange = { secondarySsid = it },
                        label = "Secondary SSID",
                        placeholder = "Backup / Mobile Hotspot Name"
                    )
                    Spacer(modifier = Modifier.height(10.dp))
                    ShadcnInput(
                        value = secondaryPass,
                        onValueChange = { secondaryPass = it },
                        label = "Secondary Password",
                        placeholder = "Password",
                        visualTransformation = if (showSecondaryPass) VisualTransformation.None else PasswordVisualTransformation(),
                        trailingIcon = {
                            IconButton(
                                onClick = { showSecondaryPass = !showSecondaryPass },
                                modifier = Modifier.size(32.dp)
                            ) {
                                EyeToggleIcon(visible = showSecondaryPass, tint = colors.mutedForeground)
                            }
                        }
                    )
                    Spacer(modifier = Modifier.height(12.dp))
                    // Connect priority switch
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceBetween,
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Column(modifier = Modifier.weight(1f)) {
                            Text(
                                text = "Try Secondary First",
                                style = typography.body,
                                color = colors.foreground
                            )
                            Text(
                                text = "Attempt connection to secondary network before primary",
                                style = typography.caption,
                                color = colors.mutedForeground
                            )
                        }
                        Switch(
                            checked = useSecondaryFirst,
                            onCheckedChange = { useSecondaryFirst = it },
                            colors = SwitchDefaults.colors(
                                checkedThumbColor = colors.primaryForeground,
                                checkedTrackColor = colors.primary,
                                uncheckedThumbColor = colors.mutedForeground,
                                uncheckedTrackColor = colors.secondary
                            )
                        )
                    }
                }

                Spacer(modifier = Modifier.height(20.dp))

                // Connection Mode Selector
                Text(
                    text = "CONNECTION MODE",
                    style = typography.caption,
                    fontWeight = FontWeight.Bold,
                    color = colors.foreground
                )
                Spacer(modifier = Modifier.height(4.dp))
                Text(
                    text = "AUTO uses BLE when phone is nearby (saving battery), with Wi-Fi fallback",
                    style = typography.caption,
                    color = colors.mutedForeground
                )
                Spacer(modifier = Modifier.height(8.dp))
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    listOf("AUTO", "BLE_ONLY", "WIFI_ONLY").forEach { mode ->
                        val isSelected = connectionMode.equals(mode, ignoreCase = true)
                        val label = when(mode) {
                            "BLE_ONLY" -> "BLE Only"
                            "WIFI_ONLY" -> "Wi-Fi Only"
                            else -> "Auto (Smart)"
                        }
                        ShadcnButton(
                            onClick = { connectionMode = mode },
                            variant = if (isSelected) ShadcnButtonVariant.DEFAULT else ShadcnButtonVariant.SECONDARY,
                            modifier = Modifier.weight(1f)
                        ) {
                            Text(
                                text = label,
                                style = typography.caption,
                                fontWeight = if (isSelected) FontWeight.Bold else FontWeight.Normal,
                                color = if (isSelected) colors.primaryForeground else colors.mutedForeground
                            )
                        }
                    }
                }

                Spacer(modifier = Modifier.height(20.dp))

                // Sync Interval Selector
                Text(
                    text = "UPDATE FREQUENCY",
                    style = typography.caption,
                    fontWeight = FontWeight.Bold,
                    color = colors.foreground
                )
                Spacer(modifier = Modifier.height(4.dp))
                Text(
                    text = "Polling frequency for companion bridge and direct Wi-Fi fetch",
                    style = typography.caption,
                    color = colors.mutedForeground
                )
                Spacer(modifier = Modifier.height(8.dp))
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    listOf(30, 60, 120, 300).forEach { sec ->
                        val isSelected = pollIntervalSec == sec
                        val label = if (sec < 60) "${sec}s" else "${sec / 60}m"
                        ShadcnButton(
                            onClick = { pollIntervalSec = sec },
                            variant = if (isSelected) ShadcnButtonVariant.DEFAULT else ShadcnButtonVariant.SECONDARY,
                            modifier = Modifier.weight(1f)
                        ) {
                            Text(
                                text = label,
                                style = typography.caption,
                                fontWeight = if (isSelected) FontWeight.Bold else FontWeight.Normal,
                                color = if (isSelected) colors.primaryForeground else colors.mutedForeground
                            )
                        }
                    }
                }

                Spacer(modifier = Modifier.height(20.dp))

                // Firmware & Updates Section
                Text(
                    text = "FIRMWARE & UPDATES",
                    style = typography.caption,
                    fontWeight = FontWeight.Bold,
                    color = colors.foreground
                )
                Spacer(modifier = Modifier.height(4.dp))
                Text(
                    text = "Manage Sugarota device firmware and over-the-air updates",
                    style = typography.caption,
                    color = colors.mutedForeground
                )
                Spacer(modifier = Modifier.height(8.dp))

                val devObj = service?.devices?.collectAsState()?.value?.get(deviceAddress)
                val fwVersion = devObj?.status?.version?.ifBlank { "Unknown" } ?: "Unknown"

                ShadcnCard(modifier = Modifier.fillMaxWidth()) {
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceBetween,
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Column {
                            Text(
                                text = "Installed Firmware",
                                style = typography.body,
                                fontWeight = FontWeight.SemiBold,
                                color = colors.foreground
                            )
                            Spacer(modifier = Modifier.height(2.dp))
                            Text(
                                text = fwVersion,
                                style = typography.caption,
                                color = colors.mutedForeground,
                                fontFamily = androidx.compose.ui.text.font.FontFamily.Monospace
                            )
                        }

                        ShadcnButton(
                            onClick = {
                                if (onOpenFirmwareUpdate != null) {
                                    onOpenFirmwareUpdate()
                                } else {
                                    showFirmwareUpdateScreen = true
                                }
                            },
                            variant = ShadcnButtonVariant.SECONDARY
                        ) {
                            Icon(
                                imageVector = Icons.Default.SystemUpdateAlt,
                                contentDescription = null,
                                tint = colors.foreground,
                                modifier = Modifier.size(16.dp)
                            )
                            Spacer(modifier = Modifier.width(6.dp))
                            Text(
                                text = "Check for updates",
                                style = typography.caption,
                                fontWeight = FontWeight.Bold,
                                color = colors.foreground
                            )
                        }
                    }
                }

                Spacer(modifier = Modifier.height(20.dp))
                ShadcnButton(
                    onClick = { showRawJson = !showRawJson },
                    variant = ShadcnButtonVariant.GHOST,
                    modifier = Modifier.fillMaxWidth()
                ) {
                    Text(
                        text = if (showRawJson) "Hide Raw JSON ▲" else "View / Edit Raw JSON ▼",
                        style = typography.caption,
                        color = colors.mutedForeground
                    )
                }

                if (showRawJson) {
                    Spacer(modifier = Modifier.height(8.dp))
                    ShadcnInput(
                        value = rawConfigText,
                        onValueChange = {
                            rawConfigText = it
                            populateFromText(it)
                        },
                        label = "Raw JSON Editor",
                        readOnly = isSaving,
                        isMonospace = true,
                        minHeight = 180.dp
                    )
                }

                Spacer(modifier = Modifier.height(16.dp))
            }
        }
    }
}

@Composable
fun EyeToggleIcon(visible: Boolean, tint: Color) {
    androidx.compose.foundation.Canvas(modifier = Modifier.size(18.dp)) {
        val strokeW = 1.8.dp.toPx()
        val w = size.width
        val h = size.height
        val cx = w / 2f
        val cy = h / 2f

        // Eye shape outline
        val eyePath = androidx.compose.ui.graphics.Path().apply {
            moveTo(1.dp.toPx(), cy)
            quadraticBezierTo(cx, cy - 6.dp.toPx(), w - 1.dp.toPx(), cy)
            quadraticBezierTo(cx, cy + 6.dp.toPx(), 1.dp.toPx(), cy)
        }
        drawPath(
            path = eyePath,
            color = tint,
            style = androidx.compose.ui.graphics.drawscope.Stroke(
                width = strokeW,
                cap = androidx.compose.ui.graphics.StrokeCap.Round
            )
        )

        // Pupil center
        drawCircle(
            color = tint,
            radius = 2.4.dp.toPx(),
            center = androidx.compose.ui.geometry.Offset(cx, cy)
        )

        // Slash through eye if password is hidden
        if (!visible) {
            drawLine(
                color = tint,
                start = androidx.compose.ui.geometry.Offset(2.dp.toPx(), 2.dp.toPx()),
                end = androidx.compose.ui.geometry.Offset(w - 2.dp.toPx(), h - 2.dp.toPx()),
                strokeWidth = strokeW,
                cap = androidx.compose.ui.graphics.StrokeCap.Round
            )
        }
    }
}

fun mapTrendToSymbol(direction: String): String {
    return when (direction.trim().lowercase()) {
        "doubleup", "double_up" -> "⇈"
        "singleup", "single_up" -> "↑"
        "fortyfiveup", "forty_five_up" -> "↗"
        "flat" -> "→"
        "fortyfivedown", "forty_five_down" -> "↘"
        "singledown", "single_down" -> "↓"
        "doubledown", "double_down" -> "⇊"
        else -> direction
    }
}

fun getGlucoseColor(sgv: Int): Color {
    return when {
        sgv <= 0 -> Color(0xFF848484)
        sgv < 55 || sgv >= 230 -> Color(0xFFEF4444)  // Red
        sgv < 70 || sgv > 180 -> Color(0xFFF97316)   // Orange
        else -> Color(0xFF00E676)                    // Green
    }
}

@Composable
fun TrendArrowIcon(
    direction: String,
    tint: Color,
    modifier: Modifier = Modifier
) {
    val dir = direction.trim().lowercase()
    val isDouble = dir.contains("double")
    val rotation = when {
        dir.contains("up") && !dir.contains("fortyfive") -> -90f
        dir.contains("fortyfiveup") || dir.contains("forty_five_up") -> -45f
        dir.contains("fortyfivedown") || dir.contains("forty_five_down") -> 45f
        dir.contains("down") && !dir.contains("fortyfive") -> 90f
        else -> 0f // flat or unknown
    }

    // Keep within 22.dp bounding box in all orientations so vertical and diagonal arrows don't exceed font height
    val canvasWidth = if (isDouble) 22.dp else 20.dp
    val canvasHeight = if (isDouble) 20.dp else 16.dp

    androidx.compose.foundation.Canvas(
        modifier = modifier
            .size(width = canvasWidth, height = canvasHeight)
            .graphicsLayer { rotationZ = rotation }
    ) {
        val strokeW = 3.5.dp.toPx()
        val cap = androidx.compose.ui.graphics.StrokeCap.Round
        val join = androidx.compose.ui.graphics.StrokeJoin.Round

        if (isDouble) {
            // Draw two parallel arrows
            val spacing = 5.dp.toPx()
            for (offsetY in listOf(-spacing / 2, spacing / 2)) {
                val shaftStart = androidx.compose.ui.geometry.Offset(x = 1.dp.toPx(), y = size.height / 2 + offsetY)
                val shaftEnd = androidx.compose.ui.geometry.Offset(x = size.width - 5.dp.toPx(), y = size.height / 2 + offsetY)
                drawLine(
                    color = tint,
                    start = shaftStart,
                    end = shaftEnd,
                    strokeWidth = strokeW * 0.85f,
                    cap = cap
                )
                val headPath = androidx.compose.ui.graphics.Path().apply {
                    moveTo(shaftEnd.x - 4.dp.toPx(), shaftEnd.y - 4.dp.toPx())
                    lineTo(shaftEnd.x, shaftEnd.y)
                    lineTo(shaftEnd.x - 4.dp.toPx(), shaftEnd.y + 4.dp.toPx())
                }
                drawPath(
                    path = headPath,
                    color = tint,
                    style = androidx.compose.ui.graphics.drawscope.Stroke(width = strokeW * 0.85f, cap = cap, join = join)
                )
            }
        } else {
            // Single arrow matching bold firmware arrow style
            val centerY = size.height / 2
            val shaftStart = androidx.compose.ui.geometry.Offset(x = 2.dp.toPx(), y = centerY)
            val shaftEnd = androidx.compose.ui.geometry.Offset(x = size.width - 4.dp.toPx(), y = centerY)
            
            // Shaft
            drawLine(
                color = tint,
                start = shaftStart,
                end = shaftEnd,
                strokeWidth = strokeW,
                cap = cap
            )
            // Arrowhead chevron
            val headSize = 5.5.dp.toPx()
            val headPath = androidx.compose.ui.graphics.Path().apply {
                moveTo(shaftEnd.x - headSize, centerY - headSize)
                lineTo(shaftEnd.x, centerY)
                lineTo(shaftEnd.x - headSize, centerY + headSize)
            }
            drawPath(
                path = headPath,
                color = tint,
                style = androidx.compose.ui.graphics.drawscope.Stroke(width = strokeW, cap = cap, join = join)
            )
        }
    }
}

@Composable
fun DeviceCard(
    device: SugarotaDevice,
    bridgeStatusText: String = "Idle",
    lastReading: org.sugarota.companion.model.GlucoseData? = null,
    units: String = "mg/dL",
    isConfigured: Boolean = true,
    onClick: () -> Unit = {},
    onConfigureClick: () -> Unit = {},
    onConnect: () -> Unit,
    onSync: () -> Unit,
    onDisconnect: () -> Unit
) {
    val colors = ShadcnTheme.colors
    val typography = ShadcnTheme.typography

    val syncRotation = remember { androidx.compose.animation.core.Animatable(0f) }
    val syncScope = rememberCoroutineScope()

    ShadcnCard(
        modifier = Modifier
            .fillMaxWidth()
            .clickable(onClick = onClick)
    ) {
        Column(modifier = Modifier.fillMaxWidth()) {
            // Header Row: Device Name + Green/Red light icon indicator & Action icons (Sync, Disconnect or Connect)
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    modifier = Modifier.weight(1f, fill = false)
                ) {
                    // Status light indicator (green circle for connected, red circle for disconnected)
                    Box(
                        modifier = Modifier
                            .size(10.dp)
                            .background(
                                color = if (device.isConnected) Color(0xFF00E676) else Color(0xFFEF4444),
                                shape = androidx.compose.foundation.shape.CircleShape
                            )
                    )
                    Spacer(modifier = Modifier.width(8.dp))
                    Text(
                        text = device.name,
                        style = typography.h3,
                        color = colors.foreground
                    )
                }

                Row(verticalAlignment = Alignment.CenterVertically) {
                    if (device.isConnected) {
                        // Sync Now icon button
                        IconButton(
                            onClick = {
                                syncScope.launch {
                                    syncRotation.snapTo(0f)
                                    syncRotation.animateTo(
                                        targetValue = 360f,
                                        animationSpec = tween(durationMillis = 700, easing = FastOutSlowInEasing)
                                    )
                                }
                                onSync()
                            },
                            modifier = Modifier.size(36.dp)
                        ) {
                            Icon(
                                imageVector = Icons.Default.Refresh,
                                contentDescription = "Sync Now",
                                tint = colors.primary,
                                modifier = Modifier
                                    .size(20.dp)
                                    .graphicsLayer {
                                        rotationZ = syncRotation.value
                                    }
                            )
                        }
                        IconButton(
                            onClick = onDisconnect,
                            modifier = Modifier.size(36.dp)
                        ) {
                            Icon(
                                imageVector = Icons.Default.BluetoothDisabled,
                                contentDescription = "Disconnect",
                                tint = colors.destructive,
                                modifier = Modifier.size(20.dp)
                            )
                        }
                    } else {
                        ShadcnButton(
                            onClick = onConnect,
                            variant = ShadcnButtonVariant.DEFAULT
                        ) {
                            Text(
                                text = "Connect",
                                color = colors.primaryForeground,
                                fontWeight = FontWeight.Bold,
                                fontSize = 12.sp
                            )
                        }
                    }
                }
            }

            // Bridge Status / Glucose Reading block content
            if (device.isConnected && !device.isBonded) {
                Spacer(modifier = Modifier.height(10.dp))
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    modifier = Modifier
                        .fillMaxWidth()
                        .background(
                            color = colors.secondary.copy(alpha = 0.5f),
                            shape = androidx.compose.foundation.shape.RoundedCornerShape(8.dp)
                        )
                        .padding(horizontal = 10.dp, vertical = 8.dp)
                ) {
                    Icon(
                        imageVector = Icons.Default.Lock,
                        contentDescription = "Pairing Required",
                        tint = colors.primary,
                        modifier = Modifier.size(16.dp)
                    )
                    Spacer(modifier = Modifier.width(8.dp))
                    Column {
                        Text(
                            text = "Pairing Required",
                            style = typography.body.copy(fontWeight = FontWeight.SemiBold, fontSize = 13.sp),
                            color = colors.foreground
                        )
                        Text(
                            text = "Confirm 6-digit PIN on display to view glucose",
                            style = typography.caption.copy(fontSize = 11.sp),
                            color = colors.mutedForeground
                        )
                    }
                }
            } else if (!isConfigured) {
                Spacer(modifier = Modifier.height(10.dp))
                // Account not configured banner leading to config screen
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    modifier = Modifier
                        .fillMaxWidth()
                        .clip(androidx.compose.foundation.shape.RoundedCornerShape(8.dp))
                        .background(Color(0xFFF59E0B).copy(alpha = 0.15f))
                        .clickable(onClick = onConfigureClick)
                        .padding(horizontal = 10.dp, vertical = 8.dp)
                ) {
                    Icon(
                        imageVector = Icons.Default.Settings,
                        contentDescription = "Configure Account",
                        tint = Color(0xFFF59E0B),
                        modifier = Modifier.size(16.dp)
                    )
                    Spacer(modifier = Modifier.width(8.dp))
                    Column(modifier = Modifier.weight(1f)) {
                        Text(
                            text = "Account not configured",
                            style = typography.body.copy(fontWeight = FontWeight.SemiBold, fontSize = 13.sp),
                            color = Color(0xFFF59E0B)
                        )
                        Text(
                            text = "Tap to set up Nightscout or Dexcom provider",
                            style = typography.caption.copy(fontSize = 11.sp),
                            color = colors.mutedForeground
                        )
                    }
                    Icon(
                        imageVector = Icons.Default.ChevronRight,
                        contentDescription = null,
                        tint = Color(0xFFF59E0B),
                        modifier = Modifier.size(18.dp)
                    )
                }
            } else if (lastReading != null) {
                Spacer(modifier = Modifier.height(10.dp))
                val isMmol = units.equals("mmol/l", ignoreCase = true)
                val formattedSgv = if (isMmol) {
                    String.format(java.util.Locale.US, "%.1f", lastReading.sgv / 18.0182f)
                } else {
                    "${lastReading.sgv}"
                }
                val deltaVal = if (isMmol) {
                    val mmolVal = lastReading.delta / 18.0182f
                    if (lastReading.delta == 0) "+0.0" else String.format(java.util.Locale.US, "%s%.1f", if (lastReading.delta > 0) "+" else "", mmolVal)
                } else {
                    "${if (lastReading.delta > 0) "+" else ""}${lastReading.delta}"
                }
                val bgCol = getGlucoseColor(lastReading.sgv)
                val timeStr = java.text.SimpleDateFormat("HH:mm:ss", java.util.Locale.getDefault())
                    .format(java.util.Date(lastReading.timestamp * 1000))
                val minsAgo = ((System.currentTimeMillis() / 1000 - lastReading.timestamp) / 60).coerceAtLeast(0)

                // Prominent one line display: {last BG_value} {trend_icon} (color coded)  {delta_value} {units}
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
                        text = "$deltaVal $units",
                        fontSize = 20.sp,
                        fontWeight = FontWeight.SemiBold,
                        color = colors.foreground
                    )
                }

                Spacer(modifier = Modifier.height(3.dp))

                // "Synced on {HH:mm:ss} ({M} min ago)" in smaller font
                Text(
                    text = "Synced on $timeStr ($minsAgo min ago)",
                    style = typography.caption,
                    color = colors.mutedForeground
                )
            } else if (device.isConnected) {
                Spacer(modifier = Modifier.height(10.dp))
                // Fallback when connected but no reading is fetched yet
                Text(
                    text = bridgeStatusText,
                    style = typography.caption,
                    color = if (bridgeStatusText.startsWith("Synced")) colors.primary else colors.mutedForeground
                )
            }

            Spacer(modifier = Modifier.height(10.dp))

            // Footer line: Battery & version when connected, or "Connect the display for more settings" when offline
            if (device.isConnected) {
                Text(
                    text = "Battery: ${device.status.batteryPct}% ${if (device.status.isCharging) "(+)" else ""} · ${device.status.version}",
                    style = typography.caption,
                    color = colors.mutedForeground
                )
            } else {
                Text(
                    text = "Connect the display for more settings",
                    style = typography.caption,
                    color = colors.mutedForeground
                )
            }
        }
    }
}

@androidx.compose.ui.tooling.preview.Preview(showBackground = true, backgroundColor = 0xFF09090B)
@Composable
fun CompanionAppPreview() {
    CompositionLocalProvider(
        LocalShadcnColors provides ShadcnColors(),
        LocalShadcnTypography provides ShadcnTypography(),
        LocalShadcnShapes provides ShadcnShapes()
    ) {
        Column(
            modifier = Modifier
                .fillMaxSize()
                .background(ShadcnTheme.colors.background)
                .padding(16.dp)
        ) {
            Text(
                text = "Connected Displays",
                style = ShadcnTheme.typography.h3,
                color = ShadcnTheme.colors.mutedForeground
            )
            Spacer(modifier = Modifier.height(12.dp))

            DeviceCard(
                device = SugarotaDevice(
                    name = "Sugarota-D695",
                    address = "20:6E:F1:9B:D6:95",
                    isConnected = true,
                    status = org.sugarota.companion.model.DeviceStatus(batteryPct = 85, isCharging = true, version = "v0.09.04.8")
                ),
                bridgeStatusText = "Synced 208 → (+0) at 18:00",
                lastReading = org.sugarota.companion.model.GlucoseData(
                    sgv = 208,
                    direction = "Flat",
                    delta = 0,
                    timestamp = System.currentTimeMillis() / 1000 - 180,
                    units = "mg/dL"
                ),
                onConnect = {},
                onSync = {},
                onDisconnect = {}
            )
            Spacer(modifier = Modifier.height(8.dp))
            DeviceCard(
                device = SugarotaDevice(
                    name = "Sugarota-LivingRoom",
                    address = "20:6E:F1:9B:AA:11",
                    isConnected = false,
                    status = org.sugarota.companion.model.DeviceStatus(batteryPct = 42, isCharging = false, version = "v0.09.04.8")
                ),
                bridgeStatusText = "Idle",
                lastReading = null,
                onConnect = {},
                onSync = {},
                onDisconnect = {}
            )
        }
    }
}

@Composable
fun SplashWelcomeScreen() {
    val context = androidx.compose.ui.platform.LocalContext.current
    val appVersion = remember(context) {
        try {
            val pInfo = context.packageManager.getPackageInfo(context.packageName, 0)
            pInfo.versionName ?: "v0.09.09.0"
        } catch (e: Exception) {
            "v0.09.09.0"
        }
    }

    Box(
        modifier = Modifier
            .fillMaxSize()
            .background(ShadcnTheme.colors.background),
        contentAlignment = Alignment.Center
    ) {
        Column(
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.Center
        ) {
            androidx.compose.foundation.Image(
                painter = androidx.compose.ui.res.painterResource(id = org.sugarota.companion.R.drawable.ic_sugarota_logo),
                contentDescription = "Sugarota Companion Logo",
                modifier = Modifier.size(96.dp)
            )
            Spacer(modifier = Modifier.height(16.dp))
            Text(
                text = "Sugarota Companion",
                style = ShadcnTheme.typography.h2,
                color = ShadcnTheme.colors.foreground,
                fontWeight = FontWeight.Bold
            )
            Spacer(modifier = Modifier.height(6.dp))
            Text(
                text = appVersion,
                style = ShadcnTheme.typography.body,
                color = ShadcnTheme.colors.mutedForeground
            )
        }
    }
}
