[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

function Test-Administrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}
if (-not (Test-Administrator)) {
    throw 'Run this script from an elevated PowerShell window.'
}

$sdk = Get-ItemProperty -LiteralPath 'HKLM:\Software\AMD\RyzenMasterMonitoringSDK' `
    -ErrorAction SilentlyContinue
if (-not $sdk) {
    throw 'Install the official AMD Ryzen Master Monitoring SDK first. This project does not redistribute or download it.'
}

$dataDirectory = Join-Path $env:ProgramData 'CougarLCD'
New-Item -ItemType Directory -Path $dataDirectory -Force | Out-Null
$installedScript = Join-Path $dataDirectory 'Start-AmdTelemetry.ps1'
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'Start-AmdTelemetry.ps1') `
    -Destination $installedScript -Force

$taskName = 'COUGAR LCD AMD Telemetry'
$action = New-ScheduledTaskAction `
    -Execute "$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe" `
    -Argument "-NoProfile -NonInteractive -WindowStyle Hidden -ExecutionPolicy Bypass -File `"$installedScript`"" `
    -WorkingDirectory $dataDirectory
$trigger = New-ScheduledTaskTrigger -AtLogOn
$principal = New-ScheduledTaskPrincipal -UserId `
    ([Security.Principal.WindowsIdentity]::GetCurrent().Name) `
    -LogonType Interactive -RunLevel Highest
$settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries `
    -DontStopIfGoingOnBatteries -StartWhenAvailable -MultipleInstances IgnoreNew
$task = New-ScheduledTask -Action $action -Trigger $trigger -Principal $principal `
    -Settings $settings -Description 'Starts the AMD-signed CPU temperature CSV provider for cougarLCD.cpp.'
Register-ScheduledTask -TaskName $taskName -InputObject $task -Force | Out-Null
Start-ScheduledTask -TaskName $taskName

Write-Host 'AMD telemetry startup installed.' -ForegroundColor Green
Write-Host 'If the CSV does not update, close other hardware monitors and see the FAQ.'
