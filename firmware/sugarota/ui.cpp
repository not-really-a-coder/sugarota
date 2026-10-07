#include "ui.h"
#include "audio.h"
#include "ble.h"
#include "qrcode.h"
#include <WiFi.h>

void updateUI() {
  if (isBooting || screenManuallyOff || brightnessLevel == 0)
    return;

  if (isVerticalMode) {
    drawVerticalScreen();
    return;
  }

  uint16_t bgColor = isDarkTheme ? BLACK : WHITE;

  gfx->fillScreen(bgColor);
  drawStatusBar();

  if (isConfigMode && configScreenState != CONFIG_SCREEN_NONE) {
    if (configScreenState == CONFIG_SCREEN_PROMPT) {
      drawConfigPromptScreen();
      return;
    } else if (configScreenState == CONFIG_SCREEN_CONNECTING) {
      drawConfigConnectingScreen();
      return;
    } else if (configScreenState == CONFIG_SCREEN_INFO) {
      drawConfigInfoScreen();
      return;
    }
  }

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

  if (isShowingPairingDialog) {
    int w = 220;
    int h = 130;
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
    gfx->fillRoundRect(dx + 120, dy + 88, 80, 32, 4, LIGHT_PINK);
    gfx->setTextColor(DARK_RED);
    gfx->setTextSize(2);
    gfx->setCursor(dx + 148, dy + 96);
    gfx->print("NO");
  }

  // Night Mode No Data Alert Dialog (relative message on screen)
  if (isNightModeActive() && !nightModeAlertSnoozed && (millis() - lastDataFetch > 15 * 60 * 1000) && !isShowingPairingDialog) {
    int w = 340;
    int h = 90;
    int dx = (640 - w) / 2;
    int dy = (172 - h) / 2;

    gfx->fillRoundRect(dx, dy, w, h, 8, DARK_RED);
    gfx->drawRoundRect(dx, dy, w, h, 8, YELLOW);

    gfx->setTextColor(YELLOW);
    gfx->setTextSize(2);
    gfx->setCursor(dx + 20, dy + 16);
    gfx->print("! NO DATA UPDATE !");

    gfx->setTextColor(WHITE);
    gfx->setTextSize(2);
    gfx->setCursor(dx + 20, dy + 42);
    unsigned long mins = (millis() - lastDataFetch) / 60000;
    gfx->printf("Last update: %lum ago", mins);

    gfx->setTextSize(1);
    gfx->setTextColor(GRAY);
    gfx->setCursor(dx + 20, dy + 68);
    gfx->print("Tap screen to snooze alert");
  }

  gfx->flush();
}

void drawHarveyBall(int x, int y, int radius, long long timestamp) {
  long long now = time(NULL);
  int diffMin = 0;
  if (now > 1700000000LL && timestamp > 1700000000LL) {
    diffMin = (int)((now - timestamp) / 60);
    if (diffMin < 0)
      diffMin = 0;
  }

  uint16_t color = isDarkTheme ? GREEN : 0x03E0;
  if (diffMin >= 15)
    color = RED;
  else if (diffMin >= 6)
    color = ORANGE;

  gfx->drawCircle(x, y, radius, color);
  gfx->drawCircle(x, y, radius - 1, color);

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

void drawTrendArrow(int x, int y, const String &direction, uint16_t color) {
  if (direction == "SingleUp") {
    int pSize = 8;
    int startX = x - 4;
    int startY = y + 18;
    for (int i = 0; i < 6; i++) {
      gfx->fillRect(startX, startY - i * pSize, pSize, pSize, color);
    }
    gfx->fillRect(startX - 2 * pSize, startY - 4 * pSize, pSize, pSize, color);
    gfx->fillRect(startX - 1 * pSize, startY - 5 * pSize, pSize, pSize, color);
    gfx->fillRect(startX, startY - 6 * pSize, pSize, pSize, color);
    gfx->fillRect(startX + 1 * pSize, startY - 5 * pSize, pSize, pSize, color);
    gfx->fillRect(startX + 2 * pSize, startY - 4 * pSize, pSize, pSize, color);
  } else if (direction == "DoubleUp") {
    int pSize = 8;
    int sX1 = x - 12;
    int sX2 = x + 4;
    int startY = y + 18;
    for (int i = 0; i < 7; i++) {
      gfx->fillRect(sX1, startY - i * pSize, pSize, pSize, color);
      gfx->fillRect(sX2, startY - i * pSize, pSize, pSize, color);
    }
    gfx->fillRect(sX1 - 2 * pSize, startY - 4 * pSize, pSize, pSize, color);
    gfx->fillRect(sX2 + 2 * pSize, startY - 4 * pSize, pSize, pSize, color);
    gfx->fillRect(sX1 - 1 * pSize, startY - 5 * pSize, pSize, pSize, color);
    gfx->fillRect(x - 4, startY - 5 * pSize, pSize, pSize, color);
    gfx->fillRect(sX2 + 1 * pSize, startY - 5 * pSize, pSize, pSize, color);
  } else if (direction == "FortyFiveUp") {
    int pSize = 8;
    int startX = x - 20;
    int startY = y + 10;
    for (int i = 0; i < 5; i++) {
      gfx->fillRect(startX + i * pSize, startY - i * pSize, pSize, pSize,
                    color);
    }
    gfx->fillRect(startX + 1 * pSize, startY - 4 * pSize, pSize, pSize, color);
    gfx->fillRect(startX + 2 * pSize, startY - 4 * pSize, pSize, pSize, color);
    gfx->fillRect(startX + 3 * pSize, startY - 4 * pSize, pSize, pSize, color);
    gfx->fillRect(startX + 4 * pSize, startY - 4 * pSize, pSize, pSize, color);
    gfx->fillRect(startX + 4 * pSize, startY - 3 * pSize, pSize, pSize, color);
    gfx->fillRect(startX + 4 * pSize, startY - 2 * pSize, pSize, pSize, color);
    gfx->fillRect(startX + 4 * pSize, startY - 1 * pSize, pSize, pSize, color);
  } else if (direction == "Flat") {
    int pSize = 8;
    int startX = x - 36;
    int startY = y - 6;
    for (int i = 1; i < 7; i++) {
      gfx->fillRect(startX + i * pSize, startY, pSize, pSize, color);
    }
    gfx->fillRect(startX + 5 * pSize, startY - 2 * pSize, pSize, pSize, color);
    gfx->fillRect(startX + 6 * pSize, startY - 1 * pSize, pSize, pSize, color);
    gfx->fillRect(startX + 7 * pSize, startY, pSize, pSize, color);
    gfx->fillRect(startX + 6 * pSize, startY + 1 * pSize, pSize, pSize, color);
    gfx->fillRect(startX + 5 * pSize, startY + 2 * pSize, pSize, pSize, color);
  } else if (direction == "FortyFiveDown") {
    int pSize = 8;
    int startX = x - 20;
    int startY = y - 22;
    for (int i = 0; i < 5; i++) {
      gfx->fillRect(startX + i * pSize, startY + i * pSize, pSize, pSize,
                    color);
    }
    gfx->fillRect(startX + 1 * pSize, startY + 4 * pSize, pSize, pSize, color);
    gfx->fillRect(startX + 2 * pSize, startY + 4 * pSize, pSize, pSize, color);
    gfx->fillRect(startX + 3 * pSize, startY + 4 * pSize, pSize, pSize, color);
    gfx->fillRect(startX + 4 * pSize, startY + 4 * pSize, pSize, pSize, color);
    gfx->fillRect(startX + 4 * pSize, startY + 3 * pSize, pSize, pSize, color);
    gfx->fillRect(startX + 4 * pSize, startY + 2 * pSize, pSize, pSize, color);
    gfx->fillRect(startX + 4 * pSize, startY + 1 * pSize, pSize, pSize, color);
  } else if (direction == "SingleDown") {
    int pSize = 8;
    int startX = x - 4;
    int startY = y - 30;
    for (int i = 0; i < 6; i++) {
      gfx->fillRect(startX, startY + i * pSize, pSize, pSize, color);
    }
    gfx->fillRect(startX - 2 * pSize, startY + 4 * pSize, pSize, pSize, color);
    gfx->fillRect(startX - 1 * pSize, startY + 5 * pSize, pSize, pSize, color);
    gfx->fillRect(startX, startY + 6 * pSize, pSize, pSize, color);
    gfx->fillRect(startX + 1 * pSize, startY + 5 * pSize, pSize, pSize, color);
    gfx->fillRect(startX + 2 * pSize, startY + 4 * pSize, pSize, pSize, color);
  } else if (direction == "DoubleDown") {
    int pSize = 8;
    int sX1 = x - 12;
    int sX2 = x + 4;
    int startY = y - 30;
    for (int i = 0; i < 7; i++) {
      gfx->fillRect(sX1, startY + i * pSize, pSize, pSize, color);
      gfx->fillRect(sX2, startY + i * pSize, pSize, pSize, color);
    }
    gfx->fillRect(sX1 - 2 * pSize, startY + 4 * pSize, pSize, pSize, color);
    gfx->fillRect(sX2 + 2 * pSize, startY + 4 * pSize, pSize, pSize, color);
    gfx->fillRect(sX1 - 1 * pSize, startY + 5 * pSize, pSize, pSize, color);
    gfx->fillRect(x - 4, startY + 5 * pSize, pSize, pSize, color);
    gfx->fillRect(sX2 + 1 * pSize, startY + 5 * pSize, pSize, pSize, color);
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

  if (showHarveyBallInfo) {
    long long now = time(NULL);
    int diffMin = 0;
    char ageStr[16];
    if (now > 1700000000LL && bgHistory[0].timestamp > 1700000000LL) {
      diffMin = (int)((now - bgHistory[0].timestamp) / 60);
      if (diffMin < 0)
        diffMin = 0;
      if (diffMin == 0)
        strcpy(ageStr, "Now");
      else if (diffMin > 99)
        strcpy(ageStr, ">99m ago");
      else
        snprintf(ageStr, sizeof(ageStr), "%dm ago", diffMin);
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
  int arrowX = (bgUnits == UNIT_MMOLL) ? 265 : 255;
  int arrowLeft = arrowX - 25; // Left visual extent of trend arrow
  int hbRight = 56; // Right edge of harvey ball (center 40 + radius 16)
  int centerTarget = (hbRight + arrowLeft) / 2; // ~143

  int sgvX;
  if (bgUnits == UNIT_MMOLL) {
    // e.g. "5.4" (2 digits + 12px dot = 100px) -> sgvX ~ 93; "12.3" (3 digits +
    // dot = 148px) -> sgvX ~ 69
    int dotIdx = sgvStr.indexOf('.');
    int totalWidth;
    if (dotIdx > 0) {
      int intLen = dotIdx;
      int decLen = sgvStr.length() - dotIdx - 1;
      int intWidth = (intLen == 2 && sgvStr[0] == '1') ? 86 : (intLen * 48 - 8);
      totalWidth = intWidth + 14 + (decLen * 48 - 8);
    } else {
      totalWidth = sgvStr.length() * 48 - 8;
    }
    sgvX = centerTarget - (totalWidth / 2);
  } else {
    int numDigits = sgvStr.length();
    int totalWidth;
    if (numDigits == 2) {
      // 2-digit: compensate if leading digit is '1' (which is visually narrower
      // in standard 5x7 font)
      totalWidth = (sgvStr[0] == '1') ? 78 : 88;
    } else if (numDigits == 3) {
      // 3-digit: standard 3 digits is 136px; if leading digit is '1' (e.g.
      // 100-199), ~128px
      totalWidth = (sgvStr[0] == '1') ? 128 : 136;
    } else {
      totalWidth = numDigits * 48 - 8;
    }
    sgvX = centerTarget - (totalWidth / 2);
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

  drawTrendArrow(arrowX, 90, latest.direction, bgValColor);

  gfx->setTextColor(isDarkTheme ? WHITE : BLACK);
  gfx->setTextSize(3);
  int deltaX = (bgUnits == UNIT_MMOLL) ? 55 : 85;
  gfx->setCursor(deltaX, 135);
  gfx->printf("%s %s", formatDelta(latest.delta).c_str(), getBGUnitsStr());
}

void drawHistoryChart() {
  if (historyCount < 2)
    return;

  int chartX = 300;
  int chartRight = 625;
  int chartY = 50;
  int chartWidth = 325;
  int chartHeight = 110;

  int minBG = 400;
  int maxBG = 0;
  for (int i = 0; i < historyCount; i++) {
    if (bgHistory[i].sgv > maxBG)
      maxBG = bgHistory[i].sgv;
    if (bgHistory[i].sgv < minBG)
      minBG = bgHistory[i].sgv;
  }
  if (maxBG < 200)
    maxBG = 200;
  if (minBG > 60)
    minBG = 60;
  maxBG += 20;
  minBG -= 20;

  auto getY = [&](int bg) {
    return chartY + chartHeight - map(bg, minBG, maxBG, 0, chartHeight);
  };

  int visualIndex[MAX_HISTORY];
  visualIndex[0] = 0;
  for (int i = 1; i < historyCount; i++) {
    if (bgHistory[i - 1].timestamp - bgHistory[i].timestamp > 360) {
      visualIndex[i] = visualIndex[i - 1] + 3;
    } else {
      visualIndex[i] = visualIndex[i - 1] + 1;
    }
  }

  int y180 = getY(180);
  int y70 = getY(70);
  float barWidth = 325.0 / MAX_HISTORY;

  int oldestX = chartRight - (int)(visualIndex[historyCount - 1] * barWidth);
  if (oldestX < chartX)
    oldestX = chartX;
  gfx->fillRect(oldestX, y180, (chartRight - oldestX) + 1, y70 - y180,
                isDarkTheme ? 0x2104 : 0xEF7D);

  // Draw gaps as two vertical zigzag lines with width equal to 1 data point
  // interval
  uint16_t zigzagColor = isDarkTheme ? GRAY : GRAY;
  uint16_t insideBgColor = isDarkTheme ? BLACK : WHITE;
  uint16_t targetRangeColor = isDarkTheme ? 0x2104 : 0xEF7D;

  struct GapRegion {
    int x1;
    int x2;
    int gapIdx;
    long timeDiff;
  };
  GapRegion gapRegions[MAX_HISTORY];
  int gapCount = 0;

  for (int i = 0; i < historyCount - 1; i++) {
    long timeDiff = bgHistory[i].timestamp - bgHistory[i + 1].timestamp;
    if (timeDiff > 360) {
      int xRight = chartRight - (int)(visualIndex[i] * barWidth);
      int xLeft = chartRight - (int)(visualIndex[i + 1] * barWidth);

      if (xRight < chartX || xLeft < chartX)
        continue;

      // The gap width is equal to standard width between two data points
      // (barWidth)
      int xMid = (xLeft + xRight) / 2;
      int gapW = (int)(barWidth + 0.5f);
      if (gapW < 6)
        gapW = 6;
      int gx1 = xMid - gapW / 2;
      int gx2 = gx1 + gapW;

      if (gapCount < MAX_HISTORY) {
        gapRegions[gapCount++] = {gx1, gx2, i, timeDiff};
      }

      // 6 peaks down the 110px chart height (10 segments of 11px each)
      const int numSegments = 10;
      const int segH = chartHeight / numSegments; // 11px
      const int amp = 3; // zigzag horizontal deflection

      // Clear the interior between the two zigzag lines to canvas background
      // (BLACK or WHITE) and restore target range band outside the zigzags
      for (int s = 0; s < numSegments; s++) {
        int sy1 = chartY + s * segH;
        int sy2 =
            (s == numSegments - 1) ? (chartY + chartHeight) : (sy1 + segH);
        int dx_start = (s % 2 == 0) ? -amp : amp;
        int dx_end = (s % 2 == 0) ? amp : -amp;

        for (int curY = sy1; curY <= sy2; curY++) {
          float t = (float)(curY - sy1) / (float)(sy2 - sy1);
          int offset = (int)(dx_start + t * (dx_end - dx_start));
          int line1X = gx1 + offset;
          int line2X = gx2 + offset;

          // Fill interior between line1X and line2X with pure black / white
          if (line2X > line1X) {
            gfx->drawFastHLine(line1X, curY, (line2X - line1X) + 1,
                               insideBgColor);
          }
        }
      }

      // Draw the two vertical 1px zigzag boundary lines
      for (int s = 0; s < numSegments; s++) {
        int sy1 = chartY + s * segH;
        int sy2 =
            (s == numSegments - 1) ? (chartY + chartHeight) : (sy1 + segH);
        int dx_start = (s % 2 == 0) ? -amp : amp;
        int dx_end = (s % 2 == 0) ? amp : -amp;

        int lx1_start = gx1 + dx_start;
        int lx1_end = gx1 + dx_end;
        int lx2_start = gx2 + dx_start;
        int lx2_end = gx2 + dx_end;

        gfx->drawLine(lx1_start, sy1, lx1_end, sy2, zigzagColor);
        gfx->drawLine(lx2_start, sy1, lx2_end, sy2, zigzagColor);
      }
    }
  }

  for (int i = 0; i < historyCount; i++) {
    int x = chartRight - (int)(visualIndex[i] * barWidth);
    if (x < chartX)
      continue;
    int y = getY(bgHistory[i].sgv);
    uint16_t color = (bgHistory[i].sgv >= 70 && bgHistory[i].sgv <= 180)
                         ? (isDarkTheme ? GREEN : 0x03E0)
                         : ORANGE;
    gfx->fillCircle(x, y, 2, color);
  }

  for (int i = 0; i < historyCount - 1; i++) {
    if (bgHistory[i].timestamp - bgHistory[i + 1].timestamp > 360)
      continue;

    int x1 = chartRight - (int)(visualIndex[i] * barWidth);
    int x2 = chartRight - (int)(visualIndex[i + 1] * barWidth);

    if (x1 < chartX && x2 < chartX)
      continue;

    int y1 = getY(bgHistory[i].sgv);
    int y2 = getY(bgHistory[i + 1].sgv);

    if (x2 < chartX) {
      if (x1 != x2) {
        y2 = y1 + (y2 - y1) * (chartX - x1) / (x2 - x1);
      }
      x2 = chartX;
    }

    uint16_t color = (bgHistory[i].sgv >= 70 && bgHistory[i].sgv <= 180)
                         ? (isDarkTheme ? GREEN : 0x03E0)
                         : ORANGE;

    gfx->drawLine(x1, y1, x2, y2, color);
    gfx->drawLine(x1, y1 + 1, x2, y2 + 1, color);
    gfx->drawLine(x1, y1 - 1, x2, y2 - 1, color);
  }

  bool showScrubber =
      isTouching && touchX >= chartX && touchX <= chartX + chartWidth;
  if (!showScrubber && (millis() - lastScrubberTouchTime < 3000) &&
      lastScrubberX != -1) {
    showScrubber = true;
  }

  if (showScrubber) {
    int curX = isTouching ? touchX : lastScrubberX;

    // Check if scrubber cursor falls inside or near a gap
    int touchedGapIdx = -1;
    for (int g = 0; g < gapCount; g++) {
      if (curX >= gapRegions[g].x1 - 4 && curX <= gapRegions[g].x2 + 4) {
        touchedGapIdx = g;
        break;
      }
    }

    if (touchedGapIdx != -1) {
      // Scrubber over Gap: Display gap duration and start/end times with
      // textSize(2)
      int gxMid =
          (gapRegions[touchedGapIdx].x1 + gapRegions[touchedGapIdx].x2) / 2;
      gfx->drawFastVLine(gxMid, chartY, chartHeight,
                         isDarkTheme ? WHITE : BLACK);

      int boxW = 150;
      int boxH = 40;
      // Position to the left of the scrubber line if there is room; otherwise
      // to the right
      int boxX = (gxMid - boxW >= chartX) ? (gxMid - boxW) : gxMid;
      if (boxX + boxW > chartRight + 10)
        boxX = chartRight + 10 - boxW;
      if (boxX < chartX)
        boxX = chartX;
      int boxY = chartY - 45;

      gfx->fillRoundRect(boxX, boxY, boxW, boxH, 4, GRAY);
      gfx->drawRoundRect(boxX, boxY, boxW, boxH, 4,
                         isDarkTheme ? WHITE : BLACK);

      int gapMins = (int)(gapRegions[touchedGapIdx].timeDiff / 60);
      int i = gapRegions[touchedGapIdx].gapIdx;
      time_t tStart = (time_t)bgHistory[i + 1].timestamp;
      time_t tEnd = (time_t)bgHistory[i].timestamp;
      struct tm tmStart, tmEnd;
      localtime_r(&tStart, &tmStart);
      localtime_r(&tEnd, &tmEnd);

      char line1[32];
      snprintf(line1, sizeof(line1), "GAP %d min", gapMins);
      char line2[32];
      snprintf(line2, sizeof(line2), "%02d:%02d-%02d:%02d", tmStart.tm_hour,
               tmStart.tm_min, tmEnd.tm_hour, tmEnd.tm_min);

      gfx->setTextSize(2);
      gfx->setTextColor(BLACK);
      gfx->setCursor(boxX + 10, boxY + 4);
      gfx->print(line1);

      gfx->setCursor(boxX + 10, boxY + 22);
      gfx->print(line2);
    } else {
      // Standard BG reading scrubber
      int closestIdx = 0;
      int minDiff = 10000;
      for (int i = 0; i < historyCount; i++) {
        int pointX = chartRight - (int)(visualIndex[i] * barWidth);
        if (pointX < chartX)
          continue;
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

      gfx->drawFastVLine(curX, chartY, chartHeight,
                         isDarkTheme ? WHITE : BLACK);
      gfx->fillCircle(curX, curY, 4, isDarkTheme ? WHITE : BLACK);
      gfx->drawCircle(curX, curY, 5, isDarkTheme ? BLACK : WHITE);

      int boxW = 90;
      int boxH = 40;
      int boxX = curX - boxW;
      if (boxX < chartX)
        boxX = chartX;
      int boxY = chartY - 45;

      gfx->fillRoundRect(boxX, boxY, boxW, boxH, 4, GRAY);
      gfx->drawRoundRect(boxX, boxY, boxW, boxH, 4,
                         isDarkTheme ? WHITE : BLACK);

      gfx->setTextSize(2);
      gfx->setTextColor(BLACK);
      gfx->setCursor(boxX + 10, boxY + 5);
      gfx->print(formatBG(r.sgv));

      gfx->setTextColor(BLACK);
      gfx->setTextSize(2);
      gfx->setCursor(boxX + 10, boxY + 23);

      time_t rt = (time_t)r.timestamp;
      struct tm *ti = localtime(&rt);
      gfx->printf("%02d:%02d", ti->tm_hour, ti->tm_min);
    }
  }
}

void drawBluetoothIcon(int x, int y, uint16_t color) {
  // 5x7 bitmap scaled by 2 -> 10x14 pixels (matching the 14px font cap height
  // and battery icon) Rows: 0: ..#.. (0x04) 1: #.##. (0x16) 2: .##.# (0x0D) 3:
  // ..##. (0x06) 4: .##.# (0x0D) 5: #.##. (0x16) 6: ..#.. (0x04)
  static const uint8_t btBitmap[7] = {0x04, 0x16, 0x0D, 0x06, 0x0D, 0x16, 0x04};
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

bool isNightModeActive() {
  if (!nightModeEnabled) return false;
  struct tm timeinfo;
  bool gotTime = getLocalTime(&timeinfo, 10);
  if (!gotTime) {
    time_t now = time(NULL);
    localtime_r(&now, &timeinfo);
  }
  // Schedule: 22:00 to 07:00 (active when hour >= 22 or hour < 7)
  return (timeinfo.tm_hour >= 22 || timeinfo.tm_hour < 7);
}

void drawMoonIcon(int x, int y, uint16_t color) {
  // 10x14 crescent moon icon (height 14px matching font cap height at textSize 2)
  static const uint16_t moonBitmap[14] = {
    0b0000011100,
    0b0001111110,
    0b0011111000,
    0b0111110000,
    0b1111100000,
    0b1111100000,
    0b1111100000,
    0b1111100000,
    0b1111100000,
    0b1111100000,
    0b0111110000,
    0b0011111000,
    0b0001111110,
    0b0000011100
  };
  for (int row = 0; row < 14; row++) {
    uint16_t rowBits = moonBitmap[row];
    for (int col = 0; col < 10; col++) {
      if (rowBits & (1 << (9 - col))) {
        gfx->drawPixel(x + col, y + row, color);
      }
    }
  }
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

  int leftOffset = 15 + strlen(timeStr) * 12 + 4; // ~79px

  if (isNightModeActive()) {
    // Moon icon near the clock: 14px tall at y=7 (aligned with font), WHITE in dark theme, BLACK in light theme
    drawMoonIcon(leftOffset, 7, textColor);
    leftOffset += 14;
  }

  bool showSpinner = isFetching;
  if (showSpinner) {
    gfx->setTextColor(ORANGE);
    const char spinnerFrames[] = {'|', '/', '-', '\\'};
    char spinnerChar = spinnerFrames[(millis() / 150) % 4];
    int spinnerX = isConfigMode ? (leftOffset + 2) : (isTimerMode ? 85 : 90);
    gfx->setCursor(spinnerX, 7);
    gfx->print(spinnerChar);
    gfx->setTextColor(textColor);
  }

  bool showBG = false;
  int bgX = isConfigMode ? (leftOffset + (showSpinner ? 20 : 6)) : 110;

  if (isTimerMode) {
    showBG = true;
    bgX = 110;
  }

  if (showBG && historyCount > 0) {
    BGReading latest = bgHistory[0];
    String sgvStr = (bgUnits == UNIT_MMOLL) ? String(latest.sgv / 18.0182, 1)
                                            : String(latest.sgv);

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

    if (currentBatteryPct <= 3) {
      batColor = RED;
      if ((millis() / 1000) % 2 != 0) {
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

    bool isBtConnected = SugarotaBLE::getInstance().isConnected();
    bool isBtPairing = SugarotaBLE::getInstance().isPairingModeEnabled();
    // If pairing mode is enabled: ALWAYS flash at 1s cadence (even if already connected to a device)
    // If connected and pairing mode is not enabled: solid icon
    // If not connected and pairing mode is not enabled: hide icon
    bool showBtIcon = false;
    if (isBtPairing) {
      showBtIcon = ((millis() / 1000) % 2 == 0);
    } else if (isBtConnected) {
      showBtIcon = true;
    }

    bool isWifiActive = (WiFi.status() == WL_CONNECTED || isConfigMode);
    int batLeftX = cursorX;
    if (isBtConnected || isBtPairing)
      batLeftX -= 18;
    if (isWifiActive)
      batLeftX -= 18;



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
        if (currentBatteryPct >= 80)
          sections = 5;
        else if (currentBatteryPct >= 60)
          sections = 4;
        else if (currentBatteryPct >= 40)
          sections = 3;
        else if (currentBatteryPct >= 20)
          sections = 2;
        else if (currentBatteryPct > 3)
          sections = 1;

        for (int i = 0; i < sections; i++) {
          gfx->fillRect(cursorX + 2 + i * 5, batY + 2, 4, 10, batColor);
        }
      }
      gfx->setTextColor(textColor);
    }

    int currentLeftX = cursorX;

    if (isBtConnected || isBtPairing) {
      currentLeftX -= 18;
      if (showBtIcon) {
        uint16_t btColor = isDarkTheme ? CYAN : BLUE;
        drawBluetoothIcon(currentLeftX + 3, 7, btColor);
      }
    }

    if (isWifiActive) {
      currentLeftX -= 18;
      uint16_t wifiColor = (WiFi.status() == WL_CONNECTED)
                               ? (isDarkTheme ? GREEN : 0x03E0)
                               : ORANGE;
      drawWiFiIcon(currentLeftX + 2, 9, wifiColor);
    }

    if (isConfigMode) {
      if (configScreenState != CONFIG_SCREEN_NONE || WiFi.status() != WL_CONNECTED) {
        // In full config views or when Wi-Fi is off (e.g. user selected NO): Title status bar "Config Mode"
        const char* title = "Config Mode";
        int16_t x1, y1;
        uint16_t w, h;
        gfx->getTextBounds(title, 0, 0, &x1, &y1, &w, &h);
        int textX = (640 - w) / 2;
        gfx->setTextColor(isDarkTheme ? CYAN : BLUE);
        gfx->setCursor(textX, 7);
        gfx->print(title);
      } else {
        // Returned to standard dashboard in config mode with Wi-Fi connected: Show IP only (no QR code)
        String ipMsg = "IP: " + WiFi.localIP().toString();
        int16_t x1, y1;
        uint16_t w, h;
        gfx->getTextBounds(ipMsg.c_str(), 0, 0, &x1, &y1, &w, &h);

        int leftBoundary = leftOffset + (showSpinner ? 20 : 6);
        int rightBoundary = currentLeftX - 6;
        int textX;
        if (rightBoundary > leftBoundary + w) {
          textX = leftBoundary + ((rightBoundary - leftBoundary) - w) / 2;
        } else {
          textX = (640 - w) / 2;
        }

        gfx->setTextColor(isDarkTheme ? CYAN : BLUE);
        gfx->setCursor(textX, 7);
        gfx->print(ipMsg);
      }
      gfx->setTextColor(textColor);
    }
  } else {
    // Battery disabled / not detected
  }

  gfx->drawFastHLine(0, 30, 640, isDarkTheme ? GRAY : GRAY);
}

void drawOTAProgress(int percent, const char* statusMsg) {
  if (percent < 0) percent = 0;
  if (percent > 100) percent = 100;

  gfx->fillScreen(BLACK);

  // Status Bar
  gfx->setTextColor(CYAN);
  gfx->setTextSize(2);
  gfx->setCursor(20, 10);
  gfx->print("SUGAROTA WIRELESS FLASH");

  gfx->drawFastHLine(0, 32, 640, ZINC_BORDER);

  // Progress percentage display
  char pctBuf[16];
  snprintf(pctBuf, sizeof(pctBuf), "%d%%", percent);
  gfx->setTextColor(WHITE);
  gfx->setTextSize(5);

  int16_t x1, y1;
  uint16_t w, h;
  gfx->getTextBounds(pctBuf, 0, 0, &x1, &y1, &w, &h);
  int tx = (640 - w) / 2;
  gfx->setCursor(tx, 50);
  gfx->print(pctBuf);

  // Progress Bar
  int barX = 60;
  int barY = 105;
  int barW = 520;
  int barH = 22;

  gfx->drawRoundRect(barX, barY, barW, barH, 6, GRAY);
  int fillW = (barW - 4) * percent / 100;
  if (fillW > 0) {
    gfx->fillRoundRect(barX + 2, barY + 2, fillW, barH - 4, 4, GREEN);
  }

  // Status / Warning message
  gfx->setTextSize(2);
  if (statusMsg && strlen(statusMsg) > 0) {
    gfx->setTextColor(YELLOW);
    gfx->getTextBounds(statusMsg, 0, 0, &x1, &y1, &w, &h);
    gfx->setCursor((640 - w) / 2, 138);
    gfx->print(statusMsg);
  } else {
    const char* warn = "Do not turn off power";
    gfx->setTextColor(GRAY);
    gfx->getTextBounds(warn, 0, 0, &x1, &y1, &w, &h);
    gfx->setCursor((640 - w) / 2, 138);
    gfx->print(warn);
  }

  gfx->flush();
}

void drawVerticalScreen() {
  uint16_t bgColor = isDarkTheme ? BLACK : WHITE;
  uint16_t fgColor = isDarkTheme ? WHITE : BLACK;
  uint16_t cardBg  = isDarkTheme ? 0x18E3 : 0xDEFB; // Dark card / Light card
  uint16_t cardBorder = isDarkTheme ? ZINC_BORDER : 0xC618;
  uint16_t activeCardBg = isDarkTheme ? 0x2965 : 0xBDF7;

  // Theme-aware accent colors (crisp high contrast in light mode):
  uint16_t accentGreen = isDarkTheme ? GREEN : 0x03E0; // Emerald bright in dark, deep green in light
  uint16_t accentBlue  = isDarkTheme ? CYAN  : 0x0277; // Cyan in dark, bold royal blue in light
  uint16_t iconBgBlue  = isDarkTheme ? 0x1A2F : 0xD67F; // Soft tinted background pill
  uint16_t iconBgGreen = isDarkTheme ? 0x12E8 : 0xDE56;

  gfx->fillScreen(bgColor);

  if (verticalSubscreen == 0) {
    // === VERTICAL MENU (172 x 640) ===
    // Title
    gfx->setTextColor(fgColor);
    gfx->setTextSize(3);
    int16_t x1, y1;
    uint16_t tw, th;
    const char* title = "MENU";
    gfx->getTextBounds(title, 0, 0, &x1, &y1, &tw, &th);
    gfx->setCursor((172 - tw) / 2, 28);
    gfx->print(title);

    gfx->drawFastHLine(14, 60, 144, cardBorder);

    // Option A: Find Phone (y: 80 to 220)
    int btnX = 14, btnW = 144, btnH = 130;
    int yA = 80;
    gfx->fillRoundRect(btnX, yA, btnW, btnH, 8, cardBg);
    gfx->drawRoundRect(btnX, yA, btnW, btnH, 8, cardBorder);
    gfx->fillCircle(86, yA + 38, 18, iconBgBlue);
    gfx->setTextColor(accentBlue);
    gfx->setTextSize(2);
    gfx->setCursor(86 - 5, yA + 30);
    gfx->print("P");
    gfx->setTextColor(fgColor);
    gfx->setTextSize(2);
    const char* txtA1 = "Find Phone";
    gfx->getTextBounds(txtA1, 0, 0, &x1, &y1, &tw, &th);
    gfx->setCursor((172 - tw) / 2, yA + 70);
    gfx->print(txtA1);
    gfx->setTextColor(GRAY);
    gfx->setTextSize(1);
    char txtA2[32];
    if (bondedPhoneCount > 0) {
      int connectedCount = 0;
      for (int i = 0; i < bondedPhoneCount; i++) {
        if (bondedPhones[i].connected) connectedCount++;
      }
      if (connectedCount > 0) {
        snprintf(txtA2, sizeof(txtA2), "%d Online", connectedCount);
        gfx->setTextColor(accentGreen);
      } else {
        snprintf(txtA2, sizeof(txtA2), "%d Offline", bondedPhoneCount);
        gfx->setTextColor(GRAY);
      }
    } else {
      snprintf(txtA2, sizeof(txtA2), "No paired phone");
      gfx->setTextColor(GRAY);
    }
    gfx->getTextBounds(txtA2, 0, 0, &x1, &y1, &tw, &th);
    gfx->setCursor((172 - tw) / 2, yA + 98);
    gfx->print(txtA2);

    // Option B: Countdown Alarm (y: 230 to 370)
    int yB = 230;
    gfx->fillRoundRect(btnX, yB, btnW, btnH, 8, cardBg);
    gfx->drawRoundRect(btnX, yB, btnW, btnH, 8, cardBorder);
    gfx->fillCircle(86, yB + 38, 18, isDarkTheme ? 0x2A20 : 0xFDE8);
    gfx->setTextColor(ORANGE);
    gfx->setTextSize(2);
    gfx->setCursor(86 - 5, yB + 30);
    gfx->print("T");
    gfx->setTextColor(fgColor);
    gfx->setTextSize(2);
    const char* txtB1 = "Countdown";
    gfx->getTextBounds(txtB1, 0, 0, &x1, &y1, &tw, &th);
    gfx->setCursor((172 - tw) / 2, yB + 70);
    gfx->print(txtB1);
    gfx->setTextColor(GRAY);
    gfx->setTextSize(1);
    const char* txtB2 = "Alarm (Soon)";
    gfx->getTextBounds(txtB2, 0, 0, &x1, &y1, &tw, &th);
    gfx->setCursor((172 - tw) / 2, yB + 98);
    gfx->print(txtB2);

    // Option C: Settings (y: 380 to 520)
    int yC = 380;
    gfx->fillRoundRect(btnX, yC, btnW, btnH, 8, cardBg);
    gfx->drawRoundRect(btnX, yC, btnW, btnH, 8, cardBorder);
    gfx->fillCircle(86, yC + 38, 18, iconBgGreen);
    gfx->setTextColor(accentGreen);
    gfx->setTextSize(2);
    gfx->setCursor(86 - 5, yC + 30);
    gfx->print("S");
    gfx->setTextColor(fgColor);
    gfx->setTextSize(2);
    const char* txtC1 = "Settings";
    gfx->getTextBounds(txtC1, 0, 0, &x1, &y1, &tw, &th);
    gfx->setCursor((172 - tw) / 2, yC + 70);
    gfx->print(txtC1);
    gfx->setTextColor(GRAY);
    gfx->setTextSize(1);
    const char* txtC2 = "Sound & Display";
    gfx->getTextBounds(txtC2, 0, 0, &x1, &y1, &tw, &th);
    gfx->setCursor((172 - tw) / 2, yC + 98);
    gfx->print(txtC2);

    // Footer Hint (y: 575)
    gfx->setTextColor(GRAY);
    gfx->setTextSize(1);
    const char* hint = "Rotate horizontal to exit";
    gfx->getTextBounds(hint, 0, 0, &x1, &y1, &tw, &th);
    gfx->setCursor((172 - tw) / 2, 580);
    gfx->print(hint);

  } else if (verticalSubscreen == 1) {
    // === SETTINGS SCREEN (172 x 640) ===
    // Header title centered without top back button (y: 18 to 44)
    gfx->setTextColor(fgColor);
    gfx->setTextSize(2);
    const char* settTitle = "Settings";
    int16_t x1, y1; uint16_t tw, th;
    gfx->getTextBounds(settTitle, 0, 0, &x1, &y1, &tw, &th);
    gfx->setCursor((172 - tw) / 2, 22);
    gfx->print(settTitle);

    gfx->drawFastHLine(14, 52, 144, cardBorder);

    // --- Section 1: Speaker Volume (y: 65 to 195) ---
    gfx->setTextColor(fgColor);
    gfx->setTextSize(2);
    gfx->setCursor(14, 65);
    gfx->print("Volume");

    const char* volLabels[4] = {"Off", "35%", "70%", "100%"};
    int pillW = 68, pillH = 36, pillGapX = 8, pillGapY = 8, startX = 14;
    int volBaseY = 92;
    for (int i = 0; i < 4; i++) {
      int col = i % 2;
      int row = i / 2;
      int px = startX + col * (pillW + pillGapX);
      int py = volBaseY + row * (pillH + pillGapY);
      bool isSel = (volumeLevel == i);
      gfx->fillRoundRect(px, py, pillW, pillH, 6, isSel ? activeCardBg : cardBg);
      gfx->drawRoundRect(px, py, pillW, pillH, 6, isSel ? accentGreen : cardBorder);
      gfx->setTextColor(isSel ? accentGreen : fgColor);
      gfx->setTextSize(2);
      int16_t bx, by; uint16_t bw, bh;
      gfx->getTextBounds(volLabels[i], 0, 0, &bx, &by, &bw, &bh);
      gfx->setCursor(px + (pillW - bw) / 2, py + (pillH - bh) / 2);
      gfx->print(volLabels[i]);
    }

    gfx->drawFastHLine(14, 185, 144, cardBorder);

    // --- Section 2: Display Brightness (y: 198 to 330) ---
    gfx->setTextColor(fgColor);
    gfx->setTextSize(2);
    gfx->setCursor(14, 198);
    gfx->print("Brightness");

    int brightPresets[4] = {76, 153, 204, 255};
    const char* brightLabels[4] = {"30%", "60%", "80%", "100%"};
    int brightBaseY = 225;
    for (int i = 0; i < 4; i++) {
      int col = i % 2;
      int row = i / 2;
      int px = startX + col * (pillW + pillGapX);
      int py = brightBaseY + row * (pillH + pillGapY);
      bool isSel = (brightnessLevel == brightPresets[i]);
      gfx->fillRoundRect(px, py, pillW, pillH, 6, isSel ? activeCardBg : cardBg);
      gfx->drawRoundRect(px, py, pillW, pillH, 6, isSel ? accentBlue : cardBorder);
      gfx->setTextColor(isSel ? accentBlue : fgColor);
      gfx->setTextSize(2);
      int16_t bx, by; uint16_t bw, bh;
      gfx->getTextBounds(brightLabels[i], 0, 0, &bx, &by, &bw, &bh);
      gfx->setCursor(px + (pillW - bw) / 2, py + (pillH - bh) / 2);
      gfx->print(brightLabels[i]);
    }

    gfx->drawFastHLine(14, 320, 144, cardBorder);

    // --- Section 3: Night-mode Switch (y: 335 to 455) ---
    gfx->setTextColor(fgColor);
    gfx->setTextSize(2);
    gfx->setCursor(14, 335);
    gfx->print("Night Mode");

    // Toggle container
    int togW = 144, togH = 46, togX = 14, togY = 368;
    gfx->fillRoundRect(togX, togY, togW, togH, 8, cardBg);
    gfx->drawRoundRect(togX, togY, togW, togH, 8, nightModeEnabled ? accentBlue : cardBorder);

    gfx->setTextColor(nightModeEnabled ? accentBlue : fgColor);
    gfx->setTextSize(2);
    gfx->setCursor(togX + 16, togY + 14);
    gfx->print(nightModeEnabled ? "On" : "Off");

    // Switch pill inside container
    int swW = 44, swH = 24, swX = togX + togW - swW - 12, swY = togY + 11;
    gfx->fillRoundRect(swX, swY, swW, swH, 12, nightModeEnabled ? accentBlue : (isDarkTheme ? 0x4208 : 0xCE59));
    int knobX = nightModeEnabled ? (swX + swW - 20) : (swX + 4);
    gfx->fillCircle(knobX + 8, swY + 12, 8, WHITE);

    gfx->setTextColor(GRAY);
    gfx->setTextSize(1);
    int16_t bx, by; uint16_t bw, bh;
    const char* nmNote = "Active: 22:00 - 07:00";
    gfx->getTextBounds(nmNote, 0, 0, &bx, &by, &bw, &bh);
    gfx->setCursor((172 - bw) / 2, 428);
    gfx->print(nmNote);

    gfx->drawFastHLine(14, 460, 144, cardBorder);

    // Full-width Bottom Back Button (y: 565 to 613)
    int backBtnX = 14, backBtnY = 565, backBtnW = 144, backBtnH = 48;
    gfx->fillRoundRect(backBtnX, backBtnY, backBtnW, backBtnH, 8, cardBg);
    gfx->drawRoundRect(backBtnX, backBtnY, backBtnW, backBtnH, 8, cardBorder);
    gfx->setTextColor(accentBlue);
    gfx->setTextSize(2);
    const char* backTxt = "< Back";
    gfx->getTextBounds(backTxt, 0, 0, &bx, &by, &bw, &bh);
    gfx->setCursor(backBtnX + (backBtnW - bw) / 2, backBtnY + (backBtnH - bh) / 2);
    gfx->print(backTxt);

  } else if (verticalSubscreen == 2) {
    // === FIND PHONE SCREEN (172 x 640) ===
    // Header title centered without top back button (y: 18 to 44)
    gfx->setTextColor(fgColor);
    gfx->setTextSize(2);
    const char* fpTitle = "Find Phone";
    int16_t x1, y1; uint16_t tw, th;
    gfx->getTextBounds(fpTitle, 0, 0, &x1, &y1, &tw, &th);
    gfx->setCursor((172 - tw) / 2, 22);
    gfx->print(fpTitle);

    gfx->drawFastHLine(14, 52, 144, cardBorder);

    // List up to 3 bonded phones (y: 70, 215, 360)
    int cardX = 14, cardW = 144, cardH = 130;
    int startY = 70;

    if (bondedPhoneCount == 0) {
      gfx->fillRoundRect(cardX, startY, cardW, 115, 8, cardBg);
      gfx->drawRoundRect(cardX, startY, cardW, 115, 8, cardBorder);
      gfx->setTextColor(GRAY);
      gfx->setTextSize(2);
      const char* noPh1 = "No Paired";
      int16_t bx, by; uint16_t bw, bh;
      gfx->getTextBounds(noPh1, 0, 0, &bx, &by, &bw, &bh);
      gfx->setCursor((172 - bw) / 2, startY + 35);
      gfx->print(noPh1);
      const char* noPh2 = "Phones Yet";
      gfx->getTextBounds(noPh2, 0, 0, &bx, &by, &bw, &bh);
      gfx->setCursor((172 - bw) / 2, startY + 60);
      gfx->print(noPh2);
    } else {
      for (int i = 0; i < bondedPhoneCount && i < 3; i++) {
        int cy = startY + i * (cardH + 15);
        bool isConn = bondedPhones[i].connected;
        bool isAlerting = (activeFindPhoneAddr.length() > 0 && activeFindPhoneAddr.equalsIgnoreCase(bondedPhones[i].address));

        uint16_t cBg = cardBg;
        uint16_t cBorder = cardBorder;
        if (isAlerting) {
          cBg = ((millis() / 400) % 2 == 0) ? 0x2800 : cardBg;
          cBorder = RED;
        } else if (isConn) {
          cBorder = 0x0400; // Subtle emerald border
        }

        gfx->fillRoundRect(cardX, cy, cardW, cardH, 8, cBg);
        gfx->drawRoundRect(cardX, cy, cardW, cardH, 8, cBorder);

        // Status indicator dot (Left: x = cardX + 14, cy + 18)
        int dotColor = isConn ? GREEN : GRAY;
        gfx->fillCircle(cardX + 14, cy + 18, 4, dotColor);

        // Phone Name next to circle indicator (Aligned to Left, Size 1 like Online badge)
        gfx->setTextColor(isConn ? fgColor : GRAY);
        gfx->setTextSize(1);
        char dispName[32];
        strncpy(dispName, bondedPhones[i].name, sizeof(dispName) - 1);
        dispName[sizeof(dispName) - 1] = '\0';
        int16_t bx, by; uint16_t bw, bh;
        gfx->getTextBounds(dispName, 0, 0, &bx, &by, &bw, &bh);

        // Available width next to dot
        int maxNameW = cardW - 32;
        if (bw > maxNameW) {
          int len = strlen(dispName);
          while (len > 3 && bw > maxNameW) {
            len--;
            dispName[len] = '\0';
            char temp[36];
            snprintf(temp, sizeof(temp), "%s..", dispName);
            gfx->getTextBounds(temp, 0, 0, &bx, &by, &bw, &bh);
          }
          strncat(dispName, "..", sizeof(dispName) - strlen(dispName) - 1);
        }
        gfx->setCursor(cardX + 24, cy + 15);
        gfx->print(dispName);

        // Action button or offline label
        // Button is double height (h = 64) with two strings
        int pillW = 124, pillH = 64, pillX = cardX + (cardW - pillW) / 2, pillY = cy + 45;
        if (isAlerting) {
          // Red Stop button pill
          gfx->fillRoundRect(pillX, pillY, pillW, pillH, 8, RED);
          gfx->setTextColor(WHITE);
          gfx->setTextSize(2);
          const char* s1 = "STOP";
          const char* s2 = "ALARM";
          gfx->getTextBounds(s1, 0, 0, &bx, &by, &bw, &bh);
          gfx->setCursor(pillX + (pillW - bw) / 2, pillY + 14);
          gfx->print(s1);
          gfx->getTextBounds(s2, 0, 0, &bx, &by, &bw, &bh);
          gfx->setCursor(pillX + (pillW - bw) / 2, pillY + 38);
          gfx->print(s2);
        } else if (isConn) {
          // Ring Phone button pill (Double height: 64, two strings)
          gfx->fillRoundRect(pillX, pillY, pillW, pillH, 8, iconBgBlue);
          gfx->drawRoundRect(pillX, pillY, pillW, pillH, 8, accentBlue);
          gfx->setTextColor(accentBlue);
          gfx->setTextSize(2);
          const char* s1 = "Ring";
          const char* s2 = "Phone";
          gfx->getTextBounds(s1, 0, 0, &bx, &by, &bw, &bh);
          gfx->setCursor(pillX + (pillW - bw) / 2, pillY + 14);
          gfx->print(s1);
          gfx->getTextBounds(s2, 0, 0, &bx, &by, &bw, &bh);
          gfx->setCursor(pillX + (pillW - bw) / 2, pillY + 38);
          gfx->print(s2);
        } else {
          // Offline container
          gfx->fillRoundRect(pillX, pillY, pillW, pillH, 8, cardBg);
          gfx->drawRoundRect(pillX, pillY, pillW, pillH, 8, cardBorder);
          gfx->setTextColor(GRAY);
          gfx->setTextSize(1);
          const char* disTxt1 = "Phone";
          const char* disTxt2 = "Offline";
          gfx->getTextBounds(disTxt1, 0, 0, &bx, &by, &bw, &bh);
          gfx->setCursor(pillX + (pillW - bw) / 2, pillY + 20);
          gfx->print(disTxt1);
          gfx->getTextBounds(disTxt2, 0, 0, &bx, &by, &bw, &bh);
          gfx->setCursor(pillX + (pillW - bw) / 2, pillY + 36);
          gfx->print(disTxt2);
        }
      }
    }

    // Full-width Bottom Back Button (y: 565 to 613)
    int backBtnX = 14, backBtnY = 565, backBtnW = 144, backBtnH = 48;
    gfx->fillRoundRect(backBtnX, backBtnY, backBtnW, backBtnH, 8, cardBg);
    gfx->drawRoundRect(backBtnX, backBtnY, backBtnW, backBtnH, 8, cardBorder);
    gfx->setTextColor(accentBlue);
    gfx->setTextSize(2);
    const char* backTxt = "< Back";
    int16_t bx, by; uint16_t bw, bh;
    gfx->getTextBounds(backTxt, 0, 0, &bx, &by, &bw, &bh);
    gfx->setCursor(backBtnX + (backBtnW - bw) / 2, backBtnY + (backBtnH - bh) / 2);
    gfx->print(backTxt);
  }

  gfx->flush();
}

void drawConfigPromptScreen() {
  uint16_t textColor = isDarkTheme ? WHITE : BLACK;
  uint16_t subColor  = isDarkTheme ? CYAN : BLUE;

  // Title: "Enable Wi-Fi & Web Portal?"
  const char* title = "Enable Wi-Fi & Web Portal?";
  gfx->setTextSize(3);
  gfx->setTextColor(textColor);
  int16_t x1, y1;
  uint16_t w, h;
  gfx->getTextBounds(title, 0, 0, &x1, &y1, &w, &h);
  gfx->setCursor((640 - w) / 2, 45);
  gfx->print(title);

  // Subtitle / Notice: Centered "BLE remains active"
  const char* desc = "BLE remains active";
  gfx->setTextSize(2);
  gfx->setTextColor(subColor);
  gfx->getTextBounds(desc, 0, 0, &x1, &y1, &w, &h);
  gfx->setCursor((640 - w) / 2, 82);
  gfx->print(desc);

  // YES Button: x: 180..300, y: 118..158
  int yesX = 180, yesY = 118, yesW = 120, yesH = 40;
  gfx->fillRoundRect(yesX, yesY, yesW, yesH, 6, isDarkTheme ? GREEN : 0x03E0);
  gfx->setTextColor(isDarkTheme ? BLACK : WHITE);
  gfx->setTextSize(3);
  const char* yesTxt = "YES";
  gfx->getTextBounds(yesTxt, 0, 0, &x1, &y1, &w, &h);
  gfx->setCursor(yesX + (yesW - w) / 2, yesY + (yesH - h) / 2);
  gfx->print(yesTxt);

  // NO Button: x: 340..460, y: 118..158
  int noX = 340, noY = 118, noW = 120, noH = 40;
  uint16_t noBgColor = isDarkTheme ? LIGHT_PINK : 0xD186; // Light pink in dark mode; soft muted rose in light mode
  uint16_t noFgColor = isDarkTheme ? DARK_RED : DARK_RED;
  gfx->fillRoundRect(noX, noY, noW, noH, 6, noBgColor);
  gfx->drawRoundRect(noX, noY, noW, noH, 6, DARK_RED);
  gfx->setTextColor(noFgColor);
  gfx->setTextSize(3);
  const char* noTxt = "NO";
  gfx->getTextBounds(noTxt, 0, 0, &x1, &y1, &w, &h);
  gfx->setCursor(noX + (noW - w) / 2, noY + (noH - h) / 2);
  gfx->print(noTxt);

  gfx->flush();
}

void drawConfigConnectingScreen() {
  gfx->setTextSize(2);
  int logY = 38;
  int startIdx = 0;
  for (int i = 0; i < configLog.length(); i++) {
    if (configLog[i] == '\n') {
      gfx->setCursor(25, logY);
      String line = configLog.substring(startIdx, i);
      if (line.indexOf("Failed") >= 0 || line.indexOf("Error") >= 0) {
        gfx->setTextColor(RED);
      } else if (line.indexOf("Connected") >= 0 || line.indexOf("started") >= 0) {
        gfx->setTextColor(GREEN);
      } else {
        gfx->setTextColor(CYAN);
      }
      gfx->print(line);
      logY += 20;
      startIdx = i + 1;
    }
  }
  gfx->flush();
}

void drawConfigInfoScreen() {
  uint16_t textColor = isDarkTheme ? WHITE : BLACK;
  bool isConnected = (WiFi.status() == WL_CONNECTED);

  if (isConnected) {
    // Generate QR Code (URL: http://<localIP>)
    String ipStr = WiFi.localIP().toString();
    String url = "http://" + ipStr;
    QRCode qrcode;
    uint8_t qrcodeData[qrcode_getBufferSize(2)];
    qrcode_initText(&qrcode, qrcodeData, 2, 0, url.c_str());

    // Draw 3x scaled QR code (25 modules * 3 = 75px wide)
    int scale = 3;
    int qrX = 35;
    int qrY = 46;
    int qrSize = qrcode.size;
    int border = 4;

    gfx->fillRect(qrX - border, qrY - border, qrSize * scale + border * 2, qrSize * scale + border * 2, WHITE);

    for (uint8_t y = 0; y < qrSize; y++) {
      for (uint8_t x = 0; x < qrSize; x++) {
        if (qrcode_getModule(&qrcode, x, y)) {
          gfx->fillRect(qrX + x * scale, qrY + y * scale, scale, scale, BLACK);
        }
      }
    }

    // Right-side info text:
    // Line 1: IP address & local URL
    int textX = 145;
    gfx->setTextColor(textColor);
    gfx->setTextSize(2);
    gfx->setCursor(textX, 48);
    gfx->printf("IP: %s", ipStr.c_str());

    gfx->setTextColor(isDarkTheme ? CYAN : BLUE);
    gfx->setCursor(textX, 72);
    gfx->print("http://sugarota.local");

    // Line 2: Instruction
    gfx->setTextColor(isDarkTheme ? GRAY : 0x4A49);
    gfx->setTextSize(1);
    gfx->setCursor(textX, 98);
    gfx->print("Connect phone or PC to the same Wi-Fi network");
    gfx->setCursor(textX, 112);
    gfx->print("Shake device anytime to exit Config Mode");
  } else {
    // Connection Failed message
    gfx->setTextColor(RED);
    gfx->setTextSize(3);
    gfx->setCursor(35, 50);
    gfx->print("Wi-Fi Connection Failed");

    gfx->setTextColor(textColor);
    gfx->setTextSize(2);
    gfx->setCursor(35, 88);
    gfx->print("Could not reach configured Wi-Fi network.");

    // TRY AGAIN Button (x: 330..460, y: 120..158)
    int tryX = 330, tryY = 120, tryW = 130, tryH = 38;
    gfx->fillRoundRect(tryX, tryY, tryW, tryH, 6, isDarkTheme ? GREEN : 0x03E0);
    gfx->setTextColor(isDarkTheme ? BLACK : WHITE);
    gfx->setTextSize(2);
    const char* tryTxt = "TRY AGAIN";
    int16_t x1, y1;
    uint16_t w, h;
    gfx->getTextBounds(tryTxt, 0, 0, &x1, &y1, &w, &h);
    gfx->setCursor(tryX + (tryW - w) / 2, tryY + (tryH - h) / 2);
    gfx->print(tryTxt);
  }

  // DISMISS Button on bottom-right (x: 480..610, y: 120..158)
  int btnX = 480, btnY = 120, btnW = 130, btnH = 38;
  gfx->fillRoundRect(btnX, btnY, btnW, btnH, 6, isDarkTheme ? 0x2104 : 0xCE79);
  gfx->drawRoundRect(btnX, btnY, btnW, btnH, 6, isDarkTheme ? CYAN : BLACK);
  gfx->setTextColor(isDarkTheme ? CYAN : BLACK);
  gfx->setTextSize(2);
  const char* btnTxt = "DISMISS";
  int16_t x1, y1;
  uint16_t w, h;
  gfx->getTextBounds(btnTxt, 0, 0, &x1, &y1, &w, &h);
  gfx->setCursor(btnX + (btnW - w) / 2, btnY + (btnH - h) / 2);
  gfx->print(btnTxt);

  gfx->flush();
}


