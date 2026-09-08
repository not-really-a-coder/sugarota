package org.sugarota.companion.service

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.Service
import android.bluetooth.*
import android.bluetooth.le.*
import android.content.Context
import android.content.Intent
import android.os.Binder
import android.os.Build
import android.os.IBinder
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
    private val pendingConfigReads = ConcurrentHashMap<String, (String) -> Unit>()
    private val deviceConfigs = ConcurrentHashMap<String, String>() // address -> raw JSON
    private val lastPushedTimestamps = ConcurrentHashMap<String, Long>() // address -> timestamp
    private val currentMtu = ConcurrentHashMap<String, Int>() // address -> negotiated MTU
    private val manuallyDisconnected = java.util.Collections.newSetFromMap(ConcurrentHashMap<String, Boolean>())
    // Per-device write queue: each entry is a list of raw byte payloads to be sent sequentially.
    // The next chunk is sent only after onCharacteristicWrite fires for the previous one.
    private val pendingWriteQueues = ConcurrentHashMap<String, ArrayDeque<ByteArray>>()

    private val _devices = MutableStateFlow<Map<String, SugarotaDevice>>(emptyMap())
    val devices: StateFlow<Map<String, SugarotaDevice>> = _devices.asStateFlow()

    private val bridgeClient = GlucoseBridgeClient()
    private val bridgePrefs by lazy { org.sugarota.companion.data.BridgePreferences(this) }
    private var bridgeJob: Job? = null

    private val _lastReading = MutableStateFlow<GlucoseData?>(null)
    val lastReading: StateFlow<GlucoseData?> = _lastReading.asStateFlow()

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
        startPeriodicBridge()
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
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
        serviceScope.cancel()
        connectedGatts.values.forEach { it.close() }
        connectedGatts.clear()
    }

    // Custom device names storage
    private val namePrefs by lazy { getSharedPreferences("sugarota_device_names", Context.MODE_PRIVATE) }

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

    private val scanCallback = object : ScanCallback() {
        override fun onScanResult(callbackType: Int, result: ScanResult) {
            val device = result.device
            val name = try { device.name } catch (e: SecurityException) { null } ?: ""
            if (name.startsWith("SUGAROTA", ignoreCase = true)) {
                val addr = device.address
                val resolvedName = getDeviceDisplayName(addr, name)
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
                // If user explicitly disconnected, don't auto-reconnect from scan
                if (!manuallyDisconnected.contains(addr) && !connectedGatts.containsKey(addr)) {
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
        val device = bluetoothAdapter?.getRemoteDevice(address) ?: return
        serviceScope.launch {
            val gattCallback = object : BluetoothGattCallback() {
                override fun onConnectionStateChange(gatt: BluetoothGatt, status: Int, newState: Int) {
                    val addr = gatt.device.address
                    if (newState == BluetoothProfile.STATE_CONNECTED) {
                        connectedGatts[addr] = gatt
                        val bonded = gatt.device.bondState == BluetoothDevice.BOND_BONDED
                        updateDeviceState(addr, isConnected = true, isBonded = bonded)
                        gatt.requestMtu(517)
                        gatt.discoverServices()
                        updateNotification("Connected to ${connectedGatts.size} device(s)")
                    } else if (newState == BluetoothProfile.STATE_DISCONNECTED) {
                        connectedGatts.remove(addr)
                        deviceConfigs.remove(addr)
                        lastPushedTimestamps.remove(addr)
                        currentMtu.remove(addr)
                        pendingWriteQueues.remove(addr) // Clear any pending history chunks
                        val bonded = gatt.device.bondState == BluetoothDevice.BOND_BONDED
                        updateDeviceState(addr, isConnected = false, isBonded = bonded)
                        gatt.close()
                        updateNotification("Waiting for Sugarota connection...")
                    }
                }

                override fun onMtuChanged(gatt: BluetoothGatt, mtu: Int, status: Int) {
                    Log.i("SugarotaBleService", "BLE MTU changed to $mtu (status=$status) for ${gatt.device.address}")
                    if (status == BluetoothGatt.GATT_SUCCESS) {
                        currentMtu[gatt.device.address] = mtu
                    }
                    // Once MTU is updated, push immediate time sync
                    pushTimeSyncToDevice(gatt.device.address)
                }

                override fun onServicesDiscovered(gatt: BluetoothGatt, status: Int) {
                    if (status == BluetoothGatt.GATT_SUCCESS) {
                        val service = gatt.getService(BleUuids.SUGAROTA_SERVICE)
                        val statusChar = service?.getCharacteristic(BleUuids.CHAR_STATUS)
                        val configChar = service?.getCharacteristic(BleUuids.CHAR_CONFIG)

                        // Immediately push time sync upon service discovery
                        pushTimeSyncToDevice(gatt.device.address)

                        if (statusChar != null) {
                            // 1. Enable local notifications
                            gatt.setCharacteristicNotification(statusChar, true)

                            // 2. Enable remote indications/notifications via CCCD descriptor
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
                            }

                            // 3. Immediately read current status so we don't have to wait for the next notify interval
                            gatt.readCharacteristic(statusChar)
                        }

                        // Fetch config.json from device to bridge glucose data ONLY IF device is already bonded.
                        // If not bonded, reading CHAR_CONFIG will trigger the OS pairing/bonding flow.
                        // Once bonding completes, bondStateReceiver will automatically read CHAR_CONFIG.
                        if (configChar != null) {
                            serviceScope.launch {
                                delay(600) // allow status read / cccd to finish
                                val isBonded = gatt.device.bondState == BluetoothDevice.BOND_BONDED
                                if (isBonded) {
                                    gatt.readCharacteristic(configChar)
                                } else {
                                    // Reading protected configChar deliberately triggers OS pairing request
                                    Log.i("SugarotaBleService", "Device not bonded yet. Reading config to initiate BLE pairing...")
                                    gatt.readCharacteristic(configChar)
                                }
                            }
                        }
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

    private fun handleConfigReceived(address: String, payload: String) {
        if (payload.isNotBlank()) {
            deviceConfigs[address] = payload
        }
        pendingConfigReads.remove(address)?.invoke(payload)
        // Trigger an immediate sync for this device now that we have its config
        serviceScope.launch {
            delay(400) // Allow preceding GATT config/status operations to finish before pushing glucose
            fetchAndPushForDevice(address, forcePush = true)
        }
    }

    fun disconnectDevice(address: String) {
        manuallyDisconnected.add(address)
        val gatt = connectedGatts.remove(address)
        deviceConfigs.remove(address)
        lastPushedTimestamps.remove(address)
        currentMtu.remove(address)
        updateDeviceState(address, isConnected = false)
        try {
            gatt?.disconnect()
            gatt?.close()
        } catch (e: Exception) {
            e.printStackTrace()
        }
        updateNotification("Waiting for Sugarota connection...")
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

    // Push glucose to a specific connected Sugarota device.
    // Sends the primary packet immediately, then enqueues follow-up history_chunk packets
    // in pendingWriteQueues. onCharacteristicWrite dequeues and sends the next chunk
    // only after the previous write is acknowledged — the proper BLE GATT pattern.
    fun pushGlucoseToDevice(address: String, glucose: GlucoseData) {
        val gatt = connectedGatts[address] ?: return
        val service = gatt.getService(BleUuids.SUGAROTA_SERVICE)
        val glucoseChar = service?.getCharacteristic(BleUuids.CHAR_GLUCOSE) ?: return

        val deviceMtu = currentMtu[address] ?: 517
        val maxPayloadSize = (deviceMtu - 3).coerceAtLeast(20)

        // Build primary packet (root reading + first HISTORY_PER_PACKET items)
        var primaryJson = glucose.toJson()
        var primaryBytes = primaryJson.toByteArray(Charsets.UTF_8)
        if (primaryBytes.size > maxPayloadSize) {
            Log.w("SugarotaBleService", "Primary payload (${primaryBytes.size}) exceeds MTU ($maxPayloadSize). Sending root only.")
            primaryJson = glucose.copy(history = emptyList()).toJson()
            primaryBytes = primaryJson.toByteArray(Charsets.UTF_8)
        }

        // Build and enqueue follow-up history chunks BEFORE sending the primary packet,
        // because onCharacteristicWrite may fire very quickly on some devices.
        val itemsPerPacket = GlucoseData.historyItemsPerPacket()
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

        // Send primary — subsequent chunks are triggered by onCharacteristicWrite
        writeCharacteristicSafe(gatt, glucoseChar, primaryBytes, "primary sgv=${glucose.sgv}")
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
        } catch (e: Exception) {
            Log.e("SugarotaBleService", "writeCharacteristic [$label] failed", e)
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
        val gatt = connectedGatts[address] ?: return onComplete(false)
        val service = gatt.getService(BleUuids.SUGAROTA_SERVICE)
        val configChar = service?.getCharacteristic(BleUuids.CHAR_CONFIG) ?: return onComplete(false)

        deviceConfigs[address] = configJson
        val bytes = configJson.toByteArray(Charsets.UTF_8)
        val success = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            val res = gatt.writeCharacteristic(configChar, bytes, BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT)
            res == BluetoothStatusCodes.SUCCESS
        } else {
            @Suppress("DEPRECATION")
            configChar.value = bytes
            @Suppress("DEPRECATION")
            configChar.writeType = BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT
            @Suppress("DEPRECATION")
            gatt.writeCharacteristic(configChar)
        }
        if (success) {
            serviceScope.launch {
                delay(300)
                fetchAndPushForDevice(address, forcePush = true)
            }
        }
        onComplete(success)
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
        val configJson = deviceConfigs[address]
        if (configJson.isNullOrBlank()) {
            _bridgeStatus.value = "Waiting for device config..."
            return false
        }

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
                    _bridgeStatus.value = "Dexcom credentials missing in /config.json"
                    return false
                }
                bridgeClient.fetchDexcom(user, pass, server)
            } else {
                val ns = json.optJSONObject("nightscout")
                val url = ns?.optString("url", "") ?: ""
                val secret = ns?.optString("secret", "") ?: ""
                if (url.isBlank()) {
                    _bridgeStatus.value = "Nightscout URL missing in /config.json"
                    return false
                }
                bridgeClient.fetchNightscout(url, secret)
            }

            if (reading != null) {
                _lastReading.value = reading
                val timeStr = java.text.SimpleDateFormat("HH:mm", java.util.Locale.getDefault())
                    .format(java.util.Date(reading.timestamp * 1000))
                val summary = "${reading.sgv} ${reading.direction} (${if (reading.delta >= 0) "+" else ""}${reading.delta}) at $timeStr"

                val lastTs = lastPushedTimestamps[address]
                if (!forcePush && lastTs != null && lastTs == reading.timestamp) {
                    // Reading has not changed on the server yet and not a force push; do not push duplicate entry
                    _bridgeStatus.value = "Synced $summary (current)"
                    return true
                }

                lastPushedTimestamps[address] = reading.timestamp
                pushGlucoseToDevice(address, reading)
                _bridgeStatus.value = "Synced $summary"
                updateNotification("Glucose: $summary")
                return true
            } else {
                _bridgeStatus.value = "Fetch failed · Network error"
                return false
            }
        } catch (e: Exception) {
            e.printStackTrace()
            _bridgeStatus.value = "Error parsing config.json"
            return false
        }
    }

    private fun startPeriodicBridge() {
        bridgeJob?.cancel()
        bridgeJob = serviceScope.launch {
            while (isActive) {
                if (connectedGatts.isNotEmpty()) {
                    for (address in connectedGatts.keys) {
                        fetchAndPushForDevice(address)
                    }
                } else {
                    _bridgeStatus.value = "Idle · Waiting for displays"
                }

                // Dynamic interval honoring device's poll_interval_sec in /config.json (fallback 60s)
                var intervalMs = 60_000L
                for (cfg in deviceConfigs.values) {
                    try {
                        val sec = org.json.JSONObject(cfg).optLong("poll_interval_sec", 60L)
                        if (sec in 30..600) {
                            intervalMs = sec * 1000L
                            break
                        }
                    } catch (e: Exception) {
                        // ignore JSON error
                    }
                }
                delay(intervalMs)
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
        try {
            val obj = org.json.JSONObject(json)
            val bat = obj.optInt("battery", 0)
            val chg = obj.optBoolean("charging", false)
            val ver = obj.optString("version", "Unknown")
            val current = _devices.value.toMutableMap()
            val existing = current[address] ?: SugarotaDevice(name = getDeviceDisplayName(address), address = address)
            current[address] = existing.copy(status = DeviceStatus(bat, chg, ver))
            _devices.value = current

            // When device notifies status (e.g. on connect or on BOOT button press), trigger sync
            serviceScope.launch {
                fetchAndPushForDevice(address, forcePush = true)
            }
        } catch (e: Exception) {
            e.printStackTrace()
        }
    }

    private fun buildNotification(text: String): Notification {
        return NotificationCompat.Builder(this, CHANNEL_ID)
            .setContentTitle("Sugarota Companion")
            .setContentText(text)
            .setSmallIcon(android.R.drawable.stat_notify_sync)
            .setOngoing(true)
            .build()
    }

    private fun updateNotification(text: String) {
        val manager = getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
        manager.notify(NOTIFICATION_ID, buildNotification(text))
    }

    private fun createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val channel = NotificationChannel(
                CHANNEL_ID,
                "Sugarota BLE Bridge",
                NotificationManager.IMPORTANCE_LOW
            )
            val manager = getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
            manager.createNotificationChannel(channel)
        }
    }

    companion object {
        private const val CHANNEL_ID = "sugarota_ble_channel"
        private const val NOTIFICATION_ID = 101
    }
}
