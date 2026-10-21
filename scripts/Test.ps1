. "$PSScriptRoot\Common.ps1"
$context = Get-DevContext
Assert-DevContext $context
$previousPath = $env:PATH
$previousArtifacts = $env:IMAGEBATCH_ARTIFACT_DIR
$previousPlatform = $env:QT_QPA_PLATFORM
try {
    $env:PATH = "$($context.CompilerBin);$($context.Qt)\bin;$previousPath"
    $env:QT_QPA_PLATFORM = 'windows'
    $run = Get-Date -Format 'yyyyMMdd_HHmmss_fff'
    $artifacts = Join-Path $context.Build "evidence\$run"
    $env:IMAGEBATCH_ARTIFACT_DIR = $artifacts
    New-Item -ItemType Directory -Path $artifacts -Force | Out-Null
    & "$($context.Build)\imagebatch_tests.exe" -o "$artifacts/tests.xml,junitxml" -o "$artifacts/tests.txt,txt"
    $testExit = $LASTEXITCODE
    Get-Content -LiteralPath "$artifacts\tests.txt" -Encoding UTF8
    if ($testExit -ne 0) { throw "Qt Test failed. Evidence: $artifacts" }
    Remove-Item Env:IMAGEBATCH_ARTIFACT_DIR -ErrorAction SilentlyContinue
    & $context.CTest --test-dir $context.Build --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw 'CTest failed.' }
    Write-Host "Passed. Evidence: $artifacts"
} finally {
    $env:PATH = $previousPath
    $env:IMAGEBATCH_ARTIFACT_DIR = $previousArtifacts
    $env:QT_QPA_PLATFORM = $previousPlatform
}
