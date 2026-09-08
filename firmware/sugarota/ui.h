#ifndef SUGAROTA_UI_H
#define SUGAROTA_UI_H

#include "config.h"
#include "display.h"

void updateUI();
void drawStatusBar();
void drawBluetoothIcon(int x, int y, uint16_t color);
void drawWiFiIcon(int x, int y, uint16_t color);
void drawGlucoseContainer();
void drawHarveyBall(int x, int y, int radius, long long timestamp);
void drawTrendArrow(int x, int y, const String& direction, uint16_t color);
void drawHistoryChart();

#endif // SUGAROTA_UI_H
