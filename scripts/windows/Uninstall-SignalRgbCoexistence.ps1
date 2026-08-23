[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$dataDirectory = Join-Path $env:ProgramData 'CougarLCD'
$backupPath = Join-Path $dataDirectory 'signalrgb-coexistence-backup.json'
$taskName = 'COUGAR LCD Sensor Startup Order'

Unregister-ScheduledTask -TaskName $taskName -Confirm:$false `
    -ErrorAction SilentlyContinue
if (Test-Path -LiteralPath $backupPath) {
    $backup = Get-Content -Raw -LiteralPath $backupPath | ConvertFrom-Json
    foreach ($entry in $backup.RegistryValues) {
        if ($entry.Exists) {
            New-Item -Path $entry.Path -Force | Out-Null
            Set-ItemProperty -LiteralPath $entry.Path -Name $entry.Name `
                -Type String -Value $entry.Value
        } else {
            Remove-ItemProperty -LiteralPath $entry.Path -Name $entry.Name `
                -ErrorAction SilentlyContinue
        }
    }

    $approvedKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Explorer\StartupApproved\Run'
    if ($backup.StartupApprovedExists) {
        $approved = [Convert]::FromBase64String($backup.StartupApprovedBase64)
        Set-ItemProperty -LiteralPath $approvedKey -Name SignalRgb `
            -Type Binary -Value $approved
    } else {
        Remove-ItemProperty -LiteralPath $approvedKey -Name SignalRgb `
            -ErrorAction SilentlyContinue
    }

    $startupType = switch ($backup.SignalServiceStartMode) {
        'Auto' { 'Automatic' }
        'Disabled' { 'Disabled' }
        default { 'Manual' }
    }
    Set-Service -Name 'SignalRgb.Service' -StartupType $startupType
    if ($backup.StandaloneAmdTaskWasEnabled) {
        Enable-ScheduledTask -TaskName 'COUGAR LCD AMD Telemetry' `
            -ErrorAction SilentlyContinue | Out-Null
    }
}

Remove-Item -LiteralPath (Join-Path $dataDirectory 'Start-SensorsInOrder.ps1') `
    -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath $backupPath -Force -ErrorAction SilentlyContinue
Start-Service -Name 'SignalRgb.Service' -ErrorAction SilentlyContinue
Write-Host 'SignalRGB startup and monitoring settings were restored.'
