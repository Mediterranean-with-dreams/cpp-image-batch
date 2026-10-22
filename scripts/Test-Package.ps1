param([string]$Archive = '')
. "$PSScriptRoot\Common.ps1"
if (-not $Archive) { $Archive = Join-Path $ProjectRoot "dist\$PackageName.zip" }
$run = Get-Date -Format 'yyyyMMdd_HHmmss_fff'
$destination = Join-Path $ProjectRoot "tmp\package-check-$run"
Expand-Archive -LiteralPath $Archive -DestinationPath $destination
$package = Join-Path $destination $PackageName
$report = Join-Path $destination 'package-check.json'
$saved = @{}
foreach ($key in @('PATH', 'QT_PLUGIN_PATH', 'QT_QPA_PLATFORM_PLUGIN_PATH', 'QTDIR', 'QML_IMPORT_PATH', 'QML2_IMPORT_PATH', 'QT_QPA_PLATFORM')) {
    $saved[$key] = [Environment]::GetEnvironmentVariable($key, 'Process')
}
try {
    $env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
    foreach ($key in @('QT_PLUGIN_PATH', 'QT_QPA_PLATFORM_PLUGIN_PATH', 'QTDIR', 'QML_IMPORT_PATH', 'QML2_IMPORT_PATH')) {
        [Environment]::SetEnvironmentVariable($key, $null, 'Process')
    }
    $env:QT_QPA_PLATFORM = 'windows'
    $process = Start-Process -FilePath "$package\ImageBatch.exe" -ArgumentList @('--self-test', "`"$report`"") -WorkingDirectory $package -WindowStyle Hidden -PassThru
    if (-not $process.WaitForExit(30000)) { $process.Kill(); throw 'Packaged executable timed out.' }
    if (-not (Test-Path -LiteralPath $report)) { throw "Packaged executable did not write its report. Exit: $($process.ExitCode)" }
    $result = Get-Content -LiteralPath $report -Raw | ConvertFrom-Json
    Get-Content -LiteralPath $report
    if ($process.ExitCode -ne 0 -or -not $result.passed) { throw 'Packaged executable check failed.' }
    Write-Host "Independent package check passed: $report"
} finally {
    foreach ($key in $saved.Keys) { [Environment]::SetEnvironmentVariable($key, $saved[$key], 'Process') }
}
