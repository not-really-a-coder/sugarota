param(
    [string]$PortName = "COM6",
    [int]$BaudRate = 115200
)

$port = New-Object System.IO.Ports.SerialPort $PortName, $BaudRate
$port.ReadTimeout = 3000
$port.WriteTimeout = 3000
$port.DtrEnable = $true
$port.RtsEnable = $true
$port.NewLine = "`n"
try {
    $port.Open()
    Start-Sleep -Milliseconds 800
    $port.DiscardInBuffer()
    $port.WriteLine("GET_CRASH_LOG")
    Start-Sleep -Milliseconds 2500
    $data = $port.ReadExisting()
    Write-Output "--- RESPONSE ---"
    Write-Output $data
} catch {
    Write-Output "ERROR: $_"
} finally {
    if ($port.IsOpen) {
        $port.Close()
    }
}
