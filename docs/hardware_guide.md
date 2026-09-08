# Waveshare ESP32-S3-Touch-LCD-3.49 Hardware Guide

This document details pinouts, communication buses, sensor interfaces, and power configuration for the **Waveshare ESP32-S3-Touch-LCD-3.49** development board.

---

## 1. Pin Assignment Reference

| Function / Component | Net Name | ESP32-S3 GPIO | Interface / Protocol | Notes |
| :--- | :--- | :--- | :--- | :--- |
| **LCD Chip Select** | `LCD_CS` | GPIO 9 | QSPI | Active Low |
| **LCD Clock** | `LCD_PCLK` | GPIO 10 | QSPI | AXS15231B Controller |
| **LCD Data 0** | `LCD_D0` | GPIO 11 | QSPI | Data Line 0 |
| **LCD Data 1** | `LCD_D1` | GPIO 12 | QSPI | Data Line 1 |
| **LCD Data 2** | `LCD_D2` | GPIO 13 | QSPI | Data Line 2 |
| **LCD Data 3** | `LCD_D3` | GPIO 14 | QSPI | Data Line 3 |
| **LCD Reset** | `LCD_RST` | GPIO 21 | Digital Output | Hardware Reset |
| **LCD Backlight** | `PIN_BL` | GPIO 8 | PWM Output | LedC Backlight (`0..255`) |
| **Touch I2C SDA** | `TOUCH_SDA` | GPIO 17 | I2C (Bus 1) | Shared touch bus |
| **Touch I2C SCL** | `TOUCH_SCL` | GPIO 18 | I2C (Bus 1) | Address: `0x3B` |
| **System I2C SDA** | `I2C_SDA` | GPIO 47 | I2C (Bus 0) | System peripherals |
| **System I2C SCL** | `I2C_SCL` | GPIO 48 | I2C (Bus 0) | System peripherals |
| **Battery ADC** | `PIN_BAT_ADC` | GPIO 4 | ADC1 Channel 3 | Divided 1:2 (multiplier 3.0) |
| **Power Button** | `PIN_PWR_BTN` | GPIO 16 | Digital Input | Internal Pullup |
| **Boot Button** | `PIN_BOOT_BTN`| GPIO 0 | Digital Input | Pullup, Download Mode |

---

## 2. I2C Peripheral Address Map (Bus 0: GPIO 47 / 48)

| I2C Address (7-bit) | Device | Purpose |
| :--- | :--- | :--- |
| `0x20` | **TCA9554PWR** | 8-bit GPIO expander (controls peripheral power & audio power) |
| `0x51` | **PCF85063** | Real-Time Clock with battery backup |
| `0x6B` | **QMI8658** | 6-Axis Inertial Measurement Unit (Accelerometer + Gyro) |
| `0x18` | **ES8311** | High-performance audio mono DAC / Codec |

---

## 3. Power Architecture & Battery Calibration

- **Voltage Divider**: Battery voltage is connected through a resistive voltage divider to GPIO 4. The raw voltage reading is scaled with a factor of `3.0`.
- **ADC Calibration**: Uses ESP-IDF `esp_adc_cali_scheme` curve fitting with eFuse Vref calibration.
- **Power Delivery via TCA9554**:
  - The TCA9554 expander controls supply rails for audio amplification and secondary display components.
