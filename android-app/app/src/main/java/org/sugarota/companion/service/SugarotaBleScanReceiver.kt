package org.sugarota.companion.service

import android.bluetooth.BluetoothDevice
import android.bluetooth.le.BluetoothLeScanner
import android.bluetooth.le.ScanResult
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.os.Build
import android.util.Log

class SugarotaBleScanReceiver : BroadcastReceiver() {

    override fun onReceive(context: Context, intent: Intent) {
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

        val isBonded = try {
            device.bondState == BluetoothDevice.BOND_BONDED
        } catch (e: SecurityException) {
            false
        }

        // Check if device was manually disconnected by the user
        val isManuallyDisconnected = SugarotaBleService.isDeviceManuallyDisconnected(address)

        // Only auto-connect to BONDED devices. Unbonded devices should only connect when explicitly tapped by user.
        if (isBonded && !isManuallyDisconnected) {
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
    }

    companion object {
        private const val TAG = "SugarotaScanReceiver"

        const val EXTRA_DEVICE_ADDRESS = "extra_device_address"
        const val EXTRA_DEVICE_NAME = "extra_device_name"
        const val EXTRA_FROM_DISCOVERY = "extra_from_discovery"

        fun clearNotificationForDevice(context: Context, address: String) {
            // No-op kept for backwards compatibility
        }

        fun dismissStaleNearbyNotifications(context: Context, staleThresholdMs: Long = 25_000L) {
            // No-op kept for backwards compatibility
        }
    }
}
