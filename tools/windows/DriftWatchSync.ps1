# Drift-Watch USB-C time sync for Windows.
# Finds the watch by its USB-to-serial chip (WCH CH343, VID 1A86 & PID 55D3) and sets its clock.
#
#   .\DriftWatchSync.ps1          sync once, now
#   .\DriftWatchSync.ps1 -Watch   stay running and sync every time the watch is plugged in
#                                 (RegisterAutoSyncTask.ps1 starts this at sign-in)
#
# Two things on the watch shape how this talks to it (bench check on COM6, v0.15.0):
# - Opening the port can restart the watch: the CH343 control lines are wired to its reset.
#   It then needs about 0.75 s before it reads serial again, and anything sent sooner is lost.
# - With the screen off the watch is in light sleep, and the bytes that wake it are lost.
# So it never sends the time blind: it sends a newline and INFO? until the watch answers
# (up to 5 s), then TIME:, and it only reports success when the watch replies "Time set to".
# The exit code is 0 on success and 1 on failure, for scheduled tasks.
param(
    [switch]$Watch
)

$ErrorActionPreference = "Stop"
$UsbId = "VID_1A86&PID_55D3"

function Find-DriftWatchPort {
    $devices = Get-CimInstance Win32_PnPEntity | Where-Object { $_.PNPDeviceID -match $UsbId -and $_.Name -match "\(COM\d+\)" }
    foreach ($d in @($devices)) {
        if ($d.Name -match "\((COM\d+)\)") { return $matches[1] }
    }
    return $null
}

# Reads whatever has arrived within $ms milliseconds and returns it.
function Read-For($sp, [int]$ms) {
    $text = ""
    $until = (Get-Date).AddMilliseconds($ms)
    while ((Get-Date) -lt $until) {
        if ($sp.BytesToRead -gt 0) { $text += $sp.ReadExisting() }
        else { Start-Sleep -Milliseconds 20 }
    }
    return $text
}

function Sync-DriftWatch {
    $comPort = Find-DriftWatchPort
    if (-not $comPort) {
        Write-Warning "Drift-Watch not found on USB. Plug in the USB-C cable."
        return $false
    }
    Write-Host "Found Drift-Watch on $comPort." -ForegroundColor Cyan

    $sp = New-Object System.IO.Ports.SerialPort $comPort, 115200, "None", 8, "One"
    $sp.DtrEnable = $false  # keep the reset line released; some drivers still pulse it on open
    $sp.RtsEnable = $false
    $sp.NewLine = "`n"
    $sp.ReadTimeout = 500
    $sp.WriteTimeout = 1000

    try {
        $sp.Open()
        # Wait for the watch to answer: through a restart, and out of light sleep.
        $seen = ""
        $info = $null
        $until = (Get-Date).AddSeconds(5)
        while (-not $info -and (Get-Date) -lt $until) {
            $sp.Write("`n")
            Start-Sleep -Milliseconds 50
            $sp.WriteLine("INFO?")
            $seen += Read-For $sp 300
            if ($seen -match "DRIFT:VER=[^\r\n]*") { $info = $matches[0] }
        }
        if (-not $info) {
            Write-Warning "The watch did not answer on $comPort within 5 s. Is another program using the port?"
            return $false
        }
        if ($seen -match "boot: Drift-Watch|rst:0x") {
            Write-Host "The watch restarted when the port opened (normal for this USB chip)." -ForegroundColor DarkGray
        }
        Write-Host "Watch says: $info" -ForegroundColor DarkGray

        # Whole seconds since 1970 in UTC, taken now so the handshake time is not counted.
        $epoch = [DateTimeOffset]::UtcNow.ToUnixTimeSeconds()
        $sp.WriteLine("TIME:$epoch")
        $reply = Read-For $sp 800
        if ($reply -match "Time set to (\d+)") {
            Write-Host "Success! Watch time set to $($matches[1]) (UTC seconds)." -ForegroundColor Green
            return $true
        }
        Write-Warning "The watch did not confirm the time (reply: '$($reply.Trim())')."
        return $false
    } catch {
        Write-Warning "Error talking to Drift-Watch on $($comPort): $_"
        return $false
    } finally {
        if ($sp.IsOpen) { $sp.Close() }
    }
}

if (-not $Watch) {
    if (Sync-DriftWatch) { exit 0 } else { exit 1 }
}

# Watch mode: sync now if the watch is already plugged in, then on every plug-in.
# Win32_PnPEntity creation events need no administrator rights.
if (Find-DriftWatchPort) { [void](Sync-DriftWatch) }
$query = "SELECT * FROM __InstanceCreationEvent WITHIN 2 WHERE TargetInstance ISA 'Win32_PnPEntity' AND TargetInstance.PNPDeviceID LIKE '%VID_1A86&PID_55D3%'"
Register-CimIndicationEvent -Query $query -SourceIdentifier "DriftWatchPlugIn" | Out-Null
Write-Host "Waiting for Drift-Watch to be plugged in (Ctrl+C to stop)..." -ForegroundColor Cyan
try {
    while ($true) {
        $e = Wait-Event -SourceIdentifier "DriftWatchPlugIn"
        Remove-Event -EventIdentifier $e.EventIdentifier
        # Windows adds the COM port a moment after the device itself; give it up to 5 s.
        for ($i = 0; $i -lt 10 -and -not (Find-DriftWatchPort); $i++) { Start-Sleep -Milliseconds 500 }
        Start-Sleep -Milliseconds 500
        [void](Sync-DriftWatch)
        # One plug-in can raise more than one event (the USB device and its COM port).
        Start-Sleep -Seconds 3
        Get-Event -SourceIdentifier "DriftWatchPlugIn" -ErrorAction SilentlyContinue | Remove-Event
    }
} finally {
    Unregister-Event -SourceIdentifier "DriftWatchPlugIn" -ErrorAction SilentlyContinue
}
