#include "battery.h"

// Forward declarations
void updateUI();
void powerOffDevice();

static adc_oneshot_unit_handle_t adc1_handle = NULL;
static adc_cali_handle_t adc1_cali_handle = NULL;
static bool adc1_calibrated = false;

static float voltageHistory[60];
static int voltageIndex = 0;
static bool historyFilled = false;
static unsigned long lastBatteryPctUpdate = 0;
static float chargingOffset = 0.0;
static float preSpikeVoltage = 0.0;
static unsigned long lastUSBUnplugTime = 0;

void initBatteryADC() {
  adc_oneshot_unit_init_cfg_t init_config1 = {};
  init_config1.unit_id = ADC_UNIT_1;
  if (adc_oneshot_new_unit(&init_config1, &adc1_handle) == ESP_OK) {
    adc_oneshot_chan_cfg_t config = {};
    config.atten = ADC_ATTEN_DB_12;
    config.bitwidth = ADC_BITWIDTH_12;
    adc_oneshot_config_channel(adc1_handle, ADC_CHANNEL_3, &config);

    adc_cali_curve_fitting_config_t cali_config = {};
    cali_config.unit_id = ADC_UNIT_1;
    cali_config.chan = ADC_CHANNEL_3;
    cali_config.atten = ADC_ATTEN_DB_12;
    cali_config.bitwidth = ADC_BITWIDTH_12;
    if (adc_cali_create_scheme_curve_fitting(&cali_config, &adc1_cali_handle) == ESP_OK) {
      adc1_calibrated = true;
      DBG_PRINTLN("ADC: Factory curve-fitting calibration initialized successfully.");
    } else {
      adc1_calibrated = false;
      DBG_PRINTLN("ADC: Warning - calibration scheme creation failed, falling back to uncalibrated math.");
    }
  }
}

float readBatteryVoltageSingle() {
  if (!adc1_handle) {
    return (analogReadMilliVolts(PIN_BAT_ADC) * 3.0) / 1000.0;
  }
  int raw_data = 0;
  esp_err_t err = adc_oneshot_read(adc1_handle, ADC_CHANNEL_3, &raw_data);
  if (err != ESP_OK) {
    return 0.0f;
  }
  if (adc1_calibrated && adc1_cali_handle) {
    int voltage_mv = 0;
    adc_cali_raw_to_voltage(adc1_cali_handle, raw_data, &voltage_mv);
    return (0.001f * voltage_mv * 3.0f);
  }
  return ((float)raw_data * 3.3f / 4096.0f) * 3.0f;
}

void fillVoltageHistory(float voltage) {
  for (int i = 0; i < 60; i++) {
    voltageHistory[i] = voltage;
  }
  voltageIndex = 0;
  historyFilled = true;
}

int getBatteryPercentage(float voltage) {
  // Adjusted for standard 3.7V Li-ion/LiPo discharge curve under light load:
  // - Full charge rests around 4.12V-4.20V
  // - Initial voltage drop 4.20V -> 3.90V represents ~25-30% of capacity
  // - Main discharge plateau is between 3.65V and 3.80V (~30% to ~70%)
  // - Knee begins below 3.55V (~20%), steep discharge below 3.45V
  // - Cutoff protection is at 3.00V
  const float vMap[] = {3.10f, 3.35f, 3.45f, 3.55f, 3.62f, 3.68f, 3.74f, 3.80f, 3.88f, 3.98f, 4.12f};
  const int   pMap[] = {    1,     5,    10,    20,    30,    40,    50,    60,    70,    85,   100};
  const int   numPoints = sizeof(vMap) / sizeof(vMap[0]);

  if (voltage <= vMap[0]) return 1;
  if (voltage >= vMap[numPoints - 1]) return 100;

  for (int i = 0; i < numPoints - 1; i++) {
    if (voltage >= vMap[i] && voltage <= vMap[i + 1]) {
      float range = vMap[i + 1] - vMap[i];
      float offset = voltage - vMap[i];
      float pRange = pMap[i + 1] - pMap[i];
      int pct = pMap[i] + (int)((offset / range) * pRange);
      return (pct < 1) ? 1 : pct;
    }
  }
  return 100;
}

void updateBattery() {
  float currentV = readBatteryVoltageSingle(); 
  if (currentV <= 0.0f) return;

  // Charging detection logic:
  // Note: We do NOT use (Serial) because on ESP32-S3 HWCDC once a host connects,
  // `Serial` stays true indefinitely even after unplugging unless TX fails.
  // We rely on battery rail voltage:
  // - V1: charging float voltage is around 4.14V-4.20V. High threshold 4.14V, low threshold 4.08V.
  // - V2: charging float voltage reaches >= 4.20V. High threshold 4.18V, low threshold 4.12V.
  float chargeHighThreshold = (hwVersion == 1) ? 4.14f : 4.18f;
  float chargeLowThreshold  = (hwVersion == 1) ? 4.08f : 4.12f;

  bool isCharging = (currentV >= chargeHighThreshold);

  bool previousPluggedState = wasUSBPlugged;
  if (isCharging && !wasUSBPlugged) {
    wasUSBPlugged = true;
    DBG_PRINTF("Charging detected (HW: V%d, Voltage: %.2fV)\n", hwVersion, currentV);
  } else if (!isCharging && wasUSBPlugged && (currentV < chargeLowThreshold)) {
    wasUSBPlugged = false;
    DBG_PRINTF("Charging ended / Unplugged (HW: V%d, Voltage: %.2fV)\n", hwVersion, currentV);
  }

  // Update UI immediately if charging state changed
  if (previousPluggedState != wasUSBPlugged) {
    updateUI();
  }

  voltageHistory[voltageIndex] = currentV;
  voltageIndex++;
  if (voltageIndex >= 60) {
    voltageIndex = 0;
    historyFilled = true;
  }
  
  float sum = 0;
  int count = historyFilled ? 60 : voltageIndex;
  if (count == 0) return;
  
  for(int i = 0; i < count; i++) {
    sum += voltageHistory[i];
  }
  float avgV = sum / count;
  
  currentBatteryVoltage = avgV;

  static bool bootVoltageChecked = false;
  static bool bootedLow = false;
  if (!bootVoltageChecked) {
    if (avgV < 3.00) {
      bootedLow = true;
      DBG_PRINTF("Boot voltage checked: %.2fV (Low, < 3.00V). Will monitor for 10s.\n", avgV);
    } else {
      DBG_PRINTF("Boot voltage checked: %.2fV (Normal, >= 3.00V).\n", avgV);
    }
    bootVoltageChecked = true;
  }

  int targetPct = getBatteryPercentage(avgV);
  
  if (currentBatteryPct == -1) {
    currentBatteryPct = targetPct;
    lastBatteryPctUpdate = millis();
    updateUI();
  } else {
    if (currentBatteryPct != targetPct) {
      currentBatteryPct = targetPct;
      lastBatteryPctUpdate = millis();
      updateUI();
    }
  }
  
  DBG_PRINTF("Battery: %.2fV (Avg: %.2fV) Target: %d%% Disp: %d%%%s\n", 
             currentV, avgV, targetPct, currentBatteryPct, wasUSBPlugged ? " [Charging]" : "");
  
  // Only shut down for low battery if USB is definitely not plugged in AND an actual depleted battery is connected.
  // When running purely on USB or without battery, ADC may read 0.0V - 1.5V; never shut down in that state.
  if (!wasUSBPlugged && !Serial) {
    if (avgV >= 2.0f && avgV < 3.00f && millis() >= 15000) {
      DBG_PRINTLN(F("CRITICAL BATTERY: Battery depleted below 3.00V, powering off device..."));
      powerOffDevice();
    }
  }
}
