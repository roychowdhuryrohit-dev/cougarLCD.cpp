[CmdletBinding()]
param(
    [switch] $RemoveConfiguration
)

$ErrorActionPreference = 'Stop'
$taskName = 'COUGAR LCD WSL Service'
$configPath = Join-Path $env:ProgramData 'CougarLCD\host-config.json'
$distro = $null
if (Test-Path -LiteralPath $configPath) {
    $distro = (Get-Content -Raw -LiteralPath $configPath | ConvertFrom-Json).Distro
}

Unregister-ScheduledTask -TaskName $taskName -Confirm:$false -ErrorAction SilentlyContinue
if ($distro) {
    & wsl.exe -d $distro -u root -- systemctl disable --now cougar-lcd.service
}

$installDirectory = Join-Path $env:ProgramData 'CougarLCD'
Remove-Item -LiteralPath (Join-Path $installDirectory 'Start-CougarLcd.ps1') `
    -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath $configPath -Force -ErrorAction SilentlyContinue
if ($RemoveConfiguration) {
    Remove-Item -LiteralPath $installDirectory -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Host 'Startup task removed and the WSL service disabled.'
Write-Host 'The USB binding and installed binary were left in place.'
