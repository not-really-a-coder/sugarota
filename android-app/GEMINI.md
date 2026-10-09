# Android Companion App Map

## Architecture Overview
Jetpack Compose + Material 3 Android companion application communicating with Sugarota ESP32-S3 via BLE.

## Key Source Files (`app/src/main/java/org/sugarota/companion/`)
- `MainActivity.kt`: App lifecycle, navigation graph, PiP mode handling, runtime permission flows.
- `ui/AppSettingsScreen.kt`: General app preferences (polling intervals, units mg/dL vs mmol/L).
- `ui/DeviceDetailScreen.kt`: Device status, live telemetry, and BLE device settings.
- `ui/FirmwareUpdateScreen.kt`: Firmware release checks and OTA update transfer UI.
- `ui/PipGlucoseChartContent.kt`: Picture-in-Picture specialized chart renderer.
- `ui/components/GlucoseChartView.kt`: Main interactive historical glucose curve view.
- `ui/components/ShadcnComponents.kt`: Reusable design system UI elements (cards, buttons, switches).
- `service/SugarotaBleService.kt`: Foreground BLE GATT service, periodic polling, device sync.
- `service/SugarotaBleScanReceiver.kt`: Background BLE scan broadcast receiver.
- `data/AppSettingsPreferences.kt`: DataStore / SharedPreferences storage for user settings.
- `data/BridgePreferences.kt`: Nightscout/Dexcom bridge credentials and connection modes.
- `model/Models.kt`: Data models for glucose readings, device states, and sync packets.
- `network/GlucoseBridgeClient.kt`: Direct Dexcom Share / Nightscout REST client.
- `network/FirmwareUpdateManager.kt` & `AppUpdateManager.kt`: CalVer checking and GitHub release fetching.

## Build & Scope Directives
- **Generated & Build Artifacts**: NEVER inspect or search `app/build/`, `.gradle/`, or auto-generated resources (e.g. `R.java`, merged manifests, synthetic BuildConfig). All modifications belong strictly in `app/src/main/`.
- **Build**: `just build-android` (uses `rtk gradlew assembleDebug` to condense output)
- **Deploy to Devices**: `just run-android`
- **Logs**: `just logs-android`
- **Gradle Tasks**: When running ad-hoc Gradle tasks or unit tests, always proxy through RTK: `rtk gradlew testDebugUnitTest` or `rtk gradlew lint`.
- **Touch Scope**: When editing UI screens or components, do not modify background BLE services or networking layers unless explicitly instructed.
