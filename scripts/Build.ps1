param([int]$Jobs = 4)
. "$PSScriptRoot\Common.ps1"
$context = Get-DevContext
Assert-DevContext $context
$previousPath = $env:PATH
$previousGccPrefix = $env:GCC_EXEC_PREFIX
try {
    $env:PATH = "$($context.CompilerBin);$($context.Qt)\bin;$previousPath"
    $env:GCC_EXEC_PREFIX = $context.GccPrefix
    & $context.CMake -S $context.Root -B $context.Build -G Ninja "-DCMAKE_PREFIX_PATH=$($context.Qt)" "-DCMAKE_MAKE_PROGRAM=$($context.Ninja)" "-DCMAKE_CXX_COMPILER=$($context.CompilerBin)\g++.exe" -DCMAKE_BUILD_TYPE=Release
    if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
    & $context.CMake --build $context.Build --parallel $Jobs
    if ($LASTEXITCODE -ne 0) { throw 'C++ build failed.' }
    Write-Host "Built: $($context.Build)\ImageBatch.exe"
} finally { $env:PATH = $previousPath; $env:GCC_EXEC_PREFIX = $previousGccPrefix }
