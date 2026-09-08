package org.sugarota.companion.data

import android.content.Context
import android.content.SharedPreferences

data class BridgeConfig(
    val provider: String = "NIGHTSCOUT", // "NIGHTSCOUT" or "DEXCOM"
    val nightscoutUrl: String = "",
    val nightscoutSecret: String = "",
    val dexcomUser: String = "",
    val dexcomPass: String = "",
    val dexcomServer: String = "share2.dexcom.com", // or "shareous1.dexcom.com"
    val pollIntervalMinutes: Int = 5,
    val isBridgeEnabled: Boolean = true
)

class BridgePreferences(context: Context) {
    private val prefs: SharedPreferences = context.getSharedPreferences("sugarota_bridge_prefs", Context.MODE_PRIVATE)

    fun loadConfig(): BridgeConfig {
        return BridgeConfig(
            provider = prefs.getString("provider", "NIGHTSCOUT") ?: "NIGHTSCOUT",
            nightscoutUrl = prefs.getString("ns_url", "") ?: "",
            nightscoutSecret = prefs.getString("ns_secret", "") ?: "",
            dexcomUser = prefs.getString("dex_user", "") ?: "",
            dexcomPass = prefs.getString("dex_pass", "") ?: "",
            dexcomServer = prefs.getString("dex_server", "share2.dexcom.com") ?: "share2.dexcom.com",
            pollIntervalMinutes = prefs.getInt("poll_interval", 5),
            isBridgeEnabled = prefs.getBoolean("bridge_enabled", true)
        )
    }

    fun saveConfig(config: BridgeConfig) {
        prefs.edit()
            .putString("provider", config.provider)
            .putString("ns_url", config.nightscoutUrl)
            .putString("ns_secret", config.nightscoutSecret)
            .putString("dex_user", config.dexcomUser)
            .putString("dex_pass", config.dexcomPass)
            .putString("dex_server", config.dexcomServer)
            .putInt("poll_interval", config.pollIntervalMinutes)
            .putBoolean("bridge_enabled", config.isBridgeEnabled)
            .apply()
    }
}
