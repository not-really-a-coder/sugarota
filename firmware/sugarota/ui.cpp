#include "ui.h"
#include "sugarota_ble.h"
#include "qrcode.h"
#include <WiFi.h>

void updateUI() {
  if (isBooting) return;
  uint16_t bgColor = isDarkTheme ? BLACK : WHITE;
  
  gfx->fillScreen(bgColor);
  drawStatusBar();
  
  if (isTimerMode) {
    unsigned long elapsed = timerElapsedMs;
    unsigned long minutes = (elapsed / 60000) % 100;
    unsigned long seconds = (elapsed / 1000) % 60;
    unsigned long tenths = (elapsed / 100) % 10;
    
    char timerStr[16];
    sprintf(timerStr, "%02lu:%02lu.%lu", minutes, seconds, tenths);
    
    gfx->setTextColor(isDarkTheme ? GREEN : BLACK);
    gfx->setTextSize(10);
    int textWidth = 7 * 60 - 10;
    int tx = (640 - textWidth) / 2;
    int ty = 32 + (140 - 80) / 2;
    gfx->setCursor(tx, ty);
    
    if (isTimerStopped) {
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
    int dx = (640 - w) / 2;
    int dy = (172 - h) / 2;
    
    gfx->fillRoundRect(dx, dy, w, h, 8, GRAY);
    gfx->drawRoundRect(dx, dy, w, h, 8, WHITE);
    
    gfx->setTextColor(WHITE);
    gfx->setTextSize(2);
    gfx->setCursor(dx + 10, dy + 10);
    if (bgUnits == UNIT_MGDL) {
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
  } else if (isShowingPairingDialog) {
    int w = 220; int h = 130;
    int dx = (640 - w) / 2;
    int dy = (172 - h) / 2;
    
    gfx->fillRoundRect(dx, dy, w, h, 8, GRAY);
    gfx->drawRoundRect(dx, dy, w, h, 8, WHITE);
    
    gfx->setTextColor(WHITE);
    gfx->setTextSize(2);
    gfx->setCursor(dx + 12, dy + 10);
    gfx->print("Pair Phone?");

    char pinBuf[16];
    snprintf(pinBuf, sizeof(pinBuf), "%06u", blePairingPin);
    gfx->setTextColor(YELLOW);
    gfx->setTextSize(3);
    int pinWidth = 6 * 18;
    gfx->setCursor(dx + (w - pinWidth) / 2, dy + 35);
    gfx->print(pinBuf);

    gfx->setTextColor(WHITE);
    gfx->setTextSize(1);
    gfx->setCursor(dx + 15, dy + 68);
    gfx->print("Confirm code matches on phone");

    // YES Button
    gfx->fillRoundRect(dx + 20, dy + 88, 80, 32, 4, GREEN);
    gfx->setTextColor(BLACK);
    gfx->setTextSize(2);
    gfx->setCursor(dx + 42, dy + 96);
    gfx->print("YES");

    // NO Button
    gfx->fillRoundRect(dx + 120, dy + 88, 80, 32, 4, RED);
    gfx->setTextColor(WHITE);
    gfx->setTextSize(2);
    gfx->setCursor(dx + 148, dy + 96);
    gfx->print("NO");
  }
  
  gfx->flush();
}

void drawHarveyBall(int x, int y, int radius, long long timestamp) {
  long long now = time(NULL);
  int diffMin = 0;
  if (now > 1700000000LL && timestamp > 1700000000LL) {
    diffMin = (int)((now - timestamp) / 60);
    if (diffMin < 0) diffMin = 0;
  }
  
  uint16_t color = isDarkTheme ? GREEN : 0x03E0;
  if (diffMin >= 15) color = RED;
  else if (diffMin >= 6) color = ORANGE;

  gfx->drawCircle(x, y, radius, color);
  gfx->drawCircle(x, y, radius-1, color);
  
  if (diffMin > 0) {
    if (diffMin >= 5) {
      gfx->fillCircle(x, y, radius, color);
    } else {
      float startAngle = 270.0;
      float endAngle = startAngle + (72.0 * diffMin);
      gfx->fillArc(x, y, radius, 0, startAngle, endAngle, color);
    }
  }
}

void drawTrendArrow(int x, int y, const String& direction, uint16_t color) {
  if (direction == "SingleUp") {
    int pSize = 8;
    int startX = x - 4; 
    int startY = y + 18; 
    for (int i = 0; i < 6; i++) {
      gfx->fillRect(startX, startY - i*pSize, pSize, pSize, color);
    }
    gfx->fillRect(startX - 2*pSize, startY - 4*pSize, pSize, pSize, color);
    gfx->fillRect(startX - 1*pSize, startY - 5*pSize, pSize, pSize, color);
    gfx->fillRect(startX,           startY - 6*pSize, pSize, pSize, color);
    gfx->fillRect(startX + 1*pSize, startY - 5*pSize, pSize, pSize, color);
    gfx->fillRect(startX + 2*pSize, startY - 4*pSize, pSize, pSize, color);
  } else if (direction == "DoubleUp") {
    int pSize = 8;
    int sX1 = x - 12; 
    int sX2 = x + 4;  
    int startY = y + 18; 
    for (int i = 0; i < 7; i++) {
      gfx->fillRect(sX1, startY - i*pSize, pSize, pSize, color);
      gfx->fillRect(sX2, startY - i*pSize, pSize, pSize, color);
    }
    gfx->fillRect(sX1 - 2*pSize, startY - 4*pSize, pSize, pSize, color); 
    gfx->fillRect(sX2 + 2*pSize, startY - 4*pSize, pSize, pSize, color); 
    gfx->fillRect(sX1 - 1*pSize, startY - 5*pSize, pSize, pSize, color); 
    gfx->fillRect(x - 4,           startY - 5*pSize, pSize, pSize, color);
    gfx->fillRect(sX2 + 1*pSize, startY - 5*pSize, pSize, pSize, color); 
  } else if (direction == "FortyFiveUp") {
    int pSize = 8;
    int startX = x - 20;
    int startY = y + 10;
    for (int i = 0; i < 5; i++) {
      gfx->fillRect(startX + i*pSize, startY - i*pSize, pSize, pSize, color);
    }
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
    int startY = y - 6;
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
    for (int i = 0; i < 5; i++) {
      gfx->fillRect(startX + i*pSize, startY + i*pSize, pSize, pSize, color);
    }
    gfx->fillRect(startX + 1*pSize, startY + 4*pSize, pSize, pSize, color); 
    gfx->fillRect(startX + 2*pSize, startY + 4*pSize, pSize, pSize, color); 
    gfx->fillRect(startX + 3*pSize, startY + 4*pSize, pSize, pSize, color);
    gfx->fillRect(startX + 4*pSize, startY + 4*pSize, pSize, pSize, color); 
    gfx->fillRect(startX + 4*pSize, startY + 3*pSize, pSize, pSize, color); 
    gfx->fillRect(startX + 4*pSize, startY + 2*pSize, pSize, pSize, color);
    gfx->fillRect(startX + 4*pSize, startY + 1*pSize, pSize, pSize, color);
  } else if (direction == "SingleDown") {
    int pSize = 8;
    int startX = x - 4;
    int startY = y - 30;
    for (int i = 0; i < 6; i++) {
      gfx->fillRect(startX, startY + i*pSize, pSize, pSize, color);
    }
    gfx->fillRect(startX - 2*pSize, startY + 4*pSize, pSize, pSize, color);
    gfx->fillRect(startX - 1*pSize, startY + 5*pSize, pSize, pSize, color);
    gfx->fillRect(startX,           startY + 6*pSize, pSize, pSize, color);
    gfx->fillRect(startX + 1*pSize, startY + 5*pSize, pSize, pSize, color);
    gfx->fillRect(startX + 2*pSize, startY + 4*pSize, pSize, pSize, color);
  } else if (direction == "DoubleDown") {
    int pSize = 8;
    int sX1 = x - 12; 
    int sX2 = x + 4;  
    int startY = y - 30;
    for (int i = 0; i < 7; i++) {
      gfx->fillRect(sX1, startY + i*pSize, pSize, pSize, color);
      gfx->fillRect(sX2, startY + i*pSize, pSize, pSize, color);
    }
    gfx->fillRect(sX1 - 2*pSize, startY + 4*pSize, pSize, pSize, color); 
    gfx->fillRect(sX2 + 2*pSize, startY + 4*pSize, pSize, pSize, color); 
    gfx->fillRect(sX1 - 1*pSize, startY + 5*pSize, pSize, pSize, color); 
    gfx->fillRect(x - 4,           startY + 5*pSize, pSize, pSize, color);
    gfx->fillRect(sX2 + 1*pSize, startY + 5*pSize, pSize, pSize, color); 
  }
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

  drawHarveyBall(40, 85, 16, bgHistory[0].timestamp);
  
  if (showHarveyBallInfo || offlineMode) {
      long long now = time(NULL);
      int diffMin = 0;
      char ageStr[16];
      if (now > 1700000000LL && bgHistory[0].timestamp > 1700000000LL) {
        diffMin = (int)((now - bgHistory[0].timestamp) / 60);
        if (diffMin < 0) diffMin = 0;
        if (diffMin == 0) strcpy(ageStr, "Now");
        else if (diffMin > 99) strcpy(ageStr, ">99m ago");
        else snprintf(ageStr, sizeof(ageStr), "%dm ago", diffMin);
      } else {
        strcpy(ageStr, "--");
      }
      
      gfx->setTextSize(1);
      gfx->setTextColor(isDarkTheme ? CYAN : BLUE);
      int textX = 40 - (strlen(ageStr) * 6 / 2);
      gfx->setCursor(textX, 110);
      gfx->print(ageStr);
  }

  String sgvStr = formatBG(latest.sgv);
  int numChars = sgvStr.length();
  int sgvX;
  if (bgUnits == UNIT_MMOLL) {
    sgvX = (numChars >= 4) ? 68 : 92;
  } else {
    sgvX = (numChars >= 3) ? 65 : 85;
  }
  
  gfx->setTextColor(bgValColor);
  gfx->setTextSize(8); 
  gfx->setCursor(sgvX, 60);

  if (bgUnits == UNIT_MMOLL) {
    int dotIdx = sgvStr.indexOf('.');
    if (dotIdx > 0) {
      String intPart = sgvStr.substring(0, dotIdx);
      String decPart = sgvStr.substring(dotIdx + 1);
      
      gfx->print(intPart);
      int dotX = gfx->getCursorX();
      int dotY = 104;
      gfx->fillRect(dotX + 2, dotY, 8, 8, bgValColor);
      gfx->setCursor(dotX + 14, 60);
      gfx->print(decPart);
    } else {
      gfx->print(sgvStr);
    }
  } else {
    gfx->print(sgvStr);
  }

  int arrowX = (bgUnits == UNIT_MMOLL) ? 265 : 255;
  drawTrendArrow(arrowX, 90, latest.direction, bgValColor);

  gfx->setTextColor(isDarkTheme ? WHITE : BLACK);
  gfx->setTextSize(3);
  int deltaX = (bgUnits == UNIT_MMOLL) ? 55 : 85;
  gfx->setCursor(deltaX, 135);
  gfx->printf("%s %s", formatDelta(latest.delta).c_str(), getBGUnitsStr());
}

void drawHistoryChart() {
  if (historyCount < 2) return;

  int chartX = 300; 
  int chartRight = 625;
  int chartY = 50;
  int chartWidth = 325;
  int chartHeight = 110;
  
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

  int visualIndex[MAX_HISTORY];
  visualIndex[0] = 0;
  for (int i = 1; i < historyCount; i++) {
    if (bgHistory[i-1].timestamp - bgHistory[i].timestamp > 360) {
      visualIndex[i] = visualIndex[i-1] + 3;
    } else {
      visualIndex[i] = visualIndex[i-1] + 1;
    }
  }

  int y180 = getY(180);
  int y70 = getY(70);
  float barWidth = 325.0 / MAX_HISTORY;
  
  int oldestX = chartRight - (int)(visualIndex[historyCount - 1] * barWidth);
  if (oldestX < chartX) oldestX = chartX;
  gfx->fillRect(oldestX, y180, (chartRight - oldestX) + 1, y70 - y180, isDarkTheme ? 0x2104 : 0xEF7D);

  for (int i = 0; i < historyCount - 1; i++) {
    long timeDiff = bgHistory[i].timestamp - bgHistory[i+1].timestamp;
    if (timeDiff > 360) {
      int xRight = chartRight - (int)(visualIndex[i] * barWidth);
      int xLeft = chartRight - (int)(visualIndex[i+1] * barWidth);
      
      if (xRight < chartX) continue;
      if (xLeft < chartX) continue;
      
      int gapWidth = xRight - xLeft;
      if (gapWidth > 0) {
        uint16_t gapColor = isDarkTheme ? 0xFDF7 : 0xFDD0;
        gfx->fillRect(xLeft, chartY, gapWidth, chartHeight, gapColor);
        
        int diffMin = timeDiff / 60;
        String vMsg = String(diffMin) + "M DATA GAP";
        
        uint8_t curRot = gfx->getRotation();
        uint8_t textRot = (curRot + 3) % 4;
        
        int center_x_land = xLeft + gapWidth / 2;
        int center_y_land = chartY + chartHeight / 2;
        
        int textW = vMsg.length() * 6;
        int textH = 8;
        int landscapeHeight = gfx->height();
        
        int center_px = landscapeHeight - center_y_land;
        int center_py = center_x_land;
        
        int start_px = center_px - textW / 2;
        int start_py = center_py - textH / 2;
        
        gfx->setRotation(textRot);
        gfx->setTextColor(RED);
        gfx->setTextSize(1);
        gfx->setCursor(start_px, start_py);
        gfx->print(vMsg);
        gfx->setRotation(curRot);
      }
    }
  }

  for (int i = 0; i < historyCount; i++) {
    int x = chartRight - (int)(visualIndex[i] * barWidth);
    if (x < chartX) continue;
    int y = getY(bgHistory[i].sgv);
    uint16_t color = (bgHistory[i].sgv >= 70 && bgHistory[i].sgv <= 180) ? (isDarkTheme ? GREEN : 0x03E0) : ORANGE;
    gfx->fillCircle(x, y, 2, color);
  }

  for (int i = 0; i < historyCount - 1; i++) {
    if (bgHistory[i].timestamp - bgHistory[i+1].timestamp > 360) continue;
    
    int x1 = chartRight - (int)(visualIndex[i] * barWidth);
    int x2 = chartRight - (int)(visualIndex[i+1] * barWidth);
    
    if (x1 < chartX && x2 < chartX) continue;
    
    int y1 = getY(bgHistory[i].sgv);
    int y2 = getY(bgHistory[i+1].sgv);
    
    if (x2 < chartX) {
      if (x1 != x2) {
        y2 = y1 + (y2 - y1) * (chartX - x1) / (x2 - x1);
      }
      x2 = chartX;
    }
    
    uint16_t color = (bgHistory[i].sgv >= 70 && bgHistory[i].sgv <= 180) ? (isDarkTheme ? GREEN : 0x03E0) : ORANGE;
    
    gfx->drawLine(x1, y1, x2, y2, color);
    gfx->drawLine(x1, y1+1, x2, y2+1, color);
    gfx->drawLine(x1, y1-1, x2, y2-1, color);
  }

  bool showScrubber = isTouching && touchX >= chartX && touchX <= chartX + chartWidth;
  if (!showScrubber && (millis() - lastScrubberTouchTime < 3000) && lastScrubberX != -1) {
    showScrubber = true;
  }

  if (showScrubber) {
    int curX = isTouching ? touchX : lastScrubberX;
    
    int closestIdx = 0;
    int minDiff = 10000;
    for (int i = 0; i < historyCount; i++) {
      int pointX = chartRight - (int)(visualIndex[i] * barWidth);
      if (pointX < chartX) continue;
      int diff = abs(curX - pointX);
      if (diff < minDiff) {
        minDiff = diff;
        closestIdx = i;
      }
    }
    
    int dataIdx = closestIdx;
    curX = chartRight - (int)(visualIndex[dataIdx] * barWidth);
    
    BGReading r = bgHistory[dataIdx];
    int curY = getY(r.sgv);
    
    gfx->drawFastVLine(curX, chartY, chartHeight, isDarkTheme ? WHITE : BLACK);
    gfx->fillCircle(curX, curY, 4, isDarkTheme ? WHITE : BLACK);
    gfx->drawCircle(curX, curY, 5, isDarkTheme ? BLACK : WHITE);
    
    int boxW = 90;
    int boxH = 40;
    int boxX = curX - boxW; 
    if (boxX < chartX) boxX = chartX;
    int boxY = chartY - 45;
    
    gfx->fillRoundRect(boxX, boxY, boxW, boxH, 4, GRAY);
    gfx->drawRoundRect(boxX, boxY, boxW, boxH, 4, isDarkTheme ? WHITE : BLACK);
    
    gfx->setTextSize(2);
    gfx->setTextColor(BLACK);
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

void drawBluetoothIcon(int x, int y, uint16_t color) {
  // 5x7 bitmap scaled by 2 -> 10x14 pixels (matching the 14px font cap height and battery icon)
  // Rows:
  // 0: ..#.. (0x04)
  // 1: #.##. (0x16)
  // 2: .##.# (0x0D)
  // 3: ..##. (0x06)
  // 4: .##.# (0x0D)
  // 5: #.##. (0x16)
  // 6: ..#.. (0x04)
  static const uint8_t btBitmap[7] = { 0x04, 0x16, 0x0D, 0x06, 0x0D, 0x16, 0x04 };
  for (int row = 0; row < 7; row++) {
    uint8_t bits = btBitmap[row];
    for (int col = 0; col < 5; col++) {
      if (bits & (1 << (4 - col))) {
        gfx->fillRect(x + col * 2, y + row * 2, 2, 2, color);
      }
    }
  }
}

void drawWiFiIcon(int x, int y, uint16_t color) {
  // 13x11 pixelated Wi-Fi signal icon (matching status bar height)
  // Top arc (row 0-1)
  gfx->fillRect(x + 3, y, 7, 2, color);
  gfx->fillRect(x + 1, y + 2, 2, 2, color);
  gfx->fillRect(x + 10, y + 2, 2, 2, color);
  
  // Middle arc (row 3-4)
  gfx->fillRect(x + 4, y + 4, 5, 2, color);
  gfx->fillRect(x + 3, y + 6, 2, 1, color);
  gfx->fillRect(x + 8, y + 6, 2, 1, color);
  
  // Base dot (row 8-9)
  gfx->fillRect(x + 5, y + 8, 3, 3, color);
}

void drawStatusBar() {
  uint16_t textColor = isDarkTheme ? WHITE : BLACK;
  gfx->setTextColor(textColor);
  gfx->setTextSize(2);
  
  struct tm timeinfo;
  bool gotTime = getLocalTime(&timeinfo, 10);
  if (!gotTime) {
    time_t now = time(NULL);
    localtime_r(&now, &timeinfo);
  }

  char timeStr[10];
  sprintf(timeStr, "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
  gfx->setCursor(15, 7);
  gfx->print(timeStr);
  
  bool showSpinner = isFetching && !offlineMode;
  if (showSpinner) {
    gfx->setTextColor(ORANGE);
    const char spinnerFrames[] = {'|', '/', '-', '\\'};
    char spinnerChar = spinnerFrames[(millis() / 150) % 4];
    int spinnerX = isTimerMode ? 85 : 90;
    gfx->setCursor(spinnerX, 7);
    gfx->print(spinnerChar);
    gfx->setTextColor(textColor);
  }
  
  bool showBG = false;
  int bgX = 110;
  
  if (isTimerMode) {
    if (!offlineMode) {
      showBG = true;
      bgX = 110;
    }
  } else {
    if (offlineMode) {
      gfx->setTextColor(RED);
      gfx->setCursor(85, 7);
      gfx->print("LAST SAVED BG");
      gfx->setTextColor(textColor);
    }
  }
  
  if (showBG && historyCount > 0) {
    BGReading latest = bgHistory[0];
    String sgvStr = (bgUnits == UNIT_MMOLL) ? String(latest.sgv / 18.0182, 1) : String(latest.sgv);
    
    gfx->setTextColor(getBGColor(latest.sgv));
    gfx->setCursor(bgX, 7);
    gfx->print(sgvStr);
    
    gfx->setTextColor(textColor);
    String deltaStr = formatDelta(latest.delta);
    int bgWidth = sgvStr.length() * 12;
    gfx->setCursor(bgX + bgWidth + 10, 7);
    gfx->printf("(%s)", deltaStr.c_str());
  }
  
  int cursorX = 625;
  
  if (currentBatteryPct >= 0) {
    uint16_t batColor = textColor;
    bool showBat = true;
    
    if (currentBatteryPct <= 5) {
      if (!debugMode) {
        batColor = textColor;
      } else {
        batColor = RED;
      }
      if ((millis() / 500) % 2 != 0) {
        showBat = false;
      }
    }
    
    int indicatorWidth = 0;
    char batStr[32] = "";
    
    if (debugMode) {
      sprintf(batStr, "%d%% (%.2fV)", currentBatteryPct, currentBatteryVoltage);
      int16_t x1, y1;
      uint16_t w, h;
      gfx->getTextBounds(batStr, 0, 0, &x1, &y1, &w, &h);
      indicatorWidth = w;
    } else {
      indicatorWidth = 31;
    }
    
    cursorX = (640 - 15) - indicatorWidth;
    
    bool isWifiActive = (WiFi.getMode() != WIFI_OFF && (WiFi.status() == WL_CONNECTED || isConfigMode || isFetching));
    int batLeftX = cursorX;
    if (wasUSBPlugged && !pwrBtn.pressed) batLeftX -= 15;
    if (SugarotaBLE::getInstance().isConnected()) batLeftX -= 16;
    if (isWifiActive) batLeftX -= 18;
    if (isConfigMode) batLeftX -= 15;
    
    if (offlineMode && !SugarotaBLE::getInstance().isConnected()) {
      gfx->setTextColor(RED);
      gfx->setCursor(batLeftX - 95, 7);
      gfx->print("OFFLINE");
      gfx->setTextColor(textColor);
    }

    if (showBat) {
      gfx->setTextColor(batColor);
      
      if (debugMode) {
        gfx->setCursor(cursorX, 7);
        gfx->print(batStr);
      } else {
        int batY = 7;
        gfx->drawRect(cursorX, batY, 28, 14, batColor);
        gfx->fillRect(cursorX + 28, batY + 4, 3, 6, batColor);
        
        int sections = 0;
        if (currentBatteryPct >= 80) sections = 5;
        else if (currentBatteryPct >= 60) sections = 4;
        else if (currentBatteryPct >= 40) sections = 3;
        else if (currentBatteryPct >= 20) sections = 2;
        else if (currentBatteryPct >= 6) sections = 1;
        
        for (int i = 0; i < sections; i++) {
          gfx->fillRect(cursorX + 2 + i * 5, batY + 2, 4, 10, batColor);
        }
      }
      
      int currentLeftX = cursorX;
      if (wasUSBPlugged && !pwrBtn.pressed) {
        currentLeftX -= 15;
        gfx->setCursor(currentLeftX, 7); 
        gfx->print("+");
      }

      if (SugarotaBLE::getInstance().isConnected()) {
        currentLeftX -= 16;
        uint16_t btColor = isDarkTheme ? CYAN : BLUE;
        drawBluetoothIcon(currentLeftX + 3, 7, btColor);
      }
      
      if (isWifiActive) {
        currentLeftX -= 18;
        uint16_t wifiColor = (WiFi.status() == WL_CONNECTED) ? (isDarkTheme ? GREEN : 0x03E0) : ORANGE;
        drawWiFiIcon(currentLeftX + 2, 9, wifiColor);
      }
      
      if (isConfigMode) {
        currentLeftX -= 15;
        gfx->setTextColor(ORANGE);
        gfx->setCursor(currentLeftX, 7);
        gfx->print("*");
        
        String ipMsg;
        if (WiFi.status() == WL_CONNECTED) {
          ipMsg = "IP: " + WiFi.localIP().toString();
        } else {
          ipMsg = "IP: Connecting...";
        }
        int16_t x1, y1;
        uint16_t w, h;
        gfx->getTextBounds(ipMsg.c_str(), 0, 0, &x1, &y1, &w, &h);
        
        int textX = (640 - w) / 2;
        gfx->setCursor(textX, 7);
        gfx->print(ipMsg);
        
        if (WiFi.status() == WL_CONNECTED) {
          String url = "http://" + WiFi.localIP().toString();
          
          QRCode qrcode;
          uint8_t qrcodeData[qrcode_getBufferSize(2)];
          qrcode_initText(&qrcode, qrcodeData, 2, 0, url.c_str());
          
          int qrSize = qrcode.size;
          int qrX = textX + w + 10;
          int qrY = 2;
          
          gfx->fillRect(qrX - 2, qrY - 2, qrSize + 4, qrSize + 4, WHITE);
          
          for (uint8_t y = 0; y < qrSize; y++) {
            for (uint8_t x = 0; x < qrSize; x++) {
              if (qrcode_getModule(&qrcode, x, y)) {
                gfx->drawPixel(qrX + x, qrY + y, BLACK);
              }
            }
          }
        }
      }
      gfx->setTextColor(textColor);
    }
  } else {
    if (offlineMode && !SugarotaBLE::getInstance().isConnected()) {
      gfx->setTextColor(RED);
      gfx->setCursor(625 - 84, 7);
      gfx->print("OFFLINE");
      gfx->setTextColor(textColor);
    }
  }
  
  gfx->drawFastHLine(0, 30, 640, GRAY);
}
