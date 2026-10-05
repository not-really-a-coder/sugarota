# Firmware (ESP32-S3) Module Map

## Critical Scope Directive
- **Vendor Drivers Scope**: `firmware/sugarota/src/` contains ~8,000 lines of vendor driver code (`esp_codec_dev`, `codec_board`, `tca9554`). **DO NOT index, search, or edit `src/`** unless modifying low-level I2C/I2S audio hardware drivers.
- **Hardware Reference**: If investigating display/touch registers, check `extras/waveshare_lcd` (v1/v2), not online guesswork.

## Core Source Map (`firmware/sugarota/`)
- `sugarota.ino`: Main setup/loop, state machine, WiFi connection, and OTA handlers.
- `config.h`: Compile-time definitions, hardware pinouts (Waveshare 1.69" ESP32-S3 Touch LCD), and default settings.
- `storage.cpp` / `.h`: Preferences NVS read/write, credential management, reset logic.
- `ble.cpp` / `.h`: NimBLE initialization, advertising, and connection lifecycle.
- `ble_handler.cpp` / `.h`: GATT service, characteristic callbacks, configuration exchange.
- `display.cpp` / `.h`: ST7789 display controller, backlight PWM, and sleep/wake dimming.
- `ui.cpp` / `.h`: UI layouts, glucose dial/needle, stats, and alert overlays.
- `input.cpp` / `.h`: CST816T touch controller driver and gesture handling.
- `audio.cpp` / `.h`: ES8311 codec driver wrapper and audio playback.
- `battery.cpp` / `.h`: ADC voltage sampling and battery percentage curve.
- `net_client.cpp` / `.h`: HTTP client for Dexcom Share and Nightscout APIs.
- `web_portal.cpp` / `.h`: Captive AP portal for standalone WiFi onboarding.
- `partitions.csv`: Custom 16MB flash partition layout.
- `docs/battery_optimization.md`: Mandatory power budgeting rules (radio sleep, ADC interval >=30s, BLE slave latency, canvas flush guards).
