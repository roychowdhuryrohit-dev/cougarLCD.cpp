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

function Test-CougarUsbVisible {
    & wsl.exe -d $Distro --cd / -- sh -lc `
        'lsusb -d 1d6b:0126 >/dev/null 2>&1'
    $LASTEXITCODE -eq 0
}

function Repair-WindowsMount {
    & wsl.exe -d $Distro --cd / -- sh -lc `
        'mountpoint -q /mnt/c && test -d /mnt/c/Windows/System32' 2>$null
    if ($LASTEXITCODE -eq 0) { return }

    & wsl.exe -d $Distro -u root --cd / -- sh -lc `
        'umount -l /mnt/c 2>/dev/null || true; mkdir -p /mnt/c; mount -t drvfs C: /mnt/c -o metadata,uid=1000,gid=1000,umask=22,fmask=11'
    if ($LASTEXITCODE -ne 0) { throw 'The WSL /mnt/c mount could not be repaired.' }
}

function Connect-CougarUsb {
    if (Test-CougarUsbVisible) { return }

    # Binding is persistent; attachment must be repeated for each WSL VM.
    $attachOutput = @(& $usbipdPath attach --wsl $Distro `
        --hardware-id 1d6b:0126 2>&1)
    Start-Sleep -Seconds 2
    if (Test-CougarUsbVisible) { return }

    # Repair a stale Windows Attached state when the new WSL VM has no device.
    $null = & $usbipdPath detach --hardware-id 1d6b:0126 2>&1
    Start-Sleep -Seconds 2
    $attachOutput = @(& $usbipdPath attach --wsl $Distro `
        --hardware-id 1d6b:0126 2>&1)
    Start-Sleep -Seconds 2
    if (-not (Test-CougarUsbVisible)) {
        throw "USB attachment failed: $($attachOutput -join ' ')"
    }
}

Repair-WindowsMount
Connect-CougarUsb
& wsl.exe -d $Distro -u root -- systemctl restart cougar-lcd.service
if ($LASTEXITCODE -ne 0) { throw 'The WSL service did not start.' }

if ($KeepAlive) {
    while ($true) {
        # Keep a Windows-side client active, but return periodically to repair
        # stale 9P mounts and USB state after sleep, resume, or WSL shutdown.
        & wsl.exe -d $Distro --cd / -- /usr/bin/sleep 30
        Start-Sleep -Seconds 1
        try {
            Repair-WindowsMount
            Connect-CougarUsb
            & wsl.exe -d $Distro -u root -- systemctl is-active --quiet cougar-lcd.service
            if ($LASTEXITCODE -ne 0) {
                & wsl.exe -d $Distro -u root -- systemctl restart cougar-lcd.service
            }
        } catch {
            Write-Warning "COUGAR LCD health check failed: $($_.Exception.Message)"
            Start-Sleep -Seconds 5
        }
    }
}
