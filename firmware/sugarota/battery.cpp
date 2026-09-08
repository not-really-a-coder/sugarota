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
  float vMap[] = {3.00, 3.20, 3.40, 3.50, 3.55, 3.60, 3.65, 3.75, 3.85, 3.90, 3.95, 4.05};
  int pMap[]   = {   0,    5,   10,   20,   30,   40,   50,   60,   70,   80,   90,  100};
  
  if (voltage <= vMap[0]) return 0;
  if (voltage >= vMap[11]) return 100;
  
  for (int i = 0; i < 11; i++) {
    if (voltage >= vMap[i] && voltage <= vMap[i+1]) {
      float range = vMap[i+1] - vMap[i];
      float offset = voltage - vMap[i];
      float pRange = pMap[i+1] - pMap[i];
      return pMap[i] + (int)((offset / range) * pRange);
    }
  }
  return 100;
}

void updateBattery(bool isUSBPlugged) {
  float currentV = readBatteryVoltageSingle(); 

  // Detect USB state transitions
  if (isUSBPlugged && !wasUSBPlugged) {
    wasUSBPlugged = true;
    
    float lastUnpluggedV = (preSpikeVoltage > 0) ? preSpikeVoltage : ((currentBatteryVoltage > 0) ? currentBatteryVoltage : currentV);
    
    chargingOffset = currentV - lastUnpluggedV;
    if (chargingOffset < 0.05 || chargingOffset > 0.35) {
      chargingOffset = 0.15;
    }
    
    fillVoltageHistory(currentV);
    
    DBG_PRINTF("USB Plugged In. Last Unplugged: %.2fV, Current: %.2fV, Offset: %.2fV\n", 
               lastUnpluggedV, currentV, chargingOffset);
  } 
  else if (!isUSBPlugged && wasUSBPlugged) {
    wasUSBPlugged = false;
    chargingOffset = 0.0;
    
    fillVoltageHistory(currentV);
    
    float avgV = currentV;
    currentBatteryVoltage = avgV;
    int targetPct = getBatteryPercentage(avgV);
    currentBatteryPct = targetPct;
    lastBatteryPctUpdate = millis();
    lastUSBUnplugTime = millis();
    
    updateUI();
    DBG_PRINTF("USB Plugged Out. Real Battery: %.2fV, Pct: %d%%\n", currentV, currentBatteryPct);
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
  
  float estimatedV = avgV - chargingOffset;
  currentBatteryVoltage = estimatedV;

  static bool bootVoltageChecked = false;
  static bool bootedLow = false;
  if (!bootVoltageChecked) {
    if (estimatedV < 3.00) {
      bootedLow = true;
      DBG_PRINTF("Boot voltage checked: %.2fV (Low, < 3.00V). Will monitor for 10s.\n", estimatedV);
    } else {
      DBG_PRINTF("Boot voltage checked: %.2fV (Normal, >= 3.00V).\n", estimatedV);
    }
    bootVoltageChecked = true;
  }

  int targetPct = getBatteryPercentage(estimatedV);
  
  if (currentBatteryPct == -1) {
    currentBatteryPct = targetPct;
    lastBatteryPctUpdate = millis();
  } else {
    if (millis() - lastUSBUnplugTime < 30000) {
      if (currentBatteryPct != targetPct) {
        currentBatteryPct = targetPct;
        lastBatteryPctUpdate = millis();
        updateUI();
      }
    } else {
      if (millis() - lastBatteryPctUpdate >= 60000) {
        if (targetPct > currentBatteryPct) {
          currentBatteryPct++;
        } else if (targetPct < currentBatteryPct) {
          currentBatteryPct--;
        }
        lastBatteryPctUpdate = millis();
        updateUI();
      }
    }
  }
  
  DBG_PRINTF("Battery: %.2fV (Avg: %.2fV, Est: %.2fV) Target: %d%% Disp: %d%%\n", 
             currentV, avgV, estimatedV, targetPct, currentBatteryPct);
  if (isUSBPlugged) {
      DBG_PRINTLN("-> STATUS: USB Charging Detected (GPIO16 LOW)");
  }
  
  if (!isUSBPlugged) {
    if (bootedLow && millis() >= 15000) {
      DBG_PRINTLN(F("CRITICAL BATTERY: Booted with low voltage, shutting down after 15s..."));
      powerOffDevice();
    }
    if (millis() >= 15000 && estimatedV < 3.00) {
      DBG_PRINTLN(F("CRITICAL BATTERY: Voltage below 3.00V, shutting down..."));
      powerOffDevice();
    }
  }
}
