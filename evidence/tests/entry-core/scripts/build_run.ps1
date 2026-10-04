<#
.SYNOPSIS
严格runner的PowerShell入口。Linux pwsh测试不能替代Windows/MinGW验证。
.EXAMPLE
./build_run.ps1 -OutputDir /tmp/entry-run-unique -Cxx g++-14 -Ubsan
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$OutputDir,
    [string]$Cxx = 'g++',
    [string]$Python = 'python3',
    [string]$SourceDir = '',
    [string[]]$Targets = @('entry_ticket', 'entry_session', 'ds_allocator'),
    [double]$Timeout = 15,
    [double]$CompileTimeout = 60,
    [switch]$Ubsan,
    [switch]$SelfTest,
    [switch]$WithSelfTest,
    [string]$Pwsh = ''
)
$ErrorActionPreference = 'Stop'
$env:PYTHONDONTWRITEBYTECODE = '1'
try {
    $script = Join-Path $PSScriptRoot 'test_runner_contract.py'
    $runnerArgs = @('-B', $script, '--output-dir', $OutputDir, '--cxx', $Cxx,
        '--timeout', "$Timeout", '--compile-timeout', "$CompileTimeout", '--targets') + $Targets
    if ($SourceDir) { $runnerArgs += @('--source-dir', $SourceDir) }
    if ($Ubsan) { $runnerArgs += '--ubsan' }
    if ($SelfTest) { $runnerArgs += '--self-test' }
    if ($WithSelfTest) { $runnerArgs += '--with-self-test' }
    if ($Pwsh) { $runnerArgs += @('--pwsh', $Pwsh) }
    & $Python @runnerArgs
    $code = $LASTEXITCODE
    if ($null -eq $code) { throw 'Python runner returned no native exit status' }
    exit $code
} catch {
    [Console]::Error.WriteLine("runner wrapper failed: " + $_.Exception.Message)
    exit 1
}
