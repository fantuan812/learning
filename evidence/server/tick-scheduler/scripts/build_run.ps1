# build_run.ps1 — MSVC 编译并运行 tick_scheduler 模拟
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$vcvars = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
$buildDir = Join-Path $root 'build'
$resultsDir = Join-Path $root 'results'
New-Item -ItemType Directory -Force -Path $buildDir, $resultsDir | Out-Null
$src = Join-Path $root 'src\tick_scheduler.cpp'
$exe = Join-Path $buildDir 'tick_scheduler.exe'
$clLog = Join-Path $buildDir 'cl.log'
$outFile = Join-Path $resultsDir 'tick_scheduler_win_x64_msvc.txt'

$cmd = "call `"$vcvars`" >nul 2>&1 && cl /nologo /utf-8 /O2 /std:c++17 /EHsc /W4 `"$src`" /Fe:`"$exe`" > `"$clLog`" 2>&1 && `"$exe`" > `"$outFile`" 2>&1"
& cmd /d /s /c $cmd
if ($LASTEXITCODE -ne 0) {
    Get-Content -LiteralPath $clLog -ErrorAction SilentlyContinue
    throw "编译或运行失败，退出码 $LASTEXITCODE"
}
Write-Host "=== 原始结果（$outFile）==="
Get-Content -LiteralPath $outFile
