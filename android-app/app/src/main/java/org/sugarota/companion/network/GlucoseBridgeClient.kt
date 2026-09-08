package org.sugarota.companion.network

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import okhttp3.MediaType.Companion.toMediaType
import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.RequestBody.Companion.toRequestBody
import org.json.JSONArray
import org.json.JSONObject
import org.sugarota.companion.model.GlucoseData
import java.util.concurrent.TimeUnit

class GlucoseBridgeClient {

    private val httpClient = OkHttpClient.Builder()
        .connectTimeout(15, TimeUnit.SECONDS)
        .readTimeout(15, TimeUnit.SECONDS)
        .build()

    // Fetch from Nightscout /api/v1/entries.json
    suspend fun fetchNightscout(url: String, secret: String): GlucoseData? = withContext(Dispatchers.IO) {
        try {
            val cleanUrl = url.trimEnd('/') + "/api/v1/entries.json?count=48"
            val requestBuilder = Request.Builder().url(cleanUrl)
            if (secret.isNotBlank()) {
                requestBuilder.addHeader("api-secret", secret)
            }
            val response = httpClient.newCall(requestBuilder.build()).execute()
            if (!response.isSuccessful) return@withContext null

            val body = response.body?.string() ?: return@withContext null
            val array = JSONArray(body)
            if (array.length() == 0) return@withContext null

            val historyList = ArrayList<GlucoseData>()
            for (i in 0 until array.length()) {
                val item = array.getJSONObject(i)
                val itemSgv = item.getInt("sgv")
                val itemDir = item.optString("direction", "Flat")
                val itemTs = item.optLong("date", System.currentTimeMillis()) / 1000
                var itemDelta = 0
                if (i < array.length() - 1) {
                    val nextItem = array.getJSONObject(i + 1)
                    itemDelta = itemSgv - nextItem.getInt("sgv")
                }
                historyList.add(GlucoseData(sgv = itemSgv, direction = itemDir, delta = itemDelta, timestamp = itemTs))
            }

            // Ensure newest reading is at index 0 and history is sorted descending by timestamp
            historyList.sortByDescending { it.timestamp }

            val latest = historyList[0]
            latest.copy(history = historyList)
        } catch (e: Exception) {
            e.printStackTrace()
            null
        }
    }

    // Fetch from Dexcom Share API
    suspend fun fetchDexcom(user: String, pass: String, server: String): GlucoseData? = withContext(Dispatchers.IO) {
        try {
            val appId = "d89443d2-327c-4a6f-89e5-496bbb0317db"
            val jsonMedia = "application/json; charset=utf-8".toMediaType()

            // 1. Authenticate
            val authUrl = "https://$server/ShareWebServices/Services/General/AuthenticatePublisherAccount"
            val authObj = JSONObject().apply {
                put("accountName", user)
                put("password", pass)
                put("applicationId", appId)
            }
            val authReq = Request.Builder()
                .url(authUrl)
                .post(authObj.toString().toRequestBody(jsonMedia))
                .header("User-Agent", "Dexcom Share/3.0.2.11 CFNetwork/672.0.2 Darwin/14.0.0")
                .header("Accept", "application/json")
                .build()

            val authResp = httpClient.newCall(authReq).execute()
            if (!authResp.isSuccessful) return@withContext null
            val accountId = authResp.body?.string()?.replace("\"", "") ?: return@withContext null

            // 2. Login to get Session ID
            val loginUrl = "https://$server/ShareWebServices/Services/General/LoginPublisherAccountById"
            val loginObj = JSONObject().apply {
                put("accountId", accountId)
                put("password", pass)
                put("applicationId", appId)
            }
            val loginReq = Request.Builder()
                .url(loginUrl)
                .post(loginObj.toString().toRequestBody(jsonMedia))
                .header("User-Agent", "Dexcom Share/3.0.2.11 CFNetwork/672.0.2 Darwin/14.0.0")
                .header("Accept", "application/json")
                .build()

            val loginResp = httpClient.newCall(loginReq).execute()
            if (!loginResp.isSuccessful) return@withContext null
            val sessionId = loginResp.body?.string()?.replace("\"", "") ?: return@withContext null

            // 3. Fetch Latest Glucose (fetch up to 48 readings to match device MAX_HISTORY)
            val fetchUrl = "https://$server/ShareWebServices/Services/Publisher/ReadPublisherLatestGlucoseValues?sessionId=$sessionId&minutes=1440&maxCount=48"
            val fetchReq = Request.Builder()
                .url(fetchUrl)
                .post("".toRequestBody(jsonMedia))
                .header("User-Agent", "Dexcom Share/3.0.2.11 CFNetwork/672.0.2 Darwin/14.0.0")
                .header("Accept", "application/json")
                .build()

            val fetchResp = httpClient.newCall(fetchReq).execute()
            if (!fetchResp.isSuccessful) return@withContext null
            val readings = JSONArray(fetchResp.body?.string() ?: "")
            if (readings.length() == 0) return@withContext null

            val historyList = ArrayList<GlucoseData>()
            for (i in 0 until readings.length()) {
                val item = readings.getJSONObject(i)
                val itemSgv = item.getInt("Value")
                val itemTrend = parseDexcomTrend(item)
                // Prefer WT (World Time / UTC epoch), fallback to ST
                val dateStr = if (item.has("WT") && item.getString("WT").isNotBlank()) item.getString("WT") else item.optString("ST", "")
                var itemTs = System.currentTimeMillis() / 1000
                // Match the timestamp digits from formats like /Date(1725776000000)/ or /Date(1725776000000-0700)/
                val match = Regex("""/Date\((\d+)[+-]?\d*\)/""").find(dateStr) ?: Regex("""\d+""").find(dateStr)
                if (match != null) {
                    val rawStr = if (match.groupValues.size > 1) match.groupValues[1] else match.value
                    val rawNum = rawStr.toLongOrNull()
                    if (rawNum != null) {
                        itemTs = if (rawNum > 1000000000000L) rawNum / 1000L else rawNum
                    }
                }
                var itemDelta = 0
                if (i < readings.length() - 1) {
                    itemDelta = itemSgv - readings.getJSONObject(i + 1).getInt("Value")
                }
                historyList.add(GlucoseData(sgv = itemSgv, direction = itemTrend, delta = itemDelta, timestamp = itemTs))
            }

            // Ensure newest reading is at index 0 and history is sorted descending by timestamp
            historyList.sortByDescending { it.timestamp }

            val latest = historyList[0]
            latest.copy(history = historyList)
        } catch (e: Exception) {
            e.printStackTrace()
            null
        }
    }

    private fun parseDexcomTrend(item: JSONObject): String {
        val trendObj = item.opt("Trend")
        val trendInt = when (trendObj) {
            is Number -> trendObj.toInt()
            is String -> trendObj.toIntOrNull()
            else -> null
        }
        if (trendInt != null) {
            return when (trendInt) {
                1 -> "DoubleUp"
                2 -> "SingleUp"
                3 -> "FortyFiveUp"
                4 -> "Flat"
                5 -> "FortyFiveDown"
                6 -> "SingleDown"
                7 -> "DoubleDown"
                else -> "Flat"
            }
        }
        val trendStr = item.optString("Trend", "Flat")
        return if (trendStr.isNotBlank()) trendStr else "Flat"
    }
}
