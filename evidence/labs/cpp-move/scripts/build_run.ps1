# build_run.ps1 — 使用 MSVC Build Tools 2022 编译并运行 move_counter 实验
# 用法：powershell -ExecutionPolicy Bypass -File .\scripts\build_run.ps1
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$vcvars = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path -LiteralPath $vcvars)) {
    throw "未找到 vcvars64.bat: $vcvars"
}
$buildDir = Join-Path $root 'build'
$resultsDir = Join-Path $root 'results'
New-Item -ItemType Directory -Force -Path $buildDir, $resultsDir | Out-Null
$src = Join-Path $root 'src\move_counter.cpp'
$exe = Join-Path $buildDir 'move_counter.exe'
$clLog = Join-Path $buildDir 'cl.log'
$outFile = Join-Path $resultsDir 'move_counter_win_x64_msvc.txt'

$cmd = "call `"$vcvars`" >nul 2>&1 && cl /nologo /utf-8 /O2 /std:c++17 /EHsc /W4 `"$src`" /Fe:`"$exe`" > `"$clLog`" 2>&1 && `"$exe`" > `"$outFile`" 2>&1"
& cmd /d /s /c $cmd
if ($LASTEXITCODE -ne 0) {
    Get-Content -LiteralPath $clLog -ErrorAction SilentlyContinue
    throw "编译或运行失败，退出码 $LASTEXITCODE"
}
Write-Host "=== 原始结果（$outFile）==="
Get-Content -LiteralPath $outFile
