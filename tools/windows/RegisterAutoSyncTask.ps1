# Registers a Windows Scheduled Task to run DriftWatchSync.ps1 whenever the watch is plugged in over USB.
$scriptPath = Join-Path $PSScriptRoot "DriftWatchSync.ps1"
$taskName = "DriftWatch-USB-AutoSync"

$action = New-ScheduledTaskAction -Execute "powershell.exe" -Argument "-WindowStyle Hidden -ExecutionPolicy Bypass -File `"$scriptPath`""
$trigger = New-ScheduledTaskTrigger -AtLogOn

try {
    Unregister-ScheduledTask -TaskName $taskName -Confirm:$false -ErrorAction SilentlyContinue
    Register-ScheduledTask -TaskName $taskName -Action $action -Trigger $trigger -Description "Synchronizes time to DriftWatch whenever plugged into USB."
    Write-Host "Scheduled task '$taskName' successfully registered!" -ForegroundColor Green
} catch {
    Write-Error "Could not register scheduled task: $_"
}
