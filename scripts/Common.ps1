$ErrorActionPreference = 'Stop'
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$Manifest = Get-Content -LiteralPath (Join-Path $ProjectRoot 'environment.json') -Raw | ConvertFrom-Json

function Get-DevContext {
    $devRoot = $ProjectRoot
    if ($devRoot -match '[^\x00-\x7F]') {
        # Some MinGW tools use narrow filesystem APIs. The junction stores no copy.
        $hashBytes = [System.Security.Cryptography.SHA256]::Create().ComputeHash([Text.Encoding]::UTF8.GetBytes($ProjectRoot))
        $hashText = ([BitConverter]::ToString($hashBytes)).Replace('-', '').Substring(0, 10).ToLowerInvariant()
        $devRoot = Join-Path $env:TEMP "imagebatch-dev-$hashText"
        if ($devRoot -match '[^\x00-\x7F]') { throw 'Please use an ASCII TEMP directory for this MinGW toolchain.' }
        if (Test-Path -LiteralPath $devRoot) {
            $link = Get-Item -LiteralPath $devRoot -Force
            if ($link.LinkType -ne 'Junction' -or $link.Target -ne $ProjectRoot) { throw "Unexpected existing alias: $devRoot" }
        } else {
            New-Item -ItemType Junction -Path $devRoot -Target $ProjectRoot | Out-Null
        }
    }
    $qtRoot = Join-Path $devRoot ".tools\Qt\$($Manifest.qtVersion)\mingw_64"
    $mingwRoot = Join-Path $devRoot '.tools\Qt\Tools\mingw1310_64'
    [PSCustomObject]@{
        Root = $devRoot
        Qt = $qtRoot
        CompilerBin = Join-Path $mingwRoot 'bin'
        GccPrefix = ($mingwRoot -replace '\\', '/') + '/lib/gcc/'
        CMake = Join-Path $devRoot '.tools\python\cmake\data\bin\cmake.exe'
        CTest = Join-Path $devRoot '.tools\python\cmake\data\bin\ctest.exe'
        Ninja = Join-Path $devRoot '.tools\python\bin\ninja.exe'
        Build = Join-Path $devRoot 'build-windows'
    }
}
function Assert-DevContext($Context) {
    foreach ($path in @($Context.CMake, $Context.Ninja, (Join-Path $Context.CompilerBin 'g++.exe'), (Join-Path $Context.Qt 'bin\Qt6Widgets.dll'))) {
        if (-not (Test-Path -LiteralPath $path)) { throw "Missing dependency: $path. Run scripts/Setup-Environment.ps1 first." }
    }
}
