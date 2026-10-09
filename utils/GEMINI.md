# Automation & Tooling Map (`utils/`)

## Execution Context
All utility tasks should be triggered via the project `justfile`. Never execute raw compiler commands directly.

## Script Responsibilities
- `run_web_installer.py`: Starts local HTTP server (port 8123) and automatically starts `watch_version.py` in a background daemon thread. Invoked via `just installer`.
- `watch_version.py`: Unified CalVer version monitor and manager. Watches `.ino`, `.cpp`, `.h`, `.kt`, `.kts`, and `installer.html`. Can be run standalone via `just watch-version`.
- `firmware_build.ps1`: Automated build tool using `arduino-cli` with custom partition checks and monotonic progress output. Invoked via `just build-firmware`.
- `firmware_build.bat`: Thin CMD wrapper for `firmware_build.ps1`.
- `run_android_app.ps1`: Automates Gradle debug build, multi-device ADB installation, and activity launch. Invoked via `just run-android` or `just logs-android`.
- `read_crash.ps1`: Communicates over USB CDC serial (V1: `COM6` default, V2: `COM8`, 115200 baud) to send `GET_CRASH_LOG` and retrieve persisted hardware/firmware crash logs and reboot reasons from LittleFS `/crash.log`. Usage: `.\utils\read_crash.ps1 [-PortName <COM#>]`. Invoked via `just crash-log` or `just crash-log port="COM8"`.
- `read_battery.ps1`: Communicates over USB CDC serial (115200 baud) to query (`GET_BATTERY_LOG`) or clear (`CLEAR_BATTERY_LOG`) LittleFS `/battery.log` telemetry data with chunked stream reader. Usage: `.\utils\read_battery.ps1 [-PortName <COM#>] [-Clear]`. Invoked via `just battery-log` or `just clear-battery-log`.

## Guidelines
- Avoid adding external Python/PowerShell package dependencies without confirmation.
- Scripts must remain compatible with Windows PowerShell 5.1 and PowerShell 7 (`pwsh`).
