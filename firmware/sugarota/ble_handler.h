#ifndef SUGAROTA_BLE_HANDLER_H
#define SUGAROTA_BLE_HANDLER_H

#include "config.h"

void insertOrUpdateReading(long long tsVal, int sgvVal, const char* dirVal, int deltaVal);
void handleBLEGlucose(const JsonDocument& doc);
void handleBLEConfig();
void handleBLEPairingDisplay(uint32_t pin, bool active);

#endif // SUGAROTA_BLE_HANDLER_H
