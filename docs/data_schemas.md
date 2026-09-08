# Sugarota Data Schemas

This document defines the complete JSON schemas used across Sugarota's storage, WebSerial API, and BLE GATT telemetry streams.

---

## 1. Device Configuration (`/config.json`)

Stored persistently in ESP32 LittleFS root (`/config.json`). Also returned by the WebSerial API command `GET_CONFIG` and HTTP endpoint `GET /api/config`.

```json
{
  "wifi": {
    "primary_ssid": "Home-WiFi",
    "primary_pass": "SecretPassword123",
    "secondary_ssid": "Phone-Hotspot",
    "secondary_pass": "BackupPassword456"
  },
  "provider": "DEXCOM",
  "nightscout": {
    "url": "https://my-nightscout.herokuapp.com",
    "secret": "your-api-secret-hash"
  },
  "dexcom": {
    "user": "dexcom_username",
    "pass": "dexcom_password",
    "server": "shareous1.dexcom.com"
  },
  "connection_mode": "AUTO",
  "poll_interval_sec": 60,
  "units": "mg/dL",
  "timezone": {
    "ntp": "pool.ntp.org",
    "offset": 10800,
    "daylight": 0
  },
  "debug": false
}
```

### Field Definitions

| Field | Type | Options / Default | Description |
| :--- | :--- | :--- | :--- |
| `wifi.primary_ssid` | String | - | Main 2.4GHz Wi-Fi SSID |
| `wifi.primary_pass` | String | - | Main Wi-Fi password (WPA2/WPA3 Personal) |
| `wifi.secondary_ssid` | String | - | Fallback Wi-Fi / smartphone hotspot SSID |
| `wifi.secondary_pass` | String | - | Fallback Wi-Fi password |
| `provider` | String | `"DEXCOM"`, `"NIGHTSCOUT"` | Active CGM cloud data provider |
| `nightscout.url` | String | - | Full HTTPS URL of Nightscout instance |
| `nightscout.secret` | String | - | Plain text API secret or SHA1 hash |
| `dexcom.server` | String | `"shareous1.dexcom.com"` (US/Global), `"share2.dexcom.com"` (US Alt), `"share1a.dexcom.com"` (Non-US) | Dexcom Share authentication endpoint |
| `connection_mode` | String | `"AUTO"`, `"BLE_ONLY"`, `"WIFI_ONLY"` | Radio strategy & battery conservation mode |
| `poll_interval_sec` | Integer | `30` to `600` (Default: `60`) | Polling period in seconds when on Wi-Fi |
| `units` | String | `"mg/dL"`, `"mmol/L"` | Blood glucose unit display on screen |
| `timezone.offset` | Integer | Seconds (e.g. `10800` for UTC+3) | Raw UTC offset in seconds |
| `timezone.daylight` | Integer | Seconds (e.g. `3600` for DST) | Daylight saving time adjustment in seconds |

---

## 2. Version State (`data/version_state.json`)

Used by `watch_version.py` to manage CalVer auto-incrementing:

```json
{
  "last_date": "2026-09-08",
  "build_increment": 24
}
```

- **Format**: `v{YearOffset}.{Month:02d}.{Day:02d}.{Build}` (e.g. `v0.09.08.24`).

---

## 3. BLE Telemetry Schemas

### 3.1 Glucose Streaming Packet (Characteristic `0x0002`)
Sugarota accepts both verbose and compact format. The compact schema is recommended to conserve MTU and Bluetooth transmission power:

#### Compact Schema (Recommended):
```json
{
  "time": 1725801600,
  "v": 115,
  "d": "Flat",
  "dl": -2,
  "t": 1725801600,
  "history": [
    { "v": 118, "d": "Flat", "dl": 1, "t": 1725801300 },
    { "v": 117, "d": "Flat", "dl": -3, "t": 1725801000 }
  ]
}
```

#### Verbose Schema:
```json
{
  "sgv": 115,
  "direction": "Flat",
  "delta": -2,
  "timestamp": 1725801600,
  "history": [
    { "sgv": 118, "direction": "Flat", "delta": 1, "timestamp": 1725801300 }
  ]
}
```

### 3.2 Device Telemetry Notification (Characteristic `0x0004`)
Broadcast by Sugarota to connected smartphones every 10s or when battery/charging state shifts:

```json
{
  "battery": 85,
  "charging": true,
  "voltage": 4.12,
  "version": "v0.09.08.24"
}
```
