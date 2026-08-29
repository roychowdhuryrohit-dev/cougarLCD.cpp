[CmdletBinding()]
param(
    [string] $Distro,
    [switch] $KeepAlive
)

$ErrorActionPreference = 'Stop'
$configPath = Join-Path $env:ProgramData 'CougarLCD\host-config.json'
if (-not $Distro -and (Test-Path -LiteralPath $configPath)) {
    $Distro = (Get-Content -Raw -LiteralPath $configPath | ConvertFrom-Json).Distro
}
if (-not $Distro) { throw 'No WSL distribution was configured.' }

$usbipdCommand = Get-Command usbipd.exe -ErrorAction SilentlyContinue
$usbipdPath = if ($usbipdCommand) { $usbipdCommand.Source } else { $null }
if (-not $usbipdPath) {
    $fallback = Join-Path $env:ProgramFiles 'usbipd-win\usbipd.exe'
    if (Test-Path -LiteralPath $fallback) { $usbipdPath = $fallback }
}
if (-not $usbipdPath) { throw 'usbipd-win is not installed.' }

# Binding is persistent; attachment must be repeated after a Windows restart.
$attachOutput = @(& $usbipdPath attach --wsl $Distro --hardware-id 1d6b:0126 2>&1)
$attachResult = $LASTEXITCODE
if ($attachResult -ne 0 -and ($attachOutput -join "`n") -notmatch 'already attached') {
    throw "USB attachment failed: $($attachOutput -join ' ')"
}

Start-Sleep -Seconds 2
& wsl.exe -d $Distro -- sh -lc 'lsusb -d 1d6b:0126 >/dev/null 2>&1'
if ($LASTEXITCODE -ne 0) {
    # usbipd-win can report Attached after WSL restarts while the new VM has no
    # USB interface. Let the systemd pre-start check repair that stale state.
    Write-Verbose 'The USB attachment is stale; the WSL service will repair it.'
}
& wsl.exe -d $Distro -u root -- systemctl restart cougar-lcd.service
if ($LASTEXITCODE -ne 0) { throw 'The WSL service did not start.' }

if ($KeepAlive) {
    # A Windows-side WSL client prevents the distro and its systemd services
    # from being stopped when no interactive WSL terminal is open. If WSL is
    # deliberately shut down, reconnect after a short delay.
    while ($true) {
        & wsl.exe -d $Distro --cd / -- sh -lc 'exec sleep infinity'
        Start-Sleep -Seconds 2
    }
}
