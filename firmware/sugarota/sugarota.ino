// --- Version Control ---
#define SUGAROTA_VERSION "v0.09.18.35"

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
#include "ble.h"
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
unsigned long nextFetchIntervalMs = 60000;
long long lastKnownReadingTs = 0;

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
unsigned long fetchStartTime = 0;
bool pendingReboot = false;
unsigned long pendingRebootTime = 0;

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
  if (msg.length() > 0) {
    DBG_PRINTLN(msg);
  }
  if (!isBooting) return;

  if (msg.length() > 0) {
    bootLog += msg;
    bootLog += '\n';
  }
  
  int newlineCount = 0;
  for (int i = 0; i < bootLog.length(); i++) {
    if (bootLog[i] == '\n') newlineCount++;
  }
  
  while (newlineCount > 7) {
    int firstNewline = bootLog.indexOf('\n');
    bootLog = bootLog.substring(firstNewline + 1);
    newlineCount--;
  }

  gfx->fillScreen(BLACK);
  gfx->setTextColor(GREEN);
  gfx->setTextSize(2);
  gfx->setCursor(20, 10);
  gfx->println("--- Sugarota " SUGAROTA_VERSION " Booting ---");
  
  gfx->setTextSize(2); 
  int logY = 34;
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
      logY += 19;
      startIdx = i + 1;
    }
  }
  gfx->flush();
}

int hwVersion = 1;
String hwVersionConfig = "auto";

static void detectHardwareVersion() {
  if (hwVersionConfig == "v1") {
    hwVersion = 1;
    return;
  } else if (hwVersionConfig == "v2") {
    hwVersion = 2;
    return;
  }

  // Auto-detection between V1 and V2:
  // On V2, GPIO 8 is connected to TCA9554 INT (with external pull-up R39/R70)
  // and GPIO 42 is connected to the AP3032 backlight boost CTRL pin (pulled down by R46 100k to GND).
  // On V1, GPIO 8 is connected to the AP3032 backlight boost CTRL pin (pulled down to GND)
  // and GPIO 42 is NC / unrouted.
  // We sample GPIO 8 with no internal pull: if high due to the external TCA9554 pull-up, it is V2.
  // Furthermore, we configure weak internal pull-up on GPIO 42:
  // on V2, R46 (100k pull-down) forms a divider or pulls low, whereas on V1 it floats high.
  pinMode(PIN_BL_V1, INPUT);
  delay(2);
  int g8_raw = digitalRead(PIN_BL_V1);

  if (g8_raw == HIGH) {
    hwVersion = 2;
  } else {
    // Check with internal pull-down on GPIO 8:
    // On V2 (open-drain INT with external 10k pull-up to 3.3V), it stays HIGH.
    pinMode(PIN_BL_V1, INPUT_PULLDOWN);
    delay(2);
    int g8_pd = digitalRead(PIN_BL_V1);
    if (g8_pd == HIGH) {
      hwVersion = 2;
    } else {
      hwVersion = 1;
    }
  }
}

void powerOffDevice() {
  DBG_PRINTLN(F("Powering off device..."));
  deviceOn = false;
  
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

  // Turn off backlight power enable on V2
  updateBacklightPower(false);

  // Disable peripheral power rails on TCA9554
  Wire.beginTransmission(TCA9554_ADDR);
  Wire.write(0x01); // Output Port
  Wire.write(0x00);
  Wire.endTransmission();
  
  // Set active backlight PWM pin to inactive (HIGH for inverted AXS15231B boost) and hold state
  if (hwVersion == 2) {
    pinMode(PIN_BL_V2, OUTPUT);
    digitalWrite(PIN_BL_V2, HIGH);
    gpio_hold_en((gpio_num_t)PIN_BL_V2);
  } else {
    pinMode(PIN_BL_V1, OUTPUT);
    digitalWrite(PIN_BL_V1, HIGH);
    gpio_hold_en((gpio_num_t)PIN_BL_V1);
  }
  
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
  // Release pad holds on both potential backlight pins
  gpio_hold_dis((gpio_num_t)PIN_BL_V1);
  gpio_hold_dis((gpio_num_t)PIN_BL_V2);
  gpio_deep_sleep_hold_dis();

  Serial.setRxBufferSize(2048);
  Serial.begin(115200);
  Serial.setTxTimeoutMs(0); // CRITICAL: Prevent USB CDC writes from blocking when terminal is not open or after cold boot
  Serial.setTimeout(0);
  delay(100);

  if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0) {
    pinMode(PIN_PWR_BTN, INPUT_PULLUP);
    // Wait for user to release the power button after waking up to avoid false short/long press
    unsigned long waitStart = millis();
    while (digitalRead(PIN_PWR_BTN) == LOW && (millis() - waitStart < 2000)) {
      delay(10);
    }
  }

  DBG_PRINTLN("\n--- Sugarota " SUGAROTA_VERSION " Booting ---");

  // Power Management & RTC
  Wire.begin(I2C_SDA, I2C_SCL);
  rtc.begin(Wire, I2C_SDA, I2C_SCL);
  restoreTimeFromRTC();

  // Configure TCA9554 IO Expander for both V1 and V2:
  // Direction (Reg 0x03): 0 = output, 1 = input.
  // Outputs: EXIO1 (BL_EN), EXIO5 (LCD_RST), EXIO6 (SYS_EN), EXIO7 (NS_MODE).
  // Inputs: EXIO0 (TOUCH_INT), EXIO2 (IMU_INT1), EXIO3 (IMU_INT2), EXIO4 (RTC_INT).
  // Direction mask: ~(EXIO_PIN_BL_EN | EXIO_PIN_LCD_RST | EXIO_PIN_SYS_EN | EXIO_PIN_NS_MODE) = ~(0x02 | 0x20 | 0x40 | 0x80) = ~0xE2 = 0x1D
  Wire.beginTransmission(TCA9554_ADDR);
  Wire.write(0x03); // Configuration Register
  Wire.write(0x1D); // Set P1, P5, P6, P7 as Outputs; P0, P2, P3, P4 as Inputs
  Wire.endTransmission();

  // Output State (Reg 0x01):
  // Assert EXIO1=1 (BL_EN), EXIO5=1 (LCD_RST active high), EXIO6=1 (SYS_EN power hold), EXIO7=1 (NS_MODE audio)
  // Mask = 0x02 | 0x20 | 0x40 | 0x80 = 0xE2
  Wire.beginTransmission(TCA9554_ADDR);
  Wire.write(0x01); // Output Port Register
  Wire.write(0xE2);
  Wire.endTransmission();

  // Filesystem & Cache (Initialize early to read config options)
  if (!LittleFS.begin(true, "/littlefs", 10, "ffat")) {
    DBG_PRINTLN("FS Mount Failed!");
  }
  loadConfig();
  configTime(gmtOffset_sec, daylightOffset_sec, "");

  // Probe hardware revision (uses config override if specified, otherwise auto-detected)
  detectHardwareVersion();
  DBG_PRINTF("Hardware Revision: V%d (config: %s)\n", hwVersion, hwVersionConfig.c_str());

  // Audio Codec
  initAudioCodec();

  // Inputs: Buttons, Touch, IMU
  initInputs();

  // Display
  initDisplay();
  setBrightness(brightnessLevel);

  // Battery ADC
  initBatteryADC();
  updateBattery();
  
  char batMsg[40];
  snprintf(batMsg, sizeof(batMsg), "Battery: %.2fV (%d%%)", currentBatteryVoltage, currentBatteryPct);

  char hwMsg[40];
  snprintf(hwMsg, sizeof(hwMsg), "Hardware: V%d (%s)", hwVersion, hwVersionConfig.c_str());
  logBoot(hwMsg);

  if (loadHistoryFromCache()) {
    char cacheMsg[40];
    snprintf(cacheMsg, sizeof(cacheMsg), "Loaded %d reading%s from cache", historyCount, historyCount == 1 ? "" : "s");
    logBoot(cacheMsg);
  } else {
    logBoot("No cache available");
  }
  logBoot(batMsg);

  // BLE Peripheral
  SugarotaBLE::getInstance().setGlucoseCallback(handleBLEGlucose);
  SugarotaBLE::getInstance().setConfigCallback(handleBLEConfig);
  SugarotaBLE::getInstance().setPairingCallback(handleBLEPairingDisplay);
  SugarotaBLE::getInstance().begin("Sugarota");
  SugarotaBLE::getInstance().notifyStatus(currentBatteryPct, wasUSBPlugged, SUGAROTA_VERSION, brightnessLevel, isDarkTheme ? 1 : 0);

  bool bleConnectedEarly = false;

  if (connectionMode == "BLE_ONLY") {
    logBoot("Mode: BLE Only (Power Save)");
    logBoot("Waiting for Companion...");
    unsigned long bleWaitStart = millis();
    while (millis() - bleWaitStart < 3000) {
      SugarotaBLE::getInstance().update();
      checkBootButtons();
      if (!deviceOn) return;
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
      
      unsigned long bleCheckDuration = isFirstLaunch ? 15000 : 8000;
      if (isFirstLaunch) {
        logBoot("First Launch: Pairing Mode");
        logBoot("Open App & Tap Scan to Pair");
      }

      unsigned long bleCheckStart = millis();
      while (millis() - bleCheckStart < bleCheckDuration) {
        SugarotaBLE::getInstance().update();
        checkBootButtons();
        if (!deviceOn) return;
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
      checkBootButtons();
      if (!deviceOn) return;
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
      
    if (SugarotaBLE::getInstance().isConnected()) {
      logBoot("BLE Active. Wi-Fi sleeping...");
      offlineMode = false;
      WiFi.disconnect(true);
      WiFi.mode(WIFI_OFF);

      logBoot("Waiting for BLE Data Sync...");
      bleGlucoseReceived = false;
      unsigned long syncWaitStart = millis();
      while (millis() - syncWaitStart < 5000) {
        SugarotaBLE::getInstance().update();
        checkBootButtons();
        if (!deviceOn) return;
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
    } else if (WiFi.status() == WL_CONNECTED) {
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
  
  checkButtons();
  if (!deviceOn) {
    SugarotaBLE::getInstance().disconnect();
    powerOffDevice();
    return;
  }
  if (pendingReboot) {
    if (millis() - pendingRebootTime >= 300) {
      SugarotaBLE::getInstance().disconnect();
      delay(100);
      ESP.restart();
    }
    return;
  }

  if (isConfigMode && (millis() - configModeStartTime > 300000)) {
    isConfigMode = false;
    DBG_PRINTLN("CONFIG MODE: AUTO OFF (5m Timeout)");
    if (!isFetching) { WiFi.disconnect(true); WiFi.mode(WIFI_OFF); }
    updateUI();
  }

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
  
  updateFindDevice();
  pollIMU();

  static unsigned long lastBatCheck = 0;
  if (millis() - lastBatCheck >= 5000 || lastBatCheck == 0) {
    lastBatCheck = millis();
    updateBattery();
  }

  static unsigned long lastBleStatus = 0;
  static int lastNotifiedBattery = -1;
  static bool lastNotifiedCharging = false;
  bool batteryChanged = (currentBatteryPct != lastNotifiedBattery) || (wasUSBPlugged != lastNotifiedCharging);

  if (SugarotaBLE::getInstance().isConnected()) {
    if (batteryChanged || (millis() - lastBleStatus >= 60000)) {
      lastBleStatus = millis();
      lastNotifiedBattery = currentBatteryPct;
      lastNotifiedCharging = wasUSBPlugged;
      SugarotaBLE::getInstance().notifyStatus(currentBatteryPct, wasUSBPlugged, SUGAROTA_VERSION, brightnessLevel, isDarkTheme ? 1 : 0);
    }
  }

  if (isConfigMode) {
    server.handleClient();
  }

  // Smooth spinner animation while fetching
  static unsigned long lastSpinnerTick = 0;
  if (isFetching) {
    if (millis() - lastSpinnerTick >= 150) {
      lastSpinnerTick = millis();
      if (!isTimerMode) {
        updateUI();
      }
    }
    // Safety timeout: prevent spinner from freezing or staying active indefinitely
    if (fetchStartTime > 0 && (millis() - fetchStartTime > 15000)) {
      DBG_PRINTLN("FETCH: Timed out after 15s, clearing isFetching");
      isFetching = false;
      fetchStartTime = 0;
      if (!isConfigMode && WiFi.getMode() != WIFI_OFF) {
        WiFi.disconnect(false, false);
        WiFi.mode(WIFI_OFF);
      }
      updateUI();
    }
  }

  static unsigned long lastClockTick = 0;
  if (millis() - lastClockTick >= 1000) {
    lastClockTick = millis();
    if (!isTimerMode && !isFetching) {
      updateUI();
    }
  }

  bool isBleConnected = SugarotaBLE::getInstance().isConnected();
  // Don't wake Wi-Fi if BLE is connected and we received data within the last 10 minutes
  bool bleDataStale = (SugarotaBLE::getInstance().getLastPacketTime() == 0) || (millis() - SugarotaBLE::getInstance().getLastPacketTime() > 600000);
  bool canFetchWifi = (connectionMode != "BLE_ONLY") && (!isBleConnected || bleDataStale);
  
  if (canFetchWifi && !isConfigMode && (millis() - lastDataFetch >= nextFetchIntervalMs)) {
    fetchData();
  }

  if (historyDirty && (millis() - lastHistorySaveTime >= 1800000)) {
    saveHistoryToCache();
  }

  delay(10);
}
