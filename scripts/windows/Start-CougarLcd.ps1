[CmdletBinding()]
param(
    [string] $Distro
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
& wsl.exe -d $Distro -u root -- systemctl restart cougar-lcd.service
if ($LASTEXITCODE -ne 0) { throw 'The WSL service did not start.' }
