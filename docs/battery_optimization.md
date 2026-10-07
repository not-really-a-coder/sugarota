# Battery Performance & Power Optimization Principles

This document defines the architectural principles and operational guardrails required to maintain maximum battery runtime (target: 20+ hours on full charge) across Sugarota ESP32-S3 hardware (V1 and V2) and the Android companion app.

---

## 0. Battery Chemistry & Hardware Architecture

### Cell Chemistry & Discharge Behavior
* **Form Factor & Capacity**: Sugarota hardware utilizes an **18650 Li-Ion cylindrical cell** (~2400–3500 mAh real capacity).
* **Discharge Curve Characteristics**:
  - Full charge float voltage is 4.15V–4.20V.
  - Initial IR drop and surface charge dissipation drop cell voltage quickly from ~4.20V to ~3.95V under active CPU/radio load.
  - The dominant discharge plateau sits between 3.60V and 3.80V, where the cell spends the majority of its operating cycle.
  - At very low current draw (screen off / BLE idle ~15–20 mA), the cell maintains a prolonged tail between 3.35V and 3.00V before reaching the 3.00V safety shutdown cutoff.
  - The percentage mapping curve in `battery.cpp` (`getBatteryPercentage()`) is calibrated to this plateau to prevent premature low-battery alarms while ensuring accurate empty warnings.

### Charging Circuitry & State Detection
* **Charger IC**: Waveshare hardware integrates an **ETA6098** Li-Ion charger.
* **Charge LED Status**:
  - The charger IC drives a green charge status LED directly via its `STAT` pin (pin 9).
  - The `STAT` line is strictly analog hardware: it is **not routed** to any ESP32-S3 GPIO or TCA9554 IO expander pin and cannot be queried via software.
  - Software charging detection relies on ADC rail voltage thresholds (hysteresis between `chargeHighThreshold` and `chargeLowThreshold`).

### Telemetry Logging
* **Diagnostic Battery Log (`/battery.log`)**:
  - Firmware records timestamp, battery voltage, percentage, charging status, screen status, and Wi-Fi status on LittleFS.
  - Entries are captured on boot, on every 1% battery change, or at least every 5 minutes (bounded to 16 KB).
  - Accessible via serial command `GET_BATTERY_LOG` (`just battery-log`) and cleared with `CLEAR_BATTERY_LOG` (`just clear-battery-log`).

---

## 1. Radio Power Management (Wi-Fi & BLE Coexistence)

The Wi-Fi and Bluetooth radios are by far the largest consumers of power on the ESP32-S3. Active Wi-Fi reception/transmission consumes 80–120 mA, compared to ~15–25 mA in idle/BLE sleep.

### Wi-Fi Radio
* **Deep Radio Off Between Fetches**:
  - Wi-Fi radio must never stay running in STA mode between background polls.
  - Call `sleepWiFi()` immediately after each fetch or failed attempt, which explicitly sets `WiFi.mode(WIFI_OFF)`.
  - Do NOT leave Wi-Fi in modem sleep while remaining in `WIFI_STA` mode (modem sleep leaks ~18–25 mA continuous baseline).
* **BLE Bridge Priority over Wi-Fi**:
  - When connected via BLE to the Android companion, never wake the Wi-Fi radio for routine polls. Rely exclusively on the phone companion push/bridge.
  - Keep fallback timeout to Wi-Fi at **35+ seconds** (never lower than 30s) to give Android background Doze/WorkManager time to respond without triggering an unnecessary Wi-Fi connection burst.
* **Web Server Listening Scope**:
  - `server.handleClient()` must only run when `isConfigMode` (Shake portal) or `isOTAUpdating` is active. Never run it during routine 1-second background Wi-Fi connects.

### BLE Radio & Advertising
* **Halt Background Advertising Once Connected**:
  - While a Central device (phone) is actively connected, background BLE advertising must be stopped (`SugarotaBLE::updateAdvertising()`).
  - Keep advertising active only during the boot pairing window (2 minutes) or when explicitly triggered in Config Mode.
* **Connection Interval & Slave Latency**:
  - BLE connection parameters negotiate a slave latency of 4 intervals (`updateConnParams(..., latency = 4, timeout = 6000)`).
  - This allows the ESP32 radio to skip up to 4 connection events and sleep when no new data is pending, while maintaining instant responsiveness when packets arrive.

---

## 2. Analog Sampling & Sensor Polling

* **Battery ADC Sampling Interval**:
  - Multisample ADC (`PIN_BAT_ADC`) every **30 to 60 seconds** (`lastBatCheck >= 30000`).
  - NEVER decrease this below 30s. Each sample performs a 30-sample burst read which consumes CPU and keeps ADC peripherals active.
* **IMU (Accelerometer) Polling**:
  - IMU polling (`pollIMU()`) should be throttled (300ms in normal mode, 50ms only during active timer ticking).
  - Shaking detection uses a multi-sample time-window accumulator (4 shake samples over >=1000ms) rather than instantaneous triggers to avoid spurious wakes.

---

## 3. Display, Backlight & GPU Redraw Hygiene

* **Display Canvas Flush Frequency**:
  - Full display canvas (`gfx->flush()`) pushes 172×640 pixels across the QSPI bus to the AXS15231B controller. Each flush requires significant CPU time and bus activity.
  - Do NOT trigger `updateUI()` on every loop iteration or on small battery voltage jitter.
  - Only call `updateUI()` when battery percentage (`currentBatteryPct`) or charging status (`wasUSBPlugged`) actually changes state.
  - During display-off states (`screenManuallyOff || brightnessLevel == 0`), completely guard `updateUI()` from running periodically.
* **Backlight Polarity and Deep Sleep Pin Holds**:
  - V1 (GPIO 8) and V2 (GPIO 42) both utilize inverted PWM logic (`val = 255 - level`).
  - On V2, the AP3032 boost converter is gated by `EXIO_PIN_BL_EN` on the TCA9554 expander. Disable boost converter whenever brightness is 0 or entering sleep.
  - During deep sleep (`powerOffDevice()`), lock the backlight PWM pin HIGH with `gpio_hold_en()` to ensure 0% LED draw.

---

## 4. Flash (LittleFS) Wear & I/O Reduction

* **Deferred Cache Writes**:
  - Do not write glucose history or preferences to LittleFS every minute.
  - Buffer cache writes in memory and only flush to flash every 30 minutes (`lastHistorySaveTime >= 1800000`) or cleanly during device shutdown (`powerOffDevice()`).

---

## 5. Android Companion BLE Overhead

* **Periodic Sync Alignment**:
  - Android BLE bridge schedules fetch calls timestamp-aligned to sensor epoch intervals plus a 3-second provider lag buffer, preventing redundant polling loops.
* **Single-Packet Updates**:
  - Routine polls only send current/differential readings.
  - Multi-chunk 48-reading historical backfills are restricted to initial pairing, user button force-refreshes, or genuine data gaps (>6 minutes).
