#ifndef SUGAROTA_STORAGE_H
#define SUGAROTA_STORAGE_H

#include "config.h"
#include <LittleFS.h>

void loadConfig();
void saveConfig();
bool loadHistoryFromCache();
void saveHistoryToCache();

const char* getResetReasonString(esp_reset_reason_t reason);
void recordBootResetReason();
String readCrashLog();
void clearCrashLog();

#endif // SUGAROTA_STORAGE_H
