#include "input.h"
#include "display.h"
#include "ui.h"
#include "net_client.h"
#include "audio.h"
#include "ble.h"
#include "storage.h"
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
static int currentVerticalRotation = 0;

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

            if (isNightModeActive()) {
              triggerNightModeWake();
            }

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
          if (isNightModeActive()) {
            triggerNightModeWake();
          }
          if (SugarotaBLE::getInstance().isConnected()) {
            DBG_PRINTLN("ACTION: Force Data Refresh via BLE Companion");
            isFetching = true;
            fetchStartTime = millis();
            updateUI();
            SugarotaBLE::getInstance().notifyStatus(currentBatteryPct, wasUSBPlugged, SUGAROTA_VERSION, brightnessLevel, isDarkTheme ? 1 : 0, true, -1, true);
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

    // In Night Mode: snooze/stop any active no-data alert on touch, and extend wake window
    if (isNightModeActive()) {
      bool wasOff = (brightnessLevel == 0);
      triggerNightModeWake();
      if (wasOff) {
        // If screen was off, the tap is only used to wake up the screen (prevent accidental clicks)
        setBrightness(76);
        updateUI();
        waitForRelease = true;
        isTouching = false;
        return;
      }
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

    if (isConfigMode && configScreenState == CONFIG_SCREEN_PROMPT) {
      // Prompt Screen YES (x: 180..300, y: 118..158) / NO (x: 340..460, y: 118..158)
      if (touchX >= 180 && touchX <= 300 && touchY >= 118 && touchY <= 158) {
        // YES selected: switch to connecting screen, queue main loop Wi-Fi connect
        configScreenState = CONFIG_SCREEN_CONNECTING;
        configLog = "Initializing Wi-Fi...\n";
        pendingStartConfigWifi = true;
        isTouching = false;
        waitForRelease = true;
        updateUI();
        return;
      }
      if (touchX >= 340 && touchX <= 460 && touchY >= 118 && touchY <= 158) {
        // NO selected: return to glucose dashboard in config mode (BLE remains on)
        configScreenState = CONFIG_SCREEN_NONE;
        isTouching = false;
        waitForRelease = true;
        updateUI();
        return;
      }
      return;
    }

    if (isConfigMode && configScreenState == CONFIG_SCREEN_INFO) {
      // If Wi-Fi failed, TRY AGAIN button is at (x: 330..460, y: 120..158)
      if (WiFi.status() != WL_CONNECTED && touchX >= 330 && touchX <= 460 && touchY >= 120 && touchY <= 158) {
        configScreenState = CONFIG_SCREEN_CONNECTING;
        configLog = "Retrying Wi-Fi...\n";
        pendingStartConfigWifi = true;
        isTouching = false;
        waitForRelease = true;
        updateUI();
        return;
      }
      // Dismiss button on bottom right: (x: 480..610, y: 120..158)
      if (touchX >= 480 && touchX <= 610 && touchY >= 120 && touchY <= 158) {
        configScreenState = CONFIG_SCREEN_NONE;
        isTouching = false;
        waitForRelease = true;
        updateUI();
        return;
      }
      return;
    }

    if (isConfigMode && configScreenState == CONFIG_SCREEN_CONNECTING) {
      // Ignore background taps while actively connecting
      return;
    }

    // --- Vertical Mode Touch Handling ---
    if (isVerticalMode) {
      // In vertical rotation (0 or 2), the Canvas logical coordinates are 172 width x 640 height.
      // Rotation 0 (USB-C up): vx = 172 - touchY, vy = touchX.
      // Rotation 2 (USB-C down): 180 deg flip -> vx = touchY, vy = 640 - touchX.
      int vx = (currentVerticalRotation == 2) ? touchY : (172 - touchY);
      int vy = (currentVerticalRotation == 2) ? (640 - touchX) : touchX;

      if (verticalSubscreen == 0) {
        // Vertical Main Menu Options:
        // Option A: Find Phone (y: 80 to 220, x: 14 to 158)
        if (vx >= 14 && vx <= 158 && vy >= 80 && vy <= 220) {
          verticalSubscreen = 2;
          SugarotaBLE::getInstance().syncBondedPhonesWithActiveConnections();
          SugarotaBLE::getInstance().requestPhoneInfoRefresh();
          waitForRelease = true;
          isTouching = false;
          updateUI();
          return;
        }
        // Option B: Countdown alarm (y: 230 to 370, x: 14 to 158) - not linked yet
        if (vx >= 14 && vx <= 158 && vy >= 230 && vy <= 370) {
          playBeeps(0, 1);
          waitForRelease = true;
          isTouching = false;
          return;
        }
        // Option C: Settings (y: 380 to 520, x: 14 to 158)
        if (vx >= 14 && vx <= 158 && vy >= 380 && vy <= 520) {
          verticalSubscreen = 1;
          waitForRelease = true;
          isTouching = false;
          updateUI();
          return;
        }
      } else if (verticalSubscreen == 1) {
        // Settings Subscreen:
        // Full-width Bottom Back Button: x: 14 to 158, y: 560 to 625
        if (vx >= 14 && vx <= 158 && vy >= 560 && vy <= 625) {
          verticalSubscreen = 0;
          waitForRelease = true;
          isTouching = false;
          updateUI();
          return;
        }

        // Section 1: Speaker Volume Presets (2 rows x 2 cols)
        // pillW = 68, pillH = 36, gapX = 8, gapY = 8, startX = 14, volBaseY = 92
        int pillW = 68; int pillH = 36; int gapX = 8; int gapY = 8; int startX = 14;
        int volBaseY = 92;
        for (int i = 0; i < 4; i++) {
          int col = i % 2;
          int row = i / 2;
          int px = startX + col * (pillW + gapX);
          int py = volBaseY + row * (pillH + gapY);
          if (vx >= px - 3 && vx <= px + pillW + 3 && vy >= py - 3 && vy <= py + pillH + 3) {
            setVolume(i);
            saveConfig();
            playBeeps(0, 1);
            waitForRelease = true;
            isTouching = false;
            SugarotaBLE::getInstance().notifyStatus(currentBatteryPct, wasUSBPlugged, SUGAROTA_VERSION, brightnessLevel, isDarkTheme ? 1 : 0);
            updateUI();
            return;
          }
        }

        // Section 2: Display Brightness Presets (2 rows x 2 cols)
        // brightBaseY = 225
        int brightPresets[4] = {76, 153, 204, 255};
        int brightBaseY = 225;
        for (int i = 0; i < 4; i++) {
          int col = i % 2;
          int row = i / 2;
          int px = startX + col * (pillW + gapX);
          int py = brightBaseY + row * (pillH + gapY);
          if (vx >= px - 3 && vx <= px + pillW + 3 && vy >= py - 3 && vy <= py + pillH + 3) {
            setBrightness(brightPresets[i]);
            waitForRelease = true;
            isTouching = false;
            SugarotaBLE::getInstance().notifyStatus(currentBatteryPct, wasUSBPlugged, SUGAROTA_VERSION, brightnessLevel, isDarkTheme ? 1 : 0);
            updateUI();
            return;
          }
        }

        // Section 3: Night-mode Switch (y: 368 to 414, x: 14 to 158)
        if (vx >= 14 && vx <= 158 && vy >= 360 && vy <= 420) {
          nightModeEnabled = !nightModeEnabled;
          saveConfig();
          playBeeps(0, 1);
          waitForRelease = true;
          isTouching = false;
          SugarotaBLE::getInstance().notifyStatus(currentBatteryPct, wasUSBPlugged, SUGAROTA_VERSION, brightnessLevel, isDarkTheme ? 1 : 0);
          updateUI();
          return;
        }
      } else if (verticalSubscreen == 2) {
        // === Find Phone Subscreen Touch Handling ===
        // Full-width Bottom Back Button: x: 14 to 158, y: 560 to 625
        if (vx >= 14 && vx <= 158 && vy >= 560 && vy <= 625) {
          verticalSubscreen = 0;
          waitForRelease = true;
          isTouching = false;
          updateUI();
          return;
        }

        // Tap on individual phone cards (up to 3)
        // startY = 70, cardH = 130, gap = 15
        int startY = 70, cardH = 130, gap = 15;
        for (int i = 0; i < bondedPhoneCount && i < 3; i++) {
          int cy = startY + i * (cardH + gap);
          if (vx >= 14 && vx <= 158 && vy >= cy && vy <= cy + cardH) {
            if (!bondedPhones[i].connected) {
              // Phone is offline: visibly disabled, short error beep
              playBeeps(0, 1);
              waitForRelease = true;
              isTouching = false;
              return;
            }

            bool isAlerting = (activeFindPhoneAddr.length() > 0 && activeFindPhoneAddr.equalsIgnoreCase(bondedPhones[i].address));
            if (isAlerting) {
              // Currently ringing -> Interrupt / stop alert
              SugarotaBLE::getInstance().notifyStopAlert(bondedPhones[i].address);
              activeFindPhoneAddr = "";
              playBeeps(0, 1);
            } else {
              // Not ringing -> Trigger Find Phone alert
              activeFindPhoneAddr = bondedPhones[i].address;
              SugarotaBLE::getInstance().notifyFindPhoneAlert(bondedPhones[i].address);
              playBeeps(0, 2);
            }
            waitForRelease = true;
            isTouching = false;
            updateUI();
            return;
          }
        }
      }
      return;
    }

    // --- Main Screen Touch Handling ---
    if (touchX >= 15 && touchX <= 65 && touchY >= 60 && touchY <= 110) {
      showHarveyBallInfo = true;
      lastHarveyBallTapTime = millis();
      updateUI(); 
    }

    if (touchX >= 300 && touchX <= 640 && touchY > 40) {
      lastScrubberX = touchX;
      lastScrubberTouchTime = millis();
    }

  } else {
    touchConfidence = 0;
    lastRawX = -1; lastRawY = -1;
    
    if (isTouching) {
      isTouching = false;
      lastRawX = -1; lastRawY = -1;
      updateUI();
    }
  }
}

void pollIMU() {
  static unsigned long lastImuPoll = 0;
  int imuPollInterval = (isTimerMode && !isTimerStopped) ? 50 : 300;
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
      
      if (currentZState && !wasFaceDown && (millis() - stateChangeTime >= 300)) {
        wasFaceDown = true;
        DBG_PRINTLN("FACE DOWN: Sleep");
        if (brightnessLevel > 0) lastBrightness = brightnessLevel;
        setBrightness(0);
      } else if (!currentZState && wasFaceDown && (millis() - stateChangeTime >= 300)) {
        wasFaceDown = false;
        DBG_PRINTLN("PICKED UP: Wake");
        if (brightnessLevel == 0 && !isNightModeActive()) {
          setBrightness(lastBrightness);
        }
      }
      
      // Calculate dynamic acceleration magnitude to detect movement/shaking
      float magnitude = sqrt(x*x + y*y + z*z);
      bool isMoving = (abs(magnitude - 1.0) > 0.25);

      // 2. Orientation Detection:
      // A. Regular Landscape: buttons on top (y < -0.4).
      // B. Rotated Landscape (Timer Mode): buttons on bottom (y > 0.4).
      // C. Vertical Orientations (Hold >= 2 seconds):
      //    Vertical Right (x > 0.55), Vertical Left (x < -0.55).
      bool isRotatedLandscape = isTimerMode ? (y > 0.20 && !currentZState) : (y > 0.45 && !currentZState);
      bool isVerticalCandidate = !currentZState && (abs(x) > 0.55 && abs(y) < 0.40);
      
      static unsigned long verticalHoldStartTime = 0;
      static bool verticalTimerActive = false;
      static int candidateVerticalRot = 0;

      if (isVerticalCandidate && !isMoving && !isFetching) {
        int targetRot = (x > 0) ? 0 : 2;
        if (!verticalTimerActive || candidateVerticalRot != targetRot) {
          verticalTimerActive = true;
          candidateVerticalRot = targetRot;
          verticalHoldStartTime = millis();
        } else if (millis() - verticalHoldStartTime >= 2000) {
          if (!isVerticalMode || currentVerticalRotation != candidateVerticalRot) {
            isVerticalMode = true;
            verticalSubscreen = 0;
            currentVerticalRotation = candidateVerticalRot;
            setScreenRotation(currentVerticalRotation);
            DBG_PRINTF("VERTICAL MODE: Entered (rotation %d)\n", currentVerticalRotation);
            updateUI();
          }
        }
      } else {
        verticalTimerActive = false;
        // If device was in vertical mode, exit when rotated back to horizontal (abs(x) < 0.35)
        if (isVerticalMode && abs(x) < 0.35) {
          isVerticalMode = false;
          verticalSubscreen = 0;
          setScreenRotation(isTimerMode ? 3 : 1);
          DBG_PRINTLN("VERTICAL MODE: Exited (returned to horizontal)");
          updateUI();
        }
      }
      
      // Debounce orientation transition for Timer Mode
      static unsigned long orientTransitionStartTime = 0;
      static bool pendingOrientation = false;
      
      if (!isMoving && !isFetching && !isVerticalMode) {
        if (isRotatedLandscape != isTimerMode) {
          if (!pendingOrientation) {
            pendingOrientation = true;
            orientTransitionStartTime = millis();
          } else if (millis() - orientTransitionStartTime >= 300) { // 300ms debounce (1 sample hold)
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
      // To prevent accidental triggers and require an intentional, sustained shake (~1.5-2 seconds),
      // we track continuous shaking over an extended window requiring 4 shake samples at 300ms polling rate.
      static unsigned long firstShakeTime = 0;
      static unsigned long lastShakeTime = 0;
      static unsigned long lastShakeTriggerTime = 0;
      static int shakeCount = 0;
      float totalAcc = abs(x) + abs(y) + abs(z);
      
      if (totalAcc > 2.5) {
        unsigned long now = millis();
        if (now - lastShakeTriggerTime > 1500) { // Cooldown between triggers
          if (now - lastShakeTime < 700) {
            shakeCount++;
          } else {
            shakeCount = 1;
            firstShakeTime = now;
          }
          lastShakeTime = now;
          
          // Require at least 4 shake peaks and at least 1000ms of sustained shaking
          if (shakeCount >= 4 && (now - firstShakeTime >= 1000)) {
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

              bool isBleMode = (connectionMode == "BLE_ONLY" || SugarotaBLE::getInstance().isConnected());
              if (isBleMode) {
                configScreenState = CONFIG_SCREEN_PROMPT;
                playBeeps(0, 3);
                updateUI();
              } else {
                configScreenState = CONFIG_SCREEN_NONE;
                WiFi.disconnect();
                WiFi.mode(WIFI_AP);
                WiFi.softAP("Sugarota-Setup");
                
                MDNS.end();
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
}
