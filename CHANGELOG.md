# Sugarota Changelog

All notable changes to the Sugarota project will be documented in this file. This project utilizes the Calendar Versioning (CalVer) format: v{YearOffset}.{Month:02d}.{Day:02d}.{Build}.

## [v0.09.08.25] — Modular Architecture, BLE Reliability & Status Bar Enhancements (2026-09-08)

This release reorganizes the firmware codebase into clean modules, enhances BLE data synchronization reliability and companion app bridging, and refines the status bar iconography and layout.

### Architecture & Repository

- Modularized monolithic firmware into focused domain components (`display`, `ui`, `net_client`, `web_portal`, `battery`, `audio`, `storage`, `input`, `ble_handler`)
- Moved Arduino firmware and board drivers into dedicated `firmware/sugarota/` directory
- Centralized system and protocol documentation into the `docs/` directory
- Hardened `.gitignore` and purged build cache trees from Gradle and intermediate Arduino compilation artifacts

### Interface & Status Bar

- Replaced text Bluetooth 'B' with a scaled pixelated Bluetooth icon
- Added pixelated Wi-Fi signal icon indicating active connection and background data transmission
- Adjusted battery icon height to 14px to match font cap height and align baseline across all status bar glyphs
- Adjusted Harvey ball touchscreen hit detection coordinates and clamped reading age calculation to eliminate clock skew issues
- Maintained status bar clock, battery gauge, and BLE indicators even when NTP synchronization fails by falling back to local time and hardware RTC
- Suppressed spurious OFFLINE indicator while an active BLE companion bridge connection is present
- Restored visual data fetch spinner feedback during manual refreshes triggered via BLE

### Connectivity & Bluetooth

- Synchronized boot splash screen to wait for complete glucose and history packets before rendering dashboard
- Added timestamp-based buffer eviction to guarantee fresh readings overwrite the oldest data points when the 48-reading cache is full
- Mitigated FreeRTOS task stack overflows by deferring BLE UI update triggers to the main Arduino loop
- Implemented immediate time and timezone offset synchronization upon companion app BLE connection
- Merged historical readings by timestamp across reconnects to eliminate gaps and chart line dropouts

### Android Companion App

- Initial stable release
- Configured default BLE MTU to 517 bytes to optimize throughput for large payloads
- Expanded background data sync requests to 48 readings for Nightscout and Dexcom Share
- Sorted historical reading arrays descending by timestamp to ensure chronological delivery
- Streamlined connection retry handling and telemetry logging for companion bridge operations

## [v0.09.04.6] — Bluetooth Low Energy (BLE) & Android Companion Integration (2026-09-04)

This release integrates high-efficiency Bluetooth Low Energy (BLE) GATT connectivity using NimBLE-Arduino, preparing Sugarota for direct Android companion app bridging, wireless configuration sync, and OTA updating.

### Bluetooth & Connectivity

- Integrated NimBLE-Arduino peripheral stack with dynamic naming (`Sugarota-XXXX` using hardware BT MAC address)
- Implemented BLE GATT Glucose Stream characteristic (`0x0002`) allowing companion apps to bridge data directly to the device without device Wi-Fi
- Implemented BLE GATT Config characteristic (`0x0003`) with two-way read/write sync directly to device `/config.json` on LittleFS
- Implemented BLE GATT Status telemetry characteristic (`0x0004`) broadcasting battery percentage, charging state, and firmware version
- Implemented BLE GATT OTA flashing characteristic (`0x0005`) with rollback-safe dual partition writes
- Added visual Bluetooth status indicator ('B' icon) to the top status bar when a central device is connected
- Bypassed periodic Wi-Fi radio wakeups when active BLE bridge connection is present to dramatically conserve battery

## [v0.09.04.0] — Hardware-Calibrated ADC, Interface Inversion & Build Optimization (2026-09-04)

This release upgrades battery voltage measurement to ESP-IDF hardware calibration, refines hardware button interaction gestures, and accelerates command-line compilation.

### Power Management

- Upgraded battery ADC sensing to ESP-IDF oneshot curve-fitting calibration scheme on GPIO 4 with hardware divider scaling
- Replaced raw uncalibrated readings with factory eFuse calibrated millivolt conversion for precise battery percentages

### Interface

- Inverted BOOT/Config button gestures: single short press forces immediate data refresh
- Inverted BOOT/Config button gestures: long press (1.5s hold) cycles theme between dark and light

### Build & Toolchain

- Added persistent object caching to build.ps1 and build.bat to prevent full recompilation on single-line edits
- Implemented real-time compilation progress bar with percentage, monotonic stage tracking, and active elapsed time
- Relocated vendor LCD example trees to extras directory and excluded them in .gitignore to eliminate unnecessary recursive compiler scans

## [v0.09.03.0] — Firmware Optimizations & Build Environment Setup (2026-09-03)

This release focuses on establishing command-line build automation, modernizing JSON handling, reducing flash memory write cycles, and cleaning up dead code.

### Firmware & Performance

- Deferred history cache writes to LittleFS from every minute to every 30 minutes and during shutdown to reduce flash memory wear
- Replaced deprecated DynamicJsonDocument allocations with ArduinoJson 7 JsonDocument
- Converted blood glucose unit tracking from dynamic String comparisons to an internal enum representation
- Replaced dynamic String formatting with safe stack buffers across status and metric displays
- Removed unused esp_partition header and unused buzzer pin definition
- Initialized LittleFS filesystem once at boot rather than on each read/write call

### Build & Toolchain

- Added automated verbose build script build.ps1 with target board configuration flags
- Added requirements.md documenting prerequisites, Arduino CLI toolchain commands, and hardware settings

## [v0.06.02.2] — Web Installer and Hotspot Connectivity (2026-06-02)

This release focuses on improving the initial device flashing experience and ensuring reliable wireless configuration on restrictive networks like Android mobile hotspots.

### Installation & Configuration

- Separated OTA Update from Full Install in the web installer interface
- Web installer automatically detects blank devices via serial output and defaults to full flash installation
- Removed mandatory validation for Wi-Fi credentials in the web configurator
- Web installer now reliably fetches firmware files across local and GitHub Pages hosting environments
- Changed default port of local web server for installer from 8000 to 8123 to avoid conflicts with other applications

### Interface

- Added device IP address display on the screen when Config Mode is active
- Added a dynamically generated QR Code to the screen during Config Mode for instant mobile connection (zoom in with smartphone camera to read)

## [v0.05.24.8] — Battery Icon and UI Tweaks (2026-05-24)

This release focuses on improving the battery level visibility with a new graphical icon, as well as minor UI adjustments to the status bar and config mode timeout.

### Interface

- Replaced percentage battery indicator with a graphical 5-section battery icon (debug mode retains text)
- Low battery (<= 5%) now flashes the battery icon outline instead of turning red to preserve the B/W theme
- Changed data-refresh spinner animation color from yellow to orange
- Realigned status bar footer line slightly higher

### Installation & Configuration

- Config Mode 5 minutes auto-timeout

---

## [v0.05.22.25] — Configuration Mode and Workspace Fixes (2026-05-22)

This release focuses on introducing a wireless configuration mode, improving chart rendering, and resolving compilation issues.

### Installation & Configuration

- Shake-to-Config gesture to enable an on-device local WebServer for easy wireless configuration via browser
- Added asterisk indicator to the status bar when Config Mode is active
- Added web-server page for wireless configuration, accessible at http://sugarota.local
- Device keeps WiFi connection alive while Config Mode is active
- Ignored backup and agent files in the git workspace

### Interface

- Updated appearance of data gaps on the history chart to be shorter and cleaner
- Resolved visual glitch with data gaps squeezing at the left edge of the chart

### Connectivity

- Prevented serial interface blocking when USB host is unexpectedly disconnected

---

## [v0.05.21.14] — Visual Improvements and Power Optimizations (2026-05-21)

This release focuses on optimizing power consumption, resolving battery drain issues, and significantly improving the UI representation of missing data on the historical chart.

### Interface

- Redesigned historical chart gaps for missing data (fixed 3 data points wide and info text with gap duration)
- Upgraded the scrubber snap logic to accurately jump over missing data gaps
- Removed the shake-to-update feature (use long-press on BOOT physical button instead)

### Power Management

- Dynamic open/close of the audio amplifier on demand to eliminate massive idle battery drain and device heating
- Deep hardware sleep for the display controller when brightness drops to 0 (face down)
- Fixed a bug where ADC noise randomly triggered false USB plug-in states, preventing device shutdown
- Added a 100ms debounce delay to the face-down orientation logic to prevent screen flickering

---

## [v0.05.20.31] — Battery Indication and Offline Enhancements (2026-05-20)

This release focuses on battery visibility, offline mode indicators, and build script optimizations.

### Connectivity

- Reworked Offline mode: RTS<>NTP sync, no data fetches and WiFi wakening until connected (requires reset)
- If the Secondary SSID is not set, the system will not include it in the connection loop

### Interface

- Blinking red battery percentage when battery is at or below 5%
- Added battery status to the boot log with color coding 
- Offline mode: warnings, last saved bg timing, removed minimized BG

### Power Management

- Automatic power off (15 seconds after booting) if the battery voltage is below 3.00V
- Critical hardware shutdown threshold increased from 2.95V to 3.00V

### Installation & Configuration

- Enabled manual override in versioning script

---

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