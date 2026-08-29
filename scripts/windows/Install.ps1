[CmdletBinding()]
param(
    [string] $Distro,
    [switch] $SkipLinuxBuild
)

$ErrorActionPreference = 'Stop'

function Test-Administrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

if (-not (Test-Administrator)) {
    throw 'Run this script from an elevated PowerShell window.'
}

if (-not (Get-Command wsl.exe -ErrorAction SilentlyContinue)) {
    throw 'WSL2 is not installed. Run: wsl --install'
}
$distros = @(& wsl.exe --list --quiet | ForEach-Object { $_.Trim("`0 ") } |
    Where-Object { $_ })
if (-not $Distro) {
    $Distro = $distros | Where-Object { $_ -match '^Ubuntu' } | Select-Object -First 1
}
if (-not $Distro -or $Distro -notin $distros) {
    throw "Choose an installed WSL distribution with -Distro. Found: $($distros -join ', ')"
}

$usbipdCommand = Get-Command usbipd.exe -ErrorAction SilentlyContinue
$usbipdPath = if ($usbipdCommand) { $usbipdCommand.Source } else { $null }
if (-not $usbipdPath) {
    $fallback = Join-Path $env:ProgramFiles 'usbipd-win\usbipd.exe'
    if (Test-Path -LiteralPath $fallback) { $usbipdPath = $fallback }
}
if (-not $usbipdPath) {
    throw 'Install usbipd-win first: winget install --id dorssel.usbipd-win'
}

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
if (-not $SkipLinuxBuild) {
    $wslRoot = (& wsl.exe -d $Distro -- wslpath -a $repoRoot).Trim()
    if ($LASTEXITCODE -ne 0 -or -not $wslRoot) { throw 'Could not translate the repository path for WSL.' }
    $linuxInstaller = "$wslRoot/scripts/linux/install.sh"
    & wsl.exe -d $Distro -- bash $linuxInstaller
    if ($LASTEXITCODE -ne 0) { throw 'The Linux build/install failed.' }
}

Write-Host 'Binding the confirmed COUGAR LCD USB device (VID_1D6B/PID_0126)...'
& $usbipdPath bind --hardware-id 1d6b:0126
# "already shared" is safe; visibility is checked when the launcher runs.

$installDirectory = Join-Path $env:ProgramData 'CougarLCD'
New-Item -ItemType Directory -Path $installDirectory -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'Start-CougarLcd.ps1') `
    -Destination (Join-Path $installDirectory 'Start-CougarLcd.ps1') -Force
@{ Distro = $Distro } | ConvertTo-Json | Set-Content -Encoding UTF8 `
    -LiteralPath (Join-Path $installDirectory 'host-config.json')

$taskName = 'COUGAR LCD WSL Service'
$launcher = Join-Path $installDirectory 'Start-CougarLcd.ps1'
$action = New-ScheduledTaskAction `
    -Execute "$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe" `
    -Argument "-NoProfile -NonInteractive -WindowStyle Hidden -ExecutionPolicy Bypass -File `"$launcher`" -KeepAlive"
$trigger = New-ScheduledTaskTrigger -AtLogOn
$principal = New-ScheduledTaskPrincipal -UserId `
    ([Security.Principal.WindowsIdentity]::GetCurrent().Name) `
    -LogonType Interactive -RunLevel Highest
$settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries `
    -DontStopIfGoingOnBatteries -StartWhenAvailable `
    -MultipleInstances IgnoreNew -ExecutionTimeLimit ([TimeSpan]::Zero) -Hidden
$task = New-ScheduledTask -Action $action -Trigger $trigger -Principal $principal `
    -Settings $settings -Description 'Attaches the COUGAR LCD to WSL and starts its service.'
Register-ScheduledTask -TaskName $taskName -InputObject $task -Force | Out-Null

& $launcher -Distro $Distro
Write-Host ''
Write-Host 'COUGAR LCD service installed and started.' -ForegroundColor Green
Write-Host "Check it with: wsl -d $Distro -- systemctl status cougar-lcd"
