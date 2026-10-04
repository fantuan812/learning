# New deterministic policy capture; never overwrite the historical MSVC output.
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$OutputDirectory,
    [string]$Python = 'python',
    [string]$Compiler = 'g++'
)
$ErrorActionPreference = 'Stop'
if (Test-Path -LiteralPath $OutputDirectory) {
    throw "Output already exists; refusing overwrite: $OutputDirectory"
}
if (-not (Get-Command $Python -ErrorAction SilentlyContinue)) {
    throw "Python unavailable: $Python"
}
if (-not (Get-Command $Compiler -ErrorAction SilentlyContinue)) {
    throw "GCC-compatible compiler unavailable: $Compiler"
}
$runner = Join-Path $PSScriptRoot 'run_policy.py'
& $Python -B $runner --output-dir $OutputDirectory --compiler $Compiler
if ($LASTEXITCODE -ne 0) {
    throw "Tick policy capture failed with exit code $LASTEXITCODE"
}
