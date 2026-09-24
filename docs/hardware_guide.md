# Waveshare ESP32-S3-Touch-LCD-3.49 Hardware Guide

This document details pinouts, communication buses, sensor interfaces, and power configuration for the **Waveshare ESP32-S3-Touch-LCD-3.49** development board.

---

## 1. Pin Assignment Reference

| Function / Component | Net Name | ESP32-S3 GPIO (V1) | ESP32-S3 GPIO (V2) | Interface / Protocol | Notes |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **LCD Chip Select** | `LCD_CS` | GPIO 9 | GPIO 9 | QSPI | Active Low |
| **LCD Clock** | `LCD_PCLK` | GPIO 10 | GPIO 10 | QSPI | AXS15231B Controller |
| **LCD Data 0** | `LCD_D0` | GPIO 11 | GPIO 11 | QSPI | Data Line 0 |
| **LCD Data 1** | `LCD_D1` | GPIO 12 | GPIO 12 | QSPI | Data Line 1 |
| **LCD Data 2** | `LCD_D2` | GPIO 13 | GPIO 13 | QSPI | Data Line 2 |
| **LCD Data 3** | `LCD_D3` | GPIO 14 | GPIO 14 | QSPI | Data Line 3 |
| **LCD Reset** | `LCD_RST` | GPIO 21 | TCA9554 EXIO5 | Digital Output | Active Low (held HIGH) |
| **LCD Tearing Effect** | `LCD_TE` | N/A | GPIO 21 | Digital Input | Tearing effect sync |
| **LCD Backlight PWM** | `PIN_BL` | GPIO 8 | GPIO 42 | PWM Output | LedC Backlight (`0..255`, inverted) |
| **Backlight Boost Enable**| `BL_EN` | N/A | TCA9554 EXIO1 | Digital Output | Active HIGH |
| **IO Expander Interrupt** | `EXIO_INT` | N/A | GPIO 8 | Digital Input | Open drain with pull-up |
| **Touch I2C SDA** | `TOUCH_SDA` | GPIO 17 | GPIO 17 | I2C (Bus 1) | Shared touch bus |
| **Touch I2C SCL** | `TOUCH_SCL` | GPIO 18 | GPIO 18 | I2C (Bus 1) | Address: `0x3B` |
| **System I2C SDA** | `I2C_SDA` | GPIO 47 | GPIO 47 | I2C (Bus 0) | System peripherals |
| **System I2C SCL** | `I2C_SCL` | GPIO 48 | GPIO 48 | I2C (Bus 0) | System peripherals |
| **Battery ADC** | `PIN_BAT_ADC` | GPIO 4 | GPIO 4 | ADC1 Channel 3 | Divided 1:2 (multiplier 3.0) |
| **Power Button** | `PIN_PWR_BTN` | GPIO 16 | GPIO 16 | Digital Input | Internal Pullup |
| **Boot Button** | `PIN_BOOT_BTN`| GPIO 0 | GPIO 0 | Digital Input | Pullup, Download Mode |

---

## 1.1 Hardware Revisions (V1 vs V2)

Waveshare updated the development board hardware after June 8, 2026:
- **V1 (Original)**: Discontinued. Backlight is controlled directly on `GPIO 8`. `LCD_RST` is routed to `GPIO 21`.
- **V2 (Rev 1.1)**: Has "Rev1.1" PCB silkscreen or "V2" QC sticker. Backlight PWM is routed to `GPIO 42`, and requires TCA9554 `EXIO1` (`BL_EN`) to be driven HIGH. `GPIO 8` is connected to `EXIO_INT`. `LCD_RST` is moved to TCA9554 `EXIO5`, and `GPIO 21` connects to `LCD_TE`.

**Seamless Dual-Compatibility in Sugarota Firmware**:
Sugarota automatically detects whether the board is V1 or V2 at boot by probing `GPIO 8`'s pull state (TCA9554 interrupt line pull-up on V2 vs backlight boost pull-down on V1), isolates backlight PWM exclusively to `GPIO 8` (V1) or `GPIO 42` (V2), holds `GPIO 8` as `OUTPUT HIGH` on V2 to prevent floating/PWM noise on the IO expander interrupt line, and enables `EXIO1` (`BL_EN`) and `EXIO5` (`LCD_RST`) on TCA9554. Additionally, a manual override is supported in `/config.json` via `"hw_version": "auto" | "v1" | "v2"`.

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
