#include "net_client.h"
#include "audio.h"
#include "storage.h"

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

void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;
  
  int wifiRetryLoop = 0;
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
        useSecondaryFirst = !useSecondaryFirst;
        saveConfig();
        return;
      }
      WiFi.disconnect();
    }
    
    wifiRetryLoop++;
    spinnerDelay(1000);
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
  historyCount = 0;
  
  for (int i = 0; i < arr.size() && i < MAX_HISTORY; i++) {
    JsonObject obj = arr[i];
    
    if (currentProvider == PROVIDER_NIGHTSCOUT) {
      bgHistory[i].sgv = obj["sgv"];
      bgHistory[i].timestamp = obj["date"].as<long long>() / 1000;
      strncpy(bgHistory[i].direction, parseTrend(obj).c_str(), 15);
      bgHistory[i].direction[15] = '\0';
      if (i < arr.size() - 1) {
        bgHistory[i].delta = bgHistory[i].sgv - (int)arr[i+1]["sgv"];
      } else {
        bgHistory[i].delta = 0;
      }
    } else {
      bgHistory[i].sgv = obj["Value"];
      String dateStr = obj["ST"].as<String>();
      int start = dateStr.indexOf('(') + 1;
      int end = dateStr.indexOf(')');
      if (start > 0 && end > start) {
        bgHistory[i].timestamp = dateStr.substring(start, end).substring(0, 10).toInt();
      }
      strncpy(bgHistory[i].direction, parseTrend(obj).c_str(), 15);
      bgHistory[i].direction[15] = '\0';
      if (i < arr.size() - 1) {
        bgHistory[i].delta = bgHistory[i].sgv - (int)arr[i+1]["Value"];
      } else {
        bgHistory[i].delta = 0;
      }
    }
    historyCount++;
  }
  
  if (historyCount > 0) {
    historyDirty = true;
    time_t rawtime = (time_t)bgHistory[0].timestamp;
    struct tm * ti = localtime(&rawtime);
    DBG_PRINTF("Success: %d readings. Latest SGV: %d (%s, delta: %+d) at %02d:%02d\n", 
                  historyCount, bgHistory[0].sgv, bgHistory[0].direction, bgHistory[0].delta, ti->tm_hour, ti->tm_min);
  }
  
  if (!isConfigMode) {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    DBG_PRINTLN("Power Saving: WiFi Radio OFF");
  }
  isFetching = false;
  updateUI();
}

void fetchData() {
  isFetching = true;
  updateUI();

  if (WiFi.status() != WL_CONNECTED) {
    logBoot("Fetch: Waking WiFi Radio...");
    connectWiFi();
    if (WiFi.status() != WL_CONNECTED) {
      logBoot("Fetch skipped: WiFi connect failed");
      isFetching = false;
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
    if (dexSessionId == "" && !loginDexcom()) return;
    url += "https://";
    url += dexServer;
    url += "/ShareWebServices/Services/Publisher/ReadPublisherLatestGlucoseValues?sessionId=";
    url += dexSessionId;
    url += "&minutes=1440&maxCount=";
    url += MAX_HISTORY;
  }

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
  if (httpCode == HTTP_CODE_OK) {
    parseResponse(http.getString());
  } else {
    DBG_PRINTF("HTTP Error: %d\n", httpCode);
    if (currentProvider == PROVIDER_DEXCOM && (httpCode == 401 || httpCode == 500 || httpCode == 405)) {
      dexSessionId = "";
    }
  }
  http.end();
}
