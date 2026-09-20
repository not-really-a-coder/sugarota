package org.sugarota.companion.network

import android.content.Context
import android.net.ConnectivityManager
import android.net.NetworkCapabilities
import android.net.wifi.WifiManager
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import okhttp3.*
import okhttp3.MediaType.Companion.toMediaTypeOrNull
import org.json.JSONObject
import org.sugarota.companion.model.FirmwareReleaseInfo
import java.io.File
import java.io.FileOutputStream
import java.io.InputStream
import java.security.MessageDigest
import java.util.concurrent.TimeUnit

class FirmwareUpdateManager(private val context: Context) {

    private val httpClient = OkHttpClient.Builder()
        .connectTimeout(15, TimeUnit.SECONDS)
        .readTimeout(60, TimeUnit.SECONDS)
        .writeTimeout(60, TimeUnit.SECONDS)
        .build()

    /**
     * Checks for the latest available Sugarota firmware release.
     * Tries remote release manifest (or GitHub API), and falls back to local web server or packaged manifest.
     */
    suspend fun checkForUpdates(deviceVersion: String): FirmwareReleaseInfo? = withContext(Dispatchers.IO) {
        // Candidate URLs to probe for latest release info
        // 1. Local development / web installer server (running on host machine port 8123)
        // 2. Fallback to GitHub repository releases
        val candidateUrls = listOf(
            "http://10.0.2.2:8123/data/version_state.json", // Android emulator to host
            "http://127.0.0.1:8123/data/version_state.json",
            "https://raw.githubusercontent.com/not-really-a-coder/sugarota/main/data/version_state.json"
        )

        for (url in candidateUrls) {
            try {
                val req = Request.Builder().url(url).build()
                val resp = httpClient.newCall(req).execute()
                if (resp.isSuccessful) {
                    val body = resp.body?.string() ?: continue
                    val obj = JSONObject(body)
                    val fwObj = obj.optJSONObject("firmware") ?: obj
                    val latestVer = fwObj.optString("version", "")
                    if (latestVer.isNotBlank()) {
                        val isLocal = url.contains("8123")
                        val binUrl = if (isLocal) {
                            val hostBase = url.substringBefore("/data/")
                            "$hostBase/build/esp32.esp32.esp32s3/sugarota.ino.bin"
                        } else {
                            "https://raw.githubusercontent.com/not-really-a-coder/sugarota/main/build/esp32.esp32.esp32s3/sugarota.ino.bin"
                        }
                        val changelog = fetchChangelog(url.substringBefore("/data/"), latestVer)
                        return@withContext FirmwareReleaseInfo(
                            version = latestVer,
                            changelog = changelog,
                            downloadUrl = binUrl
                        )
                    }
                }
            } catch (e: Exception) {
                // Continue to next candidate
            }
        }

        // Fallback default release info if offline/unreachable
        return@withContext null
    }

    /**
     * Fetches changelog notes for the release.
     * Returns the full project changelog markdown so the user can inspect all version notes.
     */
    private fun fetchChangelog(baseUrl: String, version: String): String {
        val changelogUrls = listOf(
            "$baseUrl/CHANGELOG.md",
            "https://raw.githubusercontent.com/not-really-a-coder/sugarota/main/CHANGELOG.md"
        )
        for (url in changelogUrls) {
            try {
                val req = Request.Builder().url(url).build()
                val resp = httpClient.newCall(req).execute()
                if (resp.isSuccessful) {
                    val fullMd = resp.body?.string() ?: continue
                    val clean = fullMd.trim()
                    if (clean.isNotBlank()) {
                        // Strip leading title & preamble: start from the first "## " version entry
                        val firstHeaderIdx = clean.indexOf("## ")
                        return if (firstHeaderIdx != -1) {
                            clean.substring(firstHeaderIdx).trim()
                        } else {
                            clean
                        }
                    }
                }
            } catch (e: Exception) {
                // Ignore and try next
            }
        }
        return "• Performance optimizations and connectivity improvements.\n• Wireless OTA flashing enhancement.\n• Bug fixes and stability updates."
    }

    private fun extractChangelogSection(markdown: String, targetVersion: String): String {
        try {
            val lines = markdown.lines()
            val section = StringBuilder()
            var capturing = false

            for (line in lines) {
                if (line.startsWith("## [") || line.startsWith("## ")) {
                    if (capturing) {
                        break // End of target version notes
                    }
                    if (line.contains(targetVersion, ignoreCase = true) || !capturing && targetVersion.isBlank()) {
                        capturing = true
                        section.append(line).append("\n\n")
                        continue
                    }
                }
                if (capturing) {
                    section.append(line).append("\n")
                }
            }

            val result = section.toString().trim()
            if (result.isNotBlank()) return result
        } catch (e: Exception) {
            e.printStackTrace()
        }
        return markdown.take(1500)
    }

    /**
     * CalVer Comparison:
     * Format: v{YearOffset}.{Month:02d}.{Day:02d}.{Build} (e.g. v0.09.20.41)
     * Returns:
     *   > 0 if v1 > v2 (v1 is newer)
     *   < 0 if v1 < v2 (v1 is older)
     *   0 if equal
     */
    fun compareCalVer(v1: String, v2: String): Int {
        val p1 = parseCalVerParts(v1)
        val p2 = parseCalVerParts(v2)

        for (i in 0 until 4) {
            val cmp = p1[i].compareTo(p2[i])
            if (cmp != 0) return cmp
        }
        return 0
    }

    private fun parseCalVerParts(v: String): IntArray {
        val clean = v.trim().removePrefix("v").removePrefix("V")
        val segments = clean.split(".")
        val parts = IntArray(4) { 0 }
        for (i in 0 until minOf(segments.size, 4)) {
            parts[i] = segments[i].toIntOrNull() ?: 0
        }
        return parts
    }

    /**
     * Download the firmware binary file to cache with progress reporting.
     */
    suspend fun downloadFirmware(
        downloadUrl: String,
        targetFile: File,
        onProgress: (Int, Long, Long) -> Unit
    ): Boolean = withContext(Dispatchers.IO) {
        try {
            val req = Request.Builder().url(downloadUrl).build()
            val resp = httpClient.newCall(req).execute()
            if (!resp.isSuccessful) return@withContext false

            val body = resp.body ?: return@withContext false
            val contentLength = body.contentLength()
            var totalRead: Long = 0

            val input: InputStream = body.byteStream()
            val output = FileOutputStream(targetFile)
            val buffer = ByteArray(8192)

            var read: Int
            while (input.read(buffer).also { read = it } != -1) {
                output.write(buffer, 0, read)
                totalRead += read
                val pct = if (contentLength > 0) ((totalRead * 100) / contentLength).toInt() else 0
                withContext(Dispatchers.Main) {
                    onProgress(pct, totalRead, contentLength)
                }
            }

            output.flush()
            output.close()
            input.close()
            return@withContext true
        } catch (e: Exception) {
            e.printStackTrace()
            targetFile.delete()
            return@withContext false
        }
    }

    /**
     * Compute MD5 hash of a file for transmission in x-MD5 header.
     */
    fun calculateMD5(file: File): String {
        return try {
            val md = MessageDigest.getInstance("MD5")
            file.inputStream().use { stream ->
                val buffer = ByteArray(8192)
                var read: Int
                while (stream.read(buffer).also { read = it } != -1) {
                    md.update(buffer, 0, read)
                }
            }
            val digest = md.digest()
            digest.joinToString("") { "%02x".format(it) }
        } catch (e: Exception) {
            ""
        }
    }

    /**
     * Check if phone is connected to Wi-Fi.
     */
    fun isPhoneOnWifi(): Boolean {
        val cm = context.getSystemService(Context.CONNECTIVITY_SERVICE) as? ConnectivityManager ?: return false
        val activeNet = cm.activeNetwork ?: return false
        val caps = cm.getNetworkCapabilities(activeNet) ?: return false
        return caps.hasTransport(NetworkCapabilities.TRANSPORT_WIFI)
    }

    /**
     * Retrieves currently connected Wi-Fi SSID (stripped of quotes).
     * Note: May return "<unknown ssid>" on Android 10+ if location permissions are restricted.
     */
    fun getCurrentWifiSSID(): String {
        try {
            val wm = context.applicationContext.getSystemService(Context.WIFI_SERVICE) as? WifiManager
            val ssid = wm?.connectionInfo?.ssid?.replace("\"", "")?.trim() ?: ""
            if (ssid.isNotBlank() && ssid != "<unknown ssid>") {
                return ssid
            }
        } catch (e: Exception) {
            // Permission or restricted API
        }
        return ""
    }

    /**
     * Probes device OTA endpoint to verify local network connectivity.
     */
    suspend fun probeDeviceSubnet(deviceIp: String): Boolean = withContext(Dispatchers.IO) {
        if (deviceIp.isBlank()) return@withContext false
        try {
            val url = "http://$deviceIp/api/ota/status"
            val req = Request.Builder()
                .url(url)
                .build()
            val probeClient = httpClient.newBuilder()
                .connectTimeout(2, TimeUnit.SECONDS)
                .readTimeout(2, TimeUnit.SECONDS)
                .build()
            val resp = probeClient.newCall(req).execute()
            return@withContext resp.isSuccessful
        } catch (e: Exception) {
            return@withContext false
        }
    }

    /**
     * Uploads the firmware file to ESP32 /api/ota with progress tracking.
     */
    suspend fun uploadFirmwareToDevice(
        deviceIp: String,
        file: File,
        onProgress: (Int) -> Unit
    ): Result<String> = withContext(Dispatchers.IO) {
        try {
            val md5 = calculateMD5(file)
            val fileSize = file.length()
            val otaUrl = "http://$deviceIp/api/ota?size=$fileSize"

            val countingBody = object : RequestBody() {
                override fun contentType(): MediaType? = "application/octet-stream".toMediaTypeOrNull()
                override fun contentLength(): Long = fileSize

                override fun writeTo(sink: okio.BufferedSink) {
                    file.inputStream().use { stream ->
                        val buffer = ByteArray(4096)
                        var written: Long = 0
                        var read: Int
                        while (stream.read(buffer).also { read = it } != -1) {
                            sink.write(buffer, 0, read)
                            written += read
                            val pct = ((written * 100) / fileSize).toInt()
                            onProgress(pct)
                        }
                    }
                }
            }

            val multipart = MultipartBody.Builder()
                .setType(MultipartBody.FORM)
                .addFormDataPart("update", file.name, countingBody)
                .build()

            val reqBuilder = Request.Builder()
                .url(otaUrl)
                .post(multipart)

            if (md5.isNotBlank()) {
                reqBuilder.addHeader("x-MD5", md5)
            }

            val uploadClient = httpClient.newBuilder()
                .connectTimeout(30, TimeUnit.SECONDS)
                .writeTimeout(90, TimeUnit.SECONDS)
                .readTimeout(90, TimeUnit.SECONDS)
                .build()

            val response = uploadClient.newCall(reqBuilder.build()).execute()
            if (response.isSuccessful) {
                val respStr = response.body?.string() ?: ""
                Result.success(respStr)
            } else {
                Result.failure(Exception("Flashing failed: HTTP ${response.code} ${response.message}"))
            }
        } catch (e: Exception) {
            Result.failure(e)
        }
    }
}
