. "$PSScriptRoot\Common.ps1"
$package = Join-Path $ProjectRoot 'dist\ImageBatch-1.0.0-windows-x64\ImageBatch.exe'
if (Test-Path -LiteralPath $package) { Start-Process -FilePath $package; return }
$context = Get-DevContext
Assert-DevContext $context
$previousPath = $env:PATH
try {
    $env:PATH = "$($context.CompilerBin);$($context.Qt)\bin;$previousPath"
    # Visible interactive application requested by the user.
    Start-Process -FilePath "$($context.Build)\ImageBatch.exe" -WorkingDirectory $ProjectRoot
} finally { $env:PATH = $previousPath }
