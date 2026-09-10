#ifndef SUGAROTA_CONFIG_H
#define SUGAROTA_CONFIG_H

#include <Arduino.h>
#include <ArduinoJson.h>

#define SUGAROTA_VERSION "v0.09.10.3"

// --- Design System Colors (RGB565 matching Shadcn Zinc Dark & Telemetry Palette) ---
#define BLACK   0x0841  // #09090B (OLED Zinc Dark Canvas)
#define WHITE   0xF7BE  // #FAFAFA (Zinc-50 High-Contrast Foreground)
#define RED     0xEA28  // #EF4444 (Destructive / Urgent Hypo & Severe Hyperglycemia)
#define GREEN   0x072E  // #00E676 (Emerald-400 Primary / In-Target Euglycemia)
#define ORANGE  0xFBA2  // #F97316 (Warning / Borderline Hyperglycemia)
#define GRAY    0x8410  // #848484 (Muted / Stale Telemetry)
#define CYAN    0x07FF  // #00F0FF (Accent Cyan)
#define YELLOW  0xFFE0  // #FFE600 (Notice Yellow)
#define BLUE    0x3CFE  // #3B82F6 (Secondary Blue)
#define ZINC_BORDER 0x2104 // #27272A (Zinc-800 subtle container border & dividers)
#define DARK_RED    0x8800 // #880000 (High-contrast dark red)
#define LIGHT_PINK  0xFF3E // #FCE7F3 (Soft light pink button background)

// --- Hardware Pins ---
#define PIN_BL         8
#define PIN_PWR_BTN    16 // Power button
#define PIN_BOOT_BTN   0  // Boot button
#define PIN_BAT_ADC    4  // Battery ADC

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

// I2C Pins for Touch
#define TOUCH_SDA      17
#define TOUCH_SCL      18
#define TOUCH_ADDR     0x3B 

// --- Data Types & Enums ---
enum Provider { PROVIDER_NIGHTSCOUT, PROVIDER_DEXCOM };
enum BGUnits { UNIT_MGDL, UNIT_MMOLL };

#define MAX_HISTORY 48
struct BGReading {
  int sgv;
  long long timestamp;
  char direction[16];
  int delta;
};

struct ButtonState {
  int pin;
  bool pressed;
  unsigned long pressTime;
  bool handled;
};

// --- Shared Global Variables (Declared extern) ---
extern bool debugMode;
extern String primarySSID;
extern String primaryPass;
extern String secondarySSID;
extern String secondaryPass;
extern bool useSecondaryFirst;

extern String nsUrl;
extern String nsSecret;

extern String dexUser;
extern String dexPass;
extern String dexServer;
extern String dexSessionId;

extern Provider currentProvider;
extern BGUnits bgUnits;
inline const char* getBGUnitsStr() { return (bgUnits == UNIT_MMOLL) ? "mmol/L" : "mg/dL"; }

extern String connectionMode;
extern unsigned long pollIntervalSec;
inline unsigned long getFetchIntervalMs() {
  unsigned long sec = (pollIntervalSec >= 30 && pollIntervalSec <= 600) ? pollIntervalSec : 60;
  return sec * 1000UL;
}

extern String ntpServer;
extern long gmtOffset_sec;
extern int daylightOffset_sec;

extern bool deviceOn;
extern bool screenManuallyOff;
extern bool offlineMode;
extern bool isConfigMode;
extern unsigned long configModeStartTime;
extern bool isBooting;
extern String bootLog;
extern bool isFetching;
extern unsigned long fetchStartTime;

extern bool isDarkTheme;
extern int brightnessLevel;
extern unsigned long lastUiUpdate;

extern BGReading bgHistory[MAX_HISTORY];
extern int historyCount;
extern bool historyDirty;
extern unsigned long lastHistorySaveTime;
extern unsigned long lastDataFetch;

extern int currentBatteryPct;
extern float currentBatteryVoltage;
extern bool wasUSBPlugged;

// BLE states
extern bool isShowingPairingDialog;
extern uint32_t blePairingPin;
extern volatile bool bleUIUpdatePending;
extern volatile bool blePairingUpdatePending;
extern volatile bool bleGlucoseReceived;

// Unit Dialog state
extern bool isShowingUnitDialog;

// Timer Mode states
extern bool isTimerMode;
extern unsigned long timerStartTime;
extern unsigned long timerElapsedMs;
extern bool isTimerStopped;
extern int lastBeepedMinute;

// Touch & UI interaction state
extern bool isTouching;
extern int touchX;
extern int touchY;
extern unsigned long lastHarveyBallTapTime;
extern bool showHarveyBallInfo;
extern unsigned long lastScrubberTouchTime;
extern int lastScrubberX;
extern ButtonState pwrBtn;
extern ButtonState bootBtn;

// Debug Macros
#define DBG_PRINT(...) do { if(debugMode && Serial) Serial.print(__VA_ARGS__); } while(0)
#define DBG_PRINTLN(...) do { if(debugMode && Serial) Serial.println(__VA_ARGS__); } while(0)
#define DBG_PRINTF(...) do { if(debugMode && Serial) Serial.printf(__VA_ARGS__); } while(0)

#endif // SUGAROTA_CONFIG_H
