package org.sugarota.companion.data

import android.app.NotificationManager
import android.content.Context
import android.content.SharedPreferences
import androidx.core.app.NotificationCompat

enum class NotificationLockScreenVisibility(val value: Int, val title: String, val description: String) {
    PUBLIC(NotificationCompat.VISIBILITY_PUBLIC, "Show all content", "Display full glucose data and charts on the lock screen"),
    PRIVATE(NotificationCompat.VISIBILITY_PRIVATE, "Hide sensitive content", "Show notification but hide content on secured lock screens"),
    SECRET(NotificationCompat.VISIBILITY_SECRET, "Don't show on lock screen", "Do not show this notification on the lock screen at all")
}

enum class NotificationImportanceLevel(val importance: Int, val priority: Int, val title: String, val description: String) {
    HIGH(NotificationManager.IMPORTANCE_HIGH, NotificationCompat.PRIORITY_HIGH, "High", "Shows at top, pops on screen, maximizes lock screen expansion"),
    DEFAULT(NotificationManager.IMPORTANCE_DEFAULT, NotificationCompat.PRIORITY_DEFAULT, "Default", "Shows at normal rank, silent, can expand on lock screen"),
    LOW(NotificationManager.IMPORTANCE_LOW, NotificationCompat.PRIORITY_LOW, "Low (Minimized)", "Silent and minimized, never auto-expands on lock screen")
}

data class AppNotificationSettings(
    val visibility: NotificationLockScreenVisibility = NotificationLockScreenVisibility.PUBLIC,
    val importance: NotificationImportanceLevel = NotificationImportanceLevel.HIGH
)

class AppSettingsPreferences(context: Context) {
    private val prefs: SharedPreferences = context.getSharedPreferences("sugarota_app_settings", Context.MODE_PRIVATE)

    fun loadSettings(): AppNotificationSettings {
        val visName = prefs.getString("notification_visibility", NotificationLockScreenVisibility.PUBLIC.name)
            ?: NotificationLockScreenVisibility.PUBLIC.name
        val impName = prefs.getString("notification_importance", NotificationImportanceLevel.HIGH.name)
            ?: NotificationImportanceLevel.HIGH.name

        val visibility = try {
            NotificationLockScreenVisibility.valueOf(visName)
        } catch (_: Exception) {
            NotificationLockScreenVisibility.PUBLIC
        }

        val importance = try {
            NotificationImportanceLevel.valueOf(impName)
        } catch (_: Exception) {
            NotificationImportanceLevel.HIGH
        }

        return AppNotificationSettings(
            visibility = visibility,
            importance = importance
        )
    }

    fun saveSettings(settings: AppNotificationSettings) {
        prefs.edit()
            .putString("notification_visibility", settings.visibility.name)
            .putString("notification_importance", settings.importance.name)
            .apply()
    }

    fun getOrCreatePhoneId(): String {
        var id = prefs.getString("companion_phone_id", null)
        if (id.isNullOrBlank()) {
            id = java.util.UUID.randomUUID().toString().substring(0, 16)
            prefs.edit().putString("companion_phone_id", id).apply()
        }
        return id
    }

    fun getFindPhoneSoundUri(): String? {
        return prefs.getString("find_phone_sound_uri", null)
    }

    fun setFindPhoneSoundUri(uriString: String?) {
        prefs.edit().putString("find_phone_sound_uri", uriString).apply()
    }

    fun getFindPhoneSoundTitle(context: Context): String {
        val uriStr = getFindPhoneSoundUri()
        val uri = if (!uriStr.isNullOrBlank()) {
            android.net.Uri.parse(uriStr)
        } else {
            android.media.RingtoneManager.getDefaultUri(android.media.RingtoneManager.TYPE_RINGTONE)
                ?: android.media.RingtoneManager.getDefaultUri(android.media.RingtoneManager.TYPE_ALARM)
        }
        return try {
            val ringtone = android.media.RingtoneManager.getRingtone(context, uri)
            ringtone?.getTitle(context) ?: "Default Ringtone"
        } catch (_: Exception) {
            "Default Ringtone"
        }
    }
}
