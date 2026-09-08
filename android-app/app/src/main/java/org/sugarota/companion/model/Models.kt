package org.sugarota.companion.model

import org.json.JSONObject
import java.util.UUID

object BleUuids {
    val SUGAROTA_SERVICE: UUID = UUID.fromString("6E400001-B5A3-F393-E0A9-E50E24DCCA9E")
    val CHAR_GLUCOSE: UUID    = UUID.fromString("6E400002-B5A3-F393-E0A9-E50E24DCCA9E")
    val CHAR_CONFIG: UUID     = UUID.fromString("6E400003-B5A3-F393-E0A9-E50E24DCCA9E")
    val CHAR_STATUS: UUID     = UUID.fromString("6E400004-B5A3-F393-E0A9-E50E24DCCA9E")
    val CHAR_OTA: UUID        = UUID.fromString("6E400005-B5A3-F393-E0A9-E50E24DCCA9E")
    val CHAR_CCCD: UUID       = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")
}

// Number of history items per BLE write packet.
// Worst-case compact item: {"v":250,"d":"FortyFiveDown","dl":-15,"t":1725780000} = ~55 bytes
// Root fields (sgv + direction + delta + timestamp + units + time + tz_offset + dst_offset) ≈ 155 bytes
// history wrapper "history":[...] ≈ 12 bytes
// Budget per packet: MTU(517) - ATT_overhead(3) = 514 bytes
// 6 items × 55 + 155 + 12 = 497 bytes → safely under 514
private const val HISTORY_PER_PACKET = 6

data class GlucoseData(
    val sgv: Int,
    val direction: String,
    val delta: Int,
    val timestamp: Long,
    val units: String = "mg/dL",
    val history: List<GlucoseData> = emptyList()
) {
    fun toJson(): String {
        val obj = JSONObject()
        obj.put("sgv", sgv)
        obj.put("direction", direction)
        obj.put("delta", delta)
        obj.put("timestamp", timestamp)
        obj.put("units", units)
        obj.put("time", System.currentTimeMillis() / 1000) // Current wall clock for BLE time sync

        // Include phone's timezone offset in seconds so Sugarota can configure local time immediately
        val tz = java.util.TimeZone.getDefault()
        val nowMs = System.currentTimeMillis()
        val rawOffsetSec = tz.rawOffset / 1000
        val dstOffsetSec = if (tz.inDaylightTime(java.util.Date(nowMs))) (tz.dstSavings / 1000) else 0
        obj.put("tz_offset", rawOffsetSec)
        obj.put("dst_offset", dstOffsetSec)

        // Only include the first HISTORY_PER_PACKET entries in the primary packet.
        // The remaining readings are sent as follow-up history_chunk packets by
        // SugarotaBleService.pushGlucoseToDevice() to fill gaps without exceeding MTU.
        if (history.isNotEmpty()) {
            val arr = org.json.JSONArray()
            for (item in history.take(HISTORY_PER_PACKET)) {
                val hObj = JSONObject()
                hObj.put("v", item.sgv)
                hObj.put("d", item.direction)
                hObj.put("dl", item.delta)
                hObj.put("t", item.timestamp)
                arr.put(hObj)
            }
            obj.put("history", arr)
        }

        return obj.toString()
    }

    companion object {
        /** Returns the number of history items included in the primary toJson() packet. */
        fun historyItemsPerPacket(): Int = HISTORY_PER_PACKET

        /**
         * Serializes a slice of history items as a lightweight history_chunk packet.
         * Each chunk is ~35 + items*55 bytes — well under the 514-byte MTU limit.
         * The firmware's handleBLEGlucose() merges these into bgHistory[] by timestamp.
         */
        fun createHistoryChunkJson(items: List<GlucoseData>): String {
            val obj = JSONObject()
            obj.put("type", "history_chunk")
            val arr = org.json.JSONArray()
            for (item in items) {
                val hObj = JSONObject()
                hObj.put("v", item.sgv)
                hObj.put("d", item.direction)
                hObj.put("dl", item.delta)
                hObj.put("t", item.timestamp)
                arr.put(hObj)
            }
            obj.put("history", arr)
            return obj.toString()
        }

        fun createTimeSyncJson(): String {
            val obj = JSONObject()
            obj.put("type", "time_sync")
            obj.put("time", System.currentTimeMillis() / 1000)
            val tz = java.util.TimeZone.getDefault()
            val nowMs = System.currentTimeMillis()
            val rawOffsetSec = tz.rawOffset / 1000
            val dstOffsetSec = if (tz.inDaylightTime(java.util.Date(nowMs))) (tz.dstSavings / 1000) else 0
            obj.put("tz_offset", rawOffsetSec)
            obj.put("dst_offset", dstOffsetSec)
            return obj.toString()
        }
    }
}

data class DeviceStatus(
    val batteryPct: Int = 0,
    val isCharging: Boolean = false,
    val version: String = "Unknown"
)

data class SugarotaDevice(
    val name: String,
    val address: String,
    val rssi: Int = 0,
    val isConnected: Boolean = false,
    val isBonded: Boolean = false,
    val status: DeviceStatus = DeviceStatus()
)
