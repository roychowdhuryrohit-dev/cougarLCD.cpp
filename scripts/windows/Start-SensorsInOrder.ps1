[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$dataDirectory = Join-Path $env:ProgramData 'CougarLCD'
$logPath = Join-Path $dataDirectory 'sensor-startup-order.log'
$signalServiceName = 'SignalRgb.Service'

function Write-StartupLog {
    param([string] $Message)
    Add-Content -LiteralPath $logPath -Value `
        "$(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')  $Message"
}

function Start-AmdLoggerHidden {
    $sdk = Get-ItemProperty -LiteralPath 'HKLM:\Software\AMD\RyzenMasterMonitoringSDK'
    $sample = Join-Path $sdk.InstallationPath `
        'RyzenMasterMonitoringSampleApp\bin-prebuilt\AMDRyzenMasterMonitoringSampleApp.exe'
    if (-not (Test-Path -LiteralPath $sample)) {
        throw "AMD monitoring sample not found: $sample"
    }
    $signature = Get-AuthenticodeSignature -LiteralPath $sample
    if ($signature.Status -ne 'Valid' -or
        $signature.SignerCertificate.Subject -notmatch 'Advanced Micro Devices') {
        throw "AMD executable signature validation failed ($($signature.Status))."
    }

    $existing = Get-Process -Name AMDRyzenMasterMonitoringSampleApp `
        -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($existing) {
        Write-StartupLog "AMD logger already runs as PID $($existing.Id)."
        return
    }

    $startInfo = [Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $sample
    $startInfo.Arguments = '-L 5256000 1 cougar'
    $startInfo.WorkingDirectory = $dataDirectory
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $process = [Diagnostics.Process]::Start($startInfo)
    if (-not $process) { throw 'Failed to start AMD telemetry.' }
    Write-StartupLog "Started AMD logger without a console window as PID $($process.Id)."
}

function Wait-ForFreshAmdSample {
    param([datetime] $NotBefore, [int] $TimeoutSeconds = 35)
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    do {
        Start-Sleep -Seconds 1
        $csv = Get-ChildItem -LiteralPath $dataDirectory -File `
            -Filter 'RMSDK_Parameter_log_cougar_*.csv' -ErrorAction SilentlyContinue |
            Where-Object {
                $_.Length -gt 0 -and $_.LastWriteTime -ge $NotBefore.AddSeconds(-5)
            } | Sort-Object LastWriteTime -Descending | Select-Object -First 1
        if ($csv -and (Select-String -LiteralPath $csv.FullName `
            -Pattern '^\d{4}-\d{2}-\d{2},' -Quiet -ErrorAction SilentlyContinue)) {
            return $csv
        }
    } while ((Get-Date) -lt $deadline)
    throw 'AMD did not produce a valid fresh temperature sample.'
}

function Start-SignalRgbUi {
    $runCommand = (Get-ItemProperty `
        -LiteralPath 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run' `
        -Name SignalRgb -ErrorAction SilentlyContinue).SignalRgb
    if ($runCommand -match '^"([^"]+)"\s*(.*)$') {
        Start-Process -FilePath $matches[1] -ArgumentList $matches[2]
    } elseif ($runCommand) {
        Start-Process -FilePath $runCommand
    }
}

try {
    New-Item -ItemType Directory -Path $dataDirectory -Force | Out-Null
    Write-StartupLog 'Beginning ordered AMD telemetry and SignalRGB startup.'

    # SignalRGB must not acquire HWiNFO's driver before AMD initializes.
    Get-Process -Name SignalRgb,SignalRgbLauncher -ErrorAction SilentlyContinue |
        Stop-Process -Force -ErrorAction SilentlyContinue
    $signalService = Get-Service -Name $signalServiceName
    if ($signalService.Status -ne 'Stopped') {
        Stop-Service -Name $signalServiceName -Force
        $signalService.WaitForStatus('Stopped', (New-TimeSpan -Seconds 15))
    }

    # A logger started by the standalone task may already be in a failed state.
    # Stop it so this task owns the complete initialization sequence.
    Get-Process -Name AMDRyzenMasterMonitoringSampleApp `
        -ErrorAction SilentlyContinue |
        Stop-Process -Force -ErrorAction SilentlyContinue

    # Stop only stale runtime instances. Nothing is deleted or disabled here.
    $drivers = @(Get-CimInstance Win32_SystemDriver | Where-Object {
        $_.Name -like 'HWiNFO_*' -and $_.State -ne 'Stopped'
    })
    foreach ($driver in $drivers) {
        if ($driver.State -eq 'Running') {
            & "$env:SystemRoot\System32\sc.exe" stop $driver.Name | Out-Null
        }
    }
    $driverDeadline = (Get-Date).AddSeconds(25)
    do {
        $remaining = @(Get-CimInstance Win32_SystemDriver | Where-Object {
            $_.Name -like 'HWiNFO_*' -and $_.State -ne 'Stopped'
        })
        if ($remaining.Count -eq 0) { break }
        Start-Sleep -Milliseconds 500
    } while ((Get-Date) -lt $driverDeadline)
    if ($remaining.Count -gt 0) {
        throw "HWiNFO driver did not stop: $($remaining.Name -join ', ')."
    }

    $started = Get-Date
    Start-AmdLoggerHidden
    $csv = Wait-ForFreshAmdSample -NotBefore $started

    # Monitoring is disabled by the installer; lighting remains available.
    Start-Service -Name $signalServiceName
    Start-SignalRgbUi

    $lengthBefore = $csv.Length
    Start-Sleep -Seconds 12
    $csv.Refresh()
    if ($csv.Length -le $lengthBefore -or
        ((Get-Date) - $csv.LastWriteTime).TotalSeconds -gt 10) {
        throw 'AMD samples stopped after SignalRGB started.'
    }
    Write-StartupLog "Success: AMD CSV is advancing and SignalRGB lighting is running."
} catch {
    Write-StartupLog "Failure: $($_.Exception.Message)"
    Start-Service -Name $signalServiceName -ErrorAction SilentlyContinue
    exit 1
}
