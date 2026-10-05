#include "storage.h"
#include "audio.h"
#include "ui.h"
#include "ble.h"
#include <time.h>
#include <WiFi.h>

void applyRuntimeConfig() {
  // 1. Timezone & NTP: update runtime libc time configuration immediately
  if (ntpServer.length() > 0) {
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer.c_str());
  } else {
    configTime(gmtOffset_sec, daylightOffset_sec, "");
  }

  // 2. Audio volume: update codec output gain immediately
  setVolume(volumeLevel);

  // 3. Provider credentials: clear active session so next poll uses new auth/endpoint
  dexSessionId = "";

  // 4. Wi-Fi reconnection: if Wi-Fi is currently connected, disconnect cleanly
  // so the next fetch will reconnect with updated SSIDs/passwords/preferences
  if (WiFi.status() == WL_CONNECTED) {
    WiFi.disconnect(false, false);
  }

  // 5. Notify BLE companion of updated system status if connected
  if (SugarotaBLE::getInstance().isConnected()) {
    SugarotaBLE::getInstance().notifyStatus(
        currentBatteryPct, wasUSBPlugged, SUGAROTA_VERSION,
        brightnessLevel, isDarkTheme ? 1 : 0, false,
        debugMode ? 1 : 0, false, volumeLevel);
  }

  // 6. Refresh UI immediately to reflect new units or other visual options
  updateUI();
  DBG_PRINTLN("Config: Applied in realtime (no reboot)");
}

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

  if (doc.containsKey("volume")) {
    volumeLevel = doc["volume"].as<int>();
    if (volumeLevel < 0) volumeLevel = 0;
    if (volumeLevel > 3) volumeLevel = 3;
  }

  if (doc.containsKey("night_mode")) {
    nightModeEnabled = doc["night_mode"].as<bool>();
  }

  DBG_PRINTF("Config: Mode: %s, Poll: %lu s, HW: %s (%d), Vol: %d\n", connectionMode.c_str(), pollIntervalSec, hwVersionConfig.c_str(), hwVersion, volumeLevel);
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
  doc["volume"] = volumeLevel;
  doc["night_mode"] = nightModeEnabled;
  
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

void loadBondedPhones() {
  bondedPhoneCount = 0;
  if (!LittleFS.exists("/phones.json")) {
    return;
  }
  File f = LittleFS.open("/phones.json", "r");
  if (!f) return;
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) {
    DBG_PRINTF("Phones: Failed to parse /phones.json: %s\n", err.c_str());
    return;
  }
  if (doc.is<JsonArray>()) {
    JsonArray arr = doc.as<JsonArray>();
    for (JsonObject obj : arr) {
      if (bondedPhoneCount >= MAX_BONDED_PHONES) break;
      const char* addr = obj["address"] | "";
      const char* name = obj["name"] | "Phone";
      if (strlen(addr) > 0) {
        // Deduplicate loaded list by address or identical name to avoid historical duplicates
        bool duplicate = false;
        for (int k = 0; k < bondedPhoneCount; k++) {
          if (strcasecmp(bondedPhones[k].address, addr) == 0 ||
              (strlen(name) > 0 && strcasecmp(bondedPhones[k].name, name) == 0)) {
            duplicate = true;
            break;
          }
        }
        if (!duplicate) {
          strncpy(bondedPhones[bondedPhoneCount].address, addr, sizeof(bondedPhones[bondedPhoneCount].address) - 1);
          bondedPhones[bondedPhoneCount].address[sizeof(bondedPhones[bondedPhoneCount].address) - 1] = '\0';
          strncpy(bondedPhones[bondedPhoneCount].name, name, sizeof(bondedPhones[bondedPhoneCount].name) - 1);
          bondedPhones[bondedPhoneCount].name[sizeof(bondedPhones[bondedPhoneCount].name) - 1] = '\0';
          bondedPhones[bondedPhoneCount].connected = false;
          bondedPhoneCount++;
        }
      }
    }
  }
  DBG_PRINTF("Phones: Loaded %d saved bonded phone(s)\n", bondedPhoneCount);
}

void saveBondedPhones() {
  File f = LittleFS.open("/phones.json", "w");
  if (!f) return;
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (int i = 0; i < bondedPhoneCount; i++) {
    JsonObject obj = arr.add<JsonObject>();
    obj["address"] = bondedPhones[i].address;
    obj["name"] = bondedPhones[i].name;
  }
  serializeJson(doc, f);
  f.close();
  DBG_PRINTF("Phones: Saved %d bonded phone(s)\n", bondedPhoneCount);
}

void updateOrRegisterPhone(const char* address, const char* name, bool connected) {
  if (!address || strlen(address) == 0) return;

  // 1. Check if phone already registered by exact address
  int existingIdx = -1;
  for (int i = 0; i < bondedPhoneCount; i++) {
    if (strcasecmp(bondedPhones[i].address, address) == 0) {
      existingIdx = i;
      break;
    }
  }

  // 2. If not found by address, check if matching by name (handles rotating BLE MAC / re-registration)
  if (existingIdx < 0 && name && strlen(name) > 0 && strcasecmp(name, "Phone") != 0) {
    for (int i = 0; i < bondedPhoneCount; i++) {
      if (strcasecmp(bondedPhones[i].name, name) == 0) {
        existingIdx = i;
        // Update to newest address for this named phone
        strncpy(bondedPhones[existingIdx].address, address, sizeof(bondedPhones[existingIdx].address) - 1);
        bondedPhones[existingIdx].address[sizeof(bondedPhones[existingIdx].address) - 1] = '\0';
        break;
      }
    }
  }

  if (existingIdx >= 0) {
    if (name && strlen(name) > 0) {
      strncpy(bondedPhones[existingIdx].name, name, sizeof(bondedPhones[existingIdx].name) - 1);
      bondedPhones[existingIdx].name[sizeof(bondedPhones[existingIdx].name) - 1] = '\0';
    }
    // Update address in case it was refined
    strncpy(bondedPhones[existingIdx].address, address, sizeof(bondedPhones[existingIdx].address) - 1);
    bondedPhones[existingIdx].address[sizeof(bondedPhones[existingIdx].address) - 1] = '\0';
    bondedPhones[existingIdx].connected = connected;

    // Remove any other disconnected duplicate with the same name if present
    for (int j = bondedPhoneCount - 1; j >= 0; j--) {
      if (j != existingIdx && strcasecmp(bondedPhones[j].name, bondedPhones[existingIdx].name) == 0) {
        for (int k = j; k < bondedPhoneCount - 1; k++) {
          bondedPhones[k] = bondedPhones[k + 1];
        }
        bondedPhoneCount--;
        if (existingIdx > j) existingIdx--;
      }
    }

    // If connected, move to top (index 0) so connected phones are shown on top
    if (connected && existingIdx > 0) {
      BondedPhone temp = bondedPhones[existingIdx];
      for (int i = existingIdx; i > 0; i--) {
        bondedPhones[i] = bondedPhones[i - 1];
      }
      bondedPhones[0] = temp;
    }
    saveBondedPhones();
    return;
  }

  // 3. Not found: add new phone if space or replace oldest disconnected
  if (bondedPhoneCount < MAX_BONDED_PHONES) {
    int idx = bondedPhoneCount++;
    strncpy(bondedPhones[idx].address, address, sizeof(bondedPhones[idx].address) - 1);
    bondedPhones[idx].address[sizeof(bondedPhones[idx].address) - 1] = '\0';
    const char* defaultName = (name && strlen(name) > 0) ? name : "Phone";
    strncpy(bondedPhones[idx].name, defaultName, sizeof(bondedPhones[idx].name) - 1);
    bondedPhones[idx].name[sizeof(bondedPhones[idx].name) - 1] = '\0';
    bondedPhones[idx].connected = connected;
    if (connected && idx > 0) {
      BondedPhone temp = bondedPhones[idx];
      for (int i = idx; i > 0; i--) {
        bondedPhones[i] = bondedPhones[i - 1];
      }
      bondedPhones[0] = temp;
    }
  } else {
    // Replace last disconnected phone
    int replaceIdx = MAX_BONDED_PHONES - 1;
    for (int i = MAX_BONDED_PHONES - 1; i >= 0; i--) {
      if (!bondedPhones[i].connected) {
        replaceIdx = i;
        break;
      }
    }
    strncpy(bondedPhones[replaceIdx].address, address, sizeof(bondedPhones[replaceIdx].address) - 1);
    bondedPhones[replaceIdx].address[sizeof(bondedPhones[replaceIdx].address) - 1] = '\0';
    const char* defaultName = (name && strlen(name) > 0) ? name : "Phone";
    strncpy(bondedPhones[replaceIdx].name, defaultName, sizeof(bondedPhones[replaceIdx].name) - 1);
    bondedPhones[replaceIdx].name[sizeof(bondedPhones[replaceIdx].name) - 1] = '\0';
    bondedPhones[replaceIdx].connected = connected;
    if (connected && replaceIdx > 0) {
      BondedPhone temp = bondedPhones[replaceIdx];
      for (int i = replaceIdx; i > 0; i--) {
        bondedPhones[i] = bondedPhones[i - 1];
      }
      bondedPhones[0] = temp;
    }
  }
  saveBondedPhones();
}

void setPhoneConnected(const char* address, bool connected) {
  if (!address) return;
  for (int i = 0; i < bondedPhoneCount; i++) {
    if (strcasecmp(bondedPhones[i].address, address) == 0) {
      bondedPhones[i].connected = connected;
      if (connected && i > 0) {
        BondedPhone temp = bondedPhones[i];
        for (int k = i; k > 0; k--) {
          bondedPhones[k] = bondedPhones[k - 1];
        }
        bondedPhones[0] = temp;
      }
      break;
    }
  }
}

