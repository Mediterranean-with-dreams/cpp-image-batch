param([string]$PythonPath = '')
. "$PSScriptRoot\Common.ps1"
$tools = Join-Path $ProjectRoot '.tools'
New-Item -ItemType Directory -Path $tools -Force | Out-Null
if (-not $PythonPath) {
    $pythonCommand = Get-Command python -ErrorAction SilentlyContinue
    if ($pythonCommand -and $pythonCommand.Source -notmatch 'WindowsApps') { $PythonPath = $pythonCommand.Source }
}
if (-not $PythonPath) {
    # Private bootstrap Python; never changes a system Python installation.
    $bootstrap = Join-Path $tools 'bootstrap-python'
    $PythonPath = Join-Path $bootstrap 'python.exe'
    if (-not (Test-Path -LiteralPath $PythonPath)) {
        $archive = Join-Path $tools 'python-embedded.zip'
        Invoke-WebRequest "https://www.python.org/ftp/python/$($Manifest.bootstrapPython)/python-$($Manifest.bootstrapPython)-embed-amd64.zip" -OutFile $archive
        Expand-Archive -LiteralPath $archive -DestinationPath $bootstrap
        $pth = Join-Path $bootstrap 'python312._pth'
        (Get-Content -LiteralPath $pth -Raw).Replace('#import site', 'import site') | Set-Content -LiteralPath $pth -Encoding ascii
    }
    & $PythonPath -m pip --version *> $null
    if ($LASTEXITCODE -ne 0) {
        $getPip = Join-Path $tools 'get-pip.py'
        Invoke-WebRequest 'https://bootstrap.pypa.io/get-pip.py' -OutFile $getPip
        & $PythonPath $getPip --no-warn-script-location
        if ($LASTEXITCODE -ne 0) { throw 'Failed to install the private bootstrap pip.' }
    }
}
$previousPythonPath = $env:PYTHONPATH
try {
    $packageRoot = Join-Path $tools 'python'
    # One pip transaction merges all executable scripts into the same private bin directory.
    & $PythonPath -m pip install --upgrade --target $packageRoot "aqtinstall==$($Manifest.aqtinstall)" "cmake==$($Manifest.cmake)" "ninja==$($Manifest.ninja)"
    if ($LASTEXITCODE -ne 0) { throw 'Dependency installation failed.' }
    $env:PYTHONPATH = $packageRoot
    $qt = Join-Path $tools 'Qt'
    if (-not (Test-Path -LiteralPath (Join-Path $qt "$($Manifest.qtVersion)\mingw_64\bin\Qt6Core.dll"))) {
        & $PythonPath -m aqt install-qt windows desktop $Manifest.qtVersion $Manifest.qtArchitecture -O $qt --archives qtbase qttools qttranslations
        if ($LASTEXITCODE -ne 0) { throw 'Qt download failed.' }
    }
    if (-not (Test-Path -LiteralPath (Join-Path $qt 'Tools\mingw1310_64\bin\g++.exe'))) {
        & $PythonPath -m aqt install-tool windows desktop tools_mingw1310 $Manifest.mingwPackage -O $qt
        if ($LASTEXITCODE -ne 0) { throw 'MinGW download failed.' }
    }
    $context = Get-DevContext
    Assert-DevContext $context
    Write-Host "Environment ready. All C++ dependencies: $tools"
} finally { $env:PYTHONPATH = $previousPythonPath }
