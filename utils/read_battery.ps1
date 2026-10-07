param(
    [string]$PortName = "COM6",
    [int]$BaudRate = 115200,
    [switch]$Clear = $false
)

$port = New-Object System.IO.Ports.SerialPort $PortName, $BaudRate
$port.ReadTimeout = 4000
$port.WriteTimeout = 3000
$port.DtrEnable = $true
$port.RtsEnable = $true
$port.NewLine = "`n"

try {
    $port.Open()
    Start-Sleep -Milliseconds 800
    $port.DiscardInBuffer()
    if ($Clear) {
        $port.WriteLine("CLEAR_BATTERY_LOG")
        Start-Sleep -Milliseconds 1000
        $data = $port.ReadExisting()
        Write-Output "--- CLEAR RESPONSE ---"
        Write-Output $data
    } else {
        $port.WriteLine("GET_BATTERY_LOG")
        Start-Sleep -Milliseconds 2500
        $data = $port.ReadExisting()
        Write-Output "--- BATTERY LOG RESPONSE ---"
        Write-Output $data
    }
} catch {
    Write-Output "ERROR: $_"
} finally {
    if ($port.IsOpen) {
        $port.Close()
    }
}
