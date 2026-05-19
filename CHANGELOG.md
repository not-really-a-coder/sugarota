# Sugarota Changelog

All notable changes to the Sugarota project will be documented in this file. This project utilizes the Calendar Versioning (CalVer) format: v{YearOffset}.{Month:02d}.{Day:02d}.{Build}.

## [v0.05.19.20] — Stability and Power Improvements (2026-05-19)

This release focuses on battery tracking accuracy, button interaction improvements, manual screen locking, and robust WiFi failover handling.

### Connectivity

- Smart WiFi SSID preference memory (automatically priority-tries the last successfully connected SSID)

### Power Management

- Step-by-step battery percentage dampener that adjusts smoothly both up and down to match actual charging/discharging
- Instant charging voltage-spike detection on USB plug-in
- Immediate hardware shutdown during PWR button hold (triggers after 2.0 seconds on battery)
- Immediate forced data refresh during BOOT button hold (triggers after 1.5 seconds)
- Screen clear to black on shutdown to prevent "Powering Off" message from freezing on screen during next boot

### Interface

- Pocket-mode: orientation polling and touch screen completely disabled when screen is at 0% brightness
- Waking up the screen from manual-off instantly on PWR button press instead of release, bypassing shutdown checks
- Deep sleep GPIO pad hold release on startup to fix black screen wake-up bug

---

## [v0.05.18.1] — Initial Stable Release (2026-05-18)

This is the initial release of the Sugarota ESP32-S3 Glucose Monitor and setup environment.

### Connectivity

- Dexcom and Nightscout API integrations (BG reading, trend, delta, time of reading)
- Dual-band Wi-Fi backup configuration with automatic failover
- One minute data refresh rate
- NTP timezone synchronization with local offsets and daylight savings

### Power Management

- Power Off (long press PWR button)
- Face-down screen off gesture via IMU orientation detection
- TCA9554 IO expander integration for hardware power control
- Battery level monitoring (initial)
- Auto Wi-Fi off between data fetches to save battery power

### Interface

- mg/dL and mmol/L unit support (long tap on delta to change)
- Touchscreen interface for layout navigation and setup
- Shake-to-refresh-data gesture via IMU sensor
- Screen brightness control with 5 levels (single press on PWR button)
- Default dark theme (pixelated console)
- Light theme (single press on BOOT button) to use as a soft flashlight during night
- Interactive high-DPI 4-hour historical trend graph plotting
- Timer mode on device rotation with sound alarms (for pauses between injection and meal)
- QMI8658 accelerometer telemetry integration
- DOS-style spinner, indicating data refresh
- Local time

### Installation & Configuration

- Web-based firmware installer and device configurator using WebSerial
- Calendar Versioning (CalVer) auto-incrementing build pre-processor
- Auto-generation of local config file on web submit saving
- Debug Mode (turn on to see logs in the installer and battery voltage on screen)