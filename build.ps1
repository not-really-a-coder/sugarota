param(
    [switch]$Clean,
    [switch]$VerboseOutput
)

$ErrorActionPreference = "Stop"

# Refresh environment PATH
$env:Path = [System.Environment]::GetEnvironmentVariable("Path", "Machine") + ";" + [System.Environment]::GetEnvironmentVariable("Path", "User")

# Ensure partitions.csv exists in root
if (-not (Test-Path "partitions.csv")) {
    if (Test-Path "build\esp32.esp32.esp32s3\partitions.csv") {
        Copy-Item "build\esp32.esp32.esp32s3\partitions.csv" -Destination "partitions.csv"
        Write-Host "[BUILD] Copied partitions.csv to sketch root." -ForegroundColor Cyan
    }
    else {
        Write-Error "Could not find partitions.csv in root or build folder!"
    }
}

$fqbn = "esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=16M,PSRAM=opi,PartitionScheme=custom"
$outputDir = "./build/esp32.esp32.esp32s3"
$buildPath = "./build/cache"

$compileArgs = @(
    "compile",
    "-v",
    "--jobs", "0",
    "--build-path", $buildPath,
    "--fqbn", $fqbn,
    "--output-dir", $outputDir,
    "sugarota.ino"
)

if ($Clean) {
    $compileArgs += "--clean"
}

Write-Host "==========================================================" -ForegroundColor Green
Write-Host "       Starting Sugarota Firmware Build                   " -ForegroundColor Green
Write-Host "==========================================================" -ForegroundColor Green
Write-Host "FQBN:        $fqbn" -ForegroundColor DarkGray
Write-Host "Output Dir:  $outputDir" -ForegroundColor DarkGray
Write-Host "Build Cache: $buildPath`n" -ForegroundColor DarkGray

# Pipeline sequential stages:
# 1: Analyzing (10%)
# 2: Compiling Sketch (35%)
# 3: Compiling Objects (60%)
# 4: Linking Firmware (80%)
# 5: Generating Binaries (92%)
# 6: Merging ROM Image (98%)
$currentProgress = 10
$currentStage = "Analyzing Sketch"
$startTime = [System.Diagnostics.Stopwatch]::StartNew()

function Show-ProgressBar([int]$pct, [string]$stage, [System.Diagnostics.Stopwatch]$sw) {
    $barWidth = 30
    $clampedPct = [Math]::Min(98, [Math]::Max(1, $pct))
    $filled = [int](($clampedPct / 100) * $barWidth)
    $empty = $barWidth - $filled
    $bar = ("#" * $filled) + ("-" * $empty)
    
    $elapsedSec = [int]$sw.Elapsed.TotalSeconds
    $timeStr = "Elapsed: ${elapsedSec}s"
    
    $stagePadded = if ($stage.Length -gt 26) { $stage.Substring(0, 23) + "..." } else { $stage.PadRight(26) }
    $line = "`r[$bar] $clampedPct% | $stagePadded | $timeStr    "
    Write-Host -NoNewline $line
}

# Start compiler process with stdout redirection for progress tracking
$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = "arduino-cli"
$psi.Arguments = ($compileArgs -join " ")
$psi.RedirectStandardOutput = $true
$psi.RedirectStandardError = $true
$psi.UseShellExecute = $false
$psi.CreateNoWindow = $true

$proc = [System.Diagnostics.Process]::Start($psi)

Show-ProgressBar $currentProgress $currentStage $startTime

while (-not $proc.HasExited) {
    $line = $proc.StandardOutput.ReadLine()
    if ($null -ne $line) {
        if ($VerboseOutput) {
            Write-Host "`n$line"
        }
        
        # Strictly monotonic progress based on sequential compiler stages
        if ($line -match "Compiling sketch|sugarota\.ino\.cpp") {
            if ($currentProgress -lt 35) { $currentProgress = 35 }
            $currentStage = "Compiling Sketch"
        }
        elseif ($line -match "xtensa-esp32s3-elf-(g\+\+|gcc)") {
            if ($currentProgress -lt 60) { $currentProgress = 60 }
            $currentStage = "Compiling Objects"
        }
        elseif ($line -match "Linking everything together|sugarota\.ino\.elf") {
            if ($currentProgress -lt 80) { $currentProgress = 80 }
            $currentStage = "Linking Firmware"
        }
        elseif ($line -match "Creating ESP32-S3 image|esptool\.exe.*elf2image") {
            if ($currentProgress -lt 92) { $currentProgress = 92 }
            $currentStage = "Generating Binaries"
        }
        elseif ($line -match "merge-bin|sugarota\.ino\.merged\.bin") {
            if ($currentProgress -lt 98) { $currentProgress = 98 }
            $currentStage = "Merging ROM Image"
        }
        
        Show-ProgressBar $currentProgress $currentStage $startTime
    }
    else {
        # Keep elapsed time ticking dynamically even while waiting for IO
        Show-ProgressBar $currentProgress $currentStage $startTime
        Start-Sleep -Milliseconds 100
    }
}

$remainingOut = $proc.StandardOutput.ReadToEnd()
$stderrOut = $proc.StandardError.ReadToEnd()

if ($proc.ExitCode -eq 0) {
    # 100% Completed
    $bar = "#" * 30
    $totalSec = [int]$startTime.Elapsed.TotalSeconds
    Write-Host "`r[$bar] 100% | Complete                    | Finished in ${totalSec}s    `n" -ForegroundColor Green
    
    # Extract memory summary from output if available
    $fullOut = $remainingOut
    if ($fullOut -match "Sketch uses (\d+ bytes [^\n\r]+)") {
        Write-Host "[MEMORY] Program Storage: $($matches[1])" -ForegroundColor DarkGray
    }
    if ($fullOut -match "Global variables use (\d+ bytes [^\n\r]+)") {
        Write-Host "[MEMORY] Dynamic Memory:  $($matches[1])" -ForegroundColor DarkGray
    }
    
    Write-Host "`n[SUCCESS] Firmware compiled successfully!" -ForegroundColor Green
    Write-Host "[OUTPUT]  Binaries written to: $outputDir" -ForegroundColor Cyan
}
else {
    Write-Host "`n`n[ERROR] Build failed with exit code $($proc.ExitCode)." -ForegroundColor Red
    if ($stderrOut) {
        Write-Host $stderrOut -ForegroundColor Red
    }
    if ($remainingOut) {
        Write-Host $remainingOut
    }
    exit $proc.ExitCode
}
