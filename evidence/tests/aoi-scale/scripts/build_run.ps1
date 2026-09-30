<#
.SYNOPSIS
    匹配到对局证据包：编译并运行全部程序，原始输出写入 results/（Windows 版本）。

.EXAMPLE
    & (Join-Path $RepoRoot 'evidence/tests/aoi-scale/scripts/build_run.ps1')
#>
[CmdletBinding()]
param(
    [string]$Cxx = 'C:\msys64\mingw64\bin\g++.exe'
)

$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent (Split-Path -Parent $PSCommandPath)
$build = Join-Path $here 'build'
$results = Join-Path $here 'results'
New-Item -ItemType Directory -Force -Path $build, $results | Out-Null

if (-not (Test-Path $Cxx)) {
    throw "compiler not found: $Cxx  (pass -Cxx <path to g++>)"
}

$tools = @('aoi_scale', 'dynamic_shard')
$failed = 0

foreach ($t in $tools) {
    Write-Host "=== build $t ==="
    & $Cxx -std=c++17 -O2 -Wall -o (Join-Path $build "$t.exe") (Join-Path $here "src/$t.cpp")
    if ($LASTEXITCODE -ne 0) { Write-Host "BUILD FAILED: $t"; $failed = 1; continue }

    Write-Host "=== run $t ==="
    $out = & (Join-Path $build "$t.exe") 2>&1
    $out | Out-File -FilePath (Join-Path $results "$t.txt") -Encoding utf8
    $out | Select-Object -Last 3 | ForEach-Object { Write-Host $_ }
    if ($LASTEXITCODE -ne 0) { Write-Host "RUN FAILED: $t (exit=$LASTEXITCODE)"; $failed = 1 }
}

Write-Host ''
Write-Host '=== summary ==='
Get-ChildItem $results -Filter *.txt | ForEach-Object {
    Select-String -Path $_.FullName -Pattern '^RESULT' | ForEach-Object { Write-Host $_.Line }
}
exit $failed
