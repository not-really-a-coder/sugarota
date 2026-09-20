#include "storage.h"

void saveHistoryToCache() {
  if (!historyDirty && LittleFS.exists("/history.dat")) return;
  File f = LittleFS.open("/history.dat", "w");
  if (!f) return;
  f.write((uint8_t*)&historyCount, sizeof(historyCount));
  f.write((uint8_t*)bgHistory, sizeof(BGReading) * historyCount);
  f.close();
  historyDirty = false;
  lastHistorySaveTime = millis();
  DBG_PRINTF("Cache: Saved %d readings to flash\n", historyCount);
}

bool loadHistoryFromCache() {
  if (!LittleFS.exists("/history.dat")) {
    DBG_PRINTLN("Cache: No history file");
    return false;
  }
  File f = LittleFS.open("/history.dat", "r");
  if (!f) return false;
  f.read((uint8_t*)&historyCount, sizeof(historyCount));
  if (historyCount > MAX_HISTORY) historyCount = MAX_HISTORY;
  f.read((uint8_t*)bgHistory, sizeof(BGReading) * historyCount);
  f.close();
  historyDirty = false;
  lastHistorySaveTime = millis();
  DBG_PRINTF("Cache: Loaded %d readings\n", historyCount);
  return (historyCount > 0);
}

void loadConfig() {
  if (!LittleFS.exists("/config.json")) {
    DBG_PRINTLN("Config: File not found. Creating default...");
    saveConfig(); // Create default file
    return;
  }
  
  File f = LittleFS.open("/config.json", "r");
  if (!f) {
    DBG_PRINTLN("Config: Failed to open file");
    return;
  }
  
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, f);
  f.close();
  
  if (error) {
    DBG_PRINTF("Config: JSON Parse Failed: %s\n", error.c_str());
    return;
  }

  if (doc.containsKey("debug")) {
    debugMode = doc["debug"].as<bool>();
  }

  if (doc.containsKey("wifi")) {
    primarySSID = doc["wifi"]["primary_ssid"].as<String>();
    primaryPass = doc["wifi"]["primary_pass"].as<String>();
    secondarySSID = doc["wifi"]["secondary_ssid"].as<String>();
    secondaryPass = doc["wifi"]["secondary_pass"].as<String>();
    if (doc["wifi"].containsKey("use_secondary_first")) {
      useSecondaryFirst = doc["wifi"]["use_secondary_first"].as<bool>();
    }
  }
  
  DBG_PRINTF("Config: Loaded. Primary SSID: [%s]\n", primarySSID.c_str());
  
  if (doc.containsKey("nightscout")) {
    nsUrl = doc["nightscout"]["url"].as<String>();
    nsSecret = doc["nightscout"]["secret"].as<String>();
  }
  
  if (doc.containsKey("dexcom")) {
    dexUser = doc["dexcom"]["user"].as<String>();
    dexPass = doc["dexcom"]["pass"].as<String>();
    dexServer = doc["dexcom"]["server"].as<String>();
  }

  if (doc.containsKey("provider")) {
    String p = doc["provider"].as<String>();
    if (p == "DEXCOM") currentProvider = PROVIDER_DEXCOM;
    else if (p == "NIGHTSCOUT") currentProvider = PROVIDER_NIGHTSCOUT;
  }

  if (doc.containsKey("units")) {
    bgUnits = (doc["units"].as<String>() == "mmol/L") ? UNIT_MMOLL : UNIT_MGDL;
  }

  if (doc.containsKey("timezone")) {
    ntpServer = doc["timezone"]["ntp"].as<String>();
    gmtOffset_sec = doc["timezone"]["offset"].as<long>();
    daylightOffset_sec = doc["timezone"]["daylight"].as<int>();
  }

  if (doc.containsKey("connection_mode")) {
    connectionMode = doc["connection_mode"].as<String>();
    connectionMode.toUpperCase();
    if (connectionMode != "BLE_ONLY" && connectionMode != "WIFI_ONLY") {
      connectionMode = "AUTO";
    }
  }

  if (doc.containsKey("poll_interval_sec")) {
    pollIntervalSec = doc["poll_interval_sec"].as<unsigned long>();
    if (pollIntervalSec < 30 || pollIntervalSec > 600) {
      pollIntervalSec = 60;
    }
  }
  
  if (doc.containsKey("hw_version")) {
    hwVersionConfig = doc["hw_version"].as<String>();
    hwVersionConfig.toLowerCase();
    if (hwVersionConfig == "v1") {
      hwVersion = 1;
    } else if (hwVersionConfig == "v2") {
      hwVersion = 2;
    } else {
      hwVersionConfig = "auto";
    }
  }

  DBG_PRINTF("Config: Mode: %s, Poll: %lu s, HW: %s (%d)\n", connectionMode.c_str(), pollIntervalSec, hwVersionConfig.c_str(), hwVersion);
  DBG_PRINTLN("Config: Loaded from LittleFS");
}

void saveConfig() {
  File f = LittleFS.open("/config.json", "w");
  if (!f) return;
  
  JsonDocument doc;
  doc["debug"] = debugMode;
  doc["hw_version"] = hwVersionConfig;
  doc["wifi"]["primary_ssid"] = primarySSID;
  doc["wifi"]["primary_pass"] = primaryPass;
  doc["wifi"]["secondary_ssid"] = secondarySSID;
  doc["wifi"]["secondary_pass"] = secondaryPass;
  doc["wifi"]["use_secondary_first"] = useSecondaryFirst;
  
  doc["nightscout"]["url"] = nsUrl;
  doc["nightscout"]["secret"] = nsSecret;
  
  doc["dexcom"]["user"] = dexUser;
  doc["dexcom"]["pass"] = dexPass;
  doc["dexcom"]["server"] = dexServer;
  
  doc["provider"] = currentProvider == PROVIDER_DEXCOM ? "DEXCOM" : "NIGHTSCOUT";
  doc["units"] = getBGUnitsStr();
  
  doc["timezone"]["ntp"] = ntpServer;
  doc["timezone"]["offset"] = gmtOffset_sec;
  doc["timezone"]["daylight"] = daylightOffset_sec;

  doc["connection_mode"] = connectionMode;
  doc["poll_interval_sec"] = pollIntervalSec;
  
  serializeJson(doc, f);
  f.close();
}

const char* getResetReasonString(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_POWERON:    return "POWERON";
    case ESP_RST_EXT:        return "EXT_PIN";
    case ESP_RST_SW:         return "SW_RESET";
    case ESP_RST_PANIC:      return "EXCEPTION_PANIC";
    case ESP_RST_INT_WDT:    return "INT_WDT";
    case ESP_RST_TASK_WDT:   return "TASK_WDT";
    case ESP_RST_WDT:        return "OTHER_WDT";
    case ESP_RST_DEEPSLEEP:  return "DEEP_SLEEP";
    case ESP_RST_BROWNOUT:   return "BROWNOUT";
    case ESP_RST_SDIO:       return "SDIO";
    case ESP_RST_USB:        return "USB_RESET";
    case ESP_RST_JTAG:       return "JTAG";
    case ESP_RST_EFUSE:      return "EFUSE_ERR";
    case ESP_RST_PWR_GLITCH: return "PWR_GLITCH";
    case ESP_RST_CPU_LOCKUP: return "CPU_LOCKUP";
    default:                 return "UNKNOWN";
  }
}

void recordBootResetReason() {
  esp_reset_reason_t reason = esp_reset_reason();
  const char* reasonStr = getResetReasonString(reason);
  DBG_PRINTF("SYSTEM: Boot reset reason: %s (%d)\n", reasonStr, (int)reason);

  // If this is an abnormal reset (Brownout, Panic/Crash, Watchdog), record in /crash.log
  if (reason == ESP_RST_BROWNOUT || reason == ESP_RST_PANIC || 
      reason == ESP_RST_INT_WDT || reason == ESP_RST_TASK_WDT || reason == ESP_RST_WDT) {
    
    // Check file size to avoid unbounded growth (keep last ~4KB)
    if (LittleFS.exists("/crash.log")) {
      File check = LittleFS.open("/crash.log", "r");
      if (check && check.size() > 4096) {
        check.close();
        LittleFS.remove("/crash.log");
      } else if (check) {
        check.close();
      }
    }

    File f = LittleFS.open("/crash.log", "a");
    if (f) {
      time_t now = time(NULL);
      struct tm ti;
      localtime_r(&now, &ti);
      char timeBuf[32];
      if (now > 1700000000LL) {
        snprintf(timeBuf, sizeof(timeBuf), "%04d-%02d-%02d %02d:%02d:%02d",
                 ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday, ti.tm_hour, ti.tm_min, ti.tm_sec);
      } else {
        snprintf(timeBuf, sizeof(timeBuf), "uptime_boot");
      }

      char logEntry[128];
      snprintf(logEntry, sizeof(logEntry), "[%s] CRASH: %s (code %d, bat: %.2fV / %d%%)\n",
               timeBuf, reasonStr, (int)reason, currentBatteryVoltage, currentBatteryPct);
      f.print(logEntry);
      f.close();
      DBG_PRINTF("SYSTEM: Crash event logged to /crash.log: %s", logEntry);
    }
  }
}

String readCrashLog() {
  if (!LittleFS.exists("/crash.log")) {
    return "No crash logs recorded.\n";
  }
  File f = LittleFS.open("/crash.log", "r");
  if (!f) {
    return "Failed to open crash log.\n";
  }
  String content = f.readString();
  f.close();
  return content;
}

void clearCrashLog() {
  if (LittleFS.exists("/crash.log")) {
    LittleFS.remove("/crash.log");
    DBG_PRINTLN("SYSTEM: Crash log cleared.");
  }
}

