#include "input.h"
#include "display.h"
#include "ui.h"
#include "storage.h"
#include "net_client.h"
#include "audio.h"
#include "sugarota_ble.h"
#include <ESPmDNS.h>
#include <Wire.h>

SensorQMI8658 qmi;
bool imuReady = false;

// Forward declarations
void powerOffDevice();

static int lastRawX = -1;
static int lastRawY = -1;
static int touchConfidence = 0;
static unsigned long lastTouchStartTime = 0;
static bool isLongTapping = false;

void initInputs() {
  pinMode(PIN_PWR_BTN, INPUT_PULLUP);
  pinMode(PIN_BOOT_BTN, INPUT_PULLUP);

  Wire1.begin(TOUCH_SDA, TOUCH_SCL);
  
  if (qmi.begin(Wire, QMI8658_L_SLAVE_ADDRESS, I2C_SDA, I2C_SCL)) {
    qmi.configAccelerometer(SensorQMI8658::ACC_RANGE_4G, SensorQMI8658::ACC_ODR_250Hz, SensorQMI8658::LPF_MODE_0);
    qmi.enableAccelerometer();
    imuReady = true;
    DBG_PRINTLN("QMI8658 IMU Initialized");
  } else {
    DBG_PRINTLN("QMI8658 IMU Init Failed");
  }
}

void checkButton(ButtonState &btn, const char* name) {
  bool isPressed = (digitalRead(btn.pin) == LOW);
  
  if (isPressed && !btn.pressed) {
    btn.pressed = true;
    btn.pressTime = millis();
    btn.handled = false;
    
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
          deviceOn = false;
        }
      } else if (btn.pin == PIN_BOOT_BTN) {
        if (duration >= 1500) {
          Serial.printf("%s Button: LONG Press detected (Hold >= 1.5s)\n", name);
          btn.handled = true;
          DBG_PRINTLN("ACTION: Toggle Theme");
          toggleTheme();
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
          if (SugarotaBLE::getInstance().isConnected()) {
            DBG_PRINTLN("ACTION: Force Data Refresh via BLE Companion");
            isFetching = true;
            updateUI();
            SugarotaBLE::getInstance().notifyStatus(currentBatteryPct, wasUSBPlugged, SUGAROTA_VERSION);
          } else if (!offlineMode) {
            DBG_PRINTLN("ACTION: Force Data Refresh via Wi-Fi");
            fetchData();
          } else {
            DBG_PRINTLN("ACTION: Force Data Refresh (Disabled in Offline Mode)");
          }
        }
      }
    }
  }
}

void checkButtons() {
  checkButton(pwrBtn, "PWR");
  checkButton(bootBtn, "BOOT");
}

bool readTouch(int &tx, int &ty) {
  uint8_t read_cmd[11] = {0xb5, 0xab, 0xa5, 0x5a, 0x00, 0x00, 0x00, 0x0e, 0x00, 0x00, 0x00};
  Wire1.beginTransmission(TOUCH_ADDR);
  for (int i = 0; i < 11; i++) Wire1.write(read_cmd[i]);
  if (Wire1.endTransmission() != 0) return false;
  
  Wire1.requestFrom((uint16_t)TOUCH_ADDR, (uint8_t)14, true);
  if (Wire1.available() >= 14) {
    Wire1.read();
    uint8_t num = Wire1.read();
    uint8_t x_h = Wire1.read();
    uint8_t x_l = Wire1.read();
    uint8_t y_h = Wire1.read();
    uint8_t y_l = Wire1.read();
    for (int i = 0; i < 8; i++) Wire1.read();
    
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
    if (tx < 0 || tx > 640 || ty < 0 || ty > 172) {
      touchConfidence = 0;
      return; 
    }
    
    if (lastRawX != -1 && (abs(tx - lastRawX) > 50 || abs(ty - lastRawY) > 50)) {
      lastRawX = tx; lastRawY = ty;
      touchConfidence = 1; 
      return; 
    }
    lastRawX = tx; lastRawY = ty;

    touchConfidence++;
    if (touchConfidence < 3) return; 

    touchX = tx;
    touchY = ty;
    isTouching = true;

    if (isShowingUnitDialog) {
      int w = 150; int h = 120;
      int dx = (640 - w) / 2;
      int dy = (172 - h) / 2;
      
      if (touchX > dx+10 && touchX < dx+70 && touchY > dy+80 && touchY < dy+110) {
        bgUnits = (bgUnits == UNIT_MGDL) ? UNIT_MMOLL : UNIT_MGDL;
        saveConfig();
        ESP.restart();
      }
      if (touchX > dx+80 && touchX < dx+140 && touchY > dy+80 && touchY < dy+110) {
        isShowingUnitDialog = false;
        isTouching = false; 
        waitForRelease = true;
        updateUI();
      }
      return;
    }

    if (isShowingPairingDialog) {
      int w = 220; int h = 130;
      int dx = (640 - w) / 2;
      int dy = (172 - h) / 2;
      
      if (touchX > dx+20 && touchX < dx+100 && touchY > dy+88 && touchY < dy+120) {
        SugarotaBLE::getInstance().confirmPairing(true);
        isShowingPairingDialog = false;
        isTouching = false;
        waitForRelease = true;
        updateUI();
      }
      if (touchX > dx+120 && touchX < dx+200 && touchY > dy+88 && touchY < dy+120) {
        SugarotaBLE::getInstance().confirmPairing(false);
        isShowingPairingDialog = false;
        isTouching = false;
        waitForRelease = true;
        updateUI();
      }
      return;
    }

    if (touchX >= 15 && touchX <= 65 && touchY >= 60 && touchY <= 110) {
      showHarveyBallInfo = true;
      lastHarveyBallTapTime = millis();
      updateUI(); 
    }

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
      if (isLongTapping) {
        isLongTapping = false;
        Serial.println("Touch: Long-tap cancelled (finger moved out of area)");
      }
    }

    if (touchX >= 300 && touchX <= 640 && touchY > 40) {
      lastScrubberX = touchX;
      lastScrubberTouchTime = millis();
    }

  } else {
    touchConfidence = 0;
    lastRawX = -1; lastRawY = -1;
    if (isLongTapping && (millis() - lastTouchStartTime > 100) && (millis() - lastTouchStartTime < 1000)) {
       if (millis() % 500 < 50) {
         isLongTapping = false;
         Serial.println("Touch: Release detected, long-tap timer reset.");
       }
    }
    
    if (isTouching) {
      isTouching = false;
      lastRawX = -1; lastRawY = -1;
      updateUI();
    }
  }
}

void pollIMU() {
  static unsigned long lastImuPoll = 0;
  int imuPollInterval = (isTimerMode && !isTimerStopped) ? 50 : 100;
  if (imuReady && !isBooting && deviceOn && !screenManuallyOff && (millis() - lastImuPoll > imuPollInterval)) {
    lastImuPoll = millis();
    float x, y, z;
    if (qmi.getAccelerometer(x, y, z)) {
      // 1. Face Down Logic
      bool currentZState = (z < -0.8); 
      static bool wasFaceDown = false;
      static int lastBrightness = 150;
      static unsigned long stateChangeTime = 0;
      static bool lastZState = false;
      
      if (currentZState != lastZState) {
        stateChangeTime = millis();
        lastZState = currentZState;
      }
      
      if (currentZState && !wasFaceDown && (millis() - stateChangeTime >= 100)) {
        wasFaceDown = true;
        DBG_PRINTLN("FACE DOWN: Sleep");
        if (brightnessLevel > 0) lastBrightness = brightnessLevel;
        setBrightness(0);
      } else if (!currentZState && wasFaceDown && (millis() - stateChangeTime >= 100)) {
        wasFaceDown = false;
        DBG_PRINTLN("PICKED UP: Wake");
        if (brightnessLevel == 0) {
          setBrightness(lastBrightness);
        }
      }
      
      // 2. Timer Mode
      bool isHorizontalRotated = (y > 0.8 && abs(x) < 0.4);
      if (isHorizontalRotated) {
        if (!isTimerMode) {
          isTimerMode = true;
          timerStartTime = millis();
          timerElapsedMs = 0;
          isTimerStopped = false;
          lastBeepedMinute = 0;
          DBG_PRINTLN("ROTATED HORIZONTAL: Start Timer Mode");
          playBeeps(1, 0);
          updateUI();
        } else {
          if (!isTimerStopped) {
            timerElapsedMs = millis() - timerStartTime;
            int currentMinute = timerElapsedMs / 60000;
            if (currentMinute > lastBeepedMinute) {
              lastBeepedMinute = currentMinute;
              playBeeps(0, currentMinute);
            }
          }
          static unsigned long lastTimerDraw = 0;
          if (millis() - lastTimerDraw > 100) {
            lastTimerDraw = millis();
            updateUI();
          }
        }
      } else {
        if (isTimerMode) {
          DBG_PRINTLN("ROTATED BACK: Stop/Exit Timer Mode");
          isTimerMode = false;
          isTimerStopped = false;
          playBeeps(0, 2);
          updateUI();
        }
      }
      
      // 3. Shake Detection
      static unsigned long lastShakeTime = 0;
      static int shakeCount = 0;
      float totalAcc = abs(x) + abs(y) + abs(z);
      
      if (totalAcc > 2.5) {
        if (millis() - lastShakeTime < 500) {
          shakeCount++;
        } else {
          shakeCount = 1;
        }
        lastShakeTime = millis();
        
        if (shakeCount >= 3) {
          shakeCount = 0;
          DBG_PRINTLN("SHAKE DETECTED: Entering Config Mode...");
          isConfigMode = true;
          configModeStartTime = millis();
          
          WiFi.disconnect();
          WiFi.mode(WIFI_AP);
          WiFi.softAP("Sugarota-Setup");
          
          if (!MDNS.begin("sugarota")) {
            DBG_PRINTLN("Error setting up MDNS responder!");
          } else {
            DBG_PRINTLN("mDNS responder started: http://sugarota.local");
            MDNS.addService("http", "tcp", 80);
          }
          
          playBeeps(0, 3);
          updateUI();
        }
      }
    }
  }
}
