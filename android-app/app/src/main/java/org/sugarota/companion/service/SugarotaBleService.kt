package org.sugarota.companion.service

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.bluetooth.*
import android.bluetooth.le.*
import android.content.Context
import android.content.Intent
import android.os.Binder
import android.os.Build
import android.os.Handler
import android.os.IBinder
import android.os.Looper
import org.sugarota.companion.MainActivity
import android.content.BroadcastReceiver
import android.content.IntentFilter
import android.util.Log
import androidx.core.app.NotificationCompat
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import org.sugarota.companion.model.*
import org.sugarota.companion.network.GlucoseBridgeClient
import java.util.concurrent.ConcurrentHashMap

class SugarotaBleService : Service() {

    private val binder = LocalBinder()
    private val serviceScope = CoroutineScope(Dispatchers.IO + SupervisorJob())

    private val bluetoothAdapter: BluetoothAdapter? by lazy {
        val manager = getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager
        manager.adapter
    }

    private val connectedGatts = ConcurrentHashMap<String, BluetoothGatt>()
    private val connectingDevices = java.util.Collections.newSetFromMap(ConcurrentHashMap<String, Boolean>())
    private val syncingDevices = java.util.Collections.newSetFromMap(ConcurrentHashMap<String, Boolean>())
    private val lastDisconnectTime = ConcurrentHashMap<String, Long>()
    private val pendingConfigReads = ConcurrentHashMap<String, (String) -> Unit>()
    private val deviceConfigs = ConcurrentHashMap<String, String>() // address -> raw JSON
    private val lastPushedTimestamps = ConcurrentHashMap<String, Long>() // address -> timestamp
    private val currentMtu = ConcurrentHashMap<String, Int>() // address -> negotiated MTU
    // Per-device write queue: each entry is a list of raw byte payloads to be sent sequentially.
    // The next chunk is sent only after onCharacteristicWrite fires for the previous one.
    private val pendingWriteQueues = ConcurrentHashMap<String, ArrayDeque<ByteArray>>()

    private val _devices = MutableStateFlow<Map<String, SugarotaDevice>>(emptyMap())
    val devices: StateFlow<Map<String, SugarotaDevice>> = _devices.asStateFlow()

    // Logs per device address (max 500 lines per device)
    private val _deviceLogs = MutableStateFlow<Map<String, List<String>>>(emptyMap())
    val deviceLogs: StateFlow<Map<String, List<String>>> = _deviceLogs.asStateFlow()

    fun appendDeviceLog(address: String, message: String) {
        val timestamp = java.text.SimpleDateFormat("HH:mm:ss.SSS", java.util.Locale.US).format(java.util.Date())
        val logLine = "[$timestamp] $message"
        val currentMap = _deviceLogs.value.toMutableMap()
        val currentList = currentMap[address]?.toMutableList() ?: mutableListOf()
        currentList.add(logLine)
        if (currentList.size > 500) {
            currentList.removeAt(0)
        }
        currentMap[address] = currentList
        _deviceLogs.value = currentMap
    }

    fun clearDeviceLogs(address: String) {
        val currentMap = _deviceLogs.value.toMutableMap()
        currentMap[address] = emptyList()
        _deviceLogs.value = currentMap
    }

    private val bridgeClient = GlucoseBridgeClient()
    private val bridgePrefs by lazy { org.sugarota.companion.data.BridgePreferences(this) }
    private var bridgeJob: Job? = null

    private val _lastReading = MutableStateFlow<GlucoseData?>(null)
    val lastReading: StateFlow<GlucoseData?> = _lastReading.asStateFlow()

    // Per-device latest readings (address -> GlucoseData)
    private val _deviceReadings = MutableStateFlow<Map<String, GlucoseData>>(emptyMap())
    val deviceReadings: StateFlow<Map<String, GlucoseData>> = _deviceReadings.asStateFlow()

    // Per-device configuration completeness (address -> Boolean: true if provider and credentials are set)
    private val _deviceConfigured = MutableStateFlow<Map<String, Boolean>>(emptyMap())
    val deviceConfigured: StateFlow<Map<String, Boolean>> = _deviceConfigured.asStateFlow()

    // Persistent custom device ordering (ordered list of BLE addresses)
    private val orderPrefs by lazy { getSharedPreferences("sugarota_device_order", Context.MODE_PRIVATE) }
    private val _deviceOrder = MutableStateFlow<List<String>>(emptyList())
    val deviceOrder: StateFlow<List<String>> = _deviceOrder.asStateFlow()

    private val _bridgeStatus = MutableStateFlow("Idle")
    val bridgeStatus: StateFlow<String> = _bridgeStatus.asStateFlow()

    private val bondStateReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context?, intent: Intent?) {
            val action = intent?.action
            if (action == BluetoothDevice.ACTION_BOND_STATE_CHANGED) {
                val device = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                    intent.getParcelableExtra(BluetoothDevice.EXTRA_DEVICE, BluetoothDevice::class.java)
                } else {
                    @Suppress("DEPRECATION")
                    intent.getParcelableExtra(BluetoothDevice.EXTRA_DEVICE)
                }
                val bondState = intent.getIntExtra(BluetoothDevice.EXTRA_BOND_STATE, BluetoothDevice.BOND_NONE)
                val prevBondState = intent.getIntExtra(BluetoothDevice.EXTRA_PREVIOUS_BOND_STATE, BluetoothDevice.BOND_NONE)
                val addr = device?.address ?: return

                Log.i("SugarotaBleService", "Bond state changed for $addr: prev=$prevBondState, new=$bondState")
                updateDeviceBondState(addr, bondState == BluetoothDevice.BOND_BONDED)
                if (bondState == BluetoothDevice.BOND_BONDED) {
                    Log.i("SugarotaBleService", "Device $addr successfully bonded! Re-triggering config sync.")
                    val gatt = connectedGatts[addr]
                    if (gatt != null) {
                        serviceScope.launch {
                            delay(500)
                            val service = gatt.getService(BleUuids.SUGAROTA_SERVICE)
                            val configChar = service?.getCharacteristic(BleUuids.CHAR_CONFIG)
                            if (configChar != null) {
                                gatt.readCharacteristic(configChar)
                            }
                        }
                    }
                }
            }
        }
    }

    inner class LocalBinder : Binder() {
        fun getService(): SugarotaBleService = this@SugarotaBleService
    }

    override fun onBind(intent: Intent?): IBinder = binder

    override fun onCreate() {
        super.onCreate()
        _deviceOrder.value = loadDeviceOrder()
        loadCachedDeviceConfigs()
        createNotificationChannel()
        startForeground(NOTIFICATION_ID, buildNotification("Sugarota Service Running"))

        val filter = IntentFilter().apply {
            addAction(BluetoothDevice.ACTION_BOND_STATE_CHANGED)
            addAction(BluetoothDevice.ACTION_PAIRING_REQUEST)
        }
        registerReceiver(bondStateReceiver, filter)

        // Pre-register and auto-listen for any already-bonded Sugarota devices
        reconnectBondedDevices()

        startScanning()
        startBackgroundPendingIntentScan()
        startPeriodicBridge()
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        val action = intent?.action
        if (action == ACTION_CONNECT_DEVICE) {
            val address = intent.getStringExtra(EXTRA_DEVICE_ADDRESS)
            if (!address.isNullOrBlank()) {
                if (isDeviceManuallyDisconnected(address)) {
                    Log.i("SugarotaBleService", "onStartCommand: Skipping auto-connect for $address because it was manually disconnected by user")
                } else {
                    Log.i("SugarotaBleService", "onStartCommand: Auto-connecting requested device: $address")
                    connectDevice(address)
                }
            }
        }
        return START_STICKY
    }

    override fun onDestroy() {
        super.onDestroy()
        try {
            unregisterReceiver(bondStateReceiver)
        } catch (e: Exception) {
            // Receiver might not be registered
        }
        stopScanning()
        stopBackgroundPendingIntentScan()
        serviceScope.cancel()
        connectedGatts.values.forEach { it.close() }
        connectedGatts.clear()
    }

    // Custom device names storage
    private val namePrefs by lazy { getSharedPreferences("sugarota_device_names", Context.MODE_PRIVATE) }
    // Cached device configs storage (address -> JSON string)
    private val configPrefs by lazy { getSharedPreferences("sugarota_device_configs", Context.MODE_PRIVATE) }

    private fun loadCachedDeviceConfigs() {
        try {
            val all = configPrefs.all
            for ((addr, value) in all) {
                if (value is String && value.isNotBlank()) {
                    deviceConfigs[addr] = value
                    checkConfigCompleteness(addr, value)
                }
            }
        } catch (e: Exception) {
            Log.w("SugarotaBleService", "Error loading cached device configs: ${e.message}")
        }
    }

    private fun saveCachedDeviceConfig(address: String, configJson: String) {
        if (configJson.isNotBlank()) {
            configPrefs.edit().putString(address, configJson).apply()
        }
    }

    fun getDefaultDeviceName(address: String): String {
        val clean = address.replace(":", "").replace("-", "")
        val suffix = if (clean.length >= 4) clean.takeLast(4).uppercase() else clean.uppercase()
        return "Sugarota-$suffix"
    }

    fun getDeviceDisplayName(address: String, advertisedName: String? = null): String {
        val custom = namePrefs.getString(address, null)?.takeIf { it.isNotBlank() }
        if (custom != null) return custom
        if (!advertisedName.isNullOrBlank() && advertisedName.startsWith("SUGAROTA", ignoreCase = true)) {
            return advertisedName
        }
        return getDefaultDeviceName(address)
    }

    fun setDeviceCustomName(address: String, customName: String) {
        val trimmed = customName.trim()
        if (trimmed.isBlank() || trimmed == getDefaultDeviceName(address)) {
            namePrefs.edit().remove(address).apply()
        } else {
            namePrefs.edit().putString(address, trimmed).apply()
        }
        val current = _devices.value.toMutableMap()
        current[address]?.let { dev ->
            current[address] = dev.copy(name = getDeviceDisplayName(address))
            _devices.value = current
        }
    }

    private fun loadDeviceOrder(): List<String> {
        val raw = orderPrefs.getString("device_addresses_order", "") ?: ""
        return if (raw.isBlank()) emptyList() else raw.split(",").filter { it.isNotBlank() }
    }

    fun saveDeviceOrder(orderedAddresses: List<String>) {
        _deviceOrder.value = orderedAddresses
        orderPrefs.edit().putString("device_addresses_order", orderedAddresses.joinToString(",")).apply()
    }

    fun getPrimaryDeviceAddress(): String? {
        val order = _deviceOrder.value
        val connectedMap = _devices.value.filter { it.value.isConnected }
        if (connectedMap.isNotEmpty()) {
            // Find first connected device according to user's order
            for (addr in order) {
                if (connectedMap.containsKey(addr)) return addr
            }
            // Fallback to any connected device
            return connectedMap.keys.firstOrNull()
        }
        // Fallback to first device in order or any known device
        return order.firstOrNull { _devices.value.containsKey(it) } ?: _devices.value.keys.firstOrNull()
    }

    /**
     * Forget a device: disconnects GATT, clears pairing/bond via BluetoothDevice.removeBond(),
     * purges cached configurations, readings, and logs. Does NOT modify device settings;
     * device continues working as usual and must be re-paired next time.
     */
    fun forgetDevice(address: String) {
        Log.i("SugarotaBleService", "forgetDevice requested for $address")
        manuallyDisconnected.add(address)
        connectingDevices.remove(address)
        syncingDevices.remove(address)

        // Disconnect and close GATT
        val gatt = connectedGatts.remove(address)
        try {
            gatt?.disconnect()
            gatt?.close()
        } catch (e: Exception) {
            Log.w("SugarotaBleService", "Error closing GATT during forgetDevice for $address: ${e.message}")
        }

        // Clean internal state
        deviceConfigs.remove(address)
        lastPushedTimestamps.remove(address)
        currentMtu.remove(address)
        pendingWriteQueues.remove(address)
        pendingConfigReads.remove(address)

        val updatedReadings = _deviceReadings.value.toMutableMap()
        updatedReadings.remove(address)
        _deviceReadings.value = updatedReadings

        val updatedConfigured = _deviceConfigured.value.toMutableMap()
        updatedConfigured.remove(address)
        _deviceConfigured.value = updatedConfigured

        clearDeviceLogs(address)
        namePrefs.edit().remove(address).apply()
        configPrefs.edit().remove(address).apply()

        // Remove from devices state flow
        val currentDevs = _devices.value.toMutableMap()
        currentDevs.remove(address)
        _devices.value = currentDevs

        // Remove from device order
        val newOrder = _deviceOrder.value.filter { it != address }
        saveDeviceOrder(newOrder)

        // Clear Android OS Bluetooth bond/pairing
        try {
            val device = bluetoothAdapter?.getRemoteDevice(address)
            if (device != null && device.bondState != BluetoothDevice.BOND_NONE) {
                val method = device.javaClass.getMethod("removeBond")
                val result = method.invoke(device) as? Boolean
                Log.i("SugarotaBleService", "BluetoothDevice.removeBond() invoked for $address, result=$result")
            }
        } catch (e: Exception) {
            Log.w("SugarotaBleService", "Failed to invoke removeBond on $address: ${e.message}")
        }

        // Update notification: always show actual glucose reading and chart if available
        refreshNotificationState()
    }

    private val scanCallback = object : ScanCallback() {
        override fun onScanResult(callbackType: Int, result: ScanResult) {
            val device = result.device
            val advertisedName = result.scanRecord?.deviceName
            val rawName = try { device.name } catch (e: SecurityException) { null }
            val effectiveName = advertisedName?.takeIf { it.isNotBlank() } ?: rawName ?: ""
            val isSugarotaName = effectiveName.startsWith("SUGAROTA", ignoreCase = true)
            // If the device broadcasts a name that is NOT Sugarota (e.g. unrelated nearby gadgets like ATL-D0C...), ignore immediately
            if (effectiveName.isNotBlank() && !isSugarotaName) {
                return
            }

            val hasSugarotaUuid = result.scanRecord?.serviceUuids?.any {
                it.uuid == BleUuids.SUGAROTA_SERVICE
            } == true

            // Only accept if name starts with SUGAROTA, or if unnamed but explicitly carries the Sugarota service UUID
            if (isSugarotaName || hasSugarotaUuid) {
                val addr = device.address
                val resolvedName = getDeviceDisplayName(addr, effectiveName)
                val current = _devices.value.toMutableMap()
                val existing = current[addr]
                val isBonded = device.bondState == BluetoothDevice.BOND_BONDED
                if (existing == null) {
                    current[addr] = SugarotaDevice(name = resolvedName, address = addr, isBonded = isBonded)
                    _devices.value = current
                } else if (existing.name != resolvedName || existing.isBonded != isBonded) {
                    current[addr] = existing.copy(name = resolvedName, isBonded = isBonded)
                    _devices.value = current
                }
                // If user explicitly disconnected or already connecting/connected, don't auto-reconnect from scan
                if (!manuallyDisconnected.contains(addr) && !connectedGatts.containsKey(addr) && !connectingDevices.contains(addr)) {
                    connectDevice(addr)
                }
            }
        }

        override fun onScanFailed(errorCode: Int) {
            android.util.Log.w("SugarotaBleService", "BLE onScanFailed: errorCode=$errorCode (status 6 = scanning too frequently)")
            _isScanning.value = false
        }
    }

    private val _isScanning = MutableStateFlow(false)
    val isScanning: StateFlow<Boolean> = _isScanning.asStateFlow()
    private var scanJob: Job? = null
    private var lastScanStartTime: Long = 0L

    fun startScanning() {
        try {
            val scanner = bluetoothAdapter?.bluetoothLeScanner
            val filter = android.bluetooth.le.ScanFilter.Builder()
                .build()
            val settings = android.bluetooth.le.ScanSettings.Builder()
                .setScanMode(android.bluetooth.le.ScanSettings.SCAN_MODE_LOW_LATENCY)
                .build()
            scanner?.startScan(listOf(filter), settings, scanCallback)
        } catch (e: SecurityException) {
            e.printStackTrace()
        }
    }

    fun stopScanning() {
        try {
            bluetoothAdapter?.bluetoothLeScanner?.stopScan(scanCallback)
        } catch (e: SecurityException) {
            e.printStackTrace()
        }
    }

    fun triggerScan(durationMs: Long = 5_000L) {
        val now = System.currentTimeMillis()
        // Prevent rapid stop/start cycles that trigger Android's 5-scans-per-30s throttle
        if (_isScanning.value && (now - lastScanStartTime) < 3_500L) {
            android.util.Log.i("SugarotaBleService", "triggerScan: Scan already in progress, ignoring rapid re-trigger")
            return
        }

        scanJob?.cancel()
        _isScanning.value = true
        lastScanStartTime = now
        android.util.Log.i("SugarotaBleService", "triggerScan: BLE scanning started for $durationMs ms")

        scanJob = serviceScope.launch {
            try {
                stopScanning()
                delay(250)
                startScanning()
                delay(durationMs)
            } catch (e: Exception) {
                android.util.Log.e("SugarotaBleService", "triggerScan error", e)
            } finally {
                stopScanning()
                _isScanning.value = false
                android.util.Log.i("SugarotaBleService", "triggerScan: BLE scanning completed")
            }
        }
    }

    fun reconnectBondedDevices() {
        try {
            val bonded = bluetoothAdapter?.bondedDevices?.filter { dev ->
                val name = try { dev.name } catch (e: SecurityException) { null } ?: ""
                name.startsWith("SUGAROTA", ignoreCase = true)
            } ?: emptyList()

            for (dev in bonded) {
                val addr = dev.address
                val resolvedName = getDeviceDisplayName(addr, dev.name)
                val current = _devices.value.toMutableMap()
                if (!current.containsKey(addr)) {
                    current[addr] = SugarotaDevice(name = resolvedName, address = addr, isBonded = true)
                    _devices.value = current
                }
                if (!manuallyDisconnected.contains(addr) && !connectedGatts.containsKey(addr)) {
                    Log.i("SugarotaBleService", "Pre-registering background auto-connect for bonded Sugarota: $addr ($resolvedName)")
                    connectDevice(addr)
                }
            }
        } catch (e: SecurityException) {
            Log.w("SugarotaBleService", "SecurityException while checking bonded devices", e)
        }
    }

    fun connectDevice(address: String) {
        manuallyDisconnected.remove(address)
        if (connectedGatts.containsKey(address) || connectingDevices.contains(address)) {
            Log.i("SugarotaBleService", "connectDevice: $address already connected or connecting")
            return
        }
        val lastDisc = lastDisconnectTime[address] ?: 0L
        if (System.currentTimeMillis() - lastDisc < 2500L) {
            Log.i("SugarotaBleService", "Debouncing connect for $address, recently disconnected")
            return
        }
        val device = bluetoothAdapter?.getRemoteDevice(address) ?: return
        connectingDevices.add(address)
        serviceScope.launch {
            val gattCallback = object : BluetoothGattCallback() {
                override fun onConnectionStateChange(gatt: BluetoothGatt, status: Int, newState: Int) {
                    val addr = gatt.device.address
                    if (newState == BluetoothProfile.STATE_CONNECTED) {
                        connectingDevices.remove(addr)
                        connectedGatts[addr] = gatt
                        markDeviceConnected(addr, true)
                        val bonded = gatt.device.bondState == BluetoothDevice.BOND_BONDED
                        updateDeviceState(addr, isConnected = true, isBonded = bonded)
                        SugarotaBleScanReceiver.clearNotificationForDevice(this@SugarotaBleService, addr)
                        appendDeviceLog(addr, "Connected over BLE (bonded=$bonded)")
                        gatt.requestMtu(517)
                        gatt.discoverServices()
                        updateNotification("Connected to ${connectedGatts.size} device(s)")
                    } else if (newState == BluetoothProfile.STATE_DISCONNECTED) {
                        connectingDevices.remove(addr)
                        syncingDevices.remove(addr)
                        lastDisconnectTime[addr] = System.currentTimeMillis()
                        connectedGatts.remove(addr)
                        markDeviceConnected(addr, false)
                        lastPushedTimestamps.remove(addr)
                        currentMtu.remove(addr)
                        pendingWriteQueues.remove(addr) // Clear any pending history chunks
                        val bonded = gatt.device.bondState == BluetoothDevice.BOND_BONDED
                        updateDeviceState(addr, isConnected = false, isBonded = bonded)
                        appendDeviceLog(addr, "Disconnected from BLE (status=$status)")
                        gatt.close()
                        refreshNotificationState()
                    }
                }

                override fun onMtuChanged(gatt: BluetoothGatt, mtu: Int, status: Int) {
                    val addr = gatt.device.address
                    Log.i("SugarotaBleService", "BLE MTU changed to $mtu (status=$status) for $addr")
                    appendDeviceLog(addr, "MTU changed to $mtu (status=$status)")
                    if (status == BluetoothGatt.GATT_SUCCESS) {
                        currentMtu[addr] = mtu
                    }
                }

                override fun onServicesDiscovered(gatt: BluetoothGatt, status: Int) {
                    if (status == BluetoothGatt.GATT_SUCCESS) {
                        val service = gatt.getService(BleUuids.SUGAROTA_SERVICE)
                        val statusChar = service?.getCharacteristic(BleUuids.CHAR_STATUS)
                        val configChar = service?.getCharacteristic(BleUuids.CHAR_CONFIG)

                        if (statusChar != null) {
                            gatt.setCharacteristicNotification(statusChar, true)
                            val cccd = statusChar.getDescriptor(BleUuids.CHAR_CCCD)
                            if (cccd != null) {
                                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                                    gatt.writeDescriptor(cccd, BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE)
                                } else {
                                    @Suppress("DEPRECATION")
                                    cccd.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
                                    @Suppress("DEPRECATION")
                                    gatt.writeDescriptor(cccd)
                                }
                            } else {
                                gatt.readCharacteristic(statusChar)
                            }
                        } else if (configChar != null) {
                            gatt.readCharacteristic(configChar)
                        }
                    }
                }

                override fun onDescriptorWrite(gatt: BluetoothGatt, descriptor: BluetoothGattDescriptor, status: Int) {
                    Log.i("SugarotaBleService", "onDescriptorWrite: desc=${descriptor.uuid}, status=$status")
                    if (status == BluetoothGatt.GATT_SUCCESS && descriptor.characteristic.uuid == BleUuids.CHAR_STATUS) {
                        // Read status sequentially once CCCD notification descriptor write is confirmed
                        gatt.readCharacteristic(descriptor.characteristic)
                    }
                }

                @Deprecated("Deprecated in Java")
                override fun onCharacteristicRead(gatt: BluetoothGatt, characteristic: BluetoothGattCharacteristic, status: Int) {
                    if (Build.VERSION.SDK_INT < Build.VERSION_CODES.TIRAMISU) {
                        val addr = gatt.device.address
                        if (status == BluetoothGatt.GATT_SUCCESS) {
                            @Suppress("DEPRECATION")
                            val payload = String(characteristic.value ?: ByteArray(0), Charsets.UTF_8)
                            if (characteristic.uuid == BleUuids.CHAR_STATUS) {
                                parseDeviceStatus(addr, payload)
                                val configChar = gatt.getService(BleUuids.SUGAROTA_SERVICE)?.getCharacteristic(BleUuids.CHAR_CONFIG)
                                if (configChar != null && (!deviceConfigs.containsKey(addr) || deviceConfigs[addr].isNullOrBlank())) {
                                    gatt.readCharacteristic(configChar)
                                }
                            } else if (characteristic.uuid == BleUuids.CHAR_CONFIG) {
                                handleConfigReceived(addr, payload)
                            }
                        }
                    }
                }

                override fun onCharacteristicRead(gatt: BluetoothGatt, characteristic: BluetoothGattCharacteristic, value: ByteArray, status: Int) {
                    val addr = gatt.device.address
                    if (status == BluetoothGatt.GATT_SUCCESS) {
                        val payload = String(value, Charsets.UTF_8)
                        if (characteristic.uuid == BleUuids.CHAR_STATUS) {
                            parseDeviceStatus(addr, payload)
                            val configChar = gatt.getService(BleUuids.SUGAROTA_SERVICE)?.getCharacteristic(BleUuids.CHAR_CONFIG)
                            if (configChar != null && (!deviceConfigs.containsKey(addr) || deviceConfigs[addr].isNullOrBlank())) {
                                gatt.readCharacteristic(configChar)
                            }
                        } else if (characteristic.uuid == BleUuids.CHAR_CONFIG) {
                            handleConfigReceived(addr, payload)
                        }
                    }
                }

                override fun onCharacteristicWrite(gatt: BluetoothGatt, characteristic: BluetoothGattCharacteristic, status: Int) {
                    Log.i("SugarotaBleService", "onCharacteristicWrite: uuid=${characteristic.uuid}, status=$status")
                    if (characteristic.uuid == BleUuids.CHAR_GLUCOSE) {
                        // Dequeue and send the next pending history chunk, if any
                        val queue = pendingWriteQueues[gatt.device.address]
                        if (!queue.isNullOrEmpty()) {
                            val nextChunk = queue.removeFirst()
                            Log.i("SugarotaBleService", "Write queue: sending next chunk (${nextChunk.size} bytes), ${queue.size} remaining")
                            writeCharacteristicSafe(gatt, characteristic, nextChunk, "queued_chunk")
                        }
                    }
                }

                @Deprecated("Deprecated in Java")
                override fun onCharacteristicChanged(gatt: BluetoothGatt, characteristic: BluetoothGattCharacteristic) {
                    if (Build.VERSION.SDK_INT < Build.VERSION_CODES.TIRAMISU) {
                        if (characteristic.uuid == BleUuids.CHAR_STATUS) {
                            @Suppress("DEPRECATION")
                            val payload = String(characteristic.value ?: ByteArray(0), Charsets.UTF_8)
                            parseDeviceStatus(gatt.device.address, payload)
                        }
                    }
                }

                override fun onCharacteristicChanged(gatt: BluetoothGatt, characteristic: BluetoothGattCharacteristic, value: ByteArray) {
                    if (characteristic.uuid == BleUuids.CHAR_STATUS) {
                        val payload = String(value, Charsets.UTF_8)
                        parseDeviceStatus(gatt.device.address, payload)
                    }
                }
            }

            // When user taps connect, autoConnect=false forces immediate direct connection attempt
            device.connectGatt(this@SugarotaBleService, false, gattCallback)
        }
    }

    private fun updateDeviceConfiguredStatus(address: String, isConfigured: Boolean) {
        val current = _deviceConfigured.value.toMutableMap()
        current[address] = isConfigured
        _deviceConfigured.value = current
    }

    private fun checkConfigCompleteness(address: String, configJson: String) {
        try {
            val json = org.json.JSONObject(configJson)
            val provider = json.optString("provider", "NIGHTSCOUT")
            val isConfigured = if (provider.equals("DEXCOM", ignoreCase = true)) {
                val dex = json.optJSONObject("dexcom")
                val user = dex?.optString("user", "") ?: ""
                val pass = dex?.optString("pass", "") ?: ""
                user.isNotBlank() && pass.isNotBlank()
            } else {
                val ns = json.optJSONObject("nightscout")
                val url = ns?.optString("url", "") ?: ""
                url.isNotBlank()
            }
            updateDeviceConfiguredStatus(address, isConfigured)
        } catch (e: Exception) {
            updateDeviceConfiguredStatus(address, false)
        }
    }

    private fun handleConfigReceived(address: String, payload: String) {
        if (payload.isNotBlank()) {
            deviceConfigs[address] = payload
            saveCachedDeviceConfig(address, payload)
            checkConfigCompleteness(address, payload)
        }
        pendingConfigReads.remove(address)?.invoke(payload)
        // Trigger sync ONLY if not already synced or syncing for this device
        serviceScope.launch {
            delay(300)
            if (!lastPushedTimestamps.containsKey(address) && !syncingDevices.contains(address)) {
                fetchAndPushForDevice(address, forcePush = true)
            }
        }
    }

    private fun refreshNotificationState() {
        val primaryAddr = getPrimaryDeviceAddress()
        val primaryReading = (primaryAddr?.let { _deviceReadings.value[it] } ?: _lastReading.value)
        if (primaryReading != null) {
            val units = primaryReading.units.takeIf { it.isNotBlank() } ?: getDeviceUnits(primaryAddr)
            val timeStr = java.text.SimpleDateFormat("HH:mm:ss", java.util.Locale.getDefault())
                .format(java.util.Date(primaryReading.timestamp * 1000))
            val valStr = formatGlucoseValue(primaryReading.sgv, units)
            val deltaStr = formatGlucoseDelta(primaryReading.delta, units)
            val statusSuffix = if (connectedGatts.isEmpty()) " (Offline)" else ""
            val summary = "$valStr $units ${primaryReading.trendArrow} ($deltaStr) at $timeStr$statusSuffix"
            updateNotification("Glucose: $summary", reading = primaryReading)
        } else if (connectedGatts.isNotEmpty()) {
            updateNotification("Connected to ${connectedGatts.size} device(s)")
        } else {
            updateNotification("Sugarota Companion (Offline)")
        }
    }

    fun disconnectDevice(address: String) {
        manuallyDisconnected.add(address)
        connectingDevices.remove(address)
        syncingDevices.remove(address)
        val gatt = connectedGatts.remove(address)
        lastPushedTimestamps.remove(address)
        currentMtu.remove(address)
        updateDeviceState(address, isConnected = false)
        try {
            gatt?.disconnect()
            gatt?.close()
        } catch (e: Exception) {
            e.printStackTrace()
        }
        refreshNotificationState()
    }

    // Send a remote JSON command packet to the device over CHAR_GLUCOSE
    fun sendDeviceCommand(address: String, cmdObj: org.json.JSONObject, onComplete: ((Boolean) -> Unit)? = null) {
        val gatt = connectedGatts[address]
        if (gatt == null) {
            Log.w("SugarotaBleService", "sendDeviceCommand failed: $address not connected in GATT map")
            appendDeviceLog(address, "sendDeviceCommand failed: not connected over BLE")
            onComplete?.invoke(false)
            return
        }
        val service = gatt.getService(BleUuids.SUGAROTA_SERVICE)
        val glucoseChar = service?.getCharacteristic(BleUuids.CHAR_GLUCOSE)
        if (glucoseChar == null) {
            Log.w("SugarotaBleService", "sendDeviceCommand failed: CHAR_GLUCOSE not found for $address")
            appendDeviceLog(address, "sendDeviceCommand failed: CHAR_GLUCOSE missing")
            onComplete?.invoke(false)
            return
        }
        val cmdName = cmdObj.optString("cmd", "unknown")
        val bytes = cmdObj.toString().toByteArray(Charsets.UTF_8)
        try {
            val success = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                val res = gatt.writeCharacteristic(glucoseChar, bytes, BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT)
                res == BluetoothStatusCodes.SUCCESS
            } else {
                @Suppress("DEPRECATION")
                glucoseChar.value = bytes
                @Suppress("DEPRECATION")
                glucoseChar.writeType = BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT
                @Suppress("DEPRECATION")
                gatt.writeCharacteristic(glucoseChar)
            }
            Log.i("SugarotaBleService", "sendDeviceCommand to $address: cmd=$cmdName (${bytes.size}B), write accepted=$success")
            appendDeviceLog(address, "BLE command [$cmdName] initiated: success=$success")
            onComplete?.invoke(success)
        } catch (e: Exception) {
            Log.e("SugarotaBleService", "sendDeviceCommand to $address failed", e)
            appendDeviceLog(address, "BLE command [$cmdName] error: ${e.message}")
            onComplete?.invoke(false)
        }
    }

    fun setDeviceBrightness(address: String, level: Int) {
        val clamped = level.coerceIn(76, 255)
        val cmd = org.json.JSONObject().apply {
            put("cmd", "set_brightness")
            put("val", clamped)
        }
        sendDeviceCommand(address, cmd)
        // Optimistically update device model state in app
        val current = _devices.value.toMutableMap()
        val dev = current[address]
        if (dev != null) {
            current[address] = dev.copy(status = dev.status.copy(brightness = clamped))
            _devices.value = current
        }
    }

    fun setDeviceTheme(address: String, isDark: Boolean) {
        val cmd = org.json.JSONObject().apply {
            put("cmd", "set_theme")
            put("val", if (isDark) "dark" else "light")
        }
        sendDeviceCommand(address, cmd)
        // Optimistically update device model state in app
        val current = _devices.value.toMutableMap()
        val dev = current[address]
        if (dev != null) {
            current[address] = dev.copy(status = dev.status.copy(isDarkTheme = isDark))
            _devices.value = current
        }
    }

    fun setDeviceDebugMode(address: String, enabled: Boolean) {
        val cmd = org.json.JSONObject().apply {
            put("cmd", "set_debug")
            put("val", enabled)
        }
        appendDeviceLog(address, "Sending set_debug: $enabled")
        sendDeviceCommand(address, cmd)
        // Optimistically update device model state in app
        val current = _devices.value.toMutableMap()
        val dev = current[address]
        if (dev != null) {
            current[address] = dev.copy(status = dev.status.copy(isDebugMode = enabled))
            _devices.value = current
        }
    }

    fun findDevice(address: String, onComplete: ((Boolean) -> Unit)? = null) {
        val cmd = org.json.JSONObject().apply {
            put("cmd", "find_device")
        }
        sendDeviceCommand(address, cmd, onComplete)
    }

    fun rebootDevice(address: String, onComplete: ((Boolean) -> Unit)? = null) {
        val cmd = org.json.JSONObject().apply {
            put("cmd", "reboot")
        }
        sendDeviceCommand(address, cmd) { success ->
            serviceScope.launch {
                delay(150) // Allow characteristic write buffer to flush
                disconnectDevice(address)
                onComplete?.invoke(success)
            }
        }
    }

    fun startWifiOta(address: String, onComplete: ((Boolean) -> Unit)? = null) {
        val cmd = org.json.JSONObject().apply {
            put("cmd", "start_wifi_ota")
        }
        appendDeviceLog(address, "Sending start_wifi_ota command...")
        // Reset device OTA status state to waiting/idle
        val current = _devices.value.toMutableMap()
        val dev = current[address]
        if (dev != null) {
            current[address] = dev.copy(wifiOta = org.sugarota.companion.model.WifiOtaStatus(status = "connecting"))
            _devices.value = current
        }
        sendDeviceCommand(address, cmd, onComplete)
    }

    fun powerOffDevice(address: String, onComplete: ((Boolean) -> Unit)? = null) {
        val cmd = org.json.JSONObject().apply {
            put("cmd", "power_off")
        }
        sendDeviceCommand(address, cmd) { success ->
            serviceScope.launch {
                delay(150) // Allow characteristic write buffer to flush
                disconnectDevice(address)
                onComplete?.invoke(success)
            }
        }
    }

    // Push immediate time synchronization packet to device upon connection
    fun pushTimeSyncToDevice(address: String) {
        val gatt = connectedGatts[address] ?: return
        val service = gatt.getService(BleUuids.SUGAROTA_SERVICE) ?: return
        val glucoseChar = service.getCharacteristic(BleUuids.CHAR_GLUCOSE) ?: return
        val syncJson = GlucoseData.createTimeSyncJson()
        val bytes = syncJson.toByteArray(Charsets.UTF_8)
        val success = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            val res = gatt.writeCharacteristic(glucoseChar, bytes, BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT)
            res == BluetoothStatusCodes.SUCCESS
        } else {
            @Suppress("DEPRECATION")
            glucoseChar.value = bytes
            @Suppress("DEPRECATION")
            glucoseChar.writeType = BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT
            @Suppress("DEPRECATION")
            gatt.writeCharacteristic(glucoseChar)
        }
        Log.i("SugarotaBleService", "pushTimeSyncToDevice to $address: write initiated=$success")
    }

    // Push API OK status packet when server was queried successfully but has no new reading
    fun pushApiOkToDevice(address: String) {
        val gatt = connectedGatts[address] ?: return
        val service = gatt.getService(BleUuids.SUGAROTA_SERVICE) ?: return
        val glucoseChar = service.getCharacteristic(BleUuids.CHAR_GLUCOSE) ?: return
        val okJson = GlucoseData.createApiOkJson()
        val bytes = okJson.toByteArray(Charsets.UTF_8)
        writeCharacteristicSafe(gatt, glucoseChar, bytes, "api_ok")
    }

    // Push API error packet when server could not be reached or query failed, signaling Wi-Fi fallback
    fun pushApiErrToDevice(address: String, message: String = "") {
        val gatt = connectedGatts[address] ?: return
        val service = gatt.getService(BleUuids.SUGAROTA_SERVICE) ?: return
        val glucoseChar = service.getCharacteristic(BleUuids.CHAR_GLUCOSE) ?: return
        val errJson = GlucoseData.createApiErrJson(message)
        val bytes = errJson.toByteArray(Charsets.UTF_8)
        writeCharacteristicSafe(gatt, glucoseChar, bytes, "api_err")
    }



    // Push glucose to a specific connected Sugarota device.
    // When isFullSync is true (initial connection, device reconnect, or force refresh),
    // full history chunks and time synchronization are sent.
    // Push glucose to a specific connected Sugarota device.
    // When isFullSync is true (initial connection, device reconnect, force refresh, or detected data gap),
    // full history chunks are enqueued and time synchronization is sent.
    fun pushGlucoseToDevice(address: String, glucose: GlucoseData, isFullSync: Boolean = false) {
        val gatt = connectedGatts[address] ?: return
        val service = gatt.getService(BleUuids.SUGAROTA_SERVICE)
        val glucoseChar = service?.getCharacteristic(BleUuids.CHAR_GLUCOSE) ?: return

        val deviceMtu = currentMtu[address] ?: 517
        val maxPayloadSize = (deviceMtu - 3).coerceAtLeast(20)

        val itemsPerPacket = GlucoseData.historyItemsPerPacket()
        var primaryJson = glucose.toJson(maxHistory = itemsPerPacket, includeTimeSync = isFullSync)
        var primaryBytes = primaryJson.toByteArray(Charsets.UTF_8)
        if (primaryBytes.size > maxPayloadSize) {
            Log.w("SugarotaBleService", "Primary payload (${primaryBytes.size}) exceeds MTU ($maxPayloadSize). Sending root only.")
            primaryJson = glucose.copy(history = emptyList()).toJson(maxHistory = 0, includeTimeSync = isFullSync)
            primaryBytes = primaryJson.toByteArray(Charsets.UTF_8)
        }

        if (isFullSync) {
            // Build and enqueue follow-up history chunks during full sync / gap recovery
            val remainingHistory = glucose.history.drop(itemsPerPacket)
            if (remainingHistory.isNotEmpty()) {
                val queue = ArrayDeque<ByteArray>()
                var offset = 0
                while (offset < remainingHistory.size) {
                    val chunk = remainingHistory.subList(offset, minOf(offset + itemsPerPacket, remainingHistory.size))
                    val chunkBytes = GlucoseData.createHistoryChunkJson(chunk).toByteArray(Charsets.UTF_8)
                    queue.addLast(chunkBytes)
                    Log.i("SugarotaBleService", "Enqueued history_chunk offset=$offset size=${chunk.size} bytes=${chunkBytes.size}")
                    offset += itemsPerPacket
                }
                pendingWriteQueues[address] = queue
                Log.i("SugarotaBleService", "Write queue ready: ${queue.size} chunks after primary for $address")
            } else {
                pendingWriteQueues.remove(address)
            }
        } else {
            pendingWriteQueues.remove(address)
        }

        // Send primary — subsequent chunks (if any) are triggered by onCharacteristicWrite
        writeCharacteristicSafe(gatt, glucoseChar, primaryBytes, "primary sgv=${glucose.sgv} (fullSync=$isFullSync)")
    }

    private fun writeCharacteristicSafe(
        gatt: BluetoothGatt,
        characteristic: BluetoothGattCharacteristic,
        bytes: ByteArray,
        label: String
    ) {
        try {
            val success = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                val res = gatt.writeCharacteristic(characteristic, bytes, BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT)
                res == BluetoothStatusCodes.SUCCESS
            } else {
                @Suppress("DEPRECATION")
                characteristic.value = bytes
                @Suppress("DEPRECATION")
                characteristic.writeType = BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT
                @Suppress("DEPRECATION")
                gatt.writeCharacteristic(characteristic)
            }
            Log.i("SugarotaBleService", "writeCharacteristic [$label] bytes=${bytes.size}: success=$success")
            appendDeviceLog(gatt.device.address, "BLE write [$label] (${bytes.size}B): success=$success")
        } catch (e: Exception) {
            Log.e("SugarotaBleService", "writeCharacteristic [$label] failed", e)
            appendDeviceLog(gatt.device.address, "BLE write [$label] error: ${e.message}")
        }
    }

    // Broadcast glucose to ALL connected Sugarota screens
    fun pushGlucoseToAll(glucose: GlucoseData) {
        connectedGatts.keys.forEach { address ->
            pushGlucoseToDevice(address, glucose)
        }
    }

    // Read config from a specific device
    fun readConfig(address: String, onComplete: (String) -> Unit) {
        val gatt = connectedGatts[address]
        if (gatt == null) {
            val cached = deviceConfigs[address] ?: ""
            return onComplete(cached)
        }
        val service = gatt.getService(BleUuids.SUGAROTA_SERVICE)
        val configChar = service?.getCharacteristic(BleUuids.CHAR_CONFIG)
        if (configChar == null) {
            val cached = deviceConfigs[address] ?: ""
            return onComplete(cached)
        }

        pendingConfigReads[address] = onComplete
        gatt.readCharacteristic(configChar)
    }

    // Write config to a specific device
    fun writeConfig(address: String, configJson: String, onComplete: (Boolean) -> Unit) {
        val mainHandler = Handler(Looper.getMainLooper())
        val gatt = connectedGatts[address]
        if (gatt == null) {
            mainHandler.post { onComplete(false) }
            return
        }
        val service = gatt.getService(BleUuids.SUGAROTA_SERVICE)
        val configChar = service?.getCharacteristic(BleUuids.CHAR_CONFIG)
        if (configChar == null) {
            mainHandler.post { onComplete(false) }
            return
        }

        deviceConfigs[address] = configJson
        saveCachedDeviceConfig(address, configJson)
        checkConfigCompleteness(address, configJson)

        serviceScope.launch {
            try {
                // Minify JSON to ensure minimal payload footprint
                val compactJson = try {
                    org.json.JSONObject(configJson).toString()
                } catch (e: Exception) {
                    configJson.trim()
                }

                val allBytes = compactJson.toByteArray(Charsets.UTF_8)
                val deviceMtu = currentMtu[address] ?: 517
                val maxChunkSize = (deviceMtu - 3).coerceAtLeast(20)

                val overallSuccess: Boolean

                if (allBytes.size <= maxChunkSize) {
                    // Fits in a single packet directly
                    overallSuccess = writeConfigCharacteristicDirect(gatt, configChar, allBytes)
                } else {
                    // Multi-packet chunking protocol supported by Sugarota firmware:
                    // 1. Send "[START]"
                    // 2. Send chunks
                    // 3. Send "[END]"
                    val startBytes = "[START]".toByteArray(Charsets.UTF_8)
                    var ok = writeConfigCharacteristicDirect(gatt, configChar, startBytes)
                    delay(50)

                    if (ok) {
                        var offset = 0
                        while (offset < allBytes.size && ok) {
                            val chunkLen = minOf(maxChunkSize, allBytes.size - offset)
                            val chunkBytes = allBytes.copyOfRange(offset, offset + chunkLen)
                            ok = writeConfigCharacteristicDirect(gatt, configChar, chunkBytes)
                            offset += chunkLen
                            delay(60)
                        }
                    }

                    if (ok) {
                        delay(50)
                        val endBytes = "[END]".toByteArray(Charsets.UTF_8)
                        ok = writeConfigCharacteristicDirect(gatt, configChar, endBytes)
                    }
                    overallSuccess = ok
                }

                Log.i("SugarotaBleService", "writeConfig to $address completed. Success=$overallSuccess (bytes=${allBytes.size})")

                mainHandler.post {
                    onComplete(overallSuccess)
                }
            } catch (e: Exception) {
                Log.e("SugarotaBleService", "writeConfig exception", e)
                mainHandler.post {
                    onComplete(false)
                }
            }
        }
    }

    private fun writeConfigCharacteristicDirect(
        gatt: BluetoothGatt,
        characteristic: BluetoothGattCharacteristic,
        bytes: ByteArray
    ): Boolean {
        return try {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                val res = gatt.writeCharacteristic(characteristic, bytes, BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT)
                res == BluetoothStatusCodes.SUCCESS
            } else {
                @Suppress("DEPRECATION")
                characteristic.value = bytes
                @Suppress("DEPRECATION")
                characteristic.writeType = BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT
                @Suppress("DEPRECATION")
                gatt.writeCharacteristic(characteristic)
            }
        } catch (e: Exception) {
            Log.e("SugarotaBleService", "writeConfigCharacteristicDirect error", e)
            false
        }
    }

    fun getCachedConfig(address: String): String? = deviceConfigs[address]

    fun triggerManualSync(onComplete: ((Boolean) -> Unit)? = null) {
        serviceScope.launch {
            var anySuccess = false
            for (address in connectedGatts.keys) {
                val ok = fetchAndPushForDevice(address, forcePush = true)
                if (ok) anySuccess = true
            }
            onComplete?.invoke(anySuccess)
        }
    }

    private suspend fun fetchAndPushForDevice(address: String, forcePush: Boolean = false): Boolean {
        if (syncingDevices.contains(address)) {
            Log.i("SugarotaBleService", "fetchAndPushForDevice: Sync already active for $address, skipping concurrent call")
            return false
        }
        val configJson = deviceConfigs[address]
        if (configJson.isNullOrBlank()) {
            _bridgeStatus.value = "Waiting for device config..."
            return false
        }

        syncingDevices.add(address)
        try {
            val json = org.json.JSONObject(configJson)
            val provider = json.optString("provider", "NIGHTSCOUT")
            _bridgeStatus.value = "Fetching glucose ($provider)..."

            val reading: GlucoseData? = if (provider.equals("DEXCOM", ignoreCase = true)) {
                val dex = json.optJSONObject("dexcom")
                val user = dex?.optString("user", "") ?: ""
                val pass = dex?.optString("pass", "") ?: ""
                val server = dex?.optString("server", "shareous1.dexcom.com") ?: "shareous1.dexcom.com"
                if (user.isBlank() || pass.isBlank()) {
                    updateDeviceConfiguredStatus(address, false)
                    _bridgeStatus.value = "Dexcom credentials missing in /config.json"
                    return false
                }
                updateDeviceConfiguredStatus(address, true)
                bridgeClient.fetchDexcom(user, pass, server)
            } else {
                val ns = json.optJSONObject("nightscout")
                val url = ns?.optString("url", "") ?: ""
                val secret = ns?.optString("secret", "") ?: ""
                if (url.isBlank()) {
                    updateDeviceConfiguredStatus(address, false)
                    _bridgeStatus.value = "Nightscout URL missing in /config.json"
                    return false
                }
                updateDeviceConfiguredStatus(address, true)
                bridgeClient.fetchNightscout(url, secret)
            }

            if (reading != null) {
                // Update global fallback and per-device reading map
                _lastReading.value = reading
                val updatedReadings = _deviceReadings.value.toMutableMap()
                updatedReadings[address] = reading
                _deviceReadings.value = updatedReadings

                val units = reading.units.takeIf { it.isNotBlank() } ?: getDeviceUnits(address)
                val timeStr = java.text.SimpleDateFormat("HH:mm:ss", java.util.Locale.getDefault())
                    .format(java.util.Date(reading.timestamp * 1000))
                val arrow = reading.trendArrow
                val valStr = formatGlucoseValue(reading.sgv, units)
                val deltaStr = formatGlucoseDelta(reading.delta, units)
                val summary = "$valStr $units $arrow ($deltaStr) at $timeStr"

                val lastTs = lastPushedTimestamps[address]
                val isNewData = (lastTs == null || reading.timestamp > lastTs)

                // Check for a data gap between the new reading and the last pushed reading (> 360 seconds / 6 minutes),
                // or if there are any gaps within the recent readings list.
                var hasGap = false
                if (lastTs != null && (reading.timestamp - lastTs > 360)) {
                    hasGap = true
                } else if (reading.history.size >= 2) {
                    val checkCount = minOf(10, reading.history.size - 1)
                    for (k in 0 until checkCount) {
                        if (reading.history[k].timestamp - reading.history[k + 1].timestamp > 360) {
                            hasGap = true
                            break
                        }
                    }
                }

                val shouldFullSync = forcePush || hasGap

                if (!shouldFullSync && !isNewData) {
                    // Reading has not changed on the server yet, not a force push, and no gaps detected.
                    // Notify device that remote API is OK so it resets fetch timer and clears spinner without Wi-Fi fallback.
                    pushApiOkToDevice(address)
                    _bridgeStatus.value = "Synced $summary (current)"
                    return true
                }

                if (hasGap) {
                    Log.i("SugarotaBleService", "Detected data gap for $address (lastTs=$lastTs, newTs=${reading.timestamp}). Triggering full history backfill.")
                }

                lastPushedTimestamps[address] = reading.timestamp
                pushGlucoseToDevice(address, reading, isFullSync = shouldFullSync)
                _bridgeStatus.value = "Synced $summary"

                // Topmost device in list is the primary source for status notifications and chart preview
                val primaryAddr = getPrimaryDeviceAddress()
                if (primaryAddr == null || primaryAddr == address) {
                    updateNotification("Glucose: $summary", isNewData = isNewData, reading = reading)
                }
                return true
            } else {
                _bridgeStatus.value = "Fetch failed · Network error"
                // Inform device that companion could not reach remote API, triggering Wi-Fi fallback
                pushApiErrToDevice(address, "Network error")
                return false
            }
        } catch (e: Exception) {
            e.printStackTrace()
            _bridgeStatus.value = "Error parsing config.json"
            pushApiErrToDevice(address, "Config error")
            return false
        } finally {
            syncingDevices.remove(address)
        }
    }

    private fun computeNextDelayMs(): Long {
        // Find configured poll interval from connected device configs (fallback 60s)
        var pollSec = 60L
        for (cfg in deviceConfigs.values) {
            try {
                val sec = org.json.JSONObject(cfg).optLong("poll_interval_sec", 60L)
                if (sec in 30..600) {
                    pollSec = sec
                    break
                }
            } catch (e: Exception) {
                // ignore JSON error
            }
        }

        val reading = _lastReading.value
        val nowSec = System.currentTimeMillis() / 1000L

        if (reading != null && reading.timestamp > 0) {
            var targetTs = reading.timestamp + pollSec
            while (targetTs <= nowSec) {
                targetTs += pollSec
            }
            val delaySec = (targetTs - nowSec).coerceIn(10L, pollSec)
            Log.i("SugarotaBleService", "Timestamp-aligned schedule: readingTs=${reading.timestamp}, nowSec=$nowSec, nextTargetTs=$targetTs, delaySec=$delaySec")
            return delaySec * 1000L
        }

        return pollSec * 1000L
    }

    private fun startPeriodicBridge() {
        bridgeJob?.cancel()
        bridgeJob = serviceScope.launch {
            while (isActive) {
                // Collect addresses to query: connected devices take priority, followed by any configured offline devices
                val targetAddresses = linkedSetOf<String>()
                targetAddresses.addAll(connectedGatts.keys)
                val configuredOffline = deviceConfigs.keys.filter { addr ->
                    !connectedGatts.containsKey(addr) && (_deviceConfigured.value[addr] == true)
                }
                targetAddresses.addAll(configuredOffline)

                if (targetAddresses.isNotEmpty()) {
                    for (address in targetAddresses) {
                        if (connectedGatts.containsKey(address) && (!deviceConfigs.containsKey(address) || deviceConfigs[address].isNullOrBlank())) {
                            readConfig(address) { /* handleConfigReceived takes care of caching and trigger */ }
                        }
                        fetchAndPushForDevice(address)
                    }
                } else {
                    _bridgeStatus.value = "Idle · Waiting for displays"
                }

                val delayMs = computeNextDelayMs()
                // Auto-dismiss "Sugarota Detected Nearby" notification when device is no longer nearby or now connected
                SugarotaBleScanReceiver.dismissStaleNearbyNotifications(this@SugarotaBleService)
                delay(delayMs)
            }
        }
    }

    private fun updateDeviceState(address: String, isConnected: Boolean, isBonded: Boolean? = null) {
        val current = _devices.value.toMutableMap()
        val existing = current[address] ?: SugarotaDevice(name = getDeviceDisplayName(address), address = address)
        current[address] = existing.copy(
            isConnected = isConnected,
            isBonded = isBonded ?: existing.isBonded
        )
        _devices.value = current
    }

    private fun updateDeviceBondState(address: String, isBonded: Boolean) {
        val current = _devices.value.toMutableMap()
        val existing = current[address] ?: SugarotaDevice(name = getDeviceDisplayName(address), address = address)
        current[address] = existing.copy(isBonded = isBonded)
        _devices.value = current
    }

    private fun parseDeviceStatus(address: String, json: String) {
        val trimmed = json.trim()
        if (!trimmed.startsWith("{") || !trimmed.endsWith("}")) {
            Log.w("SugarotaBleService", "parseDeviceStatus: Incomplete JSON received from $address (len=${trimmed.length}): $trimmed")
            return
        }
        try {
            val obj = org.json.JSONObject(trimmed)
            val current = _devices.value.toMutableMap()
            val existing = current[address] ?: SugarotaDevice(name = getDeviceDisplayName(address), address = address)

            // Check if this notification is a wifi_ota status update
            if (obj.has("wifi_ota")) {
                val otaStatus = obj.optString("wifi_ota", "idle")
                val otaIp = obj.optString("ip", "")
                val otaMdns = obj.optString("mdns", "")
                val wifiOta = org.sugarota.companion.model.WifiOtaStatus(status = otaStatus, ip = otaIp, mdns = otaMdns)
                current[address] = existing.copy(wifiOta = wifiOta)
                _devices.value = current
                appendDeviceLog(address, "Wi-Fi OTA Status: status=$otaStatus ip=$otaIp mdns=$otaMdns")
                return
            }

            val bat = obj.optInt("battery", 0)
            val chg = obj.optBoolean("charging", false)
            val ver = obj.optString("version", "Unknown")
            val brightness = if (obj.has("brightness")) obj.optInt("brightness", existing.status.brightness) else existing.status.brightness
            val isDark = if (obj.has("dark_theme")) obj.optBoolean("dark_theme", existing.status.isDarkTheme) else existing.status.isDarkTheme
            val isDebug = if (obj.has("debug")) obj.optBoolean("debug", existing.status.isDebugMode) else existing.status.isDebugMode
            current[address] = existing.copy(status = DeviceStatus(bat, chg, ver, brightness, isDark, isDebug))
            _devices.value = current
            appendDeviceLog(address, "Status received: bat=$bat% chg=$chg ver=$ver debug=$isDebug")

            // When device notifies status:
            // 1. If we don't have its config yet, attempt to read config now
            if (!deviceConfigs.containsKey(address) || deviceConfigs[address].isNullOrBlank()) {
                val gatt = connectedGatts[address]
                if (gatt != null) {
                    val service = gatt.getService(BleUuids.SUGAROTA_SERVICE)
                    val configChar = service?.getCharacteristic(BleUuids.CHAR_CONFIG)
                    if (configChar != null) {
                        Log.i("SugarotaBleService", "Config missing on status notify; reading config for $address")
                        gatt.readCharacteristic(configChar)
                    }
                }
            }

            // 2. Trigger sync if requested by device (force refresh from BOOT button) or on initial connect
            val requestRefresh = obj.optBoolean("request_refresh", false)
            serviceScope.launch {
                val isInitial = !lastPushedTimestamps.containsKey(address)
                if ((requestRefresh || isInitial) && deviceConfigs.containsKey(address) && !syncingDevices.contains(address)) {
                    Log.i("SugarotaBleService", "Triggering sync for $address (requestRefresh=$requestRefresh, isInitial=$isInitial)")
                    fetchAndPushForDevice(address, forcePush = true)
                }
            }
        } catch (e: Exception) {
            Log.e("SugarotaBleService", "parseDeviceStatus error parsing: $trimmed", e)
        }
    }

    private fun wakeScreenBriefly() {
        try {
            val powerManager = getSystemService(Context.POWER_SERVICE) as? android.os.PowerManager
            val isInteractive = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.KITKAT_WATCH) {
                powerManager?.isInteractive == true
            } else {
                @Suppress("DEPRECATION")
                powerManager?.isScreenOn == true
            }

            // Only wake if screen is currently OFF
            if (!isInteractive && powerManager != null) {
                @Suppress("DEPRECATION")
                val wakeLock = powerManager.newWakeLock(
                    android.os.PowerManager.SCREEN_BRIGHT_WAKE_LOCK or android.os.PowerManager.ACQUIRE_CAUSES_WAKEUP,
                    "sugarota:glucose_update_wake"
                )
                wakeLock.acquire(3000L) // 3 seconds timeout
                Log.d("SugarotaBleService", "Acquired temporary WakeLock for new glucose reading")
            }
        } catch (e: Exception) {
            Log.w("SugarotaBleService", "Failed to wake screen: ${e.message}")
        }
    }

    fun getDeviceUnits(address: String?): String {
        if (address != null) {
            val cfgJson = deviceConfigs[address]
            if (!cfgJson.isNullOrBlank()) {
                try {
                    val u = org.json.JSONObject(cfgJson).optString("units", "")
                    if (u.isNotBlank()) return u
                } catch (e: Exception) {
                    // Ignore JSON parsing errors
                }
            }
        }
        return "mg/dL"
    }

    private fun formatGlucoseValue(sgv: Int, units: String): String {
        return if (units.equals("mmol/l", ignoreCase = true)) {
            String.format(java.util.Locale.US, "%.1f", sgv / 18.0182f)
        } else {
            sgv.toString()
        }
    }

    private fun formatGlucoseDelta(delta: Int, units: String): String {
        return if (units.equals("mmol/l", ignoreCase = true)) {
            val mmolVal = delta / 18.0182f
            if (delta == 0) "+0.0" else String.format(java.util.Locale.US, "%s%.1f", if (delta > 0) "+" else "", mmolVal)
        } else {
            "${if (delta >= 0) "+" else ""}$delta"
        }
    }

    private fun createGlucoseIconBitmap(text: String): android.graphics.Bitmap {
        val size = 96
        val bitmap = android.graphics.Bitmap.createBitmap(size, size, android.graphics.Bitmap.Config.ARGB_8888)
        val canvas = android.graphics.Canvas(bitmap)

        val paint = android.graphics.Paint().apply {
            color = android.graphics.Color.WHITE
            isAntiAlias = true
            textAlign = android.graphics.Paint.Align.CENTER
            typeface = android.graphics.Typeface.create("sans-serif-condensed", android.graphics.Typeface.BOLD)
            textSize = when {
                text.length <= 2 -> 76f
                text.length == 3 -> 66f
                text.length == 4 -> 54f
                else -> 42f
            }
        }

        // Measure text bounds to ensure text is precisely centered and fits within the canvas
        val bounds = android.graphics.Rect()
        paint.getTextBounds(text, 0, text.length, bounds)

        // If width exceeds canvas width minus safe padding, downscale slightly
        val maxAllowedWidth = size - 8f
        if (bounds.width() > maxAllowedWidth) {
            paint.textSize *= (maxAllowedWidth / bounds.width())
            paint.getTextBounds(text, 0, text.length, bounds)
        }

        val y = (size / 2f) - bounds.exactCenterY()
        canvas.drawText(text, size / 2f, y, paint)
        return bitmap
    }

    private fun buildNotification(text: String, reading: GlucoseData? = null): Notification {
        val launchIntent = Intent(this, MainActivity::class.java).apply {
            flags = Intent.FLAG_ACTIVITY_SINGLE_TOP or Intent.FLAG_ACTIVITY_CLEAR_TOP
        }
        val pendingIntent = PendingIntent.getActivity(
            this,
            0,
            launchIntent,
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
        )

        val primaryAddr = getPrimaryDeviceAddress()
        val units = reading?.units?.takeIf { it.isNotBlank() } ?: getDeviceUnits(primaryAddr)
        val chartReading = reading ?: _lastReading.value

        val builder = NotificationCompat.Builder(this, CHANNEL_ID)
            .setContentTitle("Sugarota Companion")
            .setContentText(text)
            .setContentIntent(pendingIntent)
            .setOngoing(true)

        if (chartReading != null) {
            val iconText = formatGlucoseValue(chartReading.sgv, units)
            val iconBitmap = createGlucoseIconBitmap(iconText)
            builder.setSmallIcon(androidx.core.graphics.drawable.IconCompat.createWithBitmap(iconBitmap))
        } else {
            builder.setSmallIcon(android.R.drawable.stat_notify_sync)
        }

        if (chartReading != null) {
            try {
                val chartBitmap = org.sugarota.companion.ui.notification.NotificationChartRenderer.renderTwoHourChart(chartReading, units)
                builder.setStyle(
                    NotificationCompat.BigPictureStyle()
                        .bigPicture(chartBitmap)
                        .setSummaryText(text)
                )
            } catch (e: Exception) {
                Log.w("SugarotaBleService", "Error generating notification chart: ${e.message}")
            }
        }

        return builder.build()
    }

    private fun updateNotification(text: String, isNewData: Boolean = false, reading: GlucoseData? = null) {
        if (isNewData) {
            wakeScreenBriefly()
        }
        val manager = getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
        manager.notify(NOTIFICATION_ID, buildNotification(text, reading))
    }

    private fun createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val bridgeChannel = NotificationChannel(
                CHANNEL_ID,
                "Sugarota BLE Bridge",
                NotificationManager.IMPORTANCE_LOW
            )
            val alertChannel = NotificationChannel(
                ALERT_CHANNEL_ID,
                "Sugarota Device Alerts",
                NotificationManager.IMPORTANCE_HIGH
            ).apply {
                description = "High-priority notifications when Sugarota displays are detected nearby"
                enableVibration(true)
                enableLights(true)
            }
            val manager = getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
            manager.createNotificationChannel(bridgeChannel)
            manager.createNotificationChannel(alertChannel)
        }
    }

    private var backgroundScanPendingIntent: PendingIntent? = null

    private fun getOrCreateBackgroundScanPendingIntent(): PendingIntent {
        if (backgroundScanPendingIntent != null) return backgroundScanPendingIntent!!
        val intent = Intent(this, SugarotaBleScanReceiver::class.java)
        backgroundScanPendingIntent = PendingIntent.getBroadcast(
            this,
            201,
            intent,
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_MUTABLE
        )
        return backgroundScanPendingIntent!!
    }

    fun startBackgroundPendingIntentScan() {
        try {
            val scanner = bluetoothAdapter?.bluetoothLeScanner ?: return
            val pendingIntent = getOrCreateBackgroundScanPendingIntent()
            val filters = listOf(
                ScanFilter.Builder()
                    .setServiceUuid(android.os.ParcelUuid(BleUuids.SUGAROTA_SERVICE))
                    .build()
            )
            val settings = ScanSettings.Builder()
                .setScanMode(ScanSettings.SCAN_MODE_LOW_POWER)
                .setMatchMode(ScanSettings.MATCH_MODE_AGGRESSIVE)
                .setNumOfMatches(ScanSettings.MATCH_NUM_ONE_ADVERTISEMENT)
                .build()

            scanner.startScan(filters, settings, pendingIntent)
            Log.i("SugarotaBleService", "Registered background PendingIntent BLE scanner for Sugarota UUID")
        } catch (e: SecurityException) {
            Log.w("SugarotaBleService", "startBackgroundPendingIntentScan SecurityException", e)
        } catch (e: Exception) {
            Log.e("SugarotaBleService", "startBackgroundPendingIntentScan error", e)
        }
    }

    fun stopBackgroundPendingIntentScan() {
        try {
            val scanner = bluetoothAdapter?.bluetoothLeScanner ?: return
            val pendingIntent = backgroundScanPendingIntent ?: return
            scanner.stopScan(pendingIntent)
            Log.i("SugarotaBleService", "Stopped background PendingIntent BLE scanner")
        } catch (e: SecurityException) {
            Log.w("SugarotaBleService", "stopBackgroundPendingIntentScan SecurityException", e)
        } catch (e: Exception) {
            Log.e("SugarotaBleService", "stopBackgroundPendingIntentScan error", e)
        }
    }

    companion object {
        const val CHANNEL_ID = "sugarota_ble_channel"
        const val ALERT_CHANNEL_ID = "sugarota_alerts_channel"
        private const val NOTIFICATION_ID = 101

        const val ACTION_CONNECT_DEVICE = "org.sugarota.companion.ACTION_CONNECT_DEVICE"
        const val EXTRA_DEVICE_ADDRESS = "extra_device_address"

        // Track active connections for fast checks across processes/receivers
        private val activeConnectedAddresses = java.util.Collections.newSetFromMap(ConcurrentHashMap<String, Boolean>())
        private val manuallyDisconnected = java.util.Collections.newSetFromMap(ConcurrentHashMap<String, Boolean>())

        fun isDeviceConnected(address: String): Boolean {
            return activeConnectedAddresses.contains(address)
        }

        internal fun markDeviceConnected(address: String, connected: Boolean) {
            if (connected) {
                activeConnectedAddresses.add(address)
            } else {
                activeConnectedAddresses.remove(address)
            }
        }

        fun isDeviceManuallyDisconnected(address: String): Boolean {
            return manuallyDisconnected.contains(address)
        }

        fun markDeviceManuallyDisconnected(address: String, disconnected: Boolean) {
            if (disconnected) {
                manuallyDisconnected.add(address)
            } else {
                manuallyDisconnected.remove(address)
            }
        }
    }
}
