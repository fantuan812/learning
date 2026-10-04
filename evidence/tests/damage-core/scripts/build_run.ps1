# Thin wrapper; Linux pwsh verification is not Windows verification.
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Out,
    [string]$Gxx = 'g++',
    [string]$Python = 'python3',
    [ValidateSet('strict', 'ubsan', 'all')][string]$Mode = 'strict',
    [string]$Source = '',
    [string]$Test = '',
    [double]$Timeout = 60
)
$ErrorActionPreference = 'Stop'
try {
    $runnerArgs = @('-B', (Join-Path $PSScriptRoot 'run_damage_contract.py'), '--out', $Out,
                    '--cxx', $Gxx, '--mode', $Mode, '--timeout', $Timeout.ToString([Globalization.CultureInfo]::InvariantCulture))
    if ($Source) { $runnerArgs += @('--source', $Source) }
    if ($Test) { $runnerArgs += @('--test', $Test) }
    & $Python @runnerArgs
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    exit 0
} catch {
    Write-Error $_
    exit 1
}
