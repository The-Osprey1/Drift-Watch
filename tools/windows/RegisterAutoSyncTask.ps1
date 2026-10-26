# Sets the watch's clock every time it is plugged in to this PC.
# Registers a scheduled task that starts at your sign-in and runs DriftWatchSync.ps1 -Watch
# hidden in the background; that waits for the watch's USB chip to appear and syncs each time.
# Runs for your own account only, so a normal (not administrator) PowerShell window is enough.
# Run it again after moving the DriftWatch folder. To remove it:
#   Unregister-ScheduledTask -TaskName DriftWatch-USB-AutoSync
$scriptPath = Join-Path $PSScriptRoot "DriftWatchSync.ps1"
$taskName = "DriftWatch-USB-AutoSync"
$user = "$env:USERDOMAIN\$env:USERNAME"

$action = New-ScheduledTaskAction -Execute "powershell.exe" `
    -Argument "-NoProfile -WindowStyle Hidden -ExecutionPolicy Bypass -File `"$scriptPath`" -Watch"
$trigger = New-ScheduledTaskTrigger -AtLogOn -User $user
$principal = New-ScheduledTaskPrincipal -UserId $user -LogonType Interactive -RunLevel Limited
# Keep running on battery and never stop it after the default 3 days.
$settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries `
    -ExecutionTimeLimit ([TimeSpan]::Zero) -MultipleInstances IgnoreNew

try {
    Stop-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue
    Unregister-ScheduledTask -TaskName $taskName -Confirm:$false -ErrorAction SilentlyContinue
    Register-ScheduledTask -TaskName $taskName -Action $action -Trigger $trigger -Principal $principal -Settings $settings `
        -Description "Sets the Drift-Watch clock whenever the watch is plugged in over USB-C." | Out-Null
    # Start it now, so there is no need to sign out and back in.
    Start-ScheduledTask -TaskName $taskName
    Write-Host "Scheduled task '$taskName' registered and running. Plug in the watch to sync it." -ForegroundColor Green
} catch {
    Write-Error "Could not register the scheduled task: $_"
}
