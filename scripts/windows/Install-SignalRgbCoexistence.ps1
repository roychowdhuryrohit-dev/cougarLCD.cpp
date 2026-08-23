[CmdletBinding()]
param(
    [switch] $StartNow
)

$ErrorActionPreference = 'Stop'

function Test-Administrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Get-RegistryBackup {
    param([string] $Path, [string] $Name)
    $value = Get-ItemPropertyValue -LiteralPath $Path -Name $Name `
        -ErrorAction SilentlyContinue
    @{
        Path = $Path
        Name = $Name
        Exists = $null -ne $value
        Value = if ($null -ne $value) { [string] $value } else { $null }
    }
}

if (-not (Test-Administrator)) {
    throw 'Run this script from an elevated PowerShell window.'
}
if (-not (Get-Service -Name 'SignalRgb.Service' -ErrorAction SilentlyContinue)) {
    throw 'SignalRGB management service was not found.'
}
if (-not (Get-ItemProperty -LiteralPath `
    'HKLM:\Software\AMD\RyzenMasterMonitoringSDK' -ErrorAction SilentlyContinue)) {
    throw 'Install the official AMD Ryzen Master Monitoring SDK first.'
}

$dataDirectory = Join-Path $env:ProgramData 'CougarLCD'
New-Item -ItemType Directory -Path $dataDirectory -Force | Out-Null
$installedScript = Join-Path $dataDirectory 'Start-SensorsInOrder.ps1'
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'Start-SensorsInOrder.ps1') `
    -Destination $installedScript -Force

$monitoringKey = 'HKCU:\Software\WhirlwindFX\SignalRgb\Monitoring'
$fanKey = Join-Path $monitoringKey 'Fans'
$approvedKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Explorer\StartupApproved\Run'
$serviceInfo = Get-CimInstance Win32_Service -Filter "Name='SignalRgb.Service'"
$approvedValue = Get-ItemPropertyValue -LiteralPath $approvedKey -Name SignalRgb `
    -ErrorAction SilentlyContinue
$standaloneTask = Get-ScheduledTask -TaskName 'COUGAR LCD AMD Telemetry' `
    -ErrorAction SilentlyContinue
$backupPath = Join-Path $dataDirectory 'signalrgb-coexistence-backup.json'
if (-not (Test-Path -LiteralPath $backupPath)) {
    $backup = @{
        SignalServiceStartMode = $serviceInfo.StartMode
        StartupApprovedExists = $null -ne $approvedValue
        StartupApprovedBase64 = if ($null -ne $approvedValue) {
            [Convert]::ToBase64String([byte[]] $approvedValue)
        } else { $null }
        StandaloneAmdTaskWasEnabled = $standaloneTask -and $standaloneTask.State -ne 'Disabled'
        RegistryValues = @(
            Get-RegistryBackup $monitoringKey 'Disabled'
            Get-RegistryBackup $monitoringKey 'GpuEnabled'
            Get-RegistryBackup $monitoringKey 'MotherboardEnabled'
            Get-RegistryBackup $fanKey 'Disabled'
        )
    }
    $backup | ConvertTo-Json -Depth 5 | Set-Content -Encoding UTF8 `
        -LiteralPath $backupPath
}

New-Item -Path $monitoringKey -Force | Out-Null
New-Item -Path $fanKey -Force | Out-Null
Set-ItemProperty -LiteralPath $monitoringKey -Name Disabled -Type String -Value true
Set-ItemProperty -LiteralPath $monitoringKey -Name GpuEnabled -Type String -Value false
Set-ItemProperty -LiteralPath $monitoringKey -Name MotherboardEnabled -Type String -Value false
Set-ItemProperty -LiteralPath $fanKey -Name Disabled -Type String -Value true

# Disable SignalRGB's normal UI race; the ordered task launches it later.
New-Item -Path $approvedKey -Force | Out-Null
$disabled = [byte[]]::new(12)
$disabled[0] = 3
Set-ItemProperty -LiteralPath $approvedKey -Name SignalRgb -Type Binary -Value $disabled
Set-Service -Name 'SignalRgb.Service' -StartupType Manual
if ($standaloneTask) { Disable-ScheduledTask -InputObject $standaloneTask | Out-Null }

$taskName = 'COUGAR LCD Sensor Startup Order'
$action = New-ScheduledTaskAction `
    -Execute "$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe" `
    -Argument "-NoProfile -NonInteractive -WindowStyle Hidden -ExecutionPolicy Bypass -File `"$installedScript`"" `
    -WorkingDirectory $dataDirectory
$trigger = New-ScheduledTaskTrigger -AtLogOn
$principal = New-ScheduledTaskPrincipal -UserId `
    ([Security.Principal.WindowsIdentity]::GetCurrent().Name) `
    -LogonType Interactive -RunLevel Highest
$settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries `
    -DontStopIfGoingOnBatteries -StartWhenAvailable -MultipleInstances IgnoreNew `
    -ExecutionTimeLimit (New-TimeSpan -Minutes 3)
$task = New-ScheduledTask -Action $action -Trigger $trigger -Principal $principal `
    -Settings $settings `
    -Description 'Starts AMD telemetry before SignalRGB and keeps SignalRGB hardware monitoring disabled.'
Register-ScheduledTask -TaskName $taskName -InputObject $task -Force | Out-Null

if ($StartNow) { Start-ScheduledTask -TaskName $taskName }
Write-Host 'Ordered SignalRGB coexistence startup installed.' -ForegroundColor Green
Write-Host 'Restart Windows, or rerun with -StartNow after closing hardware monitors.'
Write-Host "Log: $dataDirectory\sensor-startup-order.log"
