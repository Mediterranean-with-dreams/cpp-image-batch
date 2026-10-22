param([int]$Images = 36, [int]$Repeats = 3, [string]$OutputDirectory = '')
. "$PSScriptRoot\Common.ps1"
if ($Images -lt 1 -or $Images -gt 300 -or $Repeats -lt 1 -or $Repeats -gt 10) { throw 'Images: 1..300; Repeats: 1..10.' }
$context = Get-DevContext
Assert-DevContext $context
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $context.Build ('benchmarks\' + (Get-Date -Format 'yyyyMMdd_HHmmss_fff')) }
if (Test-Path -LiteralPath $OutputDirectory) { throw 'Benchmark output directory already exists; refusing to overwrite.' }
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$OutputDirectory = (Resolve-Path -LiteralPath $OutputDirectory).Path
$exe = Join-Path $context.Build 'ImageBatchBenchmark.exe'
$oldPath = $env:PATH
$oldPlatform = $env:QT_QPA_PLATFORM
try {
    $env:PATH = "$($context.CompilerBin);$($context.Qt)\bin;$oldPath"
    $env:QT_QPA_PLATFORM = 'windows'
    $inputDirectory = Join-Path $OutputDirectory 'corpus'
    & $exe --generate $inputDirectory --count $Images
    if ($LASTEXITCODE -ne 0) { throw 'Benchmark corpus generation failed.' }
    $warmup = Join-Path $OutputDirectory 'warmup.json'
    & $exe --input $inputDirectory --output (Join-Path $OutputDirectory 'warmup-output') --threads 1 --result $warmup
    if ($LASTEXITCODE -ne 0) { throw 'Benchmark warmup failed.' }
    $reference = Get-Content -LiteralPath $warmup -Raw | ConvertFrom-Json
    $inputHashes = $reference.input_hashes | ConvertTo-Json -Compress -Depth 10
    $outputHashes = $reference.output_hashes | ConvertTo-Json -Compress -Depth 10
    $rows = @()
    $measurements = @()
    for ($repeat = 0; $repeat -lt $Repeats; $repeat++) {
        # Rotate order to reduce systematic order/thermal bias. Warmup is excluded.
        $orders = @(@(1,2,4), @(2,4,1), @(4,1,2))
        foreach ($workers in $orders[$repeat % 3]) {
            $name = "r$($repeat + 1)-t$workers"
            $json = Join-Path $OutputDirectory "$name.json"
            & $exe --input $inputDirectory --output (Join-Path $OutputDirectory "$name-output") --threads $workers --result $json
            if ($LASTEXITCODE -ne 0) { throw "Benchmark failed: $name" }
            $result = Get-Content -LiteralPath $json -Raw | ConvertFrom-Json
            if (-not $result.outputs_valid -or -not $result.inputs_unchanged -or
                ($result.input_hashes | ConvertTo-Json -Compress -Depth 10) -ne $inputHashes -or
                ($result.output_hashes | ConvertTo-Json -Compress -Depth 10) -ne $outputHashes) {
                throw "Input/output equivalence failed: $name"
            }
            $measurements += $result
            $rows += [PSCustomObject]@{
                repeat = $repeat + 1; workers = $workers; images = $Images; wall_ms = $result.wall_ms
                images_per_second = [math]::Round($result.images_per_second, 3)
                sampled_peak_working_set_mib = [math]::Round($result.sampled_peak_working_set_bytes / 1MB, 2)
                estimated_peak_reserved_mib = [math]::Round($result.estimated_peak_reserved_bytes / 1MB, 2)
                peak_queued = $result.peak_queued; correct = $true
            }
            Write-Host "$name : $($result.wall_ms) ms, outputs verified"
        }
    }
    $rows | Export-Csv -LiteralPath (Join-Path $OutputDirectory 'results.csv') -NoTypeInformation -Encoding UTF8
    $medians = @()
    foreach ($workers in @(1,2,4)) {
        $values = @($rows | Where-Object workers -eq $workers | Sort-Object wall_ms)
        $middle = [int][math]::Floor($values.Count / 2)
        $median = if ($values.Count % 2) { $values[$middle].wall_ms } else { ($values[$middle - 1].wall_ms + $values[$middle].wall_ms) / 2.0 }
        $medians += [PSCustomObject]@{ workers = $workers; median_ms = $median }
    }
    $lines = @('# ImageBatch benchmark', '',
        "Version: $($reference.version); Qt: $($reference.qt); compiler: $($reference.compiler).",
        "OS: $($reference.os); architecture: $($reference.architecture); hardware threads: $($reference.hardware_threads).",
        "Dataset: $Images deterministic mixed JPEG/PNG images, $Repeats measured repetitions per worker count.",
        'Each measurement uses a fresh process. One warmup is excluded; filesystem caches are not cleared.',
        'Orders rotate 1/2/4, 2/4/1, 4/1/2. Output: JPG, max edge 1920, quality 90, identical watermark.',
        'Qt helper pool is limited to one thread. Estimated buffer budget is 512 MiB.', '',
        '| Workers | Median wall time (ms) | Speedup vs 1 |', '| --- | ---: | ---: |')
    foreach ($median in $medians) {
        $speedup = [math]::Round($medians[0].median_ms / [math]::Max(1, $median.median_ms), 3)
        $lines += "| $($median.workers) | $($median.median_ms) | $speedup |"
    }
    $lines += @('', 'All measured output SHA-256 values match the warmup output; source hashes are unchanged.',
        'Working set is sampled every 5 ms and is not an exact instantaneous peak. Estimated reservations are separate from process memory.',
        'Synthetic workload results apply to this machine and protocol; more workers do not guarantee a speedup.',
        'Raw measurement JSON and results.csv are retained with this report.')
    $lines | Set-Content -LiteralPath (Join-Path $OutputDirectory 'report.md') -Encoding UTF8
    Write-Host "Benchmark complete: $OutputDirectory"
} finally { $env:PATH = $oldPath; $env:QT_QPA_PLATFORM = $oldPlatform }
