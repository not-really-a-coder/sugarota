# Sugarota Changelog

All notable changes to the Sugarota project will be documented in this file. This project utilizes the Calendar Versioning (CalVer) format: v{YearOffset}.{Month:02d}.{Day:02d}.{Build}.

---

## [v0.05.18.1] — Initial Stable Release (2026-05-18)

This is the initial release of the Sugarota ESP32-S3 Glucose Monitor and setup environment.

### Connectivity

- Dexcom and Nightscout API integrations (BG reading, trend, delta, time of reading)
- Dual-band Wi-Fi backup configuration with automatic failover
- One minute data refresh rate
- NTP timezone synchronization with local offsets and daylight savings

### Battery

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