#include "display.h"

// Forward declaration
void updateUI();

static Arduino_DataBus *bus = new Arduino_ESP32QSPI(LCD_CS, LCD_PCLK, LCD_D0, LCD_D1, LCD_D2, LCD_D3);
static Arduino_GFX *physical_gfx = new Arduino_AXS15231B(bus, LCD_RST, 0, false, 172, 640);
Arduino_GFX *gfx = new Arduino_Canvas(172, 640, physical_gfx, 0, 0, 1);

void initDisplay() {
  if (!gfx->begin()) {
    DBG_PRINTLN("GFX Init Failed!");
    return;
  }
  gfx->fillScreen(isDarkTheme ? BLACK : WHITE);
  gfx->flush();
}

void setBrightness(int level) {
  brightnessLevel = level;
  // AXS15231B backlight is inverted (0 = max, 255 = off)
  int val = 255 - level;
  analogWrite(PIN_BL, val);
  
  if (level == 0) {
    gfx->displayOff();
  } else {
    gfx->displayOn();
  }
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
  if (sgv < 55 || sgv > 240) return RED;
  if (sgv < 70 || sgv > 180) return ORANGE;
  return isDarkTheme ? GREEN : 0x03E0;
}
