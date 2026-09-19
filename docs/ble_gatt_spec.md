# Sugarota BLE GATT Specification & Android Companion Guide

This document defines the Bluetooth Low Energy (BLE) interface for Sugarota on the ESP32-S3 and details the architecture for the Android companion application.

---

## 1. BLE Protocol Specification

### Peripheral Identification
* **Advertised Device Name**: `Sugarota-XXXX` (where `XXXX` represents the last 2 bytes of the Bluetooth MAC address in uppercase hex).
* **Primary Service UUID**: `6E400001-B5A3-F393-E0A9-E50E24DCCA9E`
* **MTU**: Dynamically negotiated up to **517 bytes** (`ATT_MTU = 512` payload bytes).

### GATT Characteristics

| Characteristic | UUID | Properties | Format / Payload Description |
| :--- | :--- | :--- | :--- |
| **Glucose Data & Command Stream** | `6E400002-B5A3-F393-E0A9-E50E24DCCA9E` | `WRITE`, `WRITE_NR`, `WRITE_ENC`, `NOTIFY` | JSON format. Protected via authenticated BLE encryption.<br>• Glucose: `{"sgv": 115, "direction": "Flat", "delta": -2, "timestamp": 1725450000}`<br>• Remote API Signals: `{"type": "api_ok"}` (server queried, no new reading), `{"type": "api_err", "msg": "Network error"}` (triggers Wi-Fi fallback)<br>• Remote Commands: `{"cmd": "set_brightness", "val": 204}`, `{"cmd": "set_theme", "val": "dark"|"light"}`, `{"cmd": "find_device"}`, `{"cmd": "reboot"}`, `{"cmd": "power_off"}` |
| **Device Config** | `6E400003-B5A3-F393-E0A9-E50E24DCCA9E` | `READ`, `READ_ENC`, `WRITE`, `WRITE_ENC`, `NOTIFY` | Protected via authenticated BLE encryption. Reads or writes the full device `/config.json`. Single JSON write or chunked `[START]...[END]` writes. Writing valid JSON commits to LittleFS and reboots device in 1s. |
| **Device Status** | `6E400004-B5A3-F393-E0A9-E50E24DCCA9E` | `READ`, `NOTIFY` | Device telemetry notification broadcast upon connection, state changes, poll timer expiry, or routine heartbeat.<br>Payload: `{"battery": 85, "charging": true, "version": "v0.09.19.18", "brightness": 204, "dark_theme": true, "request_refresh": true}` |
| **OTA Stream & Control** | `6E400005-B5A3-F393-E0A9-E50E24DCCA9E` | `WRITE`, `WRITE_NR`, `WRITE_ENC`, `NOTIFY` | Protected via authenticated BLE encryption. Dual-partition rollback safe firmware stream. Commands: `OTA_BEGIN` $\to$ binary chunks $\to$ `OTA_END` (reboots into new partition). |

### Security & Pairing Mode Architecture
* **Pairing Protocol**: BLE Security Manager Protocol (SMP) using **LE Secure Connections (SC)** with Man-in-the-Middle (MITM) protection and **Numeric Comparison** (`BLE_HS_IO_DISPLAY_YESNO`).
* **Pairing Mode Windows**:
  To protect against rogue strangers or nearby phones spamming pairing requests in public, open discovery/pairing is only enabled during designated windows:
  1. **Boot Window**: Active during the device boot sequence and initial companion check.
  2. **Config Mode Window (Shake)**: Active when the device is shaken into Config Mode (5-minute auto-timeout).
  3. **Normal Running State**: Pairing mode is disabled. Unbonded pairing requests are rejected immediately.
* **Pairing Flow**:
  1. When an unbonded phone connects during a Pairing Mode window and accesses a protected characteristic, a 6-digit PIN is generated.
  2. Sugarota LCD displays a modal: *"Pair Phone? Code: XXXXXX - Confirm code matches on phone"* with **YES** and **NO** buttons.
  3. The phone OS presents a matching confirmation prompt.
  4. Upon mutual confirmation, a 128-bit Long Term Key (LTK) is stored in NVS.
  5. Subsequent reconnects are automatic, instantaneous, and silently encrypted.
* **Multi-Device Support**: 
  - Supports up to 2 concurrent connected smartphones (`MAX_BLE_CLIENTS = 2`).
  - When 1 phone is connected, Sugarota continues advertising to bonded devices on its whitelist, allowing a second family smartphone to connect seamlessly without disconnecting the first.
  - Supports up to 3 bonded smartphones saved in NVS simultaneously.

---

## 2. Android Companion App Architecture

### Recommended Tech Stack
* **Language**: Kotlin
* **UI**: Jetpack Compose (Material 3 with AMOLED Dark Theme matching Sugarota styling)
* **Architecture**: MVVM + Clean Architecture + Kotlin Coroutines & StateFlow
* **Networking**: Retrofit2 / OkHttp (Nightscout API & Dexcom Share API client)
* **Background Tasks**: Android **Foreground Service** with a non-intrusive ongoing notification (e.g. *"Sugarota: Connected · Synced 1m ago"*).

### Data Flow (Bridge Mode)
```text
[Phone Background Service]
        │
        ├── 1. Every 1-5 mins: Checks cellular/Wi-Fi connection
        ├── 2. Queries Nightscout / Dexcom Share API via HTTPS
        ├── 3. Parses response, extracts latest SGV, delta, and trend
        │
        └── 4. Writes compact JSON to Sugarota BLE Characteristic 0x0002
                     │
                     ▼
             [ESP32-S3 Sugarota]
        - Disables Wi-Fi radio (saves battery)
        - Updates 3.49" LCD instantly
        - Displays "B" (Bluetooth) icon on status bar
```

### Config Sync Flow
1. App connects to Sugarota over BLE.
2. App reads Characteristic `0x0003` to retrieve current `/config.json`.
3. App populates the configuration settings form:
   - Primary & Backup Wi-Fi SSIDs/Passwords
   - Nightscout URL & API Secret
   - Dexcom Share Username, Password, and Server
   - Provider selector (Dexcom vs Nightscout)
   - BG Units (`mg/dL` vs `mmol/L`)
   - Timezone NTP offset and DST
4. User edits settings and taps **Save to Device**.
5. App writes JSON string to Characteristic `0x0003`.
6. Sugarota validates JSON, writes directly to LittleFS `/config.json`, and reboots.
