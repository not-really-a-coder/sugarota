# Security Model — Sugarota

## Architecture

Sugarota is designed with a strict **"Local-First, Privacy-by-Default"** security model. 

The device serves as a dedicated personal telemetry monitor for continuous glucose data. It operates completely independently of third-party cloud infrastructure beyond direct HTTPS communication with your designated CGM cloud provider (Nightscout or Dexcom Share).

Key architectural tenets:
- **Zero Cloud Intermediaries**: Sugarota does not run, connect to, or route telemetry through any Sugarota-operated servers, telemetry analytics, or third-party proxies.
- **On-Device Encrypted Storage**: All sensitive credentials (Wi-Fi pre-shared keys, Nightscout API secrets, and Dexcom account tokens) are stored exclusively in on-device flash memory (LittleFS / NVS) and never transmitted to external endpoints.
- **Direct Point-to-Point Bluetooth**: Communication between the Android companion app and the Sugarota hardware display occurs strictly point-to-point over Bluetooth Low Energy (BLE) with authenticated link encryption.

---

## Threat Model

| Threat | Mitigation |
|:---|:---|
| Rogue device BLE pairing / hijacking | LE Secure Connections (SC) with Numeric Comparison (`BLE_HS_IO_DISPLAY_YESNO`); unbonded pairing is restricted to designated time-bounded windows (boot and manual config mode) |
| Eavesdropping on wireless BLE telemetry | AES-128 link encryption with authenticated 128-bit Long Term Keys (LTK) negotiated during numeric pairing |
| Rogue BLE control / unwanted commands | All writable characteristics (`CHAR_GLUCOSE`, `CHAR_CONFIG`, `CHAR_OTA`) enforce authenticated encryption (`WRITE_ENC`); unauthenticated writes are rejected at the GATT layer |
| Unsolicited pairing spam in public | Pairing mode auto-closes 60 seconds after boot; subsequent pairing attempts from unknown devices are rejected immediately unless device is shaken into Config Mode |
| Unwanted background auto-reconnections | Explicit manual disconnect in the Android companion app suppresses passive reconnect scans and BLE auto-connect loops until explicitly initiated by the user |
| Wi-Fi credential exfiltration via local installer | WebSerial installer runs 100% client-side in the browser via Web Serial API; `utils/run_web_installer.py` serves static files locally on loopback (`127.0.0.1`) with zero telemetry |
| Wi-Fi AP exposure / unauthorized config access | Local Access Point Config Mode (`http://sugarota.local`) auto-terminates after a strict 5-minute inactivity timeout |
| Malicious firmware tampering / OTA corruption | Dual-partition A/B scheme (`ota_0` / `ota_1`) with CRC and optional MD5 hash verification; incomplete or corrupted images abort before boot flag commitment, preventing bricking |
| Remote Wi-Fi attack surface | Web portal and OTA HTTP endpoints operate strictly on the local area network subnet without opening UPnP, WAN port forwards, or public internet listeners |
| Third-party tracking or data harvesting | No analytics SDKs, trackers, crash reporting libraries, or advertising identifiers are included in either the firmware or Android companion app |

---

## Bluetooth Low Energy (BLE) Security & Pairing Flow

1. **Pairing Mode Window**:
   - **Boot Window**: Open for 60 seconds upon device power-up to allow initial pairing with family devices.
   - **Config Mode Window**: Manually opened by sustained shaking (~1.5s); can be dismissed early by shaking again or via automatic 5-minute timeout.
   - **Running State**: Pairing mode automatically disables; unbonded central pairing requests are rejected immediately.

2. **Numeric Comparison Authentication**:
   - Central initiates connection and requests pairing using LE Secure Connections (SC).
   - ESP32-S3 generates a random 6-digit passkey displayed on the 3.49" LCD alongside a confirmation dialog.
   - User verifies that the 6-digit code on the Sugarota screen matches the prompt on the smartphone OS before confirming on both devices.

3. **Key Storage, Bonding & Connection Controls**:
   - Upon mutual confirmation, a 128-bit Long Term Key (LTK) is stored in ESP32 non-volatile storage (NVS).
   - Up to 3 bonded central devices are remembered simultaneously, supporting up to 2 concurrent connected smartphones (`MAX_BLE_CLIENTS = 2`).
   - Reconnections from bonded devices resume automatically with silent AES-128 encryption.
   - Users can manually disconnect a bonded device at any time from the Android companion app. Manual disconnect enters a protected state that suppresses automatic background reconnection and opportunistic GATT re-linking until the user explicitly requests reconnection.

---

## Data Privacy & Credential Handling

- **Local Credentials**: Wi-Fi passwords and CGM API secrets are written directly to `/config.json` in local flash using LittleFS.
- **Provider Authentication**:
  - **Dexcom Share**: Account credentials (username and password) are used solely to negotiate a temporary session ID directly with Dexcom's official API endpoints over TLS/HTTPS.
  - **Nightscout**: API secrets or access tokens are used solely to construct standard authorization headers for HTTPS requests directly to your self-hosted or managed instance.
- **Web Installer**: Flashing and configuration via GitHub Pages executes directly in the user's web browser using WebSerial. No device data, credentials, or logs are uploaded to any external server.
