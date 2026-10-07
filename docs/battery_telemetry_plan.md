# Battery Optimization & Telemetry Roll-out Plan

## Context & Objectives
During real-world battery testing on hardware V1 with an 18650 Li-Ion cell (~3500mAh), runtime was ~19.3 hours with Night Mode enabled. Prior tests with screen continuously on achieved ~20 hours. 

Investigation revealed:
1. **Unintended Night Mode wakes**: In `SugarotaBleService.kt`, background queries poll on `poll_interval_sec` (often 60s), pushing `api_ok` or updates. On firmware (`ble_handler.cpp`), ingesting glucose unconditionally called `triggerNightModeWake()`, waking the display for 30s every reading.
2. **Li-Ion 18650 Curve Mismatch**: The 11-point piecewise linear curve in `battery.cpp` drops too fast at top voltages (4.2V -> 3.95V under load) and holds 3% for hours because 18650 discharge remains flat around 3.3V–3.5V under low current.
3. **UI Indicator Glitches**:
   - On <= 3%, normal mode kept `textColor` instead of turning `RED`.
   - At 4%–5%, 0 sections were drawn instead of 1 section.
4. **Hardware Charging Detection (LED / ETA6098)**:
   - Examining the official Waveshare V2 schematic: the battery charger IC is an **ETA6098** (pin 9 `STAT`), which directly drives the charging LED (`LED1`) to ground through `R10` (27k). The `STAT` pin is **not** connected to any ESP32 GPIO or the TCA9554 IO expander. Hence, the LED is pure analog hardware and cannot be read digitally via software.
5. **Telemetry / Research Logging**:
   - Provide a dedicated battery telemetry log (e.g., `/battery.log` on LittleFS) separate from `/crash.log` and ordinary serial spam, retrievable via a dedicated CLI command `just battery-log` (similar to `just crash-log`).

---

## Roll-out Phases

### Phase 1: Documentation & Safety Guardrails (Current Step)
- [x] Analyze hardware schematic regarding charging LED and charger IC (`ETA6098`).
- [x] Record this roll-out plan and rollback plan in `docs/battery_telemetry_plan.md`.

### Phase 2: UI & Indicator Bug Fixes (Firmware)
- [x] Fix battery icon color: Turn **RED** when blinking on `<= 3%` in normal mode (`ui.cpp`).
- [x] Fix section calculation: Keep 1 section filled when `> 3%` (i.e., at 4% and 5%), and 0 sections (outline only) when `<= 3%` (`ui.cpp`).

### Phase 3: Dedicated Battery Logging System (Firmware & Tooling)
- [x] Implement `/battery.log` in LittleFS with bounded circular/FIFO buffer (up to 15 KB, recording `[timestamp, voltage_mv, percentage, screen_state, wifi_active]` every 5–10 minutes or on 1% drop).
- [x] Add serial commands: `GET_BATTERY_LOG` and `CLEAR_BATTERY_LOG` in `sugarota.ino`.
- [x] Add PowerShell helper `utils/read_battery.ps1` and Justfile recipe `just battery-log` (and `just clear-battery-log`).

### Phase 4: Testing & Curve Calibration
- [ ] Verify battery icon visually at boundary values (<= 3%, 4-5%, etc.).
- [ ] Run discharge cycle with `battery.log` active to collect real-world 18650 curve data.
- [ ] Extract empirical V-discharge curve for 18650 cell to recalibrate `getBatteryPercentage()`.


---

## Rollback Plan
Since the telemetry logging system is an investigative tool to profile the 18650 discharge curve and power consumption without cluttering normal logs:
1. **File Additions Rollback**:
   - Delete `utils/read_battery.ps1`.
   - Delete `docs/battery_telemetry_plan.md`.
2. **Justfile Recipe Rollback**:
   - Remove `battery-log` and `clear-battery-log` targets from `justfile`.
3. **Firmware Rollback**:
   - Revert LittleFS `/battery.log` writing functions in `battery.cpp` / `storage.cpp`.
   - Remove `GET_BATTERY_LOG` and `CLEAR_BATTERY_LOG` handlers from `sugarota.ino`.
   - Keep the permanent UI fixes in `ui.cpp` (red blinking icon on <= 3% and 1 section on > 3%) and any calibrated lookup curve points.
4. **Clean Verification**:
   - Flash firmware and verify that LittleFS has no lingering temporary battery log routines.

