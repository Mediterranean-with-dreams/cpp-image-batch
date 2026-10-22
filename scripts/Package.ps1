. "$PSScriptRoot\Common.ps1"
$context = Get-DevContext
Assert-DevContext $context
$folder = Join-Path $ProjectRoot "dist\$PackageName"
if (Test-Path -LiteralPath $folder) { throw "Package folder already exists: $folder. Rename it before creating a new package." }
New-Item -ItemType Directory -Path $folder -Force | Out-Null
Copy-Item -LiteralPath "$($context.Build)\ImageBatch.exe" -Destination $folder
$previousPath = $env:PATH
try {
    $env:PATH = "$($context.CompilerBin);$($context.Qt)\bin;$previousPath"
    & "$($context.Qt)\bin\windeployqt.exe" --release --compiler-runtime --no-translations --no-opengl-sw "$folder\ImageBatch.exe"
    if ($LASTEXITCODE -ne 0) { throw 'Qt deployment failed.' }
} finally { $env:PATH = $previousPath }
foreach ($name in @('README.md', 'CHANGELOG.md', 'LICENSE', 'THIRD_PARTY_NOTICES.md', 'VERSION')) {
    Copy-Item -LiteralPath (Join-Path $ProjectRoot $name) -Destination $folder
}
Copy-Item -LiteralPath (Join-Path $ProjectRoot 'docs\licenses') -Destination "$folder\licenses" -Recurse
Copy-Item -LiteralPath (Join-Path $ProjectRoot 'docs') -Destination "$folder\docs" -Recurse
Copy-Item -LiteralPath (Join-Path $ProjectRoot 'samples') -Destination "$folder\samples" -Recurse
$archive = "$folder.zip"
if (Test-Path -LiteralPath $archive) { throw "Archive already exists: $archive" }
Compress-Archive -LiteralPath $folder -DestinationPath $archive
$hash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
"$hash  $(Split-Path -Leaf $archive)" | Set-Content -LiteralPath "$archive.sha256" -Encoding ascii
Write-Host "Packaged: $archive"
