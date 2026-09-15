param (
    [switch]$Install,
    [switch]$Launch,
    [switch]$Logs
)

$ErrorActionPreference = "Stop"

$AndroidSdk = "$env:LOCALAPPDATA\Android\Sdk"
$Adb = "$AndroidSdk\platform-tools\adb.exe"

if (-not (Test-Path $Adb)) {
    Write-Error "adb.exe not found at $Adb. Make sure Android SDK Platform-Tools are installed."
    exit 1
}

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = Split-Path -Parent $ScriptDir
if (-not (Test-Path (Join-Path $RepoRoot "android-app"))) {
    $RepoRoot = (Get-Location).Path
}
$AppDir = Join-Path $RepoRoot "android-app"
$Gradlew = Join-Path $AppDir "gradlew.bat"

# Set JDK 21 for gradle
$env:JAVA_HOME = "$env:USERPROFILE\.jdks\jbr-21.0.11"

if ($Install -or (-not $Install -and -not $Launch -and -not $Logs)) {
    Write-Host "==> Building Debug APK..." -ForegroundColor Cyan
    Push-Location $AppDir
    try {
        & $Gradlew assembleDebug
        if ($LASTEXITCODE -ne 0) {
            Write-Error "Gradle build failed."
            exit $LASTEXITCODE
        }
    }
    finally {
        Pop-Location
    }
    $ApkPath = Join-Path $AppDir "sugarota-app-debug.apk"
    if (-not (Test-Path $ApkPath)) {
        $ApkPath = Join-Path $AppDir "app\build\outputs\apk\debug\app-debug.apk"
    }
    if (-not (Test-Path $ApkPath)) {
        Write-Error "Built APK not found at $ApkPath"
        exit 1
    }
    Write-Host "==> Installing Debug APK via ADB..." -ForegroundColor Cyan
    & $Adb install -r $ApkPath
    if ($LASTEXITCODE -ne 0) {
        Write-Error "ADB install failed."
        exit $LASTEXITCODE
    }
    Write-Host "[OK] App successfully installed on device!" -ForegroundColor Green
}

if ($Launch -or (-not $Install -and -not $Launch -and -not $Logs)) {
    Write-Host "==> Launching Sugarota Companion on device..." -ForegroundColor Cyan
    & $Adb shell am start -n "org.sugarota.companion/.MainActivity"
}

if ($Logs) {
    Write-Host "==> Streaming Logcat (Sugarota)..." -ForegroundColor Yellow
    & $Adb logcat -c
    & $Adb logcat -s "SugarotaBleService" "MainActivity"
}
