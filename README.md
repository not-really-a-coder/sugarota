# 🍭 Sugarota: ESP32-S3 Glucose Monitor Firmware

Sugarota is a state-of-the-art wireless blood glucose display and telemetry terminal powered by the ESP32-S3 chipset. It syncs real-time CGM data from Dexcom Share or Nightscout APIs, plots high-DPI historical trend graphs on the screen, and features a gorgeous browser-based WebSerial interface to flash firmware and manage device settings.

<a href="https://www.youtube.com/watch?v=fj86CE9E_Lw" target="_blank"><img src="https://img.youtube.com/vi/fj86CE9E_Lw/0.jpg" alt="Short demo video"></a>

Initially built for Waveshare ESP32-S3-Touch-LCD-3.49 Development platform. 

See [CHANGELOG](CHANGELOG.md) for latest build updates.

---

## ⚡ Quickstart

### Installation via GitHub Pages (EASIEST)

1. Buy the Waveshare ESP32-S3-Touch-LCD-3.49 on manufacturer's website or AliExpress.
2. Go to the installation page: https://not-really-a-coder.github.io/sugarota/installer.html
3. Connect the device to your computer using USB-C cable, click "Connect USB device", and select the port.
4. The installer will automatically detect a blank device and prepare a **Full Install**. Click "Start Flashing Firmware".
5. Once flashed, the device will boot. Shake the device vigorously to enter **Config Mode**.
6. Scan the QR code displayed on the screen (or navigate to `http://sugarota.local` / the displayed IP) from your smartphone to securely configure your Wi-Fi and CGM credentials wirelessly!

All entered credentials and settings are saved ONLY on the device. Nothing is being saved or stored on the local or remote host by the author of this software.

### Local installation

The local setup center requires **zero external python packages** (no `requirements.txt` needed!)—it runs fully using standard library modules included with Python.

1.  **Launch the Setup Server**:
    Run the multi-threaded host script from the repository root:
    ```bash
    python run_installer.py
    ```
2.  **Access the Dashboard**:
    Open your browser to: **[http://localhost:8123/installer.html](http://localhost:8123/installer.html)**
3.  **Flash & Configure**:
    *   Connect your ESP32-S3 screen via USB-C.
    *   The installer will auto-detect a new device and select **Full Install**. Click **Start Flashing Firmware**.
    *   Once flashed, shake the device to enter wireless Config Mode and scan the QR code to set up Wi-Fi and CGM credentials.

---

## 🌐 Remote Cloud & VPS Deployment

If you want to host the installer server on a remote machine (such as an Oracle Cloud VPS, AWS EC2, or Linux server), keep the following in mind:

### 1. Run in Headless Mode
Start the script with the `--no-browser` or `--headless` flag to prevent Python from attempting to open a GUI browser on the host server:
```bash
python run_installer.py --no-browser
```

### 2. Browser HTTPS Secure Context Requirement (CRITICAL)
Modern browsers (Chrome, Edge, Opera) restrict the **WebSerial API** strictly to **Secure Contexts (HTTPS)** when accessed over a network. If you access the server remotely via an insecure HTTP address (e.g., `http://your-vps-ip:8123/installer.html`), the **Connect/Flash buttons will be disabled** by your browser.

You can solve this elegantly in two ways:
*   **Method A: SSH Tunneling (Recommended & Easiest)**
    Instead of dealing with domains and SSL certificates for a private setup utility, tunnel the port securely to your local machine:
    ```bash
    ssh -L 8123:localhost:8123 user@your-vps-ip
    ```
    Once connected, navigate to **`http://localhost:8123/installer.html`** in your local browser. Because it is mapped to `localhost`, the browser grants full WebSerial access natively!
*   **Method B: Reverse Proxy**
    Put the server behind Nginx/Apache and secure it with a free SSL certificate from **Let's Encrypt** (Certbot).

---

## 📚 Documentation & Technical Specifications

Detailed technical documentation, data models, and specifications are organized in the [`docs/`](docs/) directory:
* [System Architecture & State Flow](docs/architecture.md)
* [BLE GATT Protocol Specification](docs/ble_gatt_spec.md)
* [Data Schemas & Telemetry Formats](docs/data_schemas.md)
* [Hardware Guide & Pinout Reference](docs/hardware_guide.md)
* [Build & Toolchain Requirements](docs/build_requirements.md)

---

## 🛠️ Arduino Compilation Requirements

If you want to modify or compile the C++ firmware directly from source, refer to the [Build Requirements Guide](docs/build_requirements.md). The main sketch and modules are located under [`firmware/sugarota/`](firmware/sugarota/).

To compile from command line via Arduino CLI:
```powershell
.\build.ps1
```

Refer to the [Manufacturer's GitHub Repository](https://github.com/waveshareteam/ESP32-S3-Touch-LCD-3.49/) and [device technical documentation](https://docs.waveshare.com/ESP32-S3-Touch-LCD-3.49?variant=ESP32-S3-Touch-LCD-3.49-EN) for more details.

---

## 🔒 Security & Local Settings

Your local configuration containing passwords and API keys is protected by design:
*   **`data/config.template.json`**: Standard configuration template containing generic placeholders for Wi-Fi and API servers.
*   **`data/config.json`**: Your active credentials file that will be created after your first run. When you start `run_installer.py`, the server automatically creates this file from the template if it is missing. Make sure that file creation permissions are enabled in the **`data/`** folder. 