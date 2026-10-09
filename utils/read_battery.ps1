param(
    [string]$PortName = "COM6",
    [int]$BaudRate = 115200,
    [switch]$Clear = $false
)

$port = New-Object System.IO.Ports.SerialPort $PortName, $BaudRate
$port.ReadTimeout = 4000
$port.WriteTimeout = 3000
$port.DtrEnable = $false
$port.RtsEnable = $false
$port.NewLine = "`n"

try {
    $port.Open()
    Start-Sleep -Milliseconds 400
    $port.DiscardInBuffer()
    if ($Clear) {
        $port.WriteLine("CLEAR_BATTERY_LOG")
        Start-Sleep -Milliseconds 500
        $data = $port.ReadExisting()
        Write-Output "--- CLEAR RESPONSE ---"
        Write-Output $data
    } else {
        $port.WriteLine("GET_BATTERY_LOG")
        $output = New-Object System.Text.StringBuilder
        $sw = [System.Diagnostics.Stopwatch]::StartNew()
        while ($sw.ElapsedMilliseconds -lt 6000) {
            $chunk = $port.ReadExisting()
            if ($chunk) {
                [void]$output.Append($chunk)
                if ($output.ToString() -match "--- END BATTERY LOG ---") {
                    break
                }
            }
            Start-Sleep -Milliseconds 100
        }
        Write-Output "--- BATTERY LOG RESPONSE ---"
        Write-Output $output.ToString()
    }
} catch {
    Write-Output "ERROR: $_"
} finally {
    if ($port.IsOpen) {
        $port.Close()
    }
}
