#ifndef SUGAROTA_STORAGE_H
#define SUGAROTA_STORAGE_H

#include "config.h"
#include <LittleFS.h>

void loadConfig();
void applyRuntimeConfig();
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

void appendBatteryLog(float voltage, int pct, bool isCharging, bool screenOn, bool wifiActive);
String readBatteryLog();
void streamBatteryLog(Stream& out);
void clearBatteryLog();

#endif // SUGAROTA_STORAGE_H
