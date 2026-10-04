# Legacy five-model entry; Python owns output safety and all target failure handling.
# This explicit entry may run historical benchmarks in the other models. For the
# focused Inventory contracts, invoke run_inventory_contract.py without legacy mode.
[CmdletBinding()]
param(
    [string]$Root = (Join-Path $PSScriptRoot '../../../..'),
    [string]$Gxx = 'C:\msys64\mingw64\bin\g++.exe',
    [string]$OutputDir
)

$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    [Console]::Error.WriteLine('usage: build_run.ps1 -OutputDir NEW_EXTERNAL_DIRECTORY [-Root REPO] [-Gxx COMPILER]')
    exit 2
}
$python = if ([string]::IsNullOrWhiteSpace($env:PYTHON)) { 'python' } else { $env:PYTHON }
$driver = Join-Path $PSScriptRoot 'run_inventory_contract.py'
try {
    & $python -B $driver --legacy-five-targets --output-dir $OutputDir --root $Root --cxx $Gxx
    exit $LASTEXITCODE
} catch {
    [Console]::Error.WriteLine($_.Exception.Message)
    exit 1
}
