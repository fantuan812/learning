# Damage-core evidence builder (Windows).
# Compiles the programme and writes raw output into results/.
# Toolchain: MSYS2 MinGW-w64 g++ (default C:\msys64\mingw64\bin\g++.exe).
[CmdletBinding()]
param(
    [string]$Root = (Split-Path -Parent $PSScriptRoot),
    [string]$Gxx = 'C:\msys64\mingw64\bin\g++.exe'
)

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)

$repo = (Resolve-Path -LiteralPath (Join-Path $Root '.')).Path
$here = Join-Path $repo 'evidence\tests\damage-core'
$resDir = Join-Path $here 'results'
New-Item -ItemType Directory -Force -Path (Join-Path $here 'build'), $resDir | Out-Null

# native tools need the compiler directory on PATH for cc1plus / libstdc++.
$env:PATH = (Split-Path -Parent $Gxx) + ';' + $env:PATH

$stamp = (Get-Date).ToString('yyyy-MM-ddTHH:mm:sszzz')
$target = (& $Gxx -dumpmachine 2>&1 | Select-Object -First 1)
$compiler = (& $Gxx --version 2>&1 | Select-Object -First 1)

$status = 0
foreach ($t in @('damage_pipeline')) {
    Push-Location $here
    try {
        & $Gxx -std=c++17 -O2 -o "build/$t.exe" "src/$t.cpp"
        if ($LASTEXITCODE -ne 0) { Write-Warning "compile failed: $t"; $status = 1; continue }
        $lines = New-Object System.Collections.Generic.List[string]
        $lines.Add("# evidence   : tests/damage-core/$t")
        $lines.Add("# generated  : $stamp")
        $lines.Add("# host       : $([System.Environment]::OSVersion.VersionString) (cpu_count=$([System.Environment]::ProcessorCount))")
        $lines.Add("# target     : $target")
        $lines.Add("# compiler   : $compiler")
        $lines.Add("# command    : g++ -std=c++17 -O2 -o build/$t.exe src/$t.cpp ; ./build/$t.exe")
        $lines.Add("#")
        $lines.AddRange([string[]](& "./build/$t.exe" 2>&1))
        $lines.Add("# exit_code  : $LASTEXITCODE")
        [System.IO.File]::WriteAllLines((Join-Path $resDir "$t.txt"), $lines, [System.Text.UTF8Encoding]::new($false))
        Write-Host "wrote results/$t.txt"
    } finally {
        Pop-Location
    }
}

exit $status
