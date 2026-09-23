package org.sugarota.companion.service

import android.app.NotificationManager
import android.app.PendingIntent
import android.bluetooth.BluetoothDevice
import android.bluetooth.le.BluetoothLeScanner
import android.bluetooth.le.ScanResult
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.os.Build
import android.util.Log
import androidx.core.app.NotificationCompat
import org.sugarota.companion.MainActivity

class SugarotaBleScanReceiver : BroadcastReceiver() {

    override fun onReceive(context: Context, intent: Intent) {
        val action = intent.action
        if (action == ACTION_CONNECT_ACTION) {
            val address = intent.getStringExtra(EXTRA_DEVICE_ADDRESS)
            if (!address.isNullOrBlank()) {
                clearNotificationForDevice(context, address)
                try {
                    val connectIntent = Intent(context, SugarotaBleService::class.java).apply {
                        this.action = SugarotaBleService.ACTION_CONNECT_DEVICE
                        putExtra(SugarotaBleService.EXTRA_DEVICE_ADDRESS, address)
                    }
                    context.startService(connectIntent)
                } catch (e: Exception) {
                    Log.w(TAG, "Failed to startService on Connect action: ${e.message}")
                }
            }
            return
        }

        if (action == ACTION_SNOOZE_ACTION) {
            val address = intent.getStringExtra(EXTRA_DEVICE_ADDRESS)
            if (!address.isNullOrBlank()) {
                val snoozeUntil = System.currentTimeMillis() + SNOOZE_DURATION_MS
                snoozedUntilTime[address] = snoozeUntil
                clearNotificationForDevice(context, address)
                Log.i(TAG, "Device $address snoozed for 10 minutes (until $snoozeUntil)")
            }
            return
        }

        val results: List<ScanResult>? = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            intent.getParcelableArrayListExtra(BluetoothLeScanner.EXTRA_LIST_SCAN_RESULT, ScanResult::class.java)
        } else {
            @Suppress("DEPRECATION")
            intent.getParcelableArrayListExtra(BluetoothLeScanner.EXTRA_LIST_SCAN_RESULT)
        }

        if (results.isNullOrEmpty()) {
            val singleResult: ScanResult? = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                intent.getParcelableExtra(BluetoothLeScanner.EXTRA_LIST_SCAN_RESULT, ScanResult::class.java)
            } else {
                @Suppress("DEPRECATION")
                intent.getParcelableExtra(BluetoothLeScanner.EXTRA_LIST_SCAN_RESULT)
            }
            if (singleResult != null) {
                handleScanResult(context, singleResult)
            }
            return
        }

        for (result in results) {
            handleScanResult(context, result)
        }
    }

    private fun handleScanResult(context: Context, result: ScanResult) {
        val device: BluetoothDevice = result.device ?: return
        val rawDeviceName = try { device.name } catch (e: SecurityException) { null }
        val advertisedName = result.scanRecord?.deviceName
        val effectiveName: String = when {
            !advertisedName.isNullOrBlank() -> advertisedName
            !rawDeviceName.isNullOrBlank() -> rawDeviceName
            else -> ""
        }
        val address = device.address ?: return

        val isSugarotaName = effectiveName.startsWith("SUGAROTA", ignoreCase = true)
        // If the device has a name that does not start with SUGAROTA, ignore immediately
        if (effectiveName.isNotBlank() && !isSugarotaName) {
            return
        }

        // Match Sugarota devices by name or by Sugarota Service UUID
        val hasSugarotaUuid = result.scanRecord?.serviceUuids?.any {
            it.uuid == org.sugarota.companion.model.BleUuids.SUGAROTA_SERVICE
        } == true
        val isSugarota = isSugarotaName || hasSugarotaUuid

        if (!isSugarota) {
            // Ignore non-Sugarota devices (e.g. other BLE gadgets nearby)
            return
        }

        val displayName = if (effectiveName.isNotBlank()) effectiveName else {
            val clean = address.replace(":", "").replace("-", "")
            val suffix = if (clean.length >= 4) clean.takeLast(4).uppercase() else clean.uppercase()
            "Sugarota-$suffix"
        }

        val now = System.currentTimeMillis()
        // Track last seen time for nearby check
        lastSeenNearbyTime[address] = now

        // Check if device was manually disconnected by the user
        val isManuallyDisconnected = SugarotaBleService.isDeviceManuallyDisconnected(address)

        // Tell SugarotaBleService to auto-connect to this device (unless manually disconnected)
        if (!isManuallyDisconnected) {
            try {
                val connectIntent = Intent(context, SugarotaBleService::class.java).apply {
                    action = SugarotaBleService.ACTION_CONNECT_DEVICE
                    putExtra(SugarotaBleService.EXTRA_DEVICE_ADDRESS, address)
                }
                context.startService(connectIntent)
            } catch (e: Exception) {
                Log.w(TAG, "Failed to startService for auto-connect: ${e.message}")
            }
        }

        // Requirement 5: Do not show "Sugarota Detected Nearby" notification if the device is already connected
        if (SugarotaBleService.isDeviceConnected(address)) {
            Log.d(TAG, "Suppressing nearby notification: $address is already connected")
            clearNotificationForDevice(context, address)
            return
        }

        // Check if device is snoozed or manually disconnected
        val snoozedUntil = snoozedUntilTime[address] ?: 0L
        if (now < snoozedUntil || isManuallyDisconnected) {
            Log.d(TAG, "Ignoring advertisement for $address: snoozed or manually disconnected")
            return
        }

        val lastNotified = lastNotificationTime[address] ?: 0L
        if (now - lastNotified < NOTIFICATION_THROTTLE_MS) {
            // Prevent notification spam if the device continues advertising repeatedly
            return
        }
        lastNotificationTime[address] = now

        postNearbyNotification(context, address, displayName)
    }

    private fun postNearbyNotification(context: Context, address: String, displayName: String) {
        val notificationManager = context.getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager

        // Tapping the notification opens MainActivity directly to foreground
        val launchIntent = Intent(context, MainActivity::class.java).apply {
            flags = Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TOP or Intent.FLAG_ACTIVITY_SINGLE_TOP
            putExtra(EXTRA_DEVICE_ADDRESS, address)
            putExtra(EXTRA_DEVICE_NAME, displayName)
            putExtra(EXTRA_FROM_DISCOVERY, true)
        }

        val contentPendingIntent = PendingIntent.getActivity(
            context,
            NOTIFICATION_ID_OFFSET + address.hashCode(),
            launchIntent,
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
        )

        // Action 1: Connect
        val connectIntent = Intent(context, SugarotaBleScanReceiver::class.java).apply {
            action = ACTION_CONNECT_ACTION
            putExtra(EXTRA_DEVICE_ADDRESS, address)
        }
        val connectPendingIntent = PendingIntent.getBroadcast(
            context,
            (NOTIFICATION_ID_OFFSET + address.hashCode()) * 31 + 1,
            connectIntent,
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
        )

        // Action 2: Snooze (10 minutes)
        val snoozeIntent = Intent(context, SugarotaBleScanReceiver::class.java).apply {
            action = ACTION_SNOOZE_ACTION
            putExtra(EXTRA_DEVICE_ADDRESS, address)
        }
        val snoozePendingIntent = PendingIntent.getBroadcast(
            context,
            (NOTIFICATION_ID_OFFSET + address.hashCode()) * 31 + 2,
            snoozeIntent,
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
        )

        val largeIcon = android.graphics.BitmapFactory.decodeResource(context.resources, org.sugarota.companion.R.drawable.ic_sugarota_logo)

        val notification = NotificationCompat.Builder(context, SugarotaBleService.ALERT_CHANNEL_ID)
            .setSmallIcon(android.R.drawable.stat_notify_sync)
            .setLargeIcon(largeIcon)
            .setContentTitle("Sugarota Detected Nearby")
            .setContentText("$displayName is ready. Tap to open and sync.")
            .setPriority(NotificationCompat.PRIORITY_HIGH)
            .setCategory(NotificationCompat.CATEGORY_RECOMMENDATION)
            .setAutoCancel(true)
            .setContentIntent(contentPendingIntent)
            .addAction(android.R.drawable.ic_menu_rotate, "Connect", connectPendingIntent)
            .addAction(android.R.drawable.ic_lock_idle_alarm, "Snooze (10m)", snoozePendingIntent)
            .setDefaults(NotificationCompat.DEFAULT_ALL)
            .build()

        notificationManager.notify(NOTIFICATION_ID_OFFSET + address.hashCode(), notification)
        activeNearbyNotifications[address] = System.currentTimeMillis()
    }

    companion object {
        private const val TAG = "SugarotaScanReceiver"
        private const val NOTIFICATION_ID_OFFSET = 2000
        private const val NOTIFICATION_THROTTLE_MS = 60_000L // Don't re-notify within 60s for same device
        const val SNOOZE_DURATION_MS = 10 * 60 * 1000L // 10 minutes snooze

        const val ACTION_CONNECT_ACTION = "org.sugarota.companion.ACTION_NOTIFICATION_CONNECT"
        const val ACTION_SNOOZE_ACTION = "org.sugarota.companion.ACTION_NOTIFICATION_SNOOZE"

        const val EXTRA_DEVICE_ADDRESS = "extra_device_address"
        const val EXTRA_DEVICE_NAME = "extra_device_name"
        const val EXTRA_FROM_DISCOVERY = "extra_from_discovery"

        private val lastNotificationTime = java.util.concurrent.ConcurrentHashMap<String, Long>()
        private val snoozedUntilTime = java.util.concurrent.ConcurrentHashMap<String, Long>()
        private val lastSeenNearbyTime = java.util.concurrent.ConcurrentHashMap<String, Long>()
        private val activeNearbyNotifications = java.util.concurrent.ConcurrentHashMap<String, Long>()

        fun clearNotificationForDevice(context: Context, address: String) {
            val notificationManager = context.getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
            notificationManager.cancel(NOTIFICATION_ID_OFFSET + address.hashCode())
            activeNearbyNotifications.remove(address)
        }

        fun isDeviceSnoozed(address: String): Boolean {
            val until = snoozedUntilTime[address] ?: return false
            return System.currentTimeMillis() < until
        }

        /**
         * Clears "Sugarota Detected Nearby" notification if the device hasn't been seen advertising
         * within staleThresholdMs (default 25 seconds) or is already connected.
         */
        fun dismissStaleNearbyNotifications(context: Context, staleThresholdMs: Long = 25_000L) {
            val now = System.currentTimeMillis()
            val entries = activeNearbyNotifications.entries.toList()
            for (entry in entries) {
                val addr = entry.key
                val lastSeen = lastSeenNearbyTime[addr] ?: 0L
                val isConnected = SugarotaBleService.isDeviceConnected(addr)
                if (isConnected || (now - lastSeen) > staleThresholdMs) {
                    Log.i(TAG, "Auto-dismissing nearby notification for $addr (isConnected=$isConnected, lastSeenDelta=${now - lastSeen}ms)")
                    clearNotificationForDevice(context, addr)
                }
            }
        }
    }
}
