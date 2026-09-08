#ifndef SUGAROTA_BATTERY_H
#define SUGAROTA_BATTERY_H

#include "config.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

void initBatteryADC();
float readBatteryVoltageSingle();
int getBatteryPercentage(float voltage);
void updateBattery(bool isUSBPlugged);
void fillVoltageHistory(float voltage);

#endif // SUGAROTA_BATTERY_H
