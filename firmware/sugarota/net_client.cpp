#include "net_client.h"
#include "audio.h"
#include "storage.h"
#include "ble.h"
#include "ble_handler.h"

SensorPCF85063 rtc;

// Forward declarations
void logBoot(const String& msg);
void updateUI();

void restoreTimeFromRTC() {
  RTC_DateTime datetime = rtc.getDateTime();
  struct tm tm_time;
  tm_time.tm_year = datetime.getYear() - 1900;
  tm_time.tm_mon = datetime.getMonth() - 1;
  tm_time.tm_mday = datetime.getDay();
  tm_time.tm_hour = datetime.getHour();
  tm_time.tm_min = datetime.getMinute();
  tm_time.tm_sec = datetime.getSecond();
  tm_time.tm_isdst = 0;
  
  int year = tm_time.tm_year + 1900;
  int month = tm_time.tm_mon + 1;
  int day = tm_time.tm_mday;
  int hour = tm_time.tm_hour;
  int minute = tm_time.tm_min;
  int second = tm_time.tm_sec;
  
  int month_days[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
  long days = (year - 1970) * 365 + (year - 1969) / 4 - (year - 1901) / 100 + (year - 1601) / 400;
  days += month_days[month - 1];
  if (month > 2 && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0)) {
    days += 1;
  }
  days += day - 1;
  time_t t = days * 86400 + hour * 3600 + minute * 60 + second;
  
  struct timeval tv = { .tv_sec = t, .tv_usec = 0 };
  settimeofday(&tv, NULL);
}

void connectWiFi(bool allowBleBailout) {
  if (WiFi.status() == WL_CONNECTED) return;
  
  int wifiRetryLoop = 0;
  WiFi.disconnect(false, false);
  delay(50);
  WiFi.mode(WIFI_STA);
  WiFi.setHostname("Sugarota");
  
  int maxLoops = isBooting ? 2 : 1;
  while (wifiRetryLoop < maxLoops) {
    String loopMsg = "WiFi Loop " + String(wifiRetryLoop + 1) + "/" + String(maxLoops);
    logBoot(loopMsg);
    
    bool hasPrimary = (primarySSID.length() > 0);
    bool hasSecondary = (secondarySSID.length() > 0);
    
    if (!hasPrimary && !hasSecondary) {
      logBoot("No WiFi SSIDs configured!");
      return;
    }
    
    String firstSSID = "";
    String firstPass = "";
    String secondSSID = "";
    String secondPass = "";
    
    if (hasPrimary && hasSecondary) {
      firstSSID = useSecondaryFirst ? secondarySSID : primarySSID;
      firstPass = useSecondaryFirst ? secondaryPass : primaryPass;
      secondSSID = useSecondaryFirst ? primarySSID : secondarySSID;
      secondPass = useSecondaryFirst ? primaryPass : secondaryPass;
    } else if (hasPrimary) {
      firstSSID = primarySSID;
      firstPass = primaryPass;
    } else {
      firstSSID = secondarySSID;
      firstPass = secondaryPass;
    }
    
    if (firstSSID.length() > 0) {
      logBoot("Trying WiFi 1: " + firstSSID);
      WiFi.begin(firstSSID.c_str(), firstPass.c_str());
      unsigned long startAttemptTime = millis();
      while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000) {
        spinnerDelay(500);
      }
      
      if (WiFi.status() == WL_CONNECTED) {
        unsigned long ipWait = millis();
        while (WiFi.localIP() == IPAddress(0, 0, 0, 0) && millis() - ipWait < 4000) {
          delay(50);
        }
        delay(200); // Allow DNS & TCP/IP stack to stabilize
        DBG_PRINTF("WiFi: Connected! IP: %s\n", WiFi.localIP().toString().c_str());
        return;
      }
      WiFi.disconnect();
      spinnerDelay(500);
    }
    
    if (secondSSID.length() > 0) {
      logBoot("Trying WiFi 2: " + secondSSID);
      WiFi.begin(secondSSID.c_str(), secondPass.c_str());
      unsigned long startAttemptTime = millis();
      while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000) {
        spinnerDelay(500);
      }
      
      if (WiFi.status() == WL_CONNECTED) {
        unsigned long ipWait = millis();
        while (WiFi.localIP() == IPAddress(0, 0, 0, 0) && millis() - ipWait < 4000) {
          delay(50);
        }
        delay(200); // Allow DNS & TCP/IP stack to stabilize
        DBG_PRINTF("WiFi: Connected! IP: %s\n", WiFi.localIP().toString().c_str());
        useSecondaryFirst = !useSecondaryFirst;
        saveConfig();
        return;
      }
      WiFi.disconnect();
    }
    
    // BLE scan step at the end of each Wi-Fi loop (only during boot / auto detection)
    if (allowBleBailout) {
      if (SugarotaBLE::getInstance().isConnected()) {
        logBoot("BLE Companion Connected!");
        return;
      }
      logBoot("Checking BLE...");
      unsigned long bleScanStart = millis();
      while (millis() - bleScanStart < 1500) {
        SugarotaBLE::getInstance().update();
        if (SugarotaBLE::getInstance().isConnected()) {
          logBoot("BLE Companion Connected!");
          return;
        }
        delay(50);
      }
    }

    wifiRetryLoop++;
    spinnerDelay(1000);
  }

  // If connection was unsuccessful, power down Wi-Fi radio to save battery and avoid stuck STA state
  if (WiFi.status() != WL_CONNECTED && !isConfigMode) {
    WiFi.disconnect(false, false);
    WiFi.mode(WIFI_OFF);
  }
}

static const char* mapTrendToString(int trend) {
  switch (trend) {
    case 1: return "DoubleUp";
    case 2: return "SingleUp";
    case 3: return "FortyFiveUp";
    case 4: return "Flat";
    case 5: return "FortyFiveDown";
    case 6: return "SingleDown";
    case 7: return "DoubleDown";
    default: return "None";
  }
}

String parseTrend(JsonObject obj) {
  if (currentProvider == PROVIDER_NIGHTSCOUT) {
    if (obj.containsKey("direction")) return obj["direction"].as<String>();
    return mapTrendToString(obj["trend"].as<int>());
  } else {
    if (obj["Trend"].is<int>()) {
      return mapTrendToString(obj["Trend"].as<int>());
    } else if (obj["Trend"].is<String>()) {
      return obj["Trend"].as<String>();
    }
  }
  return "None";
}

bool loginDexcom() {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(10000);
  
  const char* appId = "d89443d2-327c-4a6f-89e5-496bbb0317db";
  
  String authUrl;
  authUrl.reserve(128);
  authUrl += "https://";
  authUrl += dexServer;
  authUrl += "/ShareWebServices/Services/General/AuthenticatePublisherAccount";

  String authPayload;
  authPayload.reserve(256);
  authPayload += "{\"accountName\":\"";
  authPayload += dexUser;
  authPayload += "\",\"password\":\"";
  authPayload += dexPass;
  authPayload += "\",\"applicationId\":\"";
  authPayload += appId;
  authPayload += "\"}";
  
  logBoot("Dexcom: Authenticating...");
  http.begin(client, authUrl);
  http.setUserAgent(F("Dexcom Share/3.0.2.11 CFNetwork/672.0.2 Darwin/14.0.0"));
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Accept", "application/json");
  
  int httpCode = http.POST(authPayload);
  
  if (httpCode != HTTP_CODE_OK) {
    DBG_PRINTF("Auth Failed: %d\n", httpCode);
    http.end();
    return false;
  }
  
  String accountId = http.getString();
  accountId.replace("\"", "");
  http.end();
  
  String loginUrl;
  loginUrl.reserve(128);
  loginUrl += "https://";
  loginUrl += dexServer;
  loginUrl += "/ShareWebServices/Services/General/LoginPublisherAccountById";

  String loginPayload;
  loginPayload.reserve(256);
  loginPayload += "{\"accountId\":\"";
  loginPayload += accountId;
  loginPayload += "\",\"password\":\"";
  loginPayload += dexPass;
  loginPayload += "\",\"applicationId\":\"";
  loginPayload += appId;
  loginPayload += "\"}";
  
  logBoot("Dexcom: Logging in...");
  http.begin(client, loginUrl);
  http.setUserAgent(F("Dexcom Share/3.0.2.11 CFNetwork/672.0.2 Darwin/14.0.0"));
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Accept", "application/json");
  
  httpCode = http.POST(loginPayload);
  
  if (httpCode == HTTP_CODE_OK) {
    dexSessionId = http.getString();
    dexSessionId.replace("\"", "");
    logBoot("Dexcom: Login Success.");
    http.end();
    return true;
  } else {
    DBG_PRINTF("Login Failed: %d\n", httpCode);
    http.end();
    return false;
  }
}

void parseResponse(const String& payload) {
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, payload);
  
  if (error) {
    DBG_PRINTF("JSON Error: %s\n", error.c_str());
    return;
  }

  JsonArray arr = doc.as<JsonArray>();
  
  for (int i = 0; i < arr.size() && i < MAX_HISTORY; i++) {
    JsonObject obj = arr[i];
    int sgvVal = 0;
    long long tsVal = 0;
    String dirStr = "Flat";
    int deltaVal = 0;
    
    if (currentProvider == PROVIDER_NIGHTSCOUT) {
      sgvVal = obj["sgv"] | 0;
      tsVal = obj["date"].as<long long>() / 1000;
      dirStr = parseTrend(obj);
      if (i < arr.size() - 1) {
        deltaVal = sgvVal - (int)arr[i+1]["sgv"];
      } else {
        deltaVal = 0;
      }
    } else {
      sgvVal = obj["Value"] | 0;
      String dateStr = "";
      if (obj.containsKey("WT") && obj["WT"].as<String>().length() > 0) {
        dateStr = obj["WT"].as<String>();
      } else if (obj.containsKey("ST")) {
        dateStr = obj["ST"].as<String>();
      }
      int start = dateStr.indexOf('(') + 1;
      int end = dateStr.indexOf(')');
      if (start > 0 && end > start) {
        // Strip timezone suffix if present (e.g. 1725776000000-0700)
        String rawEpochStr = dateStr.substring(start, end);
        int dashIdx = rawEpochStr.indexOf('-');
        int plusIdx = rawEpochStr.indexOf('+');
        int cutIdx = -1;
        if (dashIdx > 0) cutIdx = dashIdx;
        else if (plusIdx > 0) cutIdx = plusIdx;
        if (cutIdx > 0) rawEpochStr = rawEpochStr.substring(0, cutIdx);
        
        long long rawEpoch = atoll(rawEpochStr.c_str());
        if (rawEpoch > 1000000000000LL) {
          tsVal = rawEpoch / 1000LL;
        } else {
          tsVal = rawEpoch;
        }
      }
      dirStr = parseTrend(obj);
      if (i < arr.size() - 1) {
        deltaVal = sgvVal - (int)arr[i+1]["Value"];
      } else {
        deltaVal = 0;
      }
    }
    
    if (tsVal > 0 && sgvVal > 0) {
      insertOrUpdateReading(tsVal, sgvVal, dirStr.c_str(), deltaVal);
    }
  }
  
  // Keep history sorted descending (newest first)
  for (int i = 0; i < historyCount - 1; i++) {
    for (int j = i + 1; j < historyCount; j++) {
      if (bgHistory[j].timestamp > bgHistory[i].timestamp) {
        BGReading temp = bgHistory[i];
        bgHistory[i] = bgHistory[j];
        bgHistory[j] = temp;
      }
    }
  }
  
  if (historyCount > 0) {
    historyDirty = true;
    offlineMode = false;
    time_t rawtime = (time_t)bgHistory[0].timestamp;
    struct tm * ti = localtime(&rawtime);
    DBG_PRINTF("Success: %d readings. Latest SGV: %d (%s, delta: %+d) at %02d:%02d:%02d\n", 
                  historyCount, bgHistory[0].sgv, bgHistory[0].direction, bgHistory[0].delta, ti->tm_hour, ti->tm_min, ti->tm_sec);

    if (bgHistory[0].timestamp > lastKnownReadingTs) {
      lastKnownReadingTs = bgHistory[0].timestamp;
      nextFetchIntervalMs = computeNextFetchDelayMs(bgHistory[0].timestamp, pollIntervalSec);
      DBG_PRINTF("Schedule: New data received (ts=%lld). Next fetch in %lu ms\n", 
                 lastKnownReadingTs, nextFetchIntervalMs);
    } else {
      nextFetchIntervalMs = getFetchIntervalMs();
      DBG_PRINTF("Schedule: No newer data (ts=%lld). Next fetch in %lu ms\n", 
                 bgHistory[0].timestamp, nextFetchIntervalMs);
    }
  } else {
    nextFetchIntervalMs = getFetchIntervalMs();
  }
  
  if (!isConfigMode) {
    WiFi.disconnect(true, false);
    delay(50);
    WiFi.mode(WIFI_OFF);
    DBG_PRINTLN("Power Saving: WiFi Radio OFF");
  }
  isFetching = false;
  fetchStartTime = 0;
  updateUI();
}

void fetchData() {
  isFetching = true;
  fetchStartTime = millis();
  updateUI();

  if (WiFi.status() != WL_CONNECTED) {
    logBoot("Fetch: Waking WiFi Radio...");
    connectWiFi(false); // Don't abort on BLE during explicit data fetch fallback
    if (WiFi.status() != WL_CONNECTED) {
      logBoot("Fetch skipped: WiFi connect failed");
      isFetching = false;
      fetchStartTime = 0;
      updateUI();
      return;
    }
  }
  
  lastDataFetch = millis();
  logBoot("Starting Data Fetch...");
  
  String url;
  url.reserve(256);
  if (currentProvider == PROVIDER_NIGHTSCOUT) {
    url += nsUrl;
    url += "/api/v1/entries.json?count=";
    url += MAX_HISTORY;
  } else {
    if (dexSessionId == "" && !loginDexcom()) {
      if (!isConfigMode) {
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
      }
      isFetching = false;
      fetchStartTime = 0;
      updateUI();
      return;
    }
    url += "https://";
    url += dexServer;
    url += "/ShareWebServices/Services/Publisher/ReadPublisherLatestGlucoseValues?sessionId=";
    url += dexSessionId;
    url += "&minutes=1440&maxCount=";
    url += MAX_HISTORY;
  }

  DBG_PRINTF("HTTP: Connecting to %s (Provider=%s)...\n", 
             (currentProvider == PROVIDER_NIGHTSCOUT ? nsUrl.c_str() : dexServer.c_str()),
             (currentProvider == PROVIDER_NIGHTSCOUT ? "NIGHTSCOUT" : "DEXCOM"));

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  
  http.setTimeout(10000);
  http.begin(client, url);
  http.setUserAgent(F("Dexcom Share/3.0.2.11 CFNetwork/672.0.2 Darwin/14.0.0"));
  
  if (currentProvider == PROVIDER_NIGHTSCOUT && nsSecret.length() > 0) {
    http.addHeader("api-secret", nsSecret);
  }
  http.addHeader("Accept", "application/json");
  
  int httpCode = http.GET();
  if (httpCode < 0) {
    DBG_PRINTF("HTTP GET failed (%d), retrying once...\n", httpCode);
    http.end();
    client.stop();
    delay(1000);
    client.setInsecure();
    http.begin(client, url);
    http.setUserAgent(F("Dexcom Share/3.0.2.11 CFNetwork/672.0.2 Darwin/14.0.0"));
    if (currentProvider == PROVIDER_NIGHTSCOUT && nsSecret.length() > 0) {
      http.addHeader("api-secret", nsSecret);
    }
    http.addHeader("Accept", "application/json");
    httpCode = http.GET();
  }

  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    http.end();
    client.stop();
    parseResponse(payload);
  } else {
    DBG_PRINTF("HTTP Error: %d\n", httpCode);
    http.end();
    client.stop();
    if (currentProvider == PROVIDER_DEXCOM) {
      // Invalidate session on authorization failure, server error, or connection refusal
      if (httpCode == 401 || httpCode == 500 || httpCode == 405 || httpCode < 0) {
        dexSessionId = "";
      }
    }
    nextFetchIntervalMs = getFetchIntervalMs();
    if (!isConfigMode) {
      WiFi.disconnect(true, false);
      delay(50);
      WiFi.mode(WIFI_OFF);
      DBG_PRINTLN("Power Saving: WiFi Radio OFF after HTTP error");
    }
    isFetching = false;
    fetchStartTime = 0;
    updateUI();
  }
}
