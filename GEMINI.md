# Project Overview
Embedded/Android continuous glucose monitoring (CGM) companion system.
- Architecture: ESP32-S3 firmware (`firmware/sugarota`), Android companion app (`android-app`), and local web installer (`installer.html`).
- Reference Hardware SDK: strictly follow manufacturer examples in `extras/waveshare_lcd` (v1) and `extras/waveshare_lcd/v2` (v2). DO NOT invent proprietary display/touch drivers or guess hardware registers.

# Execution & Build (STRICT)
All builds, deployments, and checks MUST be executed via `just`. Never run raw compiler or Gradle commands directly.
- List commands: `just`
- Firmware Build: `just build-firmware` (rebuild: `just rebuild-firmware`)
- Android Build & Deploy: `just run-android` (or build only: `just build-android`, clean: `just clean-android`)
- Android Logs: `just logs-android`
- Device Listing: `just devices`
- Firmware Crash Log: `just crash-log` (V1 uses `COM6` by default; for V2 use `just crash-log port="COM8"`)
- Battery Telemetry Log: `just battery-log` (clear: `just clear-battery-log`)
- Web Installer Server: `just installer` (runs on `http://localhost:8123`, auto-runs CalVer watcher)
- Token-Optimized CLI (RTK): Whenever running terminal inspection/git commands directly outside `just`, prefix with `rtk` (e.g. `rtk git status`, `rtk git diff`, `rtk gradlew`, `rtk ls`). If output is truncated or contradictory, use `rtk proxy <cmd>`. Check token savings with `just rtk-gain` or `rtk gain`.

# Architecture & Local Maps
Detailed module maps and file indices reside directly alongside code in local instructions:
- Firmware Map: `firmware/sugarota/GEMINI.md` (excludes vendor drivers)
- Android App Map: `android-app/GEMINI.md` (screens, services, excludes build caches)
- Tooling Map: `utils/GEMINI.md` (scripts and automated tasks)
- Web Installer: `installer.html` (single-file UI + Web Serial flashing)

# Behavioral Constraints & Anti-Loop Rules
- Targeted Search First: Use `grep_search` to pinpoint functions/symbols. DO NOT read files linearly to locate code.
- Anti-Micro-Reading: Read files in large chunks (150–400 lines) or whole files. NEVER make sequential small-slice reads (10–40 lines).
- Max 2 Reads Per File: If reviewing the same snippet a second time does not yield a solution, STOP immediately, state hypothesis, and ask the user.
- Strict Re-Read Ban: Once code has been inspected, NEVER re-read the exact same snippet/lines in the same turn unless the file was modified. Rely on context rather than repeating queries.
- Tool Call Cap & Fast Checkpointing: If 10–12 inspection tool calls pass without generating code edits or resolving the issue, STOP immediately. Post a concise hypothesis/progress checkpoint rather than continuing silent verification loops.
- Prompt Decomposition: For multi-issue prompts (3+ items), decompose them into an upfront plan within the first few minutes; do NOT attempt exhaustive simultaneous investigation across all sub-items in an uninterrupted loop.
- Scope Discipline: For routine UI changes, touch only the target Screen/Composable. Do NOT trace data layers across modules unless requested.
- Hardware Solutions: When encountering display/touch issues, check `extras/waveshare_lcd` before web searches.
- Diff Hygiene: Modify ONLY files directly required for the active prompt. DO NOT reformat untouched code.
- Build Permission: ALWAYS ask user permission before running app or firmware build commands (`just build-firmware`, `just run-android`, etc.). The user often prefers to compile manually to monitor progress.
- No Timers on Builds: When waiting for compilation or background tasks, rely on reactive completion messages. NEVER schedule timers or sleep loops while waiting.
- Missing Tool Prompt: When a required system command or CLI tool is not recognized, STOP and ask the user whether to install it before proceeding. Do NOT attempt automatic installations.
- Fail Fast: If a build fails, DO NOT attempt automatic tool/dependency downloads without explicit confirmation.
- Battery Performance Guardrails: Maintain 20+ hours runtime by following `docs/battery_optimization.md`. Keep battery ADC sampling at >=30s, keep Wi-Fi completely OFF (`sleepWiFi()`) between fetches, never wake Wi-Fi prematurely during BLE connection (timeout >=35s), halt BLE advertising while connected, and guard against unnecessary full-canvas `updateUI()` flushes.

# Workflows Index
Trigger these procedures ONLY when explicitly requested:
- Pre-commit procedure: see `.agents/workflows/pre-commit.md`
- Manual version update: see `.agents/workflows/update-version.md`
