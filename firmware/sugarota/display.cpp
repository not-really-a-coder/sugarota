#include "display.h"
#include "config.h"

// Forward declaration
void updateUI();

// Reset pin is handled via hardware or TCA9554 EXIO5 on V2; pass -1 to driver so GPIO 21 (LCD_TE on V2) is not driven
static Arduino_DataBus *bus = new Arduino_ESP32QSPI(LCD_CS, LCD_PCLK, LCD_D0, LCD_D1, LCD_D2, LCD_D3);
static Arduino_GFX *physical_gfx = new Arduino_AXS15231B(bus, -1, 0, false, 172, 640);
Arduino_GFX *gfx = new Arduino_Canvas(172, 640, physical_gfx, 0, 0, 1);

void initDisplay() {
  if (!gfx->begin()) {
    DBG_PRINTLN("GFX Init Failed!");
    return;
  }
  gfx->setRotation(1);
  gfx->fillScreen(isDarkTheme ? BLACK : WHITE);
  gfx->flush();
}

void setScreenRotation(uint8_t r) {
  if (gfx) {
    gfx->setRotation(r);
  }
}

#include <Wire.h>

void updateBacklightPower(bool enable) {
  // On V2 hardware, EXIO_PIN_BL_EN (Bit 1 of TCA9554) enables the AP3032 boost converter.
  // We read the current TCA9554 output register (0x01), modify bit 1, and write it back.
  Wire.beginTransmission(TCA9554_ADDR);
  Wire.write(0x01); // Output port register
  if (Wire.endTransmission(false) == 0 && Wire.requestFrom((uint16_t)TCA9554_ADDR, (uint8_t)1) == 1) {
    uint8_t currentOut = Wire.read();
    if (enable) {
      currentOut |= EXIO_PIN_BL_EN;
    } else {
      currentOut &= ~EXIO_PIN_BL_EN;
    }
    Wire.beginTransmission(TCA9554_ADDR);
    Wire.write(0x01);
    Wire.write(currentOut);
    Wire.endTransmission();
  }
}

void setBrightness(int level) {
  brightnessLevel = level;
  // AXS15231B backlight is inverted (0 = max, 255 = off)
  int val = 255 - level;

  if (hwVersion == 2) {
    // On V2 hardware, backlight PWM is on GPIO 42
    analogWrite(PIN_BL_V2, val);
    // Ensure GPIO 8 (EXIO INT / Wi-Fi stability pin) is kept HIGH as required by Waveshare V2 hardware
    pinMode(PIN_BL_V1, OUTPUT);
    digitalWrite(PIN_BL_V1, HIGH);
    // Enable boost converter on V2 when screen is on
    updateBacklightPower(level > 0);
  } else {
    // On V1 hardware, backlight PWM is on GPIO 8
    analogWrite(PIN_BL_V1, val);
  }
  
  if (level == 0) {
    gfx->displayOff();
  } else {
    gfx->displayOn();
  }
}

void cycleBrightness() {
  if (brightnessLevel < 153) brightnessLevel = 153;
  else if (brightnessLevel < 204) brightnessLevel = 204;
  else if (brightnessLevel < 255) brightnessLevel = 255;
  else brightnessLevel = 76;

  setBrightness(brightnessLevel);
}

void toggleTheme() {
  isDarkTheme = !isDarkTheme;
  DBG_PRINTF("Theme changed: %s\n", isDarkTheme ? "DARK" : "LIGHT");
  updateUI();
}

String formatBG(int mgdl) {
  if (bgUnits == UNIT_MMOLL) {
    float mmol = mgdl / 18.0182;
    char buf[16];
    snprintf(buf, sizeof(buf), "%.1f", mmol);
    return String(buf);
  }
  return String(mgdl);
}

String formatDelta(int delta) {
  if (bgUnits == UNIT_MMOLL) {
    float mmol = delta / 18.0182;
    char buf[16];
    snprintf(buf, sizeof(buf), "%+.1f", mmol);
    return String(buf);
  }
  char buf[16];
  snprintf(buf, sizeof(buf), "%+d", delta);
  return String(buf);
}

uint16_t getBGColor(int sgv) {
  if (sgv <= 0) return GRAY;
  if (sgv < 55 || sgv >= 230) return RED;
  if (sgv < 70 || sgv > 180) return ORANGE;
  return isDarkTheme ? GREEN : 0x03E0;
}
