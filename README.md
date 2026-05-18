# 🍭 Sugarota: ESP32-S3 Glucose Monitor & Setup Center

Sugarota is a state-of-the-art wireless blood glucose display and telemetry terminal powered by the ESP32-S3 chipset. It syncs real-time CGM data from Dexcom Share or Nightscout APIs, plots high-DPI historical trend graphs on the screen, and features a gorgeous browser-based WebSerial interface to flash firmware and manage device settings.

Initially built for Waveshare ESP32-S3-Touch-LCD-3.49 Development platform. 

See [CHANGELOG](CHANGELOG.md) for latest build updates.

---

## ⚡ Quickstart

### Installation via GitHub Pages (EASIEST)

1. Buy the Waveshare ESP32-S3-Touch-LCD-3.49 on manufacturer's website or AliExpress
2. Go to installation page: https://not-really-a-coder.github.io/sugarota/installer.html
3. Connect the device to your computer using USB-C cable, click "Connect USB device", and select the port.
4. Enter Wi-Fi credentials and CGM Data Provider details.
5. Click "Start Flashing Firmware". The device will automatically restart once the firmware is flashed.

All entered credentials and settings are saved ONLY on the device during flashing. Nothing is being saved or stored on the local or remote host by the author of this software.

### Local installation

The local setup center requires **zero external python packages**—it runs fully using standard library modules included with Python.

1.  **Launch the Setup Server**:
    Run the multi-threaded host script from the repository root:
    ```bash
    python run_installer.py
    ```
2.  **Access the Dashboard**:
    Open your browser to: **[http://localhost:8000/installer.html](http://localhost:8000/installer.html)**
3.  **Flash & Configure**:
    *   Connect your ESP32-S3 screen via USB-C.
    *   Select **Bundled Latest Firmware** and click **Start Flashing**.
    *   Once flashed, use the **Device Configuration** forms to set up Wi-Fi, timezones, and CGM credentials, then apply them instantly over the serial channel.

---

## 🌐 Remote Cloud & VPS Deployment

If you want to host the installer server on a remote machine (such as an Oracle Cloud VPS, AWS EC2, or Linux server), keep the following in mind:

### 1. Run in Headless Mode
Start the script with the `--no-browser` or `--headless` flag to prevent Python from attempting to open a GUI browser on the host server:
```bash
python run_installer.py --no-browser
```

### 2. Browser HTTPS Secure Context Requirement (CRITICAL)
Modern browsers (Chrome, Edge, Opera) restrict the **WebSerial API** strictly to **Secure Contexts (HTTPS)** when accessed over a network. If you access the server remotely via an insecure HTTP address (e.g., `http://your-vps-ip:8000/installer.html`), the **Connect/Flash buttons will be disabled** by your browser.

You can solve this elegantly in two ways:
*   **Method A: SSH Tunneling (Recommended & Easiest)**
    Instead of dealing with domains and SSL certificates for a private setup utility, tunnel the port securely to your local machine:
    ```bash
    ssh -L 8000:localhost:8000 user@your-vps-ip
    ```
    Once connected, navigate to **`http://localhost:8000/installer.html`** in your local browser. Because it is mapped to `localhost`, the browser grants full WebSerial access natively!
*   **Method B: Reverse Proxy**
    Put the server behind Nginx/Apache and secure it with a free SSL certificate from **Let's Encrypt** (Certbot).

---

## 🛠️ Arduino Compilation Requirements

If you want to modify or compile the C++ firmware (`sugarota.ino`) directly from source, set up your development environment as follows:

### 1. Board Manager Configuration 
*   **Core**: Espressif **ESP32** Board Package (v2.0.x or v3.0.x compatible)
*   **Target Board**: `ESP32-S3 Dev Module`
*   **Recommended Settings**:
    *   *Flash Size*: 16MB (for robust partition maps)
    *   *Partition Scheme*: Custom or Large FFat
    *   *PSRAM*: Enabled (OPI)

### 2. External Library Dependencies
Install these libraries via the Arduino IDE Library Manager:
*   **ArduinoJson** (v6.x or v7.x) — For serial communication and settings parsing.
*   **Arduino_GFX_Library** — High-performance graphics and LCD screen drivers.
*   **SensorQMI8658** — Driver for the onboard IMU/Accelerometer sensor.

### 3. Bundled Hardware Libraries
All internal board support files (codec configurations, TCA9554 IO expanders) are pre-packaged inside the [src/](src/) directory of this repository and require **no** manual installation.

Refer to the [Manufacturer's GitHub Repository](https://github.com/waveshareteam/ESP32-S3-Touch-LCD-3.49/) and [device technical documentation](https://docs.waveshare.com/ESP32-S3-Touch-LCD-3.49?variant=ESP32-S3-Touch-LCD-3.49-EN) for more details.

---

## 🔒 Security & Local Settings

Your local configuration containing passwords and API keys is protected by design:
*   **`data/config.template.json`**: Standard configuration template containing generic placeholders for Wi-Fi and API servers.
*   **`data/config.json`**: Your active credentials file that will be created after your first run. When you start `run_installer.py`, the server automatically creates this file from the template if it is missing. Make sure that file creation permissions are enabled in the **`data/`** folder. 