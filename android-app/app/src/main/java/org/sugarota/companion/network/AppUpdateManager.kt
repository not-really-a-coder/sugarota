package org.sugarota.companion.network

import android.content.Context
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import okhttp3.OkHttpClient
import okhttp3.Request
import org.json.JSONObject
import java.util.concurrent.TimeUnit

data class AppReleaseInfo(
    val version: String,
    val changelog: String,
    val downloadUrl: String,
    val changelogUrl: String = "https://github.com/not-really-a-coder/sugarota/blob/main/CHANGELOG.md"
)

class AppUpdateManager(private val context: Context) {

    private val fastHttpClient = OkHttpClient.Builder()
        .connectTimeout(5, TimeUnit.SECONDS)
        .readTimeout(10, TimeUnit.SECONDS)
        .build()

    private val localProbeHttpClient = OkHttpClient.Builder()
        .connectTimeout(500, TimeUnit.MILLISECONDS)
        .readTimeout(1000, TimeUnit.MILLISECONDS)
        .build()

    suspend fun checkForUpdates(currentAppVersion: String): AppReleaseInfo? = withContext(Dispatchers.IO) {
        val candidateUrls = listOf(
            "https://raw.githubusercontent.com/not-really-a-coder/sugarota/main/data/version_state.json",
            "http://10.0.2.2:8123/data/version_state.json",
            "http://127.0.0.1:8123/data/version_state.json"
        )

        for (url in candidateUrls) {
            try {
                val isLocal = url.contains("8123")
                val client = if (isLocal) localProbeHttpClient else fastHttpClient
                val req = Request.Builder().url(url).build()
                val resp = client.newCall(req).execute()
                if (resp.isSuccessful) {
                    val body = resp.body?.string() ?: continue
                    val obj = JSONObject(body)
                    val androidObj = obj.optJSONObject("android")
                    val latestVer = androidObj?.optString("version", "") ?: ""
                    if (latestVer.isNotBlank()) {
                        val changelog = fetchChangelog(url.substringBefore("/data/"), latestVer)
                        val apkUrl = "https://github.com/not-really-a-coder/sugarota/releases"
                        return@withContext AppReleaseInfo(
                            version = latestVer,
                            changelog = changelog,
                            downloadUrl = apkUrl
                        )
                    }
                }
            } catch (e: Exception) {
                // Continue to next
            }
        }
        return@withContext null
    }

    private fun fetchChangelog(baseUrl: String, version: String): String {
        val changelogUrls = linkedSetOf(
            "$baseUrl/CHANGELOG.md",
            "https://raw.githubusercontent.com/not-really-a-coder/sugarota/main/CHANGELOG.md"
        )
        for (url in changelogUrls) {
            try {
                val isLocal = url.contains("8123")
                val client = if (isLocal) localProbeHttpClient else fastHttpClient
                val req = Request.Builder().url(url).build()
                val resp = client.newCall(req).execute()
                if (resp.isSuccessful) {
                    val fullMd = resp.body?.string() ?: continue
                    val clean = fullMd.trim()
                    if (clean.isNotBlank()) {
                        val extracted = extractChangelogSection(clean, version)
                        if (extracted.isNotBlank()) return extracted
                        val firstHeaderIdx = clean.indexOf("## ")
                        return if (firstHeaderIdx != -1) clean.substring(firstHeaderIdx).trim() else clean
                    }
                }
            } catch (e: Exception) {
                // Ignore and try next
            }
        }
        return "• Stability and connectivity improvements.\n• Companion UI optimizations."
    }

    private fun extractChangelogSection(markdown: String, targetVersion: String): String {
        try {
            val lines = markdown.lines()
            val section = StringBuilder()
            var capturing = false

            for (line in lines) {
                if (line.startsWith("## [") || line.startsWith("## ")) {
                    if (capturing) {
                        break
                    }
                    if (line.contains(targetVersion, ignoreCase = true) || (!capturing && targetVersion.isBlank())) {
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
}
