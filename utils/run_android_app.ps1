param (
    [switch]$Install,
    [switch]$Launch,
    [switch]$Logs,
    [string]$Device
)

$ErrorActionPreference = "Stop"

$AndroidSdk = "$env:LOCALAPPDATA\Android\Sdk"
$Adb = "$AndroidSdk\platform-tools\adb.exe"

if (-not (Test-Path $Adb)) {
    # Fallback to PATH adb if available
    $AdbCmd = Get-Command adb -ErrorAction SilentlyContinue
    if ($AdbCmd) {
        $Adb = $AdbCmd.Source
    } else {
        Write-Error "adb.exe not found at $Adb or in PATH. Make sure Android SDK Platform-Tools are installed."
        exit 1
    }
}

function Get-ConnectedDevices {
    $raw = & $Adb devices
    $devList = @()
    foreach ($line in ($raw -split "`r?`n")) {
        if ($line.Trim() -match '^([^\s]+)\s+device$') {
            $devList += $Matches[1]
        }
    }
    return $devList
}

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = Split-Path -Parent $ScriptDir
if (-not (Test-Path (Join-Path $RepoRoot "android-app"))) {
    $RepoRoot = (Get-Location).Path
}
$AppDir = Join-Path $RepoRoot "android-app"
$Gradlew = Join-Path $AppDir "gradlew.bat"

# Set JDK 21 for gradle if available
if (Test-Path "$env:USERPROFILE\.jdks\jbr-21.0.11") {
    $env:JAVA_HOME = "$env:USERPROFILE\.jdks\jbr-21.0.11"
}

# Resolve target devices
$connected = Get-ConnectedDevices
if ($connected.Count -eq 0) {
    Write-Error "No connected Android devices/emulators found via adb."
    exit 1
}

$targetDevices = if ($Device) {
    if ($connected -notcontains $Device) {
        Write-Warning "Specified device '$Device' not found in online devices list: $($connected -join ', ')"
    }
    @($Device)
} else {
    $connected
}

Write-Host "Target device(s): $($targetDevices -join ', ')" -ForegroundColor DarkGray

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
    $BuiltApk = Join-Path $AppDir "app\build\outputs\apk\debug\app-debug.apk"
    $ApkPath = Join-Path $AppDir "sugarota-app-debug.apk"
    if (Test-Path $BuiltApk) {
        Copy-Item $BuiltApk -Destination $ApkPath -Force
    }
    if (-not (Test-Path $ApkPath)) {
        Write-Error "Built APK not found at $ApkPath"
        exit 1
    }
    
    foreach ($dev in $targetDevices) {
        Write-Host "==> Installing Debug APK to [$dev]..." -ForegroundColor Cyan
        & $Adb -s $dev install -r $ApkPath
        if ($LASTEXITCODE -ne 0) {
            Write-Error "ADB install failed on device $dev."
        } else {
            Write-Host "[OK] App successfully installed on [$dev]!" -ForegroundColor Green
        }
    }
}

if ($Launch -or (-not $Install -and -not $Launch -and -not $Logs)) {
    foreach ($dev in $targetDevices) {
        Write-Host "==> Launching Sugarota Companion on [$dev]..." -ForegroundColor Cyan
        & $Adb -s $dev shell am start -n "org.sugarota.companion/.MainActivity"
    }
}

if ($Logs) {
    # If multiple devices connected and none specified, choose the first device for log streaming
    $logDev = $targetDevices[0]
    Write-Host "==> Streaming Logcat from [$logDev] (Sugarota)..." -ForegroundColor Yellow
    & $Adb -s $logDev logcat -c
    & $Adb -s $logDev logcat -s "SugarotaBleService" "MainActivity"
}
