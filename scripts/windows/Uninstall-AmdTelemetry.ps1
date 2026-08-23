[CmdletBinding()]
param()

$taskName = 'COUGAR LCD AMD Telemetry'
Unregister-ScheduledTask -TaskName $taskName -Confirm:$false -ErrorAction SilentlyContinue
Get-Process -Name AMDRyzenMasterMonitoringSampleApp -ErrorAction SilentlyContinue |
    Stop-Process -Force -ErrorAction SilentlyContinue
$scriptPath = Join-Path $env:ProgramData 'CougarLCD\Start-AmdTelemetry.ps1'
Remove-Item -LiteralPath $scriptPath -Force -ErrorAction SilentlyContinue
Write-Host 'AMD telemetry startup removed. The AMD SDK and existing CSV files were preserved.'
