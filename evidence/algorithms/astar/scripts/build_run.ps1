# Safe Windows entry point. Runs the same contract runner, never the historical log path.
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
# The unchanged astar-contract suite also requires g++.
# Optional -Compiler cl requires an initialized VS Developer PowerShell as well as g++.
# This script does not assume a particular VS installation or silently install tools.
if (-not (Get-Command $Python -ErrorAction SilentlyContinue)) {
    throw "Python unavailable: $Python"
}
if (-not (Get-Command $Compiler -ErrorAction SilentlyContinue)) {
    throw "Compiler unavailable: $Compiler; initialize the supported developer environment first"
}
$runner = Join-Path $PSScriptRoot 'run_benchmark.py'
& $Python -B $runner --output-dir $OutputDirectory --compiler $Compiler
if ($LASTEXITCODE -ne 0) {
    throw "A* contract/measurement run failed with exit code $LASTEXITCODE"
}
