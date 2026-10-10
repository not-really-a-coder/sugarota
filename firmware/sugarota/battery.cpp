#include "battery.h"
#include "storage.h"
#include "ble.h"
#include <WiFi.h>

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
  // Remapped using empirical 20.7h telemetry discharge data (Sugarota V1):
  // - 100%: >= 4.15V (full charge float / saturation)
  // - 90%:  4.03V
  // - 80%:  3.99V
  // - 70%:  3.92V
  // - 60%:  3.82V
  // - 50%:  3.72V
  // - 40%:  3.59V
  // - 30%:  3.45V
  // - 20%:  3.33V
  // - 10%:  3.14V
  // -  5%:  3.10V
  // -  2%:  3.05V
  // -  0%:  3.02V (device shutdown guard threshold: < 3.00V)
  const float vMap[] = {3.02f, 3.05f, 3.10f, 3.14f, 3.33f, 3.45f, 3.59f, 3.72f, 3.82f, 3.92f, 3.99f, 4.03f, 4.15f};
  const int   pMap[] = {    0,     2,     5,    10,    20,    30,    40,    50,    60,    70,    80,    90,   100};
  const int   numPoints = sizeof(vMap) / sizeof(vMap[0]);

  if (voltage <= vMap[0]) return 0;
  if (voltage >= vMap[numPoints - 1]) return 100;

  for (int i = 0; i < numPoints - 1; i++) {
    if (voltage >= vMap[i] && voltage <= vMap[i + 1]) {
      float range = vMap[i + 1] - vMap[i];
      float offset = voltage - vMap[i];
      float pRange = pMap[i + 1] - pMap[i];
      int pct = pMap[i] + (int)((offset / range) * pRange);
      return (pct < 0) ? 0 : ((pct > 100) ? 100 : pct);
    }
  }
  return 100;
}

void updateBattery() {
  float currentV = readBatteryVoltageSingle(); 
  if (currentV <= 0.0f) return;

  // Hybrid charging detection:
  // 1. Instant Step Jump: Plugging in jumps battery rail up abruptly (+60mV..+300mV).
  //    Unplugging drops battery rail immediately (-60mV..-300mV).
  // 2. Continuous Slope: Over recent cycles (60-90s), charging voltage slopes upward (> +0.02V).
  //    Discharging slopes downward (< -0.02V).
  // 3. Float Anchor: High saturation voltage (>= 4.15V on V1, >= 4.18V on V2) stays charging
  //    until a distinct step drop or sustained negative slope occurs.
  static float lastRawV = 0.0f;
  static float prevFilteredV = 0.0f;
  static int positiveSlopeCount = 0;
  static int negativeSlopeCount = 0;

  float chargeHighThreshold = (hwVersion == 1) ? 4.14f : 4.18f;
  float chargeLowThreshold  = (hwVersion == 1) ? 4.08f : 4.12f;

  bool previousPluggedState = wasUSBPlugged;

  if (lastRawV > 0.0f) {
    float rawDelta = currentV - lastRawV;

    // Instant step changes (connect / disconnect)
    if (rawDelta >= 0.07f && !wasUSBPlugged) {
      wasUSBPlugged = true;
      positiveSlopeCount = 3;
      negativeSlopeCount = 0;
      DBG_PRINTF("Charging detected via step jump (+%.2fV -> %.2fV)\n", rawDelta, currentV);
    } else if (rawDelta <= -0.07f && wasUSBPlugged) {
      wasUSBPlugged = false;
      negativeSlopeCount = 3;
      positiveSlopeCount = 0;
      DBG_PRINTF("Unplugged detected via step drop (%.2fV -> %.2fV)\n", rawDelta, currentV);
    }
  }
  lastRawV = currentV;

  // Track slope across moving average once available
  if (currentBatteryVoltage > 0.0f) {
    if (prevFilteredV > 0.0f) {
      float slopeDelta = currentBatteryVoltage - prevFilteredV;
      if (slopeDelta >= 0.015f) {
        positiveSlopeCount++;
        if (negativeSlopeCount > 0) negativeSlopeCount--;
      } else if (slopeDelta <= -0.015f) {
        negativeSlopeCount++;
        if (positiveSlopeCount > 0) positiveSlopeCount--;
      }
    }
    prevFilteredV = currentBatteryVoltage;

    // If slope consistently positive for >= 2 cycles, mark charging
    if (!wasUSBPlugged && positiveSlopeCount >= 2 && currentV >= 3.40f) {
      wasUSBPlugged = true;
      DBG_PRINTF("Charging confirmed via positive slope (V=%.2fV)\n", currentV);
    }
    // If slope consistently negative for >= 3 cycles and not near float voltage, mark unplugged
    else if (wasUSBPlugged && negativeSlopeCount >= 3 && currentV < chargeHighThreshold) {
      wasUSBPlugged = false;
      DBG_PRINTF("Discharging confirmed via negative slope (V=%.2fV)\n", currentV);
    }
  }

  // Fallback / anchor: high float threshold catches continuous charge on boot or after full saturation
  if (!wasUSBPlugged && currentV >= chargeHighThreshold) {
    wasUSBPlugged = true;
    DBG_PRINTF("Charging detected via float threshold (V=%.2fV)\n", currentV);
  } else if (wasUSBPlugged && currentV < chargeLowThreshold && negativeSlopeCount >= 1) {
    wasUSBPlugged = false;
    DBG_PRINTF("Unplugged detected via low threshold (V=%.2fV)\n", currentV);
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
  
  bool pctChanged = false;
  if (currentBatteryPct == -1) {
    currentBatteryPct = targetPct;
    lastBatteryPctUpdate = millis();
    pctChanged = true;
    updateUI();
  } else {
    if (currentBatteryPct != targetPct) {
      currentBatteryPct = targetPct;
      lastBatteryPctUpdate = millis();
      pctChanged = true;
      updateUI();
    }
  }
  
  DBG_PRINTF("Battery: %.2fV (Avg: %.2fV) Target: %d%% Disp: %d%%%s\n", 
             currentV, avgV, targetPct, currentBatteryPct, wasUSBPlugged ? " [Charging]" : "");

  // Dedicated Battery Telemetry Logging:
  // Log on initial boot, on every 1% battery change, or at least every 5 minutes (300,000ms)
  static unsigned long lastBatteryLogTime = 0;
  if (pctChanged || (millis() - lastBatteryLogTime >= 300000) || (lastBatteryLogTime == 0)) {
    lastBatteryLogTime = millis();
    int brightness = brightnessLevel;
    // wifiState: 0 = off/sleeping, 1 = connecting, 2 = connected / portal
    int wifiState = (WiFi.status() == WL_CONNECTED || isConfigMode) ? 2 : (WiFi.getMode() != WIFI_OFF ? 1 : 0);
    // bleState: 0 = uninit, 1 = advertising, 2 = connected
    int bleState = SugarotaBLE::getInstance().isConnected() ? 2 : (SugarotaBLE::getInstance().isInitialized() ? 1 : 0);
    uint32_t freeHeap = ESP.getFreeHeap();
    appendBatteryLog(avgV, currentBatteryPct, wasUSBPlugged, brightness, wifiState, bleState, freeHeap);
  }
  
  // Only shut down for low battery if USB is definitely not plugged in AND an actual depleted battery is connected.
  // When running purely on USB or without battery, ADC may read 0.0V - 1.5V; never shut down in that state.
  if (!wasUSBPlugged && !Serial) {
    if (avgV >= 2.0f && avgV < 3.00f && millis() >= 15000) {
      DBG_PRINTLN(F("CRITICAL BATTERY: Battery depleted below 3.00V, powering off device..."));
      powerOffDevice();
    }
  }
}
