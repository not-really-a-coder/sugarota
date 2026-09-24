#include "input.h"
#include "display.h"
#include "ui.h"
#include "net_client.h"
#include "audio.h"
#include "ble.h"
#include <ESPmDNS.h>
#include <Wire.h>

SensorQMI8658 qmi;
bool imuReady = false;

// Forward declarations
void powerOffDevice();
extern void logBoot(const String& msg);

static int lastRawX = -1;
static int lastRawY = -1;
static int touchConfidence = 0;

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

static unsigned long lastPwrReleaseTime = 0;
static int pwrClickCount = 0;
static int lastActiveBrightness = 76;
const unsigned long doubleClickWindowMs = 350;

void checkButton(ButtonState &btn, const char* name) {
  bool isPressed = (digitalRead(btn.pin) == LOW);
  
  if (isPressed && !btn.pressed) {
    if (isFindDeviceActive()) {
      stopFindDeviceAlert();
    }
    btn.pressed = true;
    btn.pressTime = millis();
    btn.handled = false;
  } else if (isPressed && btn.pressed) {
    if (!btn.handled) {
      unsigned long duration = millis() - btn.pressTime;
      if (btn.pin == PIN_PWR_BTN) {
        if (duration >= 2000) {
          Serial.printf("%s Button: LONG Press detected (Hold >= 2s)\n", name);
          btn.handled = true;
          pwrClickCount = 0; // Reset any pending double-click
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
          pwrClickCount++;
          lastPwrReleaseTime = millis();

          if (pwrClickCount == 2) {
            pwrClickCount = 0;
            Serial.println("PWR Button: DOUBLE Press detected -> Toggle Display & Touch");

            if (brightnessLevel > 0) {
              // Screen is currently ON -> turn OFF
              lastActiveBrightness = brightnessLevel;
              brightnessLevel = 0;
              screenManuallyOff = true;
              setBrightness(0);
              Serial.printf("Screen & Touch -> OFF (Saved brightness: %d)\n", lastActiveBrightness);
            } else {
              // Screen is currently OFF -> turn ON
              screenManuallyOff = false;
              brightnessLevel = (lastActiveBrightness > 0) ? lastActiveBrightness : 76;
              setBrightness(brightnessLevel);
              updateUI();
              Serial.printf("Screen & Touch -> ON (Restored brightness: %d)\n", brightnessLevel);
            }
          }
        }
      } else if (btn.pin == PIN_BOOT_BTN) {
        if (duration > 50 && duration < 1500) {
          Serial.printf("%s Button: SHORT Press detected (Release)\n", name);
          if (SugarotaBLE::getInstance().isConnected()) {
            DBG_PRINTLN("ACTION: Force Data Refresh via BLE Companion");
            isFetching = true;
            fetchStartTime = millis();
            updateUI();
            SugarotaBLE::getInstance().notifyStatus(currentBatteryPct, wasUSBPlugged, SUGAROTA_VERSION, brightnessLevel, isDarkTheme ? 1 : 0, true);
          } else if (connectionMode != "BLE_ONLY") {
            DBG_PRINTLN("ACTION: Force Data Refresh via Wi-Fi");
            fetchData();
          } else {
            DBG_PRINTLN("ACTION: Force Data Refresh (Disabled in BLE_ONLY Mode)");
          }
        }
      }
    }
  }
}

void checkBootButtons() {
  bool isPressed = (digitalRead(pwrBtn.pin) == LOW);
  if (isPressed && !pwrBtn.pressed) {
    pwrBtn.pressed = true;
    pwrBtn.pressTime = millis();
    pwrBtn.handled = false;
  } else if (isPressed && pwrBtn.pressed) {
    if (!pwrBtn.handled) {
      if (millis() - pwrBtn.pressTime >= 2000) {
        Serial.println("PWR Button: Boot LONG Press -> Power Off");
        pwrBtn.handled = true;
        pwrClickCount = 0;
        deviceOn = false;
        powerOffDevice();
        return;
      }
    }
  } else if (!isPressed && pwrBtn.pressed) {
    pwrBtn.pressed = false;
    unsigned long duration = millis() - pwrBtn.pressTime;
    if (!pwrBtn.handled) {
      if (duration > 50 && duration < 2000) {
        pwrBtn.handled = true;
        Serial.println("PWR Button: Boot SHORT Press -> Cycle Brightness");
        cycleBrightness();
        lastActiveBrightness = brightnessLevel;
      }
    }
  }
}

void checkButtons() {
  checkButton(pwrBtn, "PWR");
  checkButton(bootBtn, "BOOT");

  // Check if a single short press timed out without a second click
  if (pwrClickCount == 1 && (millis() - lastPwrReleaseTime > doubleClickWindowMs)) {
    pwrClickCount = 0;
    Serial.println("PWR Button: SINGLE Press detected");

    if (brightnessLevel <= 0) {
      // Waking screen from off via single press
      screenManuallyOff = false;
      brightnessLevel = (lastActiveBrightness > 0) ? lastActiveBrightness : 76;
      setBrightness(brightnessLevel);
      updateUI();
    } else {
      cycleBrightness();
      lastActiveBrightness = brightnessLevel;
    }
    Serial.printf("Brightness: %d, ManualOff: %d\n", brightnessLevel, screenManuallyOff);
  }
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
  static int touchStartX = -1;
  static int touchStartY = -1;
  static unsigned long touchStartTime = 0;
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

    // Track touch start point for gesture recognition
    if (touchConfidence == 3) {
      touchStartX = tx;
      touchStartY = ty;
      touchStartTime = millis();
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

    // --- On Countdown Alarm Screen Touch Handling ---
    if (currentScreen == SCREEN_COUNTDOWN_ALARM) {
      // 1) Digits HH:MM (Tap upper half to increment, lower half to decrement)
      // Digit positions: H1: 30-78, H2: 85-133, M1: 165-213, M2: 220-268; Y: 62-157
      int dX[4] = { 30, 85, 165, 220 };
      for (int i = 0; i < 4; i++) {
        if (touchX >= dX[i] && touchX <= dX[i] + 48 && touchY >= 62 && touchY <= 157) {
          bool isUp = (touchY < 62 + 95 / 2);
          int delta = isUp ? 1 : -1;
          if (i == 0) {
            // Tens of hours (0..2)
            int hTens = (alarmSetHours / 10 + delta + 3) % 3;
            alarmSetHours = hTens * 10 + (alarmSetHours % 10);
            if (alarmSetHours > 23) alarmSetHours = 23;
          } else if (i == 1) {
            // Ones of hours (0..9)
            int hTens = alarmSetHours / 10;
            int hOnes = (alarmSetHours % 10 + delta + 10) % 10;
            alarmSetHours = hTens * 10 + hOnes;
            if (alarmSetHours > 23) alarmSetHours = 23;
          } else if (i == 2) {
            // Tens of minutes (0..5)
            int mTens = (alarmSetMinutes / 10 + delta + 6) % 6;
            alarmSetMinutes = mTens * 10 + (alarmSetMinutes % 10);
          } else if (i == 3) {
            // Ones of minutes (0..9)
            int mTens = alarmSetMinutes / 10;
            int mOnes = (alarmSetMinutes % 10 + delta + 10) % 10;
            alarmSetMinutes = mTens * 10 + mOnes;
          }
          waitForRelease = true;
          updateUI();
          return;
        }
      }

      // 2) Record Button: x=300..450, y=62..106
      if (touchX >= 300 && touchX <= 450 && touchY >= 62 && touchY <= 106) {
        if (isRecordingAudio) {
          // Stop recording
          isRecordingAudio = false;
          stopVoiceRecording();
        } else {
          // Start recording (deletes previous message automatically)
          deleteVoiceRecording();
          if (startVoiceRecording()) {
            isRecordingAudio = true;
            audioRecordingStartTime = millis();
          }
        }
        waitForRelease = true;
        updateUI();
        return;
      }

      // 3) Play Voice / Test Msg Button: x=300..450, y=113..157
      if (touchX >= 300 && touchX <= 450 && touchY >= 113 && touchY <= 157) {
        if (hasVoiceRecording() && !isRecordingAudio) {
          playVoiceRecording();
        }
        waitForRelease = true;
        return;
      }

      // 4) Start / Stop Button: x=470..615, y=62..106
      if (touchX >= 470 && touchX <= 615 && touchY >= 62 && touchY <= 106) {
        if (isAlarmRunning) {
          // Stop running alarm
          isAlarmRunning = false;
          alarmEndMillis = 0;
        } else {
          // Start countdown timer
          if (alarmSetHours > 0 || alarmSetMinutes > 0) {
            unsigned long totalSeconds = (unsigned long)alarmSetHours * 3600UL + (unsigned long)alarmSetMinutes * 60UL;
            alarmEndMillis = millis() + totalSeconds * 1000UL;
            isAlarmRunning = true;
          }
        }
        waitForRelease = true;
        updateUI();
        return;
      }

      // 5) Reset Button: x=470..615, y=113..157
      if (touchX >= 470 && touchX <= 615 && touchY >= 113 && touchY <= 157) {
        isAlarmRunning = false;
        alarmEndMillis = 0;
        alarmSetHours = 0;
        alarmSetMinutes = 0;
        waitForRelease = true;
        updateUI();
        return;
      }
    }

    // --- On Main Screen Touch Handling ---
    if (currentScreen == SCREEN_MAIN) {
      if (touchX >= 15 && touchX <= 65 && touchY >= 60 && touchY <= 110) {
        showHarveyBallInfo = true;
        lastHarveyBallTapTime = millis();
        updateUI(); 
      }

      if (touchX >= 300 && touchX <= 640 && touchY > 40) {
        lastScrubberX = touchX;
        lastScrubberTouchTime = millis();
      }
    }

  } else {
    // Touch released
    if (isTouching) {
      // Check horizontal swipe gestures
      int deltaX = lastRawX - touchStartX;
      int deltaY = abs(lastRawY - touchStartY);

      // Swipe Left (deltaX < -70, predominantly horizontal) -> Main Screen to Alarm Screen
      if (currentScreen == SCREEN_MAIN && deltaX < -70 && deltaY < 80) {
        currentScreen = SCREEN_COUNTDOWN_ALARM;
        DBG_PRINTLN("NAV: Swiped left -> SCREEN_COUNTDOWN_ALARM");
      }
      // Swipe Right (deltaX > 70, predominantly horizontal) -> Alarm Screen to Main Screen
      else if (currentScreen == SCREEN_COUNTDOWN_ALARM && deltaX > 70 && deltaY < 80) {
        currentScreen = SCREEN_MAIN;
        DBG_PRINTLN("NAV: Swiped right -> SCREEN_MAIN");
      }

      isTouching = false;
      lastRawX = -1; lastRawY = -1;
      touchStartX = -1; touchStartY = -1;
      updateUI();
    }
    touchConfidence = 0;
    lastRawX = -1; lastRawY = -1;
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
      
      // Calculate dynamic acceleration magnitude to detect movement/shaking
      float magnitude = sqrt(x*x + y*y + z*z);
      bool isMoving = (abs(magnitude - 1.0) > 0.25);

      // 2. Timer Mode (Rotated Landscape)
      // Regular Landscape has buttons on top (y < -0.4).
      // Rotated Landscape has buttons on bottom (y > 0.4).
      // Hysteresis: enter when y > 0.45; exit only when rotated back upright (y < 0.20)
      bool isRotatedLandscape = isTimerMode ? (y > 0.20 && !currentZState) : (y > 0.45 && !currentZState);
      
      // Debounce orientation transition to prevent random restarts when tilted back or moved
      static unsigned long orientTransitionStartTime = 0;
      static bool pendingOrientation = false;
      
      if (!isMoving && !isFetching) {
        if (isRotatedLandscape != isTimerMode) {
          if (!pendingOrientation) {
            pendingOrientation = true;
            orientTransitionStartTime = millis();
          } else if (millis() - orientTransitionStartTime >= 250) { // 250ms debounce
            pendingOrientation = false;
            if (isRotatedLandscape) {
              if (brightnessLevel > 0) {
                isTimerMode = true;
                isTimerStopped = false;
                lastBeepedMinute = 0;
                setScreenRotation(3); // 180° rotation for upside-down device
                timerStartTime = millis();
                timerElapsedMs = 0;
                DBG_PRINTLN("TIMER START: Device rotated 180 degrees");
                updateUI();
              }
            } else {
              isTimerMode = false;
              isTimerStopped = false;
              lastBeepedMinute = 0;
              setScreenRotation(1); // Restore normal landscape
              timerStartTime = 0;
              timerElapsedMs = 0;
              DBG_PRINTLN("TIMER STOP: Device rotated back upright");
              updateUI();
            }
          }
        } else {
          pendingOrientation = false;
        }
      } else {
        pendingOrientation = false;
      }
      
      // Update timer elapsed time and play predefined beeping pattern
      if (isTimerMode) {
        if (!isTimerStopped) {
          timerElapsedMs = millis() - timerStartTime;
          int currentMinute = timerElapsedMs / 60000;
          if (currentMinute > lastBeepedMinute && currentMinute <= 10) {
            lastBeepedMinute = currentMinute;
            if (currentMinute >= 1 && currentMinute <= 4) {
              // 1 to 4: N short beeps
              playBeeps(0, currentMinute);
            } else if (currentMinute >= 5 && currentMinute <= 9) {
              // 5 to 9: 1 long beep + (N-5) short beeps
              playBeeps(1, currentMinute - 5);
            } else if (currentMinute == 10) {
              // 10: 2 long beeps and stop timer
              playBeeps(2, 0);
              isTimerStopped = true;
              timerElapsedMs = 600000;
            }
          }
          updateUI();
        } else {
          // Timer stopped at 10m: refresh at 500ms for blinking display
          static unsigned long lastStoppedBlinkTime = 0;
          if (millis() - lastStoppedBlinkTime >= 500) {
            lastStoppedBlinkTime = millis();
            updateUI();
          }
        }
      }
      
      // 3. Shake Detection
      // To prevent accidental triggers and require an intentional, sustained shake (~1.5 seconds),
      // we track continuous shaking over an extended window requiring 8 shake samples.
      static unsigned long firstShakeTime = 0;
      static unsigned long lastShakeTime = 0;
      static unsigned long lastShakeTriggerTime = 0;
      static int shakeCount = 0;
      float totalAcc = abs(x) + abs(y) + abs(z);
      
      if (totalAcc > 2.5) {
        unsigned long now = millis();
        if (now - lastShakeTriggerTime > 1500) { // Cooldown between triggers
          if (now - lastShakeTime < 500) {
            shakeCount++;
          } else {
            shakeCount = 1;
            firstShakeTime = now;
          }
          lastShakeTime = now;
          
          // Require at least 8 shake peaks and at least 1200ms of sustained shaking (~1.5s total)
          if (shakeCount >= 8 && (now - firstShakeTime >= 1200)) {
            shakeCount = 0;
            firstShakeTime = 0;
            lastShakeTriggerTime = now;
            
            if (isConfigMode) {
              DBG_PRINTLN("SHAKE DETECTED: Exiting Config Mode...");
              playBeeps(0, 2);
              exitConfigMode();
            } else {
              DBG_PRINTLN("SHAKE DETECTED: Entering Config Mode...");
              isConfigMode = true;
              configModeStartTime = millis();
              
              SugarotaBLE::getInstance().enablePairingMode(true);

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
  }
}
