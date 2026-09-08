#ifndef SUGAROTA_STORAGE_H
#define SUGAROTA_STORAGE_H

#include "config.h"
#include <LittleFS.h>

void loadConfig();
void saveConfig();
void loadHistoryFromCache();
void saveHistoryToCache();

#endif // SUGAROTA_STORAGE_H
