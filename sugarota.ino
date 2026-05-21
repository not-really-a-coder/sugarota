// --- Version Control ---
#define SUGAROTA_VERSION "v0.05.20.31"

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <time.h>
#include <Wire.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Arduino_GFX_Library.h>
#include <SensorQMI8658.hpp>
#include <SensorPCF85063.hpp>
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

// --- Config vars declaration (values defined in data/config.json) ---
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
bool useSecondaryFirst = false;
bool screenManuallyOff = false;
bool offlineMode = false;

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
bool wasUSBPlugged = false;
float chargingOffset = 0.0;
float preSpikeVoltage = 0.0;
unsigned long usbLowStartTime = 0;
unsigned long usbHighStartTime = 0;
unsigned long lastUSBUnplugTime = 0;
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
void parseResponse(const String& payload);
bool loginDexcom();

// UI Functions
void initUI();
void updateUI();
void drawStatusBar();
void drawGlucoseContainer();
void drawHistoryChart();
void drawTrendArrow(int x, int y, const String& direction, uint16_t color);
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
SensorPCF85063 rtc;

void restoreTimeFromRTC() {
  RTC_DateTime datetime = rtc.getDateTime();
  struct tm tm_time;
  tm_time.tm_year = datetime.getYear() - 1900;
  tm_time.tm_mon = datetime.getMonth() - 1;
  tm_time.tm_mday = datetime.getDay();
  tm_time.tm_hour = datetime.getHour();
  tm_time.tm_min = datetime.getMinute();
  tm_time.tm_sec = datetime.getSecond();
  tm_time.tm_isdst = 0; // UTC has no DST
  
  // Timezone-independent UTC tm to time_t conversion
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

void setup() {
  // Release pad hold on PIN_BL and disable deep sleep pad holds so backlight can turn on
  gpio_hold_dis((gpio_num_t)PIN_BL);
  gpio_deep_sleep_hold_dis();

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
  rtc.begin(Wire, I2C_SDA, I2C_SCL);
  restoreTimeFromRTC();
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
  updateBattery(digitalRead(PIN_PWR_BTN) == LOW);
  
  char batMsg[40];
  sprintf(batMsg, "Battery: %.2fV (%d%%)", currentBatteryVoltage, currentBatteryPct);

  // 4. Initial Hardware Checks
  logBoot("Initializing FS...");
  loadConfig();
  // Configure system timezone offset so getLocalTime() works offline/immediately
  configTime(gmtOffset_sec, daylightOffset_sec, "");

  logBoot("Loading Cache...");
  loadHistoryFromCache();

  logBoot(batMsg);

  logBoot("Connecting WiFi...");
  connectWiFi();
  
  if (WiFi.status() == WL_CONNECTED) {
    offlineMode = false;
    logBoot("WiFi Connected!");
    logBoot("Syncing NTP Time...");
    
    // Reset system time to epoch 1970 to force getLocalTime to wait for NTP sync
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
    
    // Check if we got data successfully
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
    delay(2000);
  }

  // Mark buttons as handled if they are already held down at boot/wakeup
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

  isBooting = false;
  updateUI();
}

void loop() {
  checkSerialConsole();
  
  if (!deviceOn) {
    powerOffDevice();
    return;
  }

  checkButtons();
  if (brightnessLevel > 0) {
    checkTouch();
  } else {
    isTouching = false;
    touchConfidence = 0;
  }

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
  if (imuReady && !isBooting && deviceOn && !screenManuallyOff && (millis() - lastImuPoll > imuPollInterval)) {
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
           if (!offlineMode) {
             DBG_PRINTLN("SHAKE DETECTED! Forcing Refresh...");
             fetchData();
           } else {
             DBG_PRINTLN("SHAKE DETECTED! Refresh disabled in Offline Mode");
           }
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

  // Low battery blinking refresh (5% or less, 500ms rate)
  if (currentBatteryPct >= 0 && currentBatteryPct <= 5 && !isBooting && deviceOn && !screenManuallyOff) {
    static unsigned long lastLowBatBlinkRefresh = 0;
    if (millis() - lastLowBatBlinkRefresh >= 500) {
      lastLowBatBlinkRefresh = millis();
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

  // Battery Monitoring (Every 10 seconds or instantly on USB plugin/unplug with instant spike detection and debounces)
  bool pinLow = (digitalRead(PIN_PWR_BTN) == LOW);
  bool currentUSB = wasUSBPlugged; // Default to last known state
  float instantV = analogReadMilliVolts(PIN_BAT_ADC) * 3.0 / 1000.0;
  
  if (pinLow) {
    usbHighStartTime = 0; // Reset unplug debounce timer
    if (!wasUSBPlugged) {
      if (usbLowStartTime == 0) {
        usbLowStartTime = millis();
        preSpikeVoltage = currentBatteryVoltage; // Store the stable voltage before spike!
      }
      // Instant spike detection: if voltage rises by >= 0.03V, it's USB!
      if (preSpikeVoltage > 2.0 && (instantV - preSpikeVoltage >= 0.03)) {
        currentUSB = true;
        usbLowStartTime = 0;
      }
      // Fallback debounce for plug-in (3 seconds)
      else if (millis() - usbLowStartTime >= 3000) {
        currentUSB = true;
      }
    } else {
      usbLowStartTime = 0;
      currentUSB = true;
    }
  } else {
    usbLowStartTime = 0; // Reset plug-in debounce timer
    if (wasUSBPlugged) {
      if (usbHighStartTime == 0) {
        usbHighStartTime = millis();
      }
      // Confirm unplugged only after staying HIGH for 1.0 second (1000 ms)
      if (millis() - usbHighStartTime >= 1000) {
        currentUSB = false;
      }
    } else {
      usbHighStartTime = 0;
      currentUSB = false;
    }
  }

  if (currentUSB != wasUSBPlugged || (millis() - lastBatRead >= 10000)) {
    lastBatRead = millis();
    updateBattery(currentUSB);
  }

  // Periodic data fetch every minute
  if (!offlineMode && (millis() - lastDataFetch >= FETCH_INTERVAL)) {
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
  static int16_t buf[chunkFrames * 2]; // Keep off the stack to prevent stack overflow
  static bool bufInitialized = false;
  
  if (!bufInitialized) {
    // Fill the chunk buffer with a 2000Hz square wave
    // 24000 / 2000 = 12 frames per cycle (6 high, 6 low)
    // Amplitude decreased by 25% (20000 -> 15000)
    for (int i = 0; i < chunkFrames; i++) {
      int16_t val = ((i / 6) % 2 == 0) ? 15000 : -15000;
      buf[i * 2] = val;     // Left
      buf[i * 2 + 1] = val; // Right
    }
    bufInitialized = true;
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
  
  // Read raw PCM data in 4KB chunks and write to I2S codec (using static buffer to save stack)
  const int bufSize = 4096;
  static uint8_t buf[bufSize];
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
    
    // Try first SSID
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
    
    // Try second SSID
    if (secondSSID.length() > 0) {
      logBoot("Trying WiFi 2: " + secondSSID);
      WiFi.begin(secondSSID.c_str(), secondPass.c_str());
      unsigned long startAttemptTime = millis();
      while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000) {
        spinnerDelay(500);
      }
      
      if (WiFi.status() == WL_CONNECTED) {
        // Succeeded with the fallback SSID - toggle preference!
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

void checkButton(ButtonState &btn, const char* name) {
  bool isPressed = (digitalRead(btn.pin) == LOW);
  
  if (isPressed && !btn.pressed) {
    btn.pressed = true;
    btn.pressTime = millis();
    btn.handled = false;
    
    // Instantly wake screen on press if manually off to prevent false long-press shutdowns
    if (btn.pin == PIN_PWR_BTN && screenManuallyOff) {
      Serial.printf("%s Button: WAKE Press detected\n", name);
      screenManuallyOff = false;
      brightnessLevel = 76;
      setBrightness(brightnessLevel);
      updateUI();
      btn.handled = true;
    }
  } else if (isPressed && btn.pressed) {
    if (!btn.handled) {
      unsigned long duration = millis() - btn.pressTime;
      if (btn.pin == PIN_PWR_BTN) {
        if (duration >= 2000 && !wasUSBPlugged) {
          Serial.printf("%s Button: LONG Press detected (Hold >= 2s)\n", name);
          btn.handled = true;
          deviceOn = false; // Trigger power off
        }
      } else if (btn.pin == PIN_BOOT_BTN) {
        if (duration >= 1500) {
          Serial.printf("%s Button: LONG Press detected (Hold >= 1.5s)\n", name);
          btn.handled = true;
          if (!offlineMode) {
            DBG_PRINTLN("ACTION: Force Data Refresh");
            fetchData();
          } else {
            DBG_PRINTLN("ACTION: Force Data Refresh (Disabled in Offline Mode)");
          }
        }
      }
    }
  } else if (!isPressed && btn.pressed) {
    btn.pressed = false;
    unsigned long duration = millis() - btn.pressTime;
    
    if (!btn.handled) {
      if (btn.pin == PIN_PWR_BTN) {
        if (duration > 50 && duration < 2000) {
          Serial.printf("%s Button: SHORT Press detected (Release)\n", name);
          
          // Cycle brightness: 0 (0%) -> 76 (30%) -> 153 (60%) -> 204 (80%) -> 255 (100%)
          if (brightnessLevel == 0) brightnessLevel = 76;
          else if (brightnessLevel < 76) brightnessLevel = 76;
          else if (brightnessLevel < 153) brightnessLevel = 153;
          else if (brightnessLevel < 204) brightnessLevel = 204;
          else if (brightnessLevel < 255) brightnessLevel = 255;
          else {
            brightnessLevel = 0;
            screenManuallyOff = true;
          }
          
          setBrightness(brightnessLevel);
          Serial.printf("Brightness: %d, ManualOff: %d\n", brightnessLevel, screenManuallyOff);
        }
      } else if (btn.pin == PIN_BOOT_BTN) {
        if (duration > 50 && duration < 1500) {
          Serial.printf("%s Button: SHORT Press detected (Release)\n", name);
          toggleTheme();
        }
      }
    }
  }
}

void checkButtons() {
  checkButton(pwrBtn, "PWR");
  checkButton(bootBtn, "BOOT");
}

void fillVoltageHistory(float voltage) {
  for (int i = 0; i < 60; i++) {
    voltageHistory[i] = voltage;
  }
  voltageIndex = 0;
  historyFilled = true;
}

void updateBattery(bool isUSBPlugged) {

  // Use analogReadMilliVolts for accurate factory-calibrated ADC measurement.
  // Multiply by 3 for the voltage divider.
  float currentV = analogReadMilliVolts(PIN_BAT_ADC) * 3.0 / 1000.0; 

  // Detect USB state transitions
  if (isUSBPlugged && !wasUSBPlugged) {
    // Transition: Unplugged -> Plugged
    wasUSBPlugged = true;
    
    // Save last unplugged voltage
    float lastUnpluggedV = (preSpikeVoltage > 0) ? preSpikeVoltage : ((currentBatteryVoltage > 0) ? currentBatteryVoltage : currentV);
    
    // Estimate the voltage spike/offset due to charging
    chargingOffset = currentV - lastUnpluggedV;
    if (chargingOffset < 0.05 || chargingOffset > 0.35) {
      chargingOffset = 0.15; // default reasonable offset fallback
    }
    
    // Flush history buffer with the new plugged-in voltage
    fillVoltageHistory(currentV);
    
    DBG_PRINTF("USB Plugged In. Last Unplugged: %.2fV, Current: %.2fV, Offset: %.2fV\n", 
               lastUnpluggedV, currentV, chargingOffset);
  } 
  else if (!isUSBPlugged && wasUSBPlugged) {
    // Transition: Plugged -> Unplugged
    wasUSBPlugged = false;
    chargingOffset = 0.0;
    
    // Flush history buffer with the new unplugged voltage
    fillVoltageHistory(currentV);
    
    // Force immediate update to the real battery charge
    float avgV = currentV;
    currentBatteryVoltage = avgV;
    int targetPct = getBatteryPercentage(avgV);
    currentBatteryPct = targetPct;
    lastBatteryPctUpdate = millis();
    lastUSBUnplugTime = millis(); // Record the unplug event to trigger settling window
    
    updateUI(); // Force instant GUI refresh
    DBG_PRINTF("USB Plugged Out. Real Battery: %.2fV, Pct: %d%%\n", currentV, currentBatteryPct);
  }

  // Update history with current reading
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
  
  // Calculate estimated battery voltage (subtract offset if charging)
  float estimatedV = avgV - chargingOffset;
  currentBatteryVoltage = estimatedV;

  static bool bootVoltageChecked = false;
  static bool bootedLow = false;
  if (!bootVoltageChecked) {
    if (estimatedV < 3.00) {
      bootedLow = true;
      DBG_PRINTF("Boot voltage checked: %.2fV (Low, < 3.00V). Will monitor for 10s.\n", estimatedV);
    } else {
      DBG_PRINTF("Boot voltage checked: %.2fV (Normal, >= 3.00V).\n", estimatedV);
    }
    bootVoltageChecked = true;
  }

  int targetPct = getBatteryPercentage(estimatedV);
  
  if (currentBatteryPct == -1) {
    currentBatteryPct = targetPct; // Initialize on first run
    lastBatteryPctUpdate = millis();
  } else {
    // If USB was unplugged in the last 30 seconds, allow instant snapping to real settled voltage
    if (millis() - lastUSBUnplugTime < 30000) {
      if (currentBatteryPct != targetPct) {
        currentBatteryPct = targetPct;
        lastBatteryPctUpdate = millis();
        updateUI();
      }
    } else {
      // Only allow percentage to change once per minute (refresh rate requirement)
      if (millis() - lastBatteryPctUpdate >= 60000) {
        if (targetPct > currentBatteryPct) {
          currentBatteryPct++;
        } else if (targetPct < currentBatteryPct) {
          currentBatteryPct--;
        }
        lastBatteryPctUpdate = millis();
        updateUI(); // Force GUI refresh when percentage changes
      }
    }
  }
  
  DBG_PRINTF("Battery: %.2fV (Avg: %.2fV, Est: %.2fV) Target: %d%% Disp: %d%%\n", 
             currentV, avgV, estimatedV, targetPct, currentBatteryPct);
  if (isUSBPlugged) {
      DBG_PRINTLN("-> STATUS: USB Charging Detected (GPIO16 LOW)");
  }
  
  // Auto shutdown logic (only when not charging on USB)
  if (!isUSBPlugged) {
    // 1. Boot low voltage check: if booted low, shutdown after 15 seconds
    if (bootedLow && millis() >= 15000) {
      DBG_PRINTLN(F("CRITICAL BATTERY: Booted with low voltage, shutting down after 15s..."));
      powerOffDevice();
    }
    // 2. Normal running check: shutdown if voltage is below 3.00V
    // We check millis() >= 15000 to allow voltage to stabilize and avoid false startup shutdowns if it didn't boot low
    if (millis() >= 15000 && estimatedV < 3.00) {
      DBG_PRINTLN(F("CRITICAL BATTERY: Voltage below 3.00V, shutting down..."));
      powerOffDevice();
    }
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
  url.reserve(256); // Prevent multiple heap reallocations
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

void parseResponse(const String& payload) {
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

  // Built-in system clock is initialized from external hardware RTC or NTP
}

// Disabled filesystem tools removed (obsolete/unused)

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
  doc["wifi"]["use_secondary_first"] = useSecondaryFirst;
  
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

const char* mapTrendToString(int trend) {
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
  
  // Step 2: Login with AccountId to get SessionId
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
  return isDarkTheme ? GREEN : 0x03E0;
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
  
  if (showHarveyBallInfo || offlineMode) {
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
  uint16_t color = isDarkTheme ? GREEN : 0x03E0;
  
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

void drawTrendArrow(int x, int y, const String& direction, uint16_t color) {
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

// --- Historical Chart & Touch ---
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

  // 3. Draw Chart Line and Dots
  float barWidth = 325.0 / MAX_HISTORY;
  
  // First draw circles at all data points to keep them visible even if isolated
  for (int i = 0; i < historyCount; i++) {
    int x = chartRight - (int)(i * barWidth);
    int y = getY(bgHistory[i].sgv);
    uint16_t color = (bgHistory[i].sgv >= 70 && bgHistory[i].sgv <= 180) ? (isDarkTheme ? GREEN : 0x03E0) : ORANGE;
    gfx->fillCircle(x, y, 2, color);
  }

  // Draw connecting lines only if readings are <= 6 minutes (360 seconds) apart
  for (int i = 0; i < historyCount - 1; i++) {
    if (bgHistory[i].timestamp - bgHistory[i+1].timestamp > 360) {
      continue;
    }
    int x1 = chartRight - (int)(i * barWidth);
    int x2 = chartRight - (int)((i+1) * barWidth);
    int y1 = getY(bgHistory[i].sgv);
    int y2 = getY(bgHistory[i+1].sgv);
    uint16_t color = (bgHistory[i].sgv >= 70 && bgHistory[i].sgv <= 180) ? (isDarkTheme ? GREEN : 0x03E0) : ORANGE;
    
    // Thicker line (3 pixels)
    gfx->drawLine(x1, y1, x2, y2, color);
    gfx->drawLine(x1, y1+1, x2, y2+1, color);
    gfx->drawLine(x1, y1-1, x2, y2-1, color);
    // gfx->drawLine(x1, y1+2, x2, y2+2, color);
  }

  // 4. Scrubber Popup (3s Persistence)
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
  // 0% is mapped to 3.00V, triggering critical shutdown below 3.00V
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
  // Available in online mode only, with no placeholder gap when inactive
  bool showSpinner = isFetching && !offlineMode;
  if (showSpinner) {
    gfx->setTextColor(YELLOW);
    const char spinnerFrames[] = {'|', '/', '-', '\\'};
    char spinnerChar = spinnerFrames[(millis() / 150) % 4];
    int spinnerX = isTimerMode ? 85 : 90;
    gfx->setCursor(spinnerX, 7);
    gfx->print(spinnerChar);
    gfx->setTextColor(textColor);
  }
  
  // 3. Last Saved BG / Minimized BG
  bool showBG = false;
  int bgX = 110; // Default position for timer screen
  
  if (isTimerMode) {
    if (!offlineMode) {
      showBG = true;
      bgX = 110; // Fixed position to leave gap for spinner
    }
  } else {
    // Main dashboard screen
    if (offlineMode) {
      gfx->setTextColor(RED);
      gfx->setCursor(85, 7);
      gfx->print("LAST SAVED BG");
      gfx->setTextColor(textColor);
      // showBG remains false to completely hide the minimized value
    }
  }
  
  if (showBG && historyCount > 0) {
    BGReading latest = bgHistory[0];
    String sgvStr = (bgUnits == "mmol/L") ? String(latest.sgv / 18.0182, 1) : String(latest.sgv);
    
    // Draw BG value with its specific status color
    gfx->setTextColor(getBGColor(latest.sgv));
    gfx->setCursor(bgX, 7);
    gfx->print(sgvStr);
    
    // Draw Delta in standard textColor
    gfx->setTextColor(textColor);
    String deltaStr = formatDelta(latest.delta);
    int bgWidth = sgvStr.length() * 12; // At size 2, each char is 12px wide
    gfx->setCursor(bgX + bgWidth + 10, 7);
    gfx->printf("(%s)", deltaStr.c_str());
  }
  
  // 2. Battery & Charging Indicator
  int cursorX = 625; // Default start if no battery info
  
  if (currentBatteryPct >= 0) {
    uint16_t batColor = textColor;
    bool showBat = true;
    
    if (currentBatteryPct <= 5) {
      batColor = RED;
      if ((millis() / 500) % 2 != 0) {
        showBat = false;
      }
    }
    
    char batStr[32];
    if (debugMode) {
      sprintf(batStr, "%d%% (%.2fV)", currentBatteryPct, currentBatteryVoltage);
    } else {
      sprintf(batStr, "%d%%", currentBatteryPct);
    }
    int strWidth = strlen(batStr) * 12;
    cursorX = 625 - strWidth;
    
    int batLeftX = (wasUSBPlugged && !pwrBtn.pressed) ? (cursorX - 15) : cursorX;
    
    if (offlineMode) {
      gfx->setTextColor(RED);
      gfx->setCursor(batLeftX - 95, 7);
      gfx->print("OFFLINE");
      gfx->setTextColor(textColor);
    }

    if (showBat) {
      gfx->setTextColor(batColor);
      gfx->setCursor(cursorX, 7);
      gfx->print(batStr);
      
      if (wasUSBPlugged && !pwrBtn.pressed) {
        gfx->setCursor(cursorX - 15, 7); 
        gfx->print("+");
      }
      gfx->setTextColor(textColor); // Restore standard text color
    }
  } else {
    if (offlineMode) {
      gfx->setTextColor(RED);
      gfx->setCursor(625 - 84, 7);
      gfx->print("OFFLINE");
      gfx->setTextColor(textColor);
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

void logBoot(const String& msg) {
  DBG_PRINTLN(msg);
  if (!isBooting) return;

  bootLog += msg;
  bootLog += '\n';
  
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

  // Clear the display properly to prevent frozen message on next boot
  gfx->fillScreen(BLACK);
  gfx->flush();
  delay(50); // Give SPI a moment to finish

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

