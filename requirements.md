# Sugarota Build & Development Requirements

This document specifies the required toolchains, libraries, and board configuration required to compile the Sugarota ESP32-S3 firmware locally.

---

## 1. Toolchains & Runtimes

| Tool | Version | Purpose |
|------|---------|---------|
| **Python** | 3.10+ | Runs `run_installer.py` (local WebSerial flash server) & `watch_version.py` |
| **Arduino CLI** | 1.x+ | Command-line toolchain for board cores, library management, and compiling |
| **Git** | 2.x+ | Source control |

---

## 2. Arduino Board Package

* **Core**: `esp32:esp32` by Espressif Systems
* **Additional Boards Manager URL**:
  ```text
  https://espressif.github.io/arduino-esp32/package_esp32_index.json
  ```
* **Target Board**: `esp32:esp32:esp32s3` (ESP32-S3 Dev Module)

### Target Hardware Settings
* **Target Hardware**: Waveshare ESP32-S3-Touch-LCD-3.49
* **Flash Size**: `16MB` (`FlashSize=16M`)
* **Flash Mode / Frequency**: `dio`, `80m`
* **PSRAM**: `OPI PSRAM` (`PSRAM=opi`)
* **Partition Scheme**: Custom 16MB map with OTA & FFat:
  * Partitions file: `build/esp32.esp32.esp32s3/partitions.csv`

---

## 3. Library Dependencies

### External Libraries (Installed via `arduino-cli lib install`)

| Library Name | Target Header(s) | Description |
|--------------|-------------------|-------------|
| **ArduinoJson** | `<ArduinoJson.h>` | JSON parsing for configuration and API responses |
| **GFX Library for Arduino** | `<Arduino_GFX_Library.h>` | Display driver (AXS15231B over QSPI) |
| **SensorLib** | `<SensorQMI8658.hpp>`, `<SensorPCF85063.hpp>` | Drivers for QMI8658 IMU & PCF85063 RTC |

### Bundled Libraries (In-Tree)

The following components are located directly in the repository and do not require external installation:
* **Audio Codec**: `src/codec_board` & `src/esp_codec_dev` (ES8311 codec driver and audio HAL)
* **IO Expander**: `src/tca9554` (TCA9554 GPIO expander driver)
* **QR Code Generator**: `qrcode.h` & `qrcode.c` (Richard Moore QR generation)

---

## 4. Quick Setup via Arduino CLI (Option A)

### Step 1: Install Python & Arduino CLI
```powershell
winget install -e --id Python.Python.3.12 --accept-source-agreements --accept-package-agreements
winget install -e --id ArduinoSA.CLI --accept-source-agreements --accept-package-agreements
```

### Step 2: Initialize Configuration & ESP32 Core
```powershell
arduino-cli config init
arduino-cli config add board_manager.additional_urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli core update-index
arduino-cli core install esp32:esp32
```

### Step 3: Install Required Libraries
```powershell
arduino-cli lib install "ArduinoJson"
arduino-cli lib install "GFX Library for Arduino"
arduino-cli lib install "SensorLib"
```

### Step 4: Ensure Custom Partition File
Ensure `partitions.csv` is present in the sketch root directory (it defines the 16MB layout with OTA and FFat):
```powershell
Copy-Item "build\esp32.esp32.esp32s3\partitions.csv" -Destination "partitions.csv"
```

### Step 5: Compile Firmware

#### Easy One-Command Build Script (with Live Progress & Build Caching)
Run the bundled batch wrapper or PowerShell script from the repository root:
```powershell
.\build.bat
```
*(or `.\build.ps1`)*

To force a clean rebuild without using cached objects:
```powershell
.\build.bat -Clean
```
*(or `.\build.ps1 -Clean`)*

The script automatically:
* Copies `partitions.csv` to the sketch root if not already present.
* Uses `--build-path ./build/cache` to reuse previously compiled object files across minor edits.
* Leverages all CPU cores (`--jobs 0`).
* Displays a live progress bar with elapsed time.

#### Manual CLI Command
```powershell
arduino-cli compile -v --jobs 0 --build-path ./build/cache --fqbn esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=16M,PSRAM=opi,PartitionScheme=custom --output-dir ./build/esp32.esp32.esp32s3 sugarota.ino
```

---

## 5. Setup via Arduino IDE 2.x GUI (Option B)

1. Add `https://espressif.github.io/arduino-esp32/package_esp32_index.json` to **File > Preferences > Additional boards manager URLs**.
2. Open **Tools > Board > Boards Manager**, search for `esp32`, and install **esp32 by Espressif Systems**.
3. Open **Tools > Manage Libraries...** and install:
   - `ArduinoJson`
   - `GFX Library for Arduino`
   - `SensorLib`
4. Select board **ESP32-S3 Dev Module** and configure:
   - **Flash Size**: 16MB (128Mb)
   - **PSRAM**: OPI PSRAM
   - **Upload Speed**: 921600
   - **USB CDC On Boot**: Enabled (if using direct USB serial)
   - **Partition Scheme**: Custom / 16M Flash (3MB APP / 9.9MB FATFS)
