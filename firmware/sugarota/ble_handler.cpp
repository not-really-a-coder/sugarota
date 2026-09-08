#include "ble_handler.h"
#include "net_client.h"
#include <time.h>

void insertOrUpdateReading(long long tsVal, int sgvVal, const char* dirVal, int deltaVal) {
  if (tsVal <= 0 || sgvVal <= 0) return;

  for (int k = 0; k < historyCount; k++) {
    if (bgHistory[k].timestamp == tsVal) {
      bgHistory[k].sgv = sgvVal;
      strncpy(bgHistory[k].direction, dirVal, sizeof(bgHistory[k].direction) - 1);
      bgHistory[k].direction[sizeof(bgHistory[k].direction) - 1] = '\0';
      bgHistory[k].delta = deltaVal;
      return;
    }
  }

  if (historyCount < MAX_HISTORY) {
    bgHistory[historyCount].sgv = sgvVal;
    bgHistory[historyCount].timestamp = tsVal;
    strncpy(bgHistory[historyCount].direction, dirVal, sizeof(bgHistory[historyCount].direction) - 1);
    bgHistory[historyCount].direction[sizeof(bgHistory[historyCount].direction) - 1] = '\0';
    bgHistory[historyCount].delta = deltaVal;
    historyCount++;
    return;
  }

  int oldestIdx = 0;
  for (int k = 1; k < MAX_HISTORY; k++) {
    if (bgHistory[k].timestamp < bgHistory[oldestIdx].timestamp) {
      oldestIdx = k;
    }
  }

  if (tsVal > bgHistory[oldestIdx].timestamp) {
    bgHistory[oldestIdx].sgv = sgvVal;
    bgHistory[oldestIdx].timestamp = tsVal;
    strncpy(bgHistory[oldestIdx].direction, dirVal, sizeof(bgHistory[oldestIdx].direction) - 1);
    bgHistory[oldestIdx].direction[sizeof(bgHistory[oldestIdx].direction) - 1] = '\0';
    bgHistory[oldestIdx].delta = deltaVal;
  }
}

void handleBLEGlucose(const JsonDocument& doc) {
  if (doc.containsKey("time")) {
    long long phoneEpoch = doc["time"].as<long long>();
    if (phoneEpoch > 1700000000LL) {
      struct timeval tv = { .tv_sec = (time_t)phoneEpoch, .tv_usec = 0 };
      settimeofday(&tv, NULL);
      struct tm utc_tm;
      time_t t = (time_t)phoneEpoch;
      gmtime_r(&t, &utc_tm);
      rtc.setDateTime(utc_tm.tm_year + 1900, utc_tm.tm_mon + 1, utc_tm.tm_mday,
                      utc_tm.tm_hour, utc_tm.tm_min, utc_tm.tm_sec);

      if (doc.containsKey("tz_offset")) {
        gmtOffset_sec = doc["tz_offset"].as<long>();
        daylightOffset_sec = doc["dst_offset"] | 0;
        configTime(gmtOffset_sec, daylightOffset_sec, "");
      }
      DBG_PRINTF("BLE: Clock and RTC synchronized to phone time: %lld (TZ offset: %ld)\n", phoneEpoch, gmtOffset_sec);
    }
  }

  const char* pType = doc["type"] | "";
  if (strcmp(pType, "time_sync") == 0) {
    bleUIUpdatePending = true;
    return;
  }

  bool isChunk = (strcmp(pType, "history_chunk") == 0);
  bool hasRootReading = !isChunk && (doc.containsKey("sgv") || doc.containsKey("v"));
  int rootSgv = doc["v"] | (doc["sgv"] | 0);
  long long rootTs = doc["t"] | (doc["timestamp"] | (long long)time(NULL));
  const char* rootDir = doc["d"] | (doc["direction"] | "Flat");
  int rootDelta = doc["dl"] | (doc["delta"] | 0);

  if (doc["history"].is<JsonArrayConst>()) {
    JsonArrayConst arr = doc["history"].as<JsonArrayConst>();

    for (size_t i = 0; i < arr.size(); i++) {
      JsonObjectConst obj = arr[i];
      int sgvVal = 0;
      if (obj.containsKey("v")) sgvVal = obj["v"].as<int>();
      else if (obj.containsKey("sgv")) sgvVal = obj["sgv"].as<int>();

      long long tsVal = 0;
      if (obj.containsKey("t")) tsVal = obj["t"].as<long long>();
      else if (obj.containsKey("timestamp")) tsVal = obj["timestamp"].as<long long>();
      if (tsVal <= 0) continue;

      const char* dirVal = "Flat";
      if (obj.containsKey("d")) dirVal = obj["d"].as<const char*>();
      else if (obj.containsKey("direction")) dirVal = obj["direction"].as<const char*>();

      int deltaVal = 0;
      if (obj.containsKey("dl")) deltaVal = obj["dl"].as<int>();
      else if (obj.containsKey("delta")) deltaVal = obj["delta"].as<int>();

      insertOrUpdateReading(tsVal, sgvVal, dirVal, deltaVal);
    }

    if (hasRootReading) {
      insertOrUpdateReading(rootTs, rootSgv, rootDir, rootDelta);
    }

    for (int i = 0; i < historyCount - 1; i++) {
      for (int j = i + 1; j < historyCount; j++) {
        if (bgHistory[j].timestamp > bgHistory[i].timestamp) {
          BGReading temp = bgHistory[i];
          bgHistory[i] = bgHistory[j];
          bgHistory[j] = temp;
        }
      }
    }
  } else if (hasRootReading) {
    insertOrUpdateReading(rootTs, rootSgv, rootDir, rootDelta);

    for (int i = 0; i < historyCount - 1; i++) {
      for (int j = i + 1; j < historyCount; j++) {
        if (bgHistory[j].timestamp > bgHistory[i].timestamp) {
          BGReading temp = bgHistory[i];
          bgHistory[i] = bgHistory[j];
          bgHistory[j] = temp;
        }
      }
    }
  }

  offlineMode = false;
  historyDirty = true;
  isFetching = false;
  bleGlucoseReceived = true;
  DBG_PRINTF("BLE: Ingested glucose successfully. Readings: %d, Latest SGV: %d (%s, delta: %+d, ts: %lld)\n",
             historyCount, (historyCount > 0 ? bgHistory[0].sgv : 0),
             (historyCount > 0 ? bgHistory[0].direction : "--"),
             (historyCount > 0 ? bgHistory[0].delta : 0),
             (historyCount > 0 ? bgHistory[0].timestamp : 0LL));
  bleUIUpdatePending = true;
}

void handleBLEConfig() {
  DBG_PRINTLN("BLE: Config updated from smartphone, rebooting in 1s...");
  delay(1000);
  ESP.restart();
}

void handleBLEPairingDisplay(uint32_t pin, bool active) {
  isShowingPairingDialog = active;
  blePairingPin = pin;
  DBG_PRINTF("BLE Pairing Display: pin=%06u, active=%d\n", pin, active);
  blePairingUpdatePending = true;
}
