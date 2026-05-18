// --- Version Control ---
#define SUGAROTA_VERSION "v0.05.18.1"

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <time.h>
#include <Wire.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Arduino_GFX_Library.h>
#include <SensorQMI8658.hpp>
#include <LittleFS.h>
#include <esp_partition.h>
#include "src/codec_board/codec_board.h"
#include "src/codec_board/codec_init.h"

esp_codec_dev_handle_t playback = NULL;

// --- Colors ---
#define BLACK   0x0000
#define WHITE   0xFFFF
#define RED     0xF800
#define GREEN   0x07E0
#define CYAN    0x07FF
#define YELLOW  0xFFE0
#define BLUE    0x001F
#define GRAY    0x8410
#define ORANGE  0xFD20

// --- Configuration (Dynamic - Loaded from LittleFS) ---
String primarySSID     = "";
String primaryPass     = "";
String secondarySSID   = "";
String secondaryPass   = "";

String nsUrl           = "";
String nsSecret        = "";

String dexUser         = "";
String dexPass         = "";
String dexServer       = "shareous1.dexcom.com";

bool debugMode = true;

// Debug Macros
#define DBG_PRINT(...) if(debugMode) Serial.print(__VA_ARGS__)
#define DBG_PRINTLN(...) if(debugMode) Serial.println(__VA_ARGS__)
#define DBG_PRINTF(...) if(debugMode) Serial.printf(__VA_ARGS__)

String dexSessionId    = "";

enum Provider { PROVIDER_NIGHTSCOUT, PROVIDER_DEXCOM };
Provider currentProvider = PROVIDER_DEXCOM;

String ntpServer       = "pool.ntp.org";
long gmtOffset_sec     = 0;
int daylightOffset_sec = 0;

// Hardware Pins
#define PIN_BL         8
#define PIN_PWR_BTN    16 // Power button
#define PIN_BOOT_BTN   0  // Boot button
#define PIN_BAT_ADC    4  // Battery ADC
#define PIN_BUZZER     15 // Buzzer output pin

// I2C Pins for TCA9554 (Power control)
#define I2C_SDA        47
#define I2C_SCL        48
#define TCA9554_ADDR   0x20 

// LCD Pins (Manufacturer Standard)
#define LCD_CS         9
#define LCD_PCLK       10
#define LCD_D0         11
#define LCD_D1         12
#define LCD_D2         13
#define LCD_D3         14
#define LCD_RST        21

// --- State Variables ---
bool deviceOn = true;
int wifiRetryLoop = 0;

// UI State
bool isDarkTheme = true;
int brightnessLevel = 128; // 0-255
unsigned long lastUiUpdate = 0;

// GFX Objects
Arduino_DataBus *bus = new Arduino_ESP32QSPI(LCD_CS, LCD_PCLK, LCD_D0, LCD_D1, LCD_D2, LCD_D3);
Arduino_GFX *physical_gfx = new Arduino_AXS15231B(bus, LCD_RST, 0, false, 172, 640);
Arduino_GFX *gfx = new Arduino_Canvas(172, 640, physical_gfx, 0, 0, 1);

// Button Tracking
struct ButtonState {
  int pin;
  bool pressed;
  unsigned long pressTime;
  bool handled;
};
ButtonState pwrBtn = {PIN_PWR_BTN, false, 0, false};
ButtonState bootBtn = {PIN_BOOT_BTN, false, 0, false};

// Battery Tracking
float voltageHistory[60];
int voltageIndex = 0;
bool historyFilled = false;
unsigned long lastBatRead = 0;
int currentBatteryPct = -1;
unsigned long lastBatteryPctUpdate = 0;
float currentBatteryVoltage = 0.0;
String bgUnits = "mg/dL";
bool isShowingUnitDialog = false;
unsigned long lastTouchStartTime = 0;
bool isLongTapping = false;

// Timer Mode State
bool isTimerMode = false;
unsigned long timerStartTime = 0;
unsigned long timerElapsedMs = 0;
bool isTimerStopped = false;
int lastBeepedMinute = 0;

// Glucose Data Tracking
struct BGReading {
  int sgv;
  long long timestamp;
  char direction[16];
  int delta;
};

String parseTrend(JsonObject obj);
String formatBG(int mgdl);
String formatDelta(int delta);

#define MAX_HISTORY 48
BGReading bgHistory[MAX_HISTORY];
int historyCount = 0;
unsigned long lastDataFetch = 0;
const unsigned long FETCH_INTERVAL = 60000; // 1 minute

// Function Declarations
void powerOffDevice();
void connectWiFi();
void spinnerDelay(unsigned long ms);
void playBeeps(int longBeeps, int shortBeeps);
void codecBeep(int durationMs);
void playWav(const char *path);
void checkButtons();
void checkSerialConsole();
void fetchData();
void parseResponse(String payload);
bool loginDexcom();

// UI Functions
void initUI();
void updateUI();
void drawStatusBar();
void drawGlucoseContainer();
void drawHistoryChart();
void drawTrendArrow(int x, int y, String direction, uint16_t color);
void drawHarveyBall(int x, int y, int radius, long long timestamp);
uint16_t getBGColor(int sgv);
void checkTouch();
void saveHistoryToCache();
void loadHistoryFromCache();
void loadConfig();
void saveConfig();
void toggleTheme();
// I2C Pins for Touch
#define TOUCH_SDA      17
#define TOUCH_SCL      18
#define TOUCH_ADDR     0x3B 

// State Variables
bool isBooting = true;
String bootLog = "";
bool isTouching = false;
int touchX = 0;
int touchY = 0;
unsigned long lastHarveyBallTapTime = 0;
bool showHarveyBallInfo = false;
unsigned long lastScrubberTouchTime = 0;
int lastScrubberX = -1;
bool isFetching = false;
int touchConfidence = 0;
int lastRawX = -1, lastRawY = -1;

void setBrightness(int level);
bool readTouch(int &x, int &y);

SensorQMI8658 qmi;
bool imuReady = false;

void setup() {
  Serial.setRxBufferSize(2048);
  Serial.begin(115200);
  delay(100); // Small delay to let serial init

  // --- Wake Verification (Long Press Boot) ---
  if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0) {
    // We woke up from the power button. Check if it's a long press.
    pinMode(PIN_PWR_BTN, INPUT_PULLUP);
    unsigned long wakeStart = millis();
    bool released = false;
    while (millis() - wakeStart < 2000) { // Require 2 seconds hold
      if (digitalRead(PIN_PWR_BTN) == HIGH) {
        released = true;
        break;
      }
      delay(10);
    }
    
    if (released) {
      // Short press. Go back to sleep silently.
      esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_PWR_BTN, 0); 
      esp_deep_sleep_start();
    }
  }

  DBG_PRINTLN("\n--- Sugarota " SUGAROTA_VERSION " Booting ---");

  // 1. Initialize Power Management (TCA9554)
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.beginTransmission(TCA9554_ADDR);
  Wire.write(0x03); 
  Wire.write(0x3F); 
  Wire.endTransmission();
  Wire.beginTransmission(TCA9554_ADDR);
  Wire.write(0x01); 
  Wire.write(0xC0); // Power on peripherals
  Wire.endTransmission();

  // 1.2 Initialize Audio Codec (ES8311)
  set_codec_board_type("S3_LCD_3_49");
  codec_init_cfg_t codec_cfg;
  codec_cfg.in_mode = CODEC_I2S_MODE_TDM;
  codec_cfg.out_mode = CODEC_I2S_MODE_TDM;
  codec_cfg.in_use_tdm = false;
  codec_cfg.reuse_dev = false;
  init_codec(&codec_cfg);
  playback = get_playback_handle();
  if (playback) {
    esp_codec_dev_set_out_vol(playback, 75.0);
    esp_codec_dev_sample_info_t fs;
    memset(&fs, 0, sizeof(fs));
    fs.sample_rate = 24000;
    fs.channel = 2;
    fs.bits_per_sample = 16;
    esp_codec_dev_open(playback, &fs);
  }

  // 1.5 Initialize IMU
  if (qmi.begin(Wire, QMI8658_L_SLAVE_ADDRESS, I2C_SDA, I2C_SCL)) {
      qmi.configAccelerometer(SensorQMI8658::ACC_RANGE_4G, SensorQMI8658::ACC_ODR_1000Hz, SensorQMI8658::LPF_MODE_0);
      qmi.enableAccelerometer();
      imuReady = true;
  }

  // 2. Initialize Touch I2C Bus (Standard mode 100kHz for low-voltage stability)
  Wire1.begin(TOUCH_SDA, TOUCH_SCL);
  Wire1.setClock(100000);

  // 2. Initialize UI
  initUI();
  setBrightness(brightnessLevel);

  // 3. Initialize Buttons
  pinMode(PIN_PWR_BTN, INPUT_PULLUP);
  pinMode(PIN_BOOT_BTN, INPUT_PULLUP);

  // Init battery history
  for(int i=0; i<60; i++) voltageHistory[i] = 0;
  updateBattery();

  // 4. Initial Hardware Checks
  logBoot("Initializing FS...");
  loadConfig();

  logBoot("Loading Cache...");
  loadHistoryFromCache();

  logBoot("Connecting WiFi...");
  connectWiFi();
  
  if (WiFi.status() == WL_CONNECTED) {
    logBoot("WiFi Connected!");
    logBoot("Syncing NTP Time...");
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer.c_str());
    
    logBoot("Fetching Initial Data...");
    fetchData();
    
    // Check if we got data successfully
    if (historyCount > 0) {
      logBoot("Success! Loading Dashboard...");
      delay(1000);
    } else {
      logBoot("Warning: Using Cache...");
    }
  } else {
    logBoot("WiFi Failed. Using Cache...");
    delay(2000);
  }

  isBooting = false;
  updateUI();
}

void loop() {
  if (isBooting) return; // Wait for setup to finish boot sequence
  
  checkSerialConsole();
  
  if (!deviceOn) {
    powerOffDevice();
    return;
  }

  checkButtons();
  checkTouch();

  // Handle Harvey Ball Info Timeout
  if (showHarveyBallInfo && (millis() - lastHarveyBallTapTime > 3000)) {
    showHarveyBallInfo = false;
    updateUI(); 
  }

  // Handle Scrubber Timeout
  if (lastScrubberX != -1 && (millis() - lastScrubberTouchTime > 3000) && !isTouching) {
    lastScrubberX = -1; // Reset memory
    updateUI(); 
  }
  
  // Check Face Down, Rotated Horizontal Timer & Shake (Dynamic interval to save power vs ensure smooth counting)
  static unsigned long lastImuPoll = 0;
  int imuPollInterval = (isTimerMode && !isTimerStopped) ? 50 : 100;
  if (imuReady && !isBooting && deviceOn && (millis() - lastImuPoll > imuPollInterval)) {
    lastImuPoll = millis();
    float x, y, z;
    if (qmi.getAccelerometer(x, y, z)) {
      // 1. Face Down Logic
      bool isFaceDown = (z < -0.8); 
      static bool wasFaceDown = false;
      static int lastBrightness = 150;
      
      if (isFaceDown && !wasFaceDown) {
        wasFaceDown = true;
        DBG_PRINTLN("FACE DOWN: Sleep");
        if (brightnessLevel > 0) lastBrightness = brightnessLevel;
        setBrightness(0);
      } else if (!isFaceDown && wasFaceDown) {
        wasFaceDown = false;
        DBG_PRINTLN("PICKED UP: Wake");
        if (brightnessLevel == 0) {
          setBrightness(lastBrightness);
        }
      }

      // Calculate dynamic acceleration magnitude to detect movement/shaking
      float magnitude = sqrt(x*x + y*y + z*z);
      bool isMoving = (abs(magnitude - 1.0) > 0.25);

      // 1.5. Rotated Landscape (Timer Mode) Logic
      // Regular Landscape has buttons on top (y < -0.4).
      // Rotated Landscape has buttons on bottom (y > 0.4).
      bool isRotatedLandscape = (y > 0.4 && !isFaceDown);
      
      // Lock orientation transition when vigorously moving/shaking or actively fetching data to prevent ghost resets
      if (!isMoving && !isFetching) {
        if (isRotatedLandscape) {
          if (!isTimerMode && brightnessLevel > 0) {
            isTimerMode = true;
            isTimerStopped = false;
            lastBeepedMinute = 0;
            gfx->setRotation(3); // Rotate screen 180° (Upright for upside-down device)
            timerStartTime = millis();
            timerElapsedMs = 0;
            DBG_PRINTLN("TIMER START: Device rotated 180 degrees");
            updateUI();
          }
        } else {
          if (isTimerMode) {
            isTimerMode = false;
            isTimerStopped = false;
            lastBeepedMinute = 0;
            gfx->setRotation(1); // Restore regular landscape rotation
            timerStartTime = 0;
            timerElapsedMs = 0;
            DBG_PRINTLN("TIMER STOP: Device rotated back upright");
            updateUI();
          }
        }
      }
      
      // Update timer elapsed time and draw it smoothly
      if (isTimerMode) {
        if (!isTimerStopped) {
          timerElapsedMs = millis() - timerStartTime;
          
          // Check minute beep triggers
          int currentMinute = timerElapsedMs / 60000;
          if (currentMinute > lastBeepedMinute && currentMinute <= 10) {
            lastBeepedMinute = currentMinute;
            if (currentMinute >= 1 && currentMinute <= 4) {
              playBeeps(0, currentMinute);
            } else if (currentMinute >= 5 && currentMinute <= 9) {
              playBeeps(1, currentMinute - 5);
            } else if (currentMinute == 10) {
              playBeeps(2, 0);
              isTimerStopped = true;
              timerElapsedMs = 600000; // Lock to exactly 10 minutes
            }
          }
          updateUI();
        } else {
          // Timer has stopped. Only update UI at standard 500ms intervals to support blinking and save battery life.
          static unsigned long lastStoppedBlinkTime = 0;
          if (millis() - lastStoppedBlinkTime >= 500) {
            lastStoppedBlinkTime = millis();
            updateUI();
          }
        }
      }

      // 2. Shake Detection Logic
      static unsigned long shakeSequenceStart = 0;
      static unsigned long lastHighAccTime = 0;

      if (abs(magnitude - 1.0) > 0.5) { // Threshold for vigorous shaking
        if (shakeSequenceStart == 0) shakeSequenceStart = millis();
        lastHighAccTime = millis();
        
        if (millis() - shakeSequenceStart > 800) { // 800ms of overall shaking
           DBG_PRINTLN("SHAKE DETECTED! Forcing Refresh...");
           fetchData();
           shakeSequenceStart = 0; // Reset
           lastHighAccTime = 0;
           spinnerDelay(1000); // Debounce pause using spinnerDelay to keep timer counting seamlessly
        }
      } else {
        // If 250ms pass without high acceleration, reset the shake sequence
        if (millis() - lastHighAccTime > 250) {
          shakeSequenceStart = 0;
        }
      }
    }
  }
  
  // 3. UI Update Logic (Only redraw once per minute to prevent touch lag)
  static int lastMinute = -1;
  struct tm timeinfo;
  if (getLocalTime(&timeinfo, 10)) {
    if (timeinfo.tm_min != lastMinute) {
      lastMinute = timeinfo.tm_min;
      updateUI();
    }
  }

  // Dynamic Spinner Animation while fetching
  if (isFetching) {
    static unsigned long lastSpinnerFrameTime = 0;
    if (millis() - lastSpinnerFrameTime > 150) {
      lastSpinnerFrameTime = millis();
      updateUI();
    }
  }

  // 4. Battery Monitoring (Every 10 seconds is plenty)
  if (millis() - lastBatRead >= 10000) {
    lastBatRead = millis();
    updateBattery();
  }

  // Periodic data fetch every minute

  if (millis() - lastDataFetch >= FETCH_INTERVAL) {
    fetchData();
  }

  // Dynamic CPU Delay for Power Saving vs Touch Fluidity
  if (isTouching) {
    delay(10); // 100Hz for ultra-smooth UI interactions
  } else {
    delay(50); // 20Hz during idle to save CPU cycles
  }
}

void spinnerDelay(unsigned long ms) {
  unsigned long start = millis();
  while (millis() - start < ms) {
    checkSerialConsole(); // Yield to USB serial CLI even during boot delays and connection sequences!
    
    if (isFetching) {
      if (isTimerMode) {
        if (!isTimerStopped) {
          timerElapsedMs = millis() - timerStartTime;
        }
      }
      static unsigned long lastSpinnerFrameTime = 0;
      int interval = (isTimerMode && !isTimerStopped) ? 50 : 150;
      if (millis() - lastSpinnerFrameTime > interval) {
        lastSpinnerFrameTime = millis();
        updateUI();
      }
    }
    delay(10);
  }
}

void codecBeep(int durationMs) {
  if (!playback) return;
  
  // 50ms chunk size
  // 24000Hz * 0.05s = 1200 frames. 1200 * 4 bytes = 4800 bytes.
  const int chunkFrames = 1200;
  int16_t buf[chunkFrames * 2]; // 2400 elements, 4800 bytes on stack
  
  // Fill the chunk buffer with a 2000Hz square wave
  // 24000 / 2000 = 12 frames per cycle (6 high, 6 low)
  // Amplitude decreased by 25% (20000 -> 15000)
  for (int i = 0; i < chunkFrames; i++) {
    int16_t val = ((i / 6) % 2 == 0) ? 15000 : -15000;
    buf[i * 2] = val;     // Left
    buf[i * 2 + 1] = val; // Right
  }
  
  int elapsed = 0;
  while (elapsed < durationMs) {
    int playMs = min(50, durationMs - elapsed);
    int playFrames = 24000 * playMs / 1000;
    esp_codec_dev_write(playback, buf, playFrames * 4);
    elapsed += playMs;
    // Cooperatively yield while playing chunks
    spinnerDelay(playMs);
  }
}

void playWav(const char *path) {
  if (!playback) return;
  
  File f = LittleFS.open(path, "r");
  if (!f) {
    DBG_PRINTLN("Failed to open WAV file!");
    return;
  }
  
  // Read and skip the 44-byte WAV header
  uint8_t header[44];
  if (f.read(header, 44) != 44) {
    f.close();
    return;
  }
  
  // Read raw PCM data in 4KB chunks and write to I2S codec
  const int bufSize = 4096;
  uint8_t buf[bufSize];
  while (f.available()) {
    int bytesRead = f.read(buf, bufSize);
    if (bytesRead <= 0) break;
    
    esp_codec_dev_write(playback, buf, bytesRead);
    
    // Cooperatively yield while playing chunks to keep UI fluid
    spinnerDelay(5);
  }
  
  f.close();
}

void playBeeps(int longBeeps, int shortBeeps) {
  // Try to play custom WAV files if they exist on LittleFS, otherwise fall back to programmatic beeps.
  bool hasCustomLong = LittleFS.exists("/beep_long.wav");
  bool hasCustomShort = LittleFS.exists("/beep_short.wav");

  for (int i = 0; i < longBeeps; i++) {
    if (hasCustomLong) {
      playWav("/beep_long.wav");
    } else {
      codecBeep(450);
    }
    spinnerDelay(150);
  }
  for (int i = 0; i < shortBeeps; i++) {
    if (hasCustomShort) {
      playWav("/beep_short.wav");
    } else {
      codecBeep(112);
    }
    spinnerDelay(150);
  }
}

void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;
  
  wifiRetryLoop = 0;
  WiFi.mode(WIFI_STA);
  WiFi.setHostname("Sugarota");
  
  while (wifiRetryLoop < 5) {
    String loopMsg = "WiFi Loop " + String(wifiRetryLoop + 1) + "/5";
    logBoot(loopMsg);
    
    // Try Primary
    logBoot("Primary: " + primarySSID);
    WiFi.begin(primarySSID.c_str(), primaryPass.c_str());
    unsigned long startAttemptTime = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000) {
      spinnerDelay(500);
    }
    
    if (WiFi.status() == WL_CONNECTED) return;
    WiFi.disconnect();
    spinnerDelay(500);
    
    // Try Secondary
    logBoot("Secondary: " + secondarySSID);
    WiFi.begin(secondarySSID.c_str(), secondaryPass.c_str());
    startAttemptTime = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000) {
      spinnerDelay(500);
    }
    
    if (WiFi.status() == WL_CONNECTED) return;
    WiFi.disconnect();
    
    wifiRetryLoop++;
    spinnerDelay(1000);
  }
}

void checkButton(ButtonState &btn, const char* name) {
  bool isPressed = (digitalRead(btn.pin) == LOW);
  
  if (isPressed && !btn.pressed) {
    btn.pressed = true;
    btn.pressTime = millis();
    btn.handled = false;
  } else if (!isPressed && btn.pressed) {
    btn.pressed = false;
    unsigned long duration = millis() - btn.pressTime;
    
    if (!btn.handled) {
      if (duration > 2000) {
        Serial.printf("%s Button: LONG Press detected\n", name);
        if (btn.pin == PIN_PWR_BTN) {
          deviceOn = false; // Trigger power off
        } 
      } else if (duration > 50) {
        Serial.printf("%s Button: SHORT Press detected\n", name);
        if (btn.pin == PIN_BOOT_BTN) {
          toggleTheme();
        } else {
          // Cycle brightness: 0 (0%) -> 76 (30%) -> 153 (60%) -> 204 (80%) -> 255 (100%)
          if (brightnessLevel == 0) brightnessLevel = 76;
          else if (brightnessLevel < 76) brightnessLevel = 76;
          else if (brightnessLevel < 153) brightnessLevel = 153;
          else if (brightnessLevel < 204) brightnessLevel = 204;
          else if (brightnessLevel < 255) brightnessLevel = 255;
          else brightnessLevel = 0;
          
          setBrightness(brightnessLevel);
          Serial.printf("Brightness: %d\n", brightnessLevel);
        }
      }
    }
  } else if (isPressed && btn.pressed && !btn.handled) {
    // Detect long press while holding
    if (millis() - btn.pressTime > 2000) {
      Serial.printf("%s Button: LONG Press hold triggered\n", name);
      btn.handled = true; // prevent short press trigger on release
      if (btn.pin == PIN_PWR_BTN) {
        deviceOn = false; // Trigger power off
      } else if (btn.pin == PIN_BOOT_BTN) {
        DBG_PRINTLN("ACTION: Force Data Refresh (to be implemented)");
      }
    }
  }
}

void checkButtons() {
  checkButton(pwrBtn, "PWR");
  checkButton(bootBtn, "BOOT");
}

void updateBattery() {
  pinMode(16, INPUT); 
  bool isUSBPlugged = (digitalRead(16) == LOW); // Waveshare ESP32-S3 VBUS detection

  // Use analogReadMilliVolts for accurate factory-calibrated ADC measurement.
  // Multiply by 3 for the voltage divider.
  float currentV = analogReadMilliVolts(PIN_BAT_ADC) * 3.0 / 1000.0; 
  
  voltageHistory[voltageIndex] = currentV;
  voltageIndex++;
  if (voltageIndex >= 60) {
    voltageIndex = 0;
    historyFilled = true;
  }
  
  // Calculate average
  float sum = 0;
  int count = historyFilled ? 60 : voltageIndex;
  if (count == 0) return;
  
  for(int i=0; i<count; i++) {
    sum += voltageHistory[i];
  }
  float avgV = sum / count;
  currentBatteryVoltage = avgV;

  int targetPct = getBatteryPercentage(avgV);
  
  if (currentBatteryPct == -1) {
    currentBatteryPct = targetPct; // Initialize on first run
  } else {
    // Only allow percentage to change once per minute (refresh rate requirement)
    if (millis() - lastBatteryPctUpdate >= 60000) {
      if (isUSBPlugged) {
        // While charging, only allow slow increase (prevents jumping UP instantly)
        if (targetPct > currentBatteryPct) currentBatteryPct++;
      } else {
        // While discharging, only allow slow decrease (prevents jumping DOWN or increasing unexpectedly)
        if (targetPct < currentBatteryPct) currentBatteryPct--;
      }
      lastBatteryPctUpdate = millis();
      updateUI(); // Force GUI refresh when percentage changes
    }
  }
  
  DBG_PRINTF("Battery: %.2fV (Avg: %.2fV) Target: %d%% Disp: %d%%\n", currentV, avgV, targetPct, currentBatteryPct);
  if (isUSBPlugged) {
      DBG_PRINTLN("-> STATUS: USB Charging Detected (GPIO16 LOW)");
  }
  
  // Requirement: Auto shutdown at 2.95V
  if (historyFilled && avgV < 2.95) {
    DBG_PRINTLN(F("CRITICAL BATTERY: Shutting down..."));
    powerOffDevice();
  }
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
  if (currentProvider == PROVIDER_NIGHTSCOUT) {
    url = String(nsUrl) + "/api/v1/entries.json?count=" + String(MAX_HISTORY);
  } else {
    if (dexSessionId == "" && !loginDexcom()) return;
    url = "https://" + String(dexServer) + "/ShareWebServices/Services/Publisher/ReadPublisherLatestGlucoseValues?sessionId=" + dexSessionId + "&minutes=1440&maxCount=" + String(MAX_HISTORY);
  }

  WiFiClientSecure client;
  client.setInsecure(); // Required for Dexcom/Nightscout SSL without Root CA
  HTTPClient http;
  
  http.setTimeout(10000); // Give SSL extra time
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
      dexSessionId = ""; // Force re-login next time
    }
  }
  http.end();
}

void parseResponse(String payload) {
  DynamicJsonDocument doc(8192);
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
      // Dexcom Share timestamp format: /Date(1715516086000)/
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
    saveHistoryToCache();
    time_t rawtime = (time_t)bgHistory[0].timestamp;
    struct tm * ti = localtime(&rawtime);
    DBG_PRINTF("Success: %d readings. Latest SGV: %d (%s, delta: %+d) at %02d:%02d\n", 
                  historyCount, bgHistory[0].sgv, bgHistory[0].direction, bgHistory[0].delta, ti->tm_hour, ti->tm_min);
  }
  
  // Power Saving: Turn off WiFi radio until next fetch
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  isFetching = false;
  updateUI();
  DBG_PRINTLN("Power Saving: WiFi Radio OFF");
}

void saveHistoryToCache() {
  if (!LittleFS.begin(true, "/littlefs", 10, "ffat")) return;
  File f = LittleFS.open("/history.dat", "w");
  if (!f) return;
  f.write((uint8_t*)&historyCount, sizeof(historyCount));
  f.write((uint8_t*)bgHistory, sizeof(BGReading) * historyCount);
  f.close();
  DBG_PRINTF("Cache: Saved %d readings\n", historyCount);
}

void loadHistoryFromCache() {
  if (!LittleFS.begin(true, "/littlefs", 10, "ffat")) {
    DBG_PRINTLN("Cache: FS Mount Failed");
    return;
  }
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
  DBG_PRINTF("Cache: Loaded %d readings\n", historyCount);

  // Initialize RTC time if it's currently unset (e.g. cold boot without WiFi)
  struct timeval tv;
  gettimeofday(&tv, NULL);
  if (tv.tv_sec < 1000000000 && historyCount > 0) {
    // Set system time to the latest cached reading + 5 minutes
    tv.tv_sec = bgHistory[0].timestamp + 300; 
    settimeofday(&tv, NULL);
    DBG_PRINTLN("Cache: Initialized RTC time from cached data");
  }
}

#if 0 // Debug filesystem tools
void printPartitionTable() {
  DBG_PRINTLN("\n--- Partition Table ---");
  esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, NULL);
  while (it != NULL) {
    const esp_partition_t *p = esp_partition_get(it);
    DBG_PRINTF("  %s: Type: %d, Sub: %d, Addr: 0x%06X, Size: 0x%06X (%d KB)\n", 
               p->label, p->type, p->subtype, p->address, p->size, p->size / 1024);
    it = esp_partition_next(it);
  }
  esp_partition_iterator_release(it);
  DBG_PRINTLN("-----------------------\n");
}

void listDir(const char * dirname, uint8_t levels) {
  DBG_PRINTF("Listing directory: %s\n", dirname);
  File root = LittleFS.open(dirname);
  if (!root) {
    DBG_PRINTLN(" - failed to open directory");
    return;
  }
  if (!root.isDirectory()) {
    DBG_PRINTLN(" - not a directory");
    return;
  }

  File file = root.openNextFile();
  while (file) {
    if (file.isDirectory()) {
      DBG_PRINTF("  DIR : %s\n", file.name());
      if (levels) listDir(file.path(), levels - 1);
    } else {
      DBG_PRINTF("  FILE: %s  SIZE: %d\n", file.name(), file.size());
    }
    file = root.openNextFile();
  }
}
#endif

void loadConfig() {
  // printPartitionTable(); // Uncomment to debug FS
  
  if (!LittleFS.begin(true, "/littlefs", 10, "ffat")) {
    DBG_PRINTLN("Config: FS Mount Failed");
    return;
  }

  // listDir("/", 1); // Uncomment to debug FS

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
  
  DynamicJsonDocument doc(2048);
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
    bgUnits = doc["units"].as<String>();
  }

  if (doc.containsKey("timezone")) {
    ntpServer = doc["timezone"]["ntp"].as<String>();
    gmtOffset_sec = doc["timezone"]["offset"].as<long>();
    daylightOffset_sec = doc["timezone"]["daylight"].as<int>();
  }
  
  DBG_PRINTLN("Config: Loaded from LittleFS");
}

void saveConfig() {
  File f = LittleFS.open("/config.json", "w");
  if (!f) return;
  
  DynamicJsonDocument doc(2048);
  doc["debug"] = debugMode;
  doc["wifi"]["primary_ssid"] = primarySSID;
  doc["wifi"]["primary_pass"] = primaryPass;
  doc["wifi"]["secondary_ssid"] = secondarySSID;
  doc["wifi"]["secondary_pass"] = secondaryPass;
  
  doc["nightscout"]["url"] = nsUrl;
  doc["nightscout"]["secret"] = nsSecret;
  
  doc["dexcom"]["user"] = dexUser;
  doc["dexcom"]["pass"] = dexPass;
  doc["dexcom"]["server"] = dexServer;
  
  doc["provider"] = (currentProvider == PROVIDER_DEXCOM) ? "DEXCOM" : "NIGHTSCOUT";
  doc["units"] = bgUnits;
  
  doc["timezone"]["ntp"] = ntpServer;
  doc["timezone"]["offset"] = gmtOffset_sec;
  doc["timezone"]["daylight"] = daylightOffset_sec;
  
  serializeJson(doc, f);
  f.close();
}

String mapTrendToString(int trend) {
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
    // Fallback to trend int mapping if direction is missing
    return mapTrendToString(obj["trend"].as<int>());
  } else {
    // Dexcom
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
  
  // Step 1: Authenticate to get AccountId
  String authUrl = "https://" + String(dexServer) + "/ShareWebServices/Services/General/AuthenticatePublisherAccount";
  String authPayload = "{\"accountName\":\"" + String(dexUser) + "\",\"password\":\"" + String(dexPass) + "\",\"applicationId\":\"" + String(appId) + "\"}";
  
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
  
  // Step 2: Login with AccountId to get SessionId
  String loginUrl = "https://" + String(dexServer) + "/ShareWebServices/Services/General/LoginPublisherAccountById";
  String loginPayload = "{\"accountId\":\"" + accountId + "\",\"password\":\"" + String(dexPass) + "\",\"applicationId\":\"" + String(appId) + "\"}";
  
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

// --- UI Implementation ---
void initUI() {
  if (!gfx->begin()) {
    DBG_PRINTLN("GFX Init Failed!");
    return;
  }
  gfx->fillScreen(isDarkTheme ? BLACK : WHITE);
  gfx->flush();
}

void updateUI() {
  if (isBooting) return;
  uint16_t bgColor = isDarkTheme ? BLACK : WHITE;
  
  gfx->fillScreen(bgColor);
  drawStatusBar();
  
  if (isTimerMode) {
    // Center a massive timer (Format: MM:SS.0)
    unsigned long elapsed = timerElapsedMs;
    unsigned long minutes = (elapsed / 60000) % 100;
    unsigned long seconds = (elapsed / 1000) % 60;
    unsigned long tenths = (elapsed / 100) % 10;
    
    char timerStr[16];
    sprintf(timerStr, "%02lu:%02lu.%lu", minutes, seconds, tenths);
    
    gfx->setTextColor(isDarkTheme ? GREEN : BLACK); // DOS-style green timer
    gfx->setTextSize(10);
    int textWidth = 7 * 60 - 10; // 7 chars, each 60px wide at size 10 (minus spacing)
    int tx = (640 - textWidth) / 2;
    int ty = 32 + (140 - 80) / 2; // Centered below 32px status bar
    gfx->setCursor(tx, ty);
    
    if (isTimerStopped) {
      // 1Hz Blinking: Hide the text every 500ms
      if ((millis() / 500) % 2 == 0) {
        gfx->print(timerStr);
      }
    } else {
      gfx->print(timerStr);
    }
  } else {
    drawGlucoseContainer();
    drawHistoryChart();
  }
  
  if (isShowingUnitDialog) {
    int w = 150; int h = 120;
    int dx = (640 - w) / 2; // Center horizontally on 640px wide screen
    int dy = (172 - h) / 2; // Center vertically on 172px high screen
    
    gfx->fillRoundRect(dx, dy, w, h, 8, GRAY);
    gfx->drawRoundRect(dx, dy, w, h, 8, WHITE);
    
    gfx->setTextColor(WHITE);
    gfx->setTextSize(2);
    gfx->setCursor(dx + 10, dy + 10);
    if (bgUnits == "mg/dL") {
      gfx->println("Switch to");
      gfx->setCursor(dx + 10, dy + 30);
      gfx->println("mmol/L?");
    } else {
      gfx->println("Switch to");
      gfx->setCursor(dx + 10, dy + 30);
      gfx->println("mg/dL?");
    }
    
    gfx->setCursor(dx + 10, dy + 55);
    gfx->setTextSize(1);
    gfx->println("System will reboot");
    
    // YES Button
    gfx->fillRoundRect(dx + 10, dy + 80, 60, 30, 4, GREEN);
    gfx->setTextColor(BLACK);
    gfx->setTextSize(2);
    gfx->setCursor(dx + 20, dy + 87);
    gfx->print("YES");
    
    // NO Button
    gfx->fillRoundRect(dx + 80, dy + 80, 60, 30, 4, RED);
    gfx->setTextColor(WHITE);
    gfx->setCursor(dx + 95, dy + 87);
    gfx->print("NO");
  }
  
  gfx->flush();
}

uint16_t getBGColor(int sgv) {
  if (sgv <= 0) return GRAY;
  if (sgv < 55 || sgv > 240) return RED;
  if (sgv < 70 || sgv > 180) return ORANGE;
  return GREEN;
}

void drawGlucoseContainer() {
  if (historyCount == 0) {
    gfx->setTextColor(GRAY);
    gfx->setTextSize(4);
    gfx->setCursor(50, 70);
    gfx->print("??");
    return;
  }

  BGReading latest = bgHistory[0];
  uint16_t bgValColor = getBGColor(latest.sgv);

  // --- Harvey Ball ---
  drawHarveyBall(40, 85, 16, bgHistory[0].timestamp);
  
  if (showHarveyBallInfo) {
      long long now = time(NULL);
      int diffMin = (now - bgHistory[0].timestamp) / 60;
      char ageStr[16];
      if (diffMin == 0) strcpy(ageStr, "Now");
      else if (diffMin > 99) strcpy(ageStr, ">99m ago");
      else sprintf(ageStr, "%dm ago", diffMin);
      
      gfx->setTextSize(1);
      gfx->setTextColor(isDarkTheme ? CYAN : BLUE);
      // Center text: Ball X is 40. Font size 1 chars are ~6px wide.
      int textX = 40 - (strlen(ageStr) * 6 / 2);
      gfx->setCursor(textX, 110); // Moved up closer to the smaller ball
      gfx->print(ageStr);
  }

  // 2. Large SGV
  String sgvStr = formatBG(latest.sgv);
  int numChars = sgvStr.length();
  int sgvX;
  if (bgUnits == "mmol/L") {
    // Dynamic centering between Ball (56px) and Arrow (275px). 
    // Shifted 5px left from previous centered positions (73 -> 68, 97 -> 92)
    sgvX = (numChars >= 4) ? 68 : 92;
  } else {
    sgvX = (numChars == 3) ? 71 : 95;
  }
  
  gfx->setTextColor(bgValColor);
  gfx->setTextSize(8); 
  gfx->setCursor(sgvX, 60);

  if (bgUnits == "mmol/L") {
    int dotIdx = sgvStr.indexOf('.');
    if (dotIdx > 0) {
      String intPart = sgvStr.substring(0, dotIdx);
      String decPart = sgvStr.substring(dotIdx + 1);
      
      // 1. Print Integer Part
      gfx->print(intPart);
      
      // 2. Manual 8x8 Dot: 8px gap after integer
      int endOfInt = sgvX + (intPart.length() * 48) - 8;
      int dotX = endOfInt + 8;
      gfx->fillRect(dotX, 108, 8, 8, bgValColor);
      
      // 3. Exact Decimal Position: 8px gap after drawn dot
      int decCharX = dotX + 8 + 8; // Dot end + 8px gap
      gfx->setCursor(decCharX, 60);
      gfx->print(decPart);
    } else {
      gfx->print(sgvStr);
    }
  } else {
    gfx->print(sgvStr);
  }

  // 3. Custom Trend Arrow
  int arrowX = (bgUnits == "mmol/L") ? 265 : 255;
  drawTrendArrow(arrowX, 90, latest.direction, bgValColor);

  // 4. Delta
  gfx->setTextColor(isDarkTheme ? WHITE : BLACK);
  gfx->setTextSize(3);
  int deltaX = (bgUnits == "mmol/L") ? 55 : 85;
  gfx->setCursor(deltaX, 135);
  gfx->printf("%s %s", formatDelta(latest.delta).c_str(), bgUnits.c_str());
}

void drawHarveyBall(int x, int y, int radius, long long timestamp) {
  long long now = time(NULL);
  int diffMin = (now - timestamp) / 60;
  uint16_t color = GREEN;
  
  if (diffMin >= 15) color = RED;
  else if (diffMin >= 6) color = ORANGE;

  gfx->drawCircle(x, y, radius, color);
  gfx->drawCircle(x, y, radius-1, color); // Thicker stroke
  
  if (diffMin > 0) {
    if (diffMin >= 5) {
      gfx->fillCircle(x, y, radius, color);
    } else {
      // Draw pie slices (72 degrees each, clockwise from top)
      // Top (12 o'clock) is 270 degrees.
      float startAngle = 270.0;
      float endAngle = startAngle + (72.0 * diffMin);
      gfx->fillArc(x, y, radius, 0, startAngle, endAngle, color);
    }
  }
}

void drawTrendArrow(int x, int y, String direction, uint16_t color) {
  int size = 52;
  int thickness = 8;
  int headSize = 20;
  
  auto drawSingle = [&](int tx, int ty, float angle) {
    float rad = angle * PI / 180.0;
    
    // 1. Shaft & Head Alignment
    int headOverlap = 8;
    int shaftLen = size - 12;
    
    // Tip of the arrow
    int tipX = tx + cos(rad) * (size / 2);
    int tipY = ty - sin(rad) * (size / 2);
    
    // End of the shaft (where it meets the head)
    int x1 = tipX - cos(rad) * headOverlap;
    int y1 = tipY + sin(rad) * headOverlap;
    
    // Start of the shaft
    int x0 = tx - cos(rad) * (size / 2);
    int y0 = ty + sin(rad) * (size / 2);
    
    int h = thickness;
    
    if (angle == 0 || angle == 180 || angle == 90 || angle == 270) {
      // Axial arrows
      if (angle == 0) { // Flat (Right)
        gfx->fillRect(x0, y0 - h/2, (x1 - x0), h, color);
      } else if (angle == 180) { // Flat (Left) - Just in case
        gfx->fillRect(x1, y0 - h/2, (x0 - x1), h, color);
      } else if (angle == 90) { // Up
        gfx->fillRect(x0 - h/2, y1, h, (y0 - y1), color);
      } else if (angle == 270) { // Down
        gfx->fillRect(x0 - h/2, y0, h, (y1 - y0), color);
      }
    } else {
      // Diagonal arrows
      for(int i = -h/4; i <= h/4; i++) {
        gfx->drawLine(x0 + i, y0, x1 + i, y1, color);
        gfx->drawLine(x0, y0 + i, x1, y1 + i, color);
      }
    }

    // Bold Arrow Head (Sharp Triangle pointing at tipX, tipY)
    float h1 = (angle + 145) * PI / 180.0;
    float h2 = (angle - 145) * PI / 180.0;
    int hx1 = tipX + cos(h1) * headSize;
    int hy1 = tipY - sin(h1) * headSize;
    int hx2 = tipX + cos(h2) * headSize;
    int hy2 = tipY - sin(h2) * headSize;
    
    gfx->fillTriangle(tipX, tipY, hx1, hy1, hx2, hy2, color);
  };

  if (direction == "SingleUp") {
    // 6 blocks shaft + 1 block peak = 7 blocks (56px)
    int pSize = 8;
    int startX = x - 4; 
    int startY = y + 18; 
    for (int i = 0; i < 6; i++) {
      gfx->fillRect(startX, startY - i*pSize, pSize, pSize, color);
    }
    // Chevron Head
    gfx->fillRect(startX - 2*pSize, startY - 4*pSize, pSize, pSize, color);
    gfx->fillRect(startX - 1*pSize, startY - 5*pSize, pSize, pSize, color);
    gfx->fillRect(startX,           startY - 6*pSize, pSize, pSize, color); // Peak
    gfx->fillRect(startX + 1*pSize, startY - 5*pSize, pSize, pSize, color);
    gfx->fillRect(startX + 2*pSize, startY - 4*pSize, pSize, pSize, color);
  } else if (direction == "DoubleUp") {
    int pSize = 8;
    int sX1 = x - 12; 
    int sX2 = x + 4;  
    int startY = y + 18; 
    
    // Shafts (7 blocks including peak row)
    for (int i = 0; i < 7; i++) {
      gfx->fillRect(sX1, startY - i*pSize, pSize, pSize, color);
      gfx->fillRect(sX2, startY - i*pSize, pSize, pSize, color);
    }
    // Chevron Row 1 (Mid-high)
    gfx->fillRect(sX1 - 2*pSize, startY - 4*pSize, pSize, pSize, color); 
    gfx->fillRect(sX2 + 2*pSize, startY - 4*pSize, pSize, pSize, color); 
    // Chevron Row 2 (Higher)
    gfx->fillRect(sX1 - 1*pSize, startY - 5*pSize, pSize, pSize, color); 
    gfx->fillRect(x - 4,           startY - 5*pSize, pSize, pSize, color); // Shared Center
    gfx->fillRect(sX2 + 1*pSize, startY - 5*pSize, pSize, pSize, color); 
    // Peaks are already drawn by the shaft loop i=5
  } else if (direction == "FortyFiveUp") {
    int pSize = 8;
    int startX = x - 20; // Shortened shaft
    int startY = y + 10; // Recentered
    
    // 5-pixel Diagonal Shaft
    for (int i = 0; i < 5; i++) {
      gfx->fillRect(startX + i*pSize, startY - i*pSize, pSize, pSize, color);
    }
    // Head (Peak at top-right, aligned with BG top)
    gfx->fillRect(startX + 1*pSize, startY - 4*pSize, pSize, pSize, color); 
    gfx->fillRect(startX + 2*pSize, startY - 4*pSize, pSize, pSize, color); 
    gfx->fillRect(startX + 3*pSize, startY - 4*pSize, pSize, pSize, color);
    gfx->fillRect(startX + 4*pSize, startY - 4*pSize, pSize, pSize, color); 
    gfx->fillRect(startX + 4*pSize, startY - 3*pSize, pSize, pSize, color); 
    gfx->fillRect(startX + 4*pSize, startY - 2*pSize, pSize, pSize, color);
    gfx->fillRect(startX + 4*pSize, startY - 1*pSize, pSize, pSize, color);
  } else if (direction == "Flat") {
    int pSize = 8;
    int startX = x - 36; 
    int startY = y - 6; // Perfect center (88px when y=90)
    
    for (int i = 1; i < 7; i++) {
      gfx->fillRect(startX + i*pSize, startY, pSize, pSize, color);
    }
    gfx->fillRect(startX + 5*pSize, startY - 2*pSize, pSize, pSize, color); 
    gfx->fillRect(startX + 6*pSize, startY - 1*pSize, pSize, pSize, color); 
    gfx->fillRect(startX + 7*pSize, startY,           pSize, pSize, color); 
    gfx->fillRect(startX + 6*pSize, startY + 1*pSize, pSize, pSize, color); 
    gfx->fillRect(startX + 5*pSize, startY + 2*pSize, pSize, pSize, color);
  } else if (direction == "FortyFiveDown") {
    int pSize = 8;
    int startX = x - 20; 
    int startY = y - 22; 
    
    // 5-pixel Diagonal Shaft
    for (int i = 0; i < 5; i++) {
      gfx->fillRect(startX + i*pSize, startY + i*pSize, pSize, pSize, color);
    }
    // Head (Peak at bottom-right, aligned with BG bottom)
    gfx->fillRect(startX + 1*pSize, startY + 4*pSize, pSize, pSize, color); 
    gfx->fillRect(startX + 2*pSize, startY + 4*pSize, pSize, pSize, color); 
    gfx->fillRect(startX + 3*pSize, startY + 4*pSize, pSize, pSize, color);
    gfx->fillRect(startX + 4*pSize, startY + 4*pSize, pSize, pSize, color); 
    gfx->fillRect(startX + 4*pSize, startY + 3*pSize, pSize, pSize, color); 
    gfx->fillRect(startX + 4*pSize, startY + 2*pSize, pSize, pSize, color);
    gfx->fillRect(startX + 4*pSize, startY + 1*pSize, pSize, pSize, color);
  } else if (direction == "SingleDown") {
    // 6 blocks shaft + 1 block peak = 7 blocks (56px)
    int pSize = 8;
    int startX = x - 4;
    int startY = y - 30; // Shifted up 8px
    for (int i = 0; i < 6; i++) {
      gfx->fillRect(startX, startY + i*pSize, pSize, pSize, color);
    }
    // Chevron Head
    gfx->fillRect(startX - 2*pSize, startY + 4*pSize, pSize, pSize, color);
    gfx->fillRect(startX - 1*pSize, startY + 5*pSize, pSize, pSize, color);
    gfx->fillRect(startX,           startY + 6*pSize, pSize, pSize, color); // Peak
    gfx->fillRect(startX + 1*pSize, startY + 5*pSize, pSize, pSize, color);
    gfx->fillRect(startX + 2*pSize, startY + 4*pSize, pSize, pSize, color);
  } else if (direction == "DoubleDown") {
    int pSize = 8;
    int sX1 = x - 12; 
    int sX2 = x + 4;  
    int startY = y - 30; // Shifted up 8px (1 block)
    
    // Shafts (7 blocks including peak row)
    for (int i = 0; i < 7; i++) {
      gfx->fillRect(sX1, startY + i*pSize, pSize, pSize, color);
      gfx->fillRect(sX2, startY + i*pSize, pSize, pSize, color);
    }
    // Chevron Row 1 (Mid-low)
    gfx->fillRect(sX1 - 2*pSize, startY + 4*pSize, pSize, pSize, color); 
    gfx->fillRect(sX2 + 2*pSize, startY + 4*pSize, pSize, pSize, color); 
    // Chevron Row 2 (Lower)
    gfx->fillRect(sX1 - 1*pSize, startY + 5*pSize, pSize, pSize, color); 
    gfx->fillRect(x - 4,           startY + 5*pSize, pSize, pSize, color); // Shared Center
    gfx->fillRect(sX2 + 1*pSize, startY + 5*pSize, pSize, pSize, color); 
    // Peaks are already drawn by the shaft loop i=5
  }
}

// --- Phase 5: Historical Chart & Touch ---
bool readTouch(int &tx, int &ty) {
  uint8_t read_cmd[11] = {0xb5, 0xab, 0xa5, 0x5a, 0x00, 0x00, 0x00, 0x0e, 0x00, 0x00, 0x00};
  Wire1.beginTransmission(TOUCH_ADDR);
  for (int i = 0; i < 11; i++) Wire1.write(read_cmd[i]);
  if (Wire1.endTransmission() != 0) return false;
  
  Wire1.requestFrom((uint16_t)TOUCH_ADDR, (uint8_t)14, true);
  if (Wire1.available() >= 14) {
    Wire1.read(); // gesture
    uint8_t num = Wire1.read();
    uint8_t x_h = Wire1.read();
    uint8_t x_l = Wire1.read();
    uint8_t y_h = Wire1.read();
    uint8_t y_l = Wire1.read();
    for (int i = 0; i < 8; i++) Wire1.read(); // flush
    
    if (num > 0 && num < 5) {
      int rx = ((x_h & 0x0F) << 8) | x_l;
      int ry = ((y_h & 0x0F) << 8) | y_l;
      if (rx < 1000 && ry < 1000) {
        tx = 640 - rx;
        ty = 172 - ry;
        return true;
      }
    }
  }
  return false;
}

void checkTouch() {
  if (isTimerMode) {
    isTouching = false;
    touchConfidence = 0;
    return;
  }
  int tx, ty;
  bool touched = readTouch(tx, ty);
  static bool waitForRelease = false;

  if (waitForRelease) {
    if (!touched) {
      waitForRelease = false;
      isTouching = false;
      touchConfidence = 0;
    }
    return;
  }

  if (touched) {
    // 1. Noise Filter: Check for impossible coordinates or erratic jumps
    if (tx < 0 || tx > 640 || ty < 0 || ty > 172) {
      touchConfidence = 0;
      return; 
    }
    
    // If the coordinates jump > 50px in 20ms, it's likely noise (or a very fast swipe)
    // We completely reset the confidence counter so noise can never accumulate
    if (lastRawX != -1 && (abs(tx - lastRawX) > 50 || abs(ty - lastRawY) > 50)) {
      lastRawX = tx; lastRawY = ty;
      touchConfidence = 1; 
      return; 
    }
    lastRawX = tx; lastRawY = ty;

    // 2. Confidence Filter: Require 3 consecutive, stable valid frames
    touchConfidence++;
    if (touchConfidence < 3) return; 

    touchX = tx;
    touchY = ty;
    isTouching = true;

    // Unit Dialog Interaction (Consumes touch)
    if (isShowingUnitDialog) {
      int w = 150; int h = 120;
      int dx = (640 - w) / 2;
      int dy = (172 - h) / 2;
      
      // YES Button (dx+10, dy+80, 60, 30)
      if (touchX > dx+10 && touchX < dx+70 && touchY > dy+80 && touchY < dy+110) {
        bgUnits = (bgUnits == "mg/dL") ? "mmol/L" : "mg/dL";
        saveConfig();
        ESP.restart();
      }
      // NO Button (dx+80, dy+80, 60, 30)
      if (touchX > dx+80 && touchX < dx+140 && touchY > dy+80 && touchY < dy+110) {
        isShowingUnitDialog = false;
        isTouching = false; 
        waitForRelease = true; // Wait for finger to be lifted before processing new touches
        updateUI();
      }
      return; // Stop processing other touches while dialog is active
    }

    // Check for Harvey Ball tap (centered at 40, 78 with radius 16)
    if (touchX > 20 && touchX < 60 && touchY > 58 && touchY < 98) {
      showHarveyBallInfo = true;
      lastHarveyBallTapTime = millis();
      updateUI(); 
    }

    // Check for Long-press on Delta area for Unit Switch (X:20-280, Y:100-172)
    if (touchX > 20 && touchX < 280 && touchY > 100 && touchY < 172) {
      if (!isLongTapping) {
        lastTouchStartTime = millis();
        isLongTapping = true;
        Serial.println("Touch: Delta area pressed, starting long-tap timer...");
      } else {
        unsigned long duration = millis() - lastTouchStartTime;
        if (duration > 1000) {
          Serial.println("Touch: Long-tap TRIGGERED on Delta area!");
          isShowingUnitDialog = true;
          isLongTapping = false;
          updateUI();
        }
      }
    } else {
      // If finger moves out of the delta box while holding
      if (isLongTapping) {
        isLongTapping = false;
        Serial.println("Touch: Long-tap cancelled (finger moved out of area)");
      }
    }

    // Check for Chart Scrubber (Starts at 300 now)
    if (touchX >= 300 && touchX <= 640 && touchY > 40) {
      lastScrubberX = touchX;
      lastScrubberTouchTime = millis();
    }


  } else {
    touchConfidence = 0;
    lastRawX = -1; lastRawY = -1;
    // Debounce touch release for long-tap (tolerate 100ms of signal loss)
    if (isLongTapping && (millis() - lastTouchStartTime > 100) && (millis() - lastTouchStartTime < 1000)) {
       // Only reset if it's been less than 1s (haven't triggered yet) 
       // but wait a tiny bit to ignore flickers
       if (millis() % 500 < 50) { // Don't spam reset prints
         isLongTapping = false;
         Serial.println("Touch: Release detected, long-tap timer reset.");
       }
    }
    
    if (isTouching) {
      isTouching = false;
      lastRawX = -1; lastRawY = -1;
      updateUI(); // Clear scrubber
    }
  }
}

void drawHistoryChart() {
  if (historyCount < 2) return;

  // 1. Chart Dimensions (Extended left by 20px to start at 300)
  int chartX = 300; 
  int chartRight = 625; // 640 - 15
  int chartY = 50;
  int chartWidth = 325; // 625 - 300 = 325
  int chartHeight = 110;
  
  // 1. Find Min/Max for scaling
  int minBG = 400;
  int maxBG = 0;
  for (int i = 0; i < historyCount; i++) {
    if (bgHistory[i].sgv > maxBG) maxBG = bgHistory[i].sgv;
    if (bgHistory[i].sgv < minBG) minBG = bgHistory[i].sgv;
  }
  if (maxBG < 200) maxBG = 200;
  if (minBG > 60) minBG = 60;
  maxBG += 20; minBG -= 20;

  auto getY = [&](int bg) {
    return chartY + chartHeight - map(bg, minBG, maxBG, 0, chartHeight);
  };

  // 2. Draw Range Highlighter (Aligned to data width)
  int y180 = getY(180);
  int y70 = getY(70);
  int oldestX = chartX + chartWidth - ((historyCount - 1) * chartWidth / MAX_HISTORY);
  // Add +1 to width to ensure the rightmost pixel (latest data) is fully covered
  gfx->fillRect(oldestX, y180, (chartX + chartWidth) - oldestX + 1, y70 - y180, isDarkTheme ? 0x2104 : 0xEF7D);

  // 3. Draw Chart Line
  float barWidth = 325.0 / MAX_HISTORY;
  for (int i = 0; i < historyCount - 1; i++) {
    // Use pre-calculated floating point width
    int x1 = chartRight - (int)(i * barWidth);
    int x2 = chartRight - (int)((i+1) * barWidth);
    int y1 = getY(bgHistory[i].sgv);
    int y2 = getY(bgHistory[i+1].sgv);
    uint16_t color = (bgHistory[i].sgv >= 70 && bgHistory[i].sgv <= 180) ? GREEN : ORANGE;
    
    // Thicker line (5 pixels)
    gfx->drawLine(x1, y1, x2, y2, color);
    gfx->drawLine(x1, y1+1, x2, y2+1, color);
    gfx->drawLine(x1, y1-1, x2, y2-1, color);
    gfx->drawLine(x1, y1+2, x2, y2+2, color);
  }

  // 4. Scrubber Popup (Phase 6 Polish: 3s Persistence)
  bool showScrubber = isTouching && touchX >= chartX && touchX <= chartX + chartWidth;
  if (!showScrubber && (millis() - lastScrubberTouchTime < 3000) && lastScrubberX != -1) {
    showScrubber = true;
  }

  if (showScrubber) {
    int curX = isTouching ? touchX : lastScrubberX;
    
    int oldestX = chartRight - (int)((historyCount - 1) * barWidth);
    // Synchronize mapping with the actual width of the data curve
    int dataIdx = map(curX, oldestX, chartRight, historyCount - 1, 0);
    dataIdx = constrain(dataIdx, 0, historyCount - 1);
    
    // Magnetic Snap: Force the cursor X to exactly match the data point X
    curX = chartRight - (int)(dataIdx * barWidth);
    
    BGReading r = bgHistory[dataIdx];
    int curY = getY(r.sgv);
    
    gfx->drawFastVLine(curX, chartY, chartHeight, isDarkTheme ? WHITE : BLACK);
    // Draw Intersection Circle
    gfx->fillCircle(curX, curY, 4, isDarkTheme ? WHITE : BLACK);
    gfx->drawCircle(curX, curY, 5, isDarkTheme ? BLACK : WHITE);
    
    // Popup Box (Shifted left to prevent edge clipping)
    int boxW = 90;
    int boxH = 40;
    int boxX = curX - boxW; 
    int boxY = chartY - 45;
    
    gfx->fillRoundRect(boxX, boxY, boxW, boxH, 4, GRAY);
    gfx->drawRoundRect(boxX, boxY, boxW, boxH, 4, isDarkTheme ? WHITE : BLACK);
    
    gfx->setTextSize(2);
    gfx->setTextColor(BLACK); // High contrast for popup
    gfx->setCursor(boxX + 10, boxY + 5);
    gfx->print(formatBG(r.sgv));
    
    gfx->setTextColor(BLACK);
    gfx->setTextSize(2);
    gfx->setCursor(boxX + 10, boxY + 23);
    
    time_t rt = (time_t)r.timestamp;
    struct tm* ti = localtime(&rt);
    gfx->printf("%02d:%02d", ti->tm_hour, ti->tm_min);
  }
}
int getBatteryPercentage(float voltage) {
  // 4000mAh Li-Ion discharge curve map (utilizing max reasonable capacity)
  // 0% is mapped to 3.00V, ensuring 0% occurs before the 2.95V critical shutdown
  float vMap[] = {3.00, 3.25, 3.40, 3.50, 3.55, 3.60, 3.65, 3.75, 3.85, 3.95, 4.00, 4.05};
  int pMap[]   = {   0,    5,   10,   20,   30,   40,   50,   60,   70,   80,   90,  100};
  
  if (voltage <= vMap[0]) return 0;
  if (voltage >= vMap[11]) return 100;
  
  for (int i = 0; i < 11; i++) {
    if (voltage >= vMap[i] && voltage <= vMap[i+1]) {
      float range = vMap[i+1] - vMap[i];
      float offset = voltage - vMap[i];
      float pRange = pMap[i+1] - pMap[i];
      return pMap[i] + (int)((offset / range) * pRange);
    }
  }
  return 100;
}

void drawStatusBar() {
  uint16_t textColor = isDarkTheme ? WHITE : BLACK;
  gfx->setTextColor(textColor);
  gfx->setTextSize(2); // Size 2 is perfect for the 32px status bar
  
  // 1. Time (HH:MM)
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return;

  char timeStr[10];
  sprintf(timeStr, "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
  gfx->setCursor(15, 7);
  gfx->print(timeStr);
  
  // 1.5 Draw Spinner Animation (DOS-style, next to time / between time and BG)
  if (isFetching) {
    gfx->setTextColor(YELLOW);
    const char spinnerFrames[] = {'|', '/', '-', '\\'};
    char spinnerChar = spinnerFrames[(millis() / 150) % 4];
    
    int spinnerX = isTimerMode ? 85 : 90;
    gfx->setCursor(spinnerX, 7);
    gfx->print(spinnerChar);
    gfx->setTextColor(textColor);
  }
  
  // In Timer Mode: Draw color-coded BG value and Delta shifted right to leave room for the spinner
  if (isTimerMode && historyCount > 0) {
    BGReading latest = bgHistory[0];
    String sgvStr = (bgUnits == "mmol/L") ? String(latest.sgv / 18.0182, 1) : String(latest.sgv);
    
    // Draw BG value with its specific status color at X = 110
    gfx->setTextColor(getBGColor(latest.sgv));
    gfx->setCursor(110, 7);
    gfx->print(sgvStr);
    
    // Draw Delta in standard textColor
    gfx->setTextColor(textColor);
    String deltaStr = formatDelta(latest.delta);
    int bgWidth = sgvStr.length() * 12; // At size 2, each char is 12px wide
    gfx->setCursor(110 + bgWidth + 10, 7);
    gfx->printf("(%s)", deltaStr.c_str());
  }
  
  // 2. Battery & Charging Indicator
  int cursorX = 625; // Default start if no battery info
  int chargeDebounce = 0;
  
  if (currentBatteryPct >= 0) {
    char batStr[32];
    if (debugMode) {
      sprintf(batStr, "%d%% (%.2fV)", currentBatteryPct, currentBatteryVoltage);
    } else {
      sprintf(batStr, "%d%%", currentBatteryPct);
    }
    
    int strWidth = strlen(batStr) * 12;
    cursorX = 625 - strWidth;
    
    gfx->setCursor(cursorX, 7);
    gfx->print(batStr);
    
    // Draw charging indicator (with 3-sample software debounce)
    static int lastChargeState = 0;
    // Filter: Only count as charging if Pin 16 is LOW and NOT currently being pressed as a button
    if (digitalRead(16) == LOW && !pwrBtn.pressed) lastChargeState++;
    else lastChargeState = 0;
    chargeDebounce = lastChargeState;

    if (chargeDebounce >= 3) {
      gfx->setCursor(cursorX - 15, 7); 
      gfx->print("+");
    }
  }
  
  // Bottom line
  gfx->drawFastHLine(0, 32, 640, GRAY);
}

void toggleTheme() {
  isDarkTheme = !isDarkTheme;
  DBG_PRINTF("Theme changed: %s\n", isDarkTheme ? "DARK" : "LIGHT");
  updateUI();
}

void setBrightness(int level) {
  brightnessLevel = level;
  // AXS15231B backlight is inverted (0 = max, 255 = off)
  int val = 255 - level;
  analogWrite(PIN_BL, val);
}

void logBoot(String msg) {
  DBG_PRINTLN(msg);
  if (!isBooting) return;

  bootLog += msg + "\n";
  
  // Count lines and trim from start to keep only the LATEST lines that fit (5 lines at Size 2)
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
  gfx->setTextSize(2); // Header
  gfx->setCursor(20, 20);
  gfx->println("--- Sugarota " SUGAROTA_VERSION " Booting ---");
  
  gfx->setTextSize(2); 
  int logY = 50;
  int startIdx = 0;
  for (int i = 0; i < bootLog.length(); i++) {
    if (bootLog[i] == '\n') {
      gfx->setCursor(20, logY);
      gfx->print(bootLog.substring(startIdx, i));
      logY += 20;
      startIdx = i + 1;
    }
  }
  gfx->flush();
}

void powerOffDevice() {
  DBG_PRINTLN(F("--- Powering Off ---"));
  
  // 1. Show shutdown message (Size 3)
  gfx->fillScreen(BLACK);
  gfx->setTextColor(GREEN);
  gfx->setTextSize(3);
  gfx->setCursor(110, 70); 
  gfx->print("--- Powering Off ---");
  gfx->flush();

  // 2. Wait for 2 seconds as requested
  delay(2000);

  // 3. Wait for button release (if still holding) to prevent immediate wake-up
  while (digitalRead(PIN_PWR_BTN) == LOW) {
    delay(10);
  }
  delay(100); // Small debounce

  // 4. Hardware Shutdown
  Wire.beginTransmission(TCA9554_ADDR);
  Wire.write(0x01);
  Wire.write(0x00);
  Wire.endTransmission();
  
  // Kill backlight properly (prevent full brightness flash on sleep)
  pinMode(PIN_BL, OUTPUT);
  digitalWrite(PIN_BL, HIGH); // Assuming inverted logic: HIGH = off
  gpio_hold_en((gpio_num_t)PIN_BL); // Hold the pin state during deep sleep
  
  esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_PWR_BTN, 0); 
  DBG_PRINTLN(F("Entering Deep Sleep..."));
  delay(100);
  esp_deep_sleep_start();
}
String formatBG(int mgdl) {
  if (bgUnits == "mmol/L") {
    float mmol = mgdl / 18.0182; // Precise conversion
    char buf[16];
    sprintf(buf, "%.1f", mmol);
    return String(buf);
  }
  return String(mgdl);
}

String formatDelta(int delta) {
  if (bgUnits == "mmol/L") {
    float mmol = delta / 18.0182;
    char buf[16];
    sprintf(buf, "%+.1f", mmol);
    return String(buf);
  }
  char buf[16];
  sprintf(buf, "%+d", delta);
  return String(buf);
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
      
      // Perform validation check to guarantee JSON integrity before writing
      DynamicJsonDocument doc(2048);
      DeserializationError error = deserializeJson(doc, jsonPayload);
      
      if (!error) {
        File f = LittleFS.open("/config.json", "w");
        if (f) {
          f.print(jsonPayload);
          f.close();
          Serial.println("CONF_OK");
          delay(500);
          ESP.restart(); // Reboot to apply settings
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

