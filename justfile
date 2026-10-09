# Use PowerShell to run all commands
set shell := ["pwsh.exe", "-NoProfile", "-ExecutionPolicy", "Bypass", "-Command"]

# List of available commands
default:
    @just --list

# --- Android App ---

# Build Android APK (variant: debug, release)
build-android variant="debug":
    cd android-app; rtk gradlew "assemble{{capitalize(variant)}}"

# Install APK to connected device
install-android variant="debug":
    cd android-app; rtk gradlew "install{{capitalize(variant)}}"

# Run Android app helper on all devices (or specific device: device="<serial>")
run-android device="":
    ./utils/run_android_app.ps1 {{ if device != "" { "-Device " + device } else { "" } }}

# Stream Android logcat filtered for Sugarota (or specific device: device="<serial>")
logs-android device="":
    ./utils/run_android_app.ps1 -Logs {{ if device != "" { "-Device " + device } else { "" } }}

# Clean Android build artifacts
clean-android:
    cd android-app; rtk gradlew clean

# --- ESP32-S3 Firmware ---

# Build firmware binary (compiles custom ESP32-S3 firmware)
build-firmware:
    ./utils/firmware_build.ps1 -Yes

# Clean and rebuild firmware
rebuild-firmware:
    ./utils/firmware_build.ps1 -Clean -Yes

# Clean firmware build cache & outputs
clean-firmware:
    if (Test-Path ./build) { Remove-Item -Recurse -Force ./build }

# --- Development & Tools ---

# Start local web installer HTTP server (port 8123, automatically runs version watcher)
installer:
    python ./utils/run_web_installer.py

# Run standalone version watcher (only needed if installer is not running)
watch-version:
    python ./utils/watch_version.py --watch

# Bump CalVer version manually for target (firmware, installer, android, all)
bump-version target="all":
    python ./utils/watch_version.py --target {{target}} --bump

# Sync CalVer version state from source files
sync-version target="all":
    python ./utils/watch_version.py --target {{target}} --sync

# Retrieve crash log from device over serial (V1: COM6, V2: COM8; e.g. just crash-log or just crash-log port="COM8")
crash-log port="COM6":
    ./utils/read_crash.ps1 -PortName {{port}}

# Retrieve battery telemetry log from device over serial (V1: COM6, V2: COM8; e.g. just battery-log)
battery-log port="COM6":
    ./utils/read_battery.ps1 -PortName {{port}}

# Clear battery telemetry log from device over serial
clear-battery-log port="COM6":
    ./utils/read_battery.ps1 -PortName {{port}} -Clear

# Check connected Android devices and serial/COM ports
devices:
    @Write-Host "=== Connected Android Devices ===" -ForegroundColor Cyan
    @adb devices
    @Write-Host "`n=== Serial / COM Ports ===" -ForegroundColor Cyan
    @Get-CimInstance Win32_SerialPort | Select-Object DeviceID, Description | Format-Table -AutoSize

# --- RTK & Git Shortcuts ---

# Condensed working tree status via RTK
status:
    @rtk git status

# Ultra-condensed diff of working changes via RTK
diff *args:
    @rtk git diff {{args}}

# Show RTK token savings summary
rtk-gain:
    @rtk gain