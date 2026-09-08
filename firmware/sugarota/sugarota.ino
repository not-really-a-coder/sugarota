#include "config.h"
#include "storage.h"
#include "battery.h"
#include "audio.h"
#include "display.h"
#include "net_client.h"
#include "web_portal.h"
#include "ui.h"
#include "input.h"
#include "ble_handler.h"
#include "sugarota_ble.h"
#include <time.h>
#include <Wire.h>

// --- Global Variable Definitions ---
bool debugMode = true;

String primarySSID     = "";
String primaryPass     = "";
String secondarySSID   = "";
String secondaryPass   = "";
bool useSecondaryFirst = false;

String nsUrl           = "";
String nsSecret        = "";

String dexUser         = "";
String dexPass         = "";
String dexServer       = "shareous1.dexcom.com";
String dexSessionId    = "";

Provider currentProvider = PROVIDER_DEXCOM;
BGUnits bgUnits = UNIT_MGDL;

String connectionMode  = "AUTO";
unsigned long pollIntervalSec = 60;

String ntpServer       = "pool.ntp.org";
long gmtOffset_sec     = 0;
int daylightOffset_sec = 0;

bool deviceOn = true;
bool screenManuallyOff = false;
bool offlineMode = false;
bool isConfigMode = false;
unsigned long configModeStartTime = 0;
bool isBooting = true;
String bootLog = "";
bool isFetching = false;

bool isDarkTheme = true;
int brightnessLevel = 76;
unsigned long lastUiUpdate = 0;

BGReading bgHistory[MAX_HISTORY];
int historyCount = 0;
bool historyDirty = false;
unsigned long lastHistorySaveTime = 0;
unsigned long lastDataFetch = 0;

int currentBatteryPct = -1;
float currentBatteryVoltage = 0.0;
bool wasUSBPlugged = false;

bool isShowingPairingDialog = false;
uint32_t blePairingPin = 0;
volatile bool bleUIUpdatePending = false;
volatile bool blePairingUpdatePending = false;
volatile bool bleGlucoseReceived = false;

bool isShowingUnitDialog = false;

bool isTimerMode = false;
unsigned long timerStartTime = 0;
unsigned long timerElapsedMs = 0;
bool isTimerStopped = false;
int lastBeepedMinute = 0;

bool isTouching = false;
int touchX = 0;
int touchY = 0;
unsigned long lastHarveyBallTapTime = 0;
bool showHarveyBallInfo = false;
unsigned long lastScrubberTouchTime = 0;
int lastScrubberX = -1;

ButtonState pwrBtn = {PIN_PWR_BTN, false, 0, false};
ButtonState bootBtn = {PIN_BOOT_BTN, false, 0, false};

// --- Forward Declarations ---
void logBoot(const String& msg);
void checkSerialConsole();
void powerOffDevice();

void logBoot(const String& msg) {
  DBG_PRINTLN(msg);
  if (!isBooting) return;

  bootLog += msg;
  bootLog += '\n';
  
  int newlineCount = 0;
  for (int i = 0; i < bootLog.length(); i++) {
    if (bootLog[i] == '\n') newlineCount++;
  }
  
  while (newlineCount > 5) {
    int firstNewline = bootLog.indexOf('\n');
    bootLog = bootLog.substring(firstNewline + 1);
    newlineCount--;
  }

  gfx->fillScreen(BLACK);
  gfx->setTextColor(GREEN);
  gfx->setTextSize(2);
  gfx->setCursor(20, 20);
  gfx->println("--- Sugarota " SUGAROTA_VERSION " Booting ---");
  
  gfx->setTextSize(2); 
  int logY = 50;
  int startIdx = 0;
  for (int i = 0; i < bootLog.length(); i++) {
    if (bootLog[i] == '\n') {
      gfx->setCursor(20, logY);
      String line = bootLog.substring(startIdx, i);
      if (line.startsWith("Battery:") && (currentBatteryPct <= 5)) {
        gfx->setTextColor(RED);
      } else {
        gfx->setTextColor(GREEN);
      }
      gfx->print(line);
      logY += 20;
      startIdx = i + 1;
    }
  }
  gfx->flush();
}

void powerOffDevice() {
  DBG_PRINTLN(F("--- Powering Off ---"));
  
  if (historyDirty) {
    saveHistoryToCache();
  }

  gfx->fillScreen(BLACK);
  gfx->setTextColor(GREEN);
  gfx->setTextSize(3);
  gfx->setCursor(110, 70); 
  gfx->print("--- Powering Off ---");
  gfx->flush();

  delay(2000);

  while (digitalRead(PIN_PWR_BTN) == LOW) {
    delay(10);
  }
  delay(100);

  gfx->fillScreen(BLACK);
  gfx->flush();
  delay(50);

  Wire.beginTransmission(TCA9554_ADDR);
  Wire.write(0x01);
  Wire.write(0x00);
  Wire.endTransmission();
  
  pinMode(PIN_BL, OUTPUT);
  digitalWrite(PIN_BL, HIGH);
  gpio_hold_en((gpio_num_t)PIN_BL);
  
  esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_PWR_BTN, 0); 
  DBG_PRINTLN(F("Entering Deep Sleep..."));
  delay(100);
  esp_deep_sleep_start();
}

void checkSerialConsole() {
  if (Serial.available()) {
    String command = Serial.readStringUntil('\n');
    command.trim();
    
    if (command == "GET_CONFIG") {
      File f = LittleFS.open("/config.json", "r");
      if (f) {
        while (f.available()) {
          Serial.write(f.read());
        }
        f.close();
        Serial.println("\n[EOF]");
      } else {
        Serial.println("{}");
        Serial.println("[EOF]");
      }
    } 
    else if (command.startsWith("SET_CONFIG ")) {
      String jsonPayload = command.substring(11);
      
      JsonDocument doc;
      DeserializationError error = deserializeJson(doc, jsonPayload);
      
      if (!error) {
        File f = LittleFS.open("/config.json", "w");
        if (f) {
          f.print(jsonPayload);
          f.close();
          Serial.println("CONF_OK");
          delay(500);
          ESP.restart();
        } else {
          Serial.println("CONF_ERR");
        }
      } else {
        Serial.print("CONF_ERR: Invalid JSON: ");
        Serial.println(error.c_str());
      }
    }
    else if (command == "REBOOT") {
      Serial.println("SYSTEM: Rebooting device now...");
      delay(500);
      ESP.restart();
    }
  }
}

void setup() {
  gpio_hold_dis((gpio_num_t)PIN_BL);
  gpio_deep_sleep_hold_dis();

  Serial.setRxBufferSize(2048);
  Serial.begin(115200);
  Serial.setTimeout(0);
  delay(100);

  if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0) {
    pinMode(PIN_PWR_BTN, INPUT_PULLUP);
    unsigned long wakeStart = millis();
    bool released = false;
    while (millis() - wakeStart < 2000) {
      if (digitalRead(PIN_PWR_BTN) == HIGH) {
        released = true;
        break;
      }
      delay(10);
    }
    
    if (released) {
      esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_PWR_BTN, 0); 
      esp_deep_sleep_start();
    }
  }

  DBG_PRINTLN("\n--- Sugarota " SUGAROTA_VERSION " Booting ---");

  // Power Management & RTC
  Wire.begin(I2C_SDA, I2C_SCL);
  rtc.begin(Wire, I2C_SDA, I2C_SCL);
  restoreTimeFromRTC();
  Wire.beginTransmission(TCA9554_ADDR);
  Wire.write(0x03); 
  Wire.write(0x3F); 
  Wire.endTransmission();
  Wire.beginTransmission(TCA9554_ADDR);
  Wire.write(0x01); 
  Wire.write(0xC0);
  Wire.endTransmission();

  // Audio Codec
  initAudioCodec();

  // Inputs: Buttons, Touch, IMU
  initInputs();

  // Display
  initDisplay();
  setBrightness(brightnessLevel);

  // Battery ADC
  initBatteryADC();
  updateBattery(digitalRead(PIN_PWR_BTN) == LOW);
  
  char batMsg[40];
  snprintf(batMsg, sizeof(batMsg), "Battery: %.2fV (%d%%)", currentBatteryVoltage, currentBatteryPct);

  // Filesystem & Cache
  logBoot("Initializing FS...");
  if (!LittleFS.begin(true, "/littlefs", 10, "ffat")) {
    logBoot("FS Mount Failed!");
  }
  loadConfig();
  configTime(gmtOffset_sec, daylightOffset_sec, "");

  logBoot("Loading Cache...");
  loadHistoryFromCache();
  logBoot(batMsg);

  // BLE Peripheral
  SugarotaBLE::getInstance().setGlucoseCallback(handleBLEGlucose);
  SugarotaBLE::getInstance().setConfigCallback(handleBLEConfig);
  SugarotaBLE::getInstance().setPairingCallback(handleBLEPairingDisplay);
  SugarotaBLE::getInstance().begin("Sugarota");
  SugarotaBLE::getInstance().notifyStatus(currentBatteryPct, wasUSBPlugged, SUGAROTA_VERSION);

  bool bleConnectedEarly = false;

  if (connectionMode == "BLE_ONLY") {
    logBoot("Mode: BLE Only (Power Save)");
    logBoot("Waiting for Companion...");
    unsigned long bleWaitStart = millis();
    while (millis() - bleWaitStart < 3000) {
      SugarotaBLE::getInstance().update();
      if (SugarotaBLE::getInstance().isConnected()) {
        bleConnectedEarly = true;
        logBoot("BLE Companion Connected!");
        break;
      }
      delay(50);
    }
    if (!bleConnectedEarly) {
      logBoot("No Companion yet (Offline)");
      offlineMode = true;
    }
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
  } else {
    if (connectionMode == "AUTO") {
      logBoot("Mode: AUTO (Checking BLE)...");
      bool hasWifiConfigured = (primarySSID.length() > 0 || secondarySSID.length() > 0);
      bool isFirstLaunch = (!hasWifiConfigured && NimBLEDevice::getNumBonds() == 0);
      
      unsigned long bleCheckDuration = isFirstLaunch ? 15000 : 5000;
      if (isFirstLaunch) {
        logBoot("First Launch: Pairing Mode");
        logBoot("Open App & Tap Scan to Pair");
      }

      unsigned long bleCheckStart = millis();
      while (millis() - bleCheckStart < bleCheckDuration) {
        SugarotaBLE::getInstance().update();
        if (SugarotaBLE::getInstance().isConnected()) {
          bleConnectedEarly = true;
          logBoot("BLE Companion Connected!");
          break;
        }
        delay(50);
      }
    }
  }

  if (bleConnectedEarly) {
    logBoot("BLE Active. Wi-Fi sleeping...");
    offlineMode = false;
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);

    logBoot("Waiting for BLE Data Sync...");
    bleGlucoseReceived = false;
    unsigned long syncWaitStart = millis();
    while (millis() - syncWaitStart < 5000) {
      SugarotaBLE::getInstance().update();
      if (bleGlucoseReceived) {
        logBoot("BLE Glucose & Time Synced!");
        delay(800);
        break;
      }
      delay(50);
    }
    if (!bleGlucoseReceived) {
      logBoot("Sync pending. Loading Dashboard...");
      delay(500);
    }
  } else if (connectionMode != "BLE_ONLY") {
    if (primarySSID.length() > 0 || secondarySSID.length() > 0) {
      logBoot("BLE idle. Trying Wi-Fi...");
      connectWiFi();
    } else {
      logBoot("No Wi-Fi SSIDs configured.");
    }
      
    if (WiFi.status() == WL_CONNECTED) {
      offlineMode = false;
      logBoot("WiFi Connected!");
      logBoot("Syncing NTP Time...");
      
      struct timeval tv_reset = { .tv_sec = 0, .tv_usec = 0 };
      settimeofday(&tv_reset, NULL);
      
      configTime(gmtOffset_sec, daylightOffset_sec, ntpServer.c_str());
      
      struct tm timeinfo;
      if (getLocalTime(&timeinfo, 5000)) {
        time_t rawtime;
        time(&rawtime);
        struct tm utc_timeinfo;
        gmtime_r(&rawtime, &utc_timeinfo);
        rtc.setDateTime(utc_timeinfo.tm_year + 1900, utc_timeinfo.tm_mon + 1, utc_timeinfo.tm_mday, 
                        utc_timeinfo.tm_hour, utc_timeinfo.tm_min, utc_timeinfo.tm_sec);
        logBoot("NTP Synced & RTC Updated!");
      } else {
        logBoot("NTP Sync Timeout");
        restoreTimeFromRTC();
      }
      
      logBoot("Fetching Initial Data...");
      fetchData();
      
      if (historyCount > 0) {
        logBoot("Success! Loading Dashboard...");
        delay(1000);
      } else {
        logBoot("Warning: Using Cache...");
      }
    } else {
      logBoot("WiFi Failed. Entering Offline Mode...");
      offlineMode = true;
      WiFi.disconnect(true);
      WiFi.mode(WIFI_OFF);
      delay(1500);
    }
  }

  if (digitalRead(PIN_PWR_BTN) == LOW) {
    pwrBtn.pressed = true;
    pwrBtn.handled = true;
    pwrBtn.pressTime = millis();
  }
  if (digitalRead(PIN_BOOT_BTN) == LOW) {
    bootBtn.pressed = true;
    bootBtn.handled = true;
    bootBtn.pressTime = millis();
  }
  
  setupWebPortal();
  if (MDNS.begin("sugarota")) {
    MDNS.addService("http", "tcp", 80);
  }

  isBooting = false;
  updateUI();
}

void loop() {
  SugarotaBLE::getInstance().update();
  checkSerialConsole();

  if (blePairingUpdatePending) {
    blePairingUpdatePending = false;
    updateUI();
  }
  if (bleUIUpdatePending) {
    bleUIUpdatePending = false;
    updateUI();
  }
  
  if (!deviceOn) {
    powerOffDevice();
    return;
  }

  if (isConfigMode && (millis() - configModeStartTime > 300000)) {
    isConfigMode = false;
    DBG_PRINTLN("CONFIG MODE: AUTO OFF (5m Timeout)");
    if (!isFetching) { WiFi.disconnect(true); WiFi.mode(WIFI_OFF); }
    updateUI();
  }

  checkButtons();
  if (brightnessLevel > 0) {
    checkTouch();
  } else {
    isTouching = false;
  }

  if (showHarveyBallInfo && (millis() - lastHarveyBallTapTime > 3000)) {
    showHarveyBallInfo = false;
    updateUI(); 
  }

  if (lastScrubberX != -1 && (millis() - lastScrubberTouchTime > 3000) && !isTouching) {
    lastScrubberX = -1;
    updateUI(); 
  }
  
  pollIMU();

  static unsigned long lastBatCheck = 0;
  if (millis() - lastBatCheck >= 500) {
    lastBatCheck = millis();
    updateBattery(digitalRead(PIN_PWR_BTN) == LOW);
  }

  static unsigned long lastBleStatus = 0;
  if (millis() - lastBleStatus >= 10000) {
    lastBleStatus = millis();
    if (SugarotaBLE::getInstance().isConnected()) {
      SugarotaBLE::getInstance().notifyStatus(currentBatteryPct, wasUSBPlugged, SUGAROTA_VERSION);
    }
  }

  if (isConfigMode) {
    server.handleClient();
  }

  static unsigned long lastClockTick = 0;
  if (millis() - lastClockTick >= 1000) {
    lastClockTick = millis();
    if (!isTimerMode) {
      updateUI();
    }
  }

  bool isBleConnected = SugarotaBLE::getInstance().isConnected();
  bool canFetchWifi = (connectionMode != "BLE_ONLY") && (!isBleConnected || (millis() - lastDataFetch > 600000));
  
  if (canFetchWifi && !isConfigMode && !offlineMode && (millis() - lastDataFetch >= getFetchIntervalMs())) {
    fetchData();
  }

  if (historyDirty && (millis() - lastHistorySaveTime >= 1800000)) {
    saveHistoryToCache();
  }

  delay(10);
}
