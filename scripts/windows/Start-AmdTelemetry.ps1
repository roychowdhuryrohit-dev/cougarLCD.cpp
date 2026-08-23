[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$dataDirectory = Join-Path $env:ProgramData 'CougarLCD'
New-Item -ItemType Directory -Path $dataDirectory -Force | Out-Null

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
if ($existing) { exit 0 }

$startInfo = [Diagnostics.ProcessStartInfo]::new()
$startInfo.FileName = $sample
$startInfo.Arguments = '-L 5256000 1 cougar'
$startInfo.WorkingDirectory = $dataDirectory
$startInfo.UseShellExecute = $false
$startInfo.CreateNoWindow = $true
$process = [Diagnostics.Process]::Start($startInfo)
if (-not $process) { throw 'Failed to start AMD telemetry.' }

