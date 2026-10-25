# DriftWatch Instant USB-C Time Sync Script for Windows
# Automatically discovers the WCH CH343 COM port (VID 1A86 & PID 55D3) and sends current UTC time.

$ErrorActionPreference = "Stop"

function Sync-DriftWatch {
    $devices = Get-CimInstance Win32_PnPEntity | Where-Object { $_.PNPDeviceID -match "VID_1A86&PID_55D3" -and $_.Name -match "\(COM\d+\)" }
    if (-not $devices) {
        Write-Warning "DriftWatch not detected on USB. Please plug in the USB-C cable."
        return $false
    }

    if ($devices[0].Name -match "\((COM\d+)\)") {
        $comPort = $matches[1]
    } else {
        Write-Warning "Could not parse COM port from $($devices[0].Name)"
        return $false
    }
    $epoch = [DateTimeOffset]::UtcNow.ToUnixTimeSeconds()
    Write-Host "Found DriftWatch on $comPort. Synchronizing time to UTC epoch $epoch..." -ForegroundColor Cyan

    $sp = New-Object System.IO.Ports.SerialPort $comPort, 115200, "None", 8, "One"
    $sp.DtrEnable = $false
    $sp.RtsEnable = $false
    $sp.ReadTimeout = 1000
    $sp.WriteTimeout = 1000

    try {
        $sp.Open()
        Start-Sleep -Milliseconds 200
        $sp.WriteLine("TIME:$epoch")
        Start-Sleep -Milliseconds 150
        $response = ""
        while ($sp.BytesToRead -gt 0) {
            $response += $sp.ReadExisting()
        }
        $sp.Close()
        Write-Host "Success! Watch responded: $response" -ForegroundColor Green
        return $true
    } catch {
        Write-Error "Error communicating with DriftWatch on $($comPort): $_"
        if ($sp.IsOpen) { $sp.Close() }
        return $false
    }
}

Sync-DriftWatch
