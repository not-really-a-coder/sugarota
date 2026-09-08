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

void loadHistoryFromCache() {
  if (!LittleFS.exists("/history.dat")) {
    DBG_PRINTLN("Cache: No history file");
    return;
  }
  File f = LittleFS.open("/history.dat", "r");
  if (!f) return;
  f.read((uint8_t*)&historyCount, sizeof(historyCount));
  if (historyCount > MAX_HISTORY) historyCount = MAX_HISTORY;
  f.read((uint8_t*)bgHistory, sizeof(BGReading) * historyCount);
  f.close();
  historyDirty = false;
  lastHistorySaveTime = millis();
  DBG_PRINTF("Cache: Loaded %d readings\n", historyCount);
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
  
  DBG_PRINTF("Config: Mode: %s, Poll: %lu s\n", connectionMode.c_str(), pollIntervalSec);
  DBG_PRINTLN("Config: Loaded from LittleFS");
}

void saveConfig() {
  File f = LittleFS.open("/config.json", "w");
  if (!f) return;
  
  JsonDocument doc;
  doc["debug"] = debugMode;
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
