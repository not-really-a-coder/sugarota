# Sugarota System Architecture

This document provides a comprehensive technical overview of the Sugarota hardware and firmware architecture, inter-subsystem data pipelines, state management, and memory model.

---

## 1. High-Level Architecture Overview

Sugarota is an ultra-compact, low-power continuous glucose telemetry monitor powered by the **Espressif ESP32-S3** microcontroller paired with a 3.49" high-DPI display. It operates in two cooperative networking topologies:

```text
┌────────────────────────┐                   ┌────────────────────────┐
│  Nightscout / Dexcom   │                   │    Android Companion   │
│     Cloud Server       │                   │       Smartphone       │
└───────────┬────────────┘                   └───────────┬────────────┘
            │ HTTPS                                      │ BLE Encrypted (GATT)
            ▼                                            ▼
 ┌──────────────────────────────────────────────────────────────────┐
 │                     Sugarota ESP32-S3                            │
 │                                                                  │
 │   ┌───────────────────────┐         ┌────────────────────────┐   │
 │   │ Net Client (Wi-Fi)    │         │ SugarotaBLE (NimBLE)   │   │
 │   │ - Dexcom Share Auth   │         │ - AES-128 Encrypted    │   │
 │   │ - Nightscout REST     │         │ - Numeric Comparison   │   │
 │   │ - SNTP Clock Sync     │         │ - Push Glucose Stream  │   │
 │   └───────────┬───────────┘         └───────────┬────────────┘   │
 │               │                                 │                │
 │               └────────────────┬────────────────┘                │
 │                                ▼                                 │
 │                 ┌─────────────────────────────┐                  │
 │                 │    Core State & Ingestion   │                  │
 │                 │  - Dual-Schema Normalizer   │                  │
 │                 │  - Circular BG History      │                  │
 │                 │  - LittleFS Cache Storage   │                  │
 │                 └──────────────┬──────────────┘                  │
 │                                │                                 │
 │     ┌──────────────────────────┼──────────────────────────┐      │
 │     ▼                          ▼                          ▼      │
 │ ┌───────────────┐     ┌────────────────┐         ┌────────────┐  │
 │ │ Display Engine│     │ Power & Sensor │         │ Web Portal │  │
 │ │ - AXS15231B   │     │ - QMI8658 IMU  │         │ - AP Mode  │  │
 │ │ - QSPI Canvas │     │ - Calibrated   │         │ - mDNS     │  │
 │ │ - Trend Graph │     │   Battery ADC  │         │ - Captive  │  │
 │ │ - Harvey Ball │     │ - PCF85063 RTC │         │   Portal   │  │
 │ └───────────────┘     └────────────────┘         └────────────┘  │
 └──────────────────────────────────────────────────────────────────┘
```

---

## 2. Core Subsystems

### 2.1 Display Subsystem (`Arduino_GFX` + `AXS15231B`)
- **Interface**: Quad SPI (QSPI) utilizing pins `CS=9, PCLK=10, D0=11, D1=12, D2=13, D3=14`.
- **Canvas Rendering**: Uses a double-buffered 172x640 canvas (`Arduino_Canvas`) rotated into landscape orientation (640x172) for 100% flicker-free rendering.
- **Backlight PWM**: Controlled via GPIO 8 with smooth brightness levels (`0` to `255`).

### 2.2 Telemetry & Data Ingestion
Sugarota maintains an in-memory sorted circular buffer of `MAX_HISTORY = 48` glucose records:
- **Index 0**: Always guaranteed to be the most recent reading.
- **Dual Schema Support**: Seamlessly ingests verbose JSON schemas from cloud endpoints as well as ultra-compact byte-saving keys (`v`, `d`, `dl`, `t`) from the BLE companion app.
- **Persistence**: Automatically synced to `/history.cache` every 30 minutes or during clean shutdown to protect SPI flash from premature wear.

### 2.3 Connectivity & Power Architecture
Sugarota supports three operating modes configured in `config.json`:
1. **`AUTO` (Default)**: Prioritizes low-power BLE companion connectivity. Automatically switches Wi-Fi radio off when smartphone is actively streaming. If BLE stream is stale (>10 min), gracefully activates Wi-Fi to poll cloud servers directly.
2. **`BLE_ONLY`**: Disables Wi-Fi permanently. Relies exclusively on the Android Companion background foreground service, extending battery life significantly.
3. **`WIFI_ONLY`**: Disables BLE peripheral advertising. Directly contacts Dexcom Share or Nightscout APIs on a periodic interval (30s to 300s).

### 2.4 Power Management & Sensors
- **Battery Monitoring**: High-accuracy ESP32-S3 internal ADC calibration scheme (`adc_oneshot` with curve fitting). Employs rolling median and slope detection to filter out USB charging noise.
- **IMU & Gestures (`QMI8658`)**:
  - **Shake Detection**: Vigorously shaking device enters Wi-Fi Access Point **Config Mode** (`Sugarota-Setup`).
  - **Face Down**: Placing screen face down automatically dims backlight to `0` to conserve battery. Picking up instantly restores brightness.
  - **Timer Mode**: Rotating device 90° horizontally activates an integrated count-up timer with audible chime feedback.
- **Hardware RTC (`PCF85063`)**: Backed by battery power to preserve UTC timestamps across deep sleep and hard reboots. Synchronized on every phone connect or NTP sync.

---

## 3. Firmware Directory Organization & Modular Architecture

Firmware source files are located in [`firmware/sugarota/`](../firmware/sugarota/):

```text
firmware/sugarota/
├── config.h             # Pins, colors, enums (Provider, BGUnits), BGReading, extern globals
├── storage.h / .cpp     # LittleFS config load/save, history cache serialization
├── battery.h / .cpp     # ADC curve fitting calibration, voltage smoothing, battery % mapping
├── audio.h / .cpp       # ES8311 codec driver, square-wave beeps, WAV audio streaming
├── display.h / .cpp     # GFX canvas setup, brightness PWM, theme inversion, number formatters
├── net_client.h / .cpp  # Wi-Fi STA loop, Dexcom Share & Nightscout HTTPS clients, RTC sync
├── web_portal.h / .cpp  # WebServer routes, embedded captive portal HTML/JS, REST config API
├── ui.h / .cpp          # Status bar, glucose container, Harvey ball, trend arrows, chart
├── input.h / .cpp       # Button debouncing, touch controller, QMI8658 IMU (shake, face-down, timer)
├── ble_handler.h / .cpp # BLE glucose normalization, history sorting/eviction, pairing dialogs
├── sugarota.ino         # System setup(), main loop(), task scheduling, serial CLI
├── sugarota_ble.h / .cpp# NimBLE GATT server, 128-bit encryption, bonding
├── qrcode.h / .c        # Embedded QR code generator library
├── partitions.csv       # 16MB custom partition scheme (OTA + FFat + NVS)
└── src/                 # In-tree Waveshare audio codec & TCA9554 expander drivers
```
