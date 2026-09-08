#ifndef SUGAROTA_DISPLAY_H
#define SUGAROTA_DISPLAY_H

#include "config.h"
#include <Arduino_GFX_Library.h>

extern Arduino_GFX *gfx;

void initDisplay();
void setBrightness(int level);
void toggleTheme();
String formatBG(int mgdl);
String formatDelta(int delta);
uint16_t getBGColor(int sgv);

#endif // SUGAROTA_DISPLAY_H
