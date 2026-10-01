#ifndef SUGAROTA_STORAGE_H
#define SUGAROTA_STORAGE_H

#include "config.h"
#include <LittleFS.h>

void loadConfig();
void saveConfig();
bool loadHistoryFromCache();
void saveHistoryToCache();

void loadBondedPhones();
void saveBondedPhones();
void updateOrRegisterPhone(const char* address, const char* name, bool connected);
void setPhoneConnected(const char* address, bool connected);

const char* getResetReasonString(esp_reset_reason_t reason);
void recordBootResetReason();
String readCrashLog();
void clearCrashLog();

#endif // SUGAROTA_STORAGE_H
