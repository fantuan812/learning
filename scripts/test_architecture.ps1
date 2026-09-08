[CmdletBinding()]
param([string]$Checker)
$ErrorActionPreference = 'Stop'
if (-not $Checker) { $Checker = Join-Path (Split-Path -Parent $MyInvocation.MyCommand.Path) 'check_architecture.ps1' }
$utf8 = [Text.UTF8Encoding]::new($false)
$fixture = Join-Path ([IO.Path]::GetTempPath()) ('kb-architecture-test-' + [guid]::NewGuid().ToString('N'))
$engine = (Get-Process -Id $PID).Path
$checkerPath = [IO.Path]::GetFullPath($Checker)
$passed = 0
function Save([string]$Relative, [string]$Text) { [IO.File]::WriteAllText((Join-Path $fixture $Relative), $Text, $utf8) }
function Config {
    return @{ version = 1; layers = @(@{ id = 'control'; roots = @('.kb'); entrypoints = @('README.md') }, @{ id = 'knowledge'; roots = @('Knowledge'); entrypoints = @('Knowledge/topic.md') }); excluded_roots = @('.git'); contracts = @{ aliases = '.kb/aliases.yaml' } }
}
function Save-Config($Value) { Save '.kb/architecture.json' ($Value | ConvertTo-Json -Depth 8) }
function Manifest {
    $lines = @('version: 4','okf:','  profile: ".kb/okf-profile.yaml"','  compatibility: migrated','  strict_conformance: true','  strict_scope: repository_profile','  official_parser_conformance: not_claimed','documents:')
    foreach ($path in @('Knowledge/topic.md','README.md')) {
        $full = Join-Path $fixture $path
        $kind = if ($path -eq 'README.md') { 'README' } else { 'doc' }
        $lines += @("  `"$path`":", "    kind: $kind", '    maturity: L2', "    bytes: $([IO.File]::ReadAllBytes($full).Length)", "    lines: $([IO.File]::ReadAllLines($full, $utf8).Count)")
    }
    Save '.kb/manifest.yaml' (($lines -join "`n") + "`n")
}
function Expect([string]$Name, [int]$Code, [switch]$SkipManifest) {
    $arguments = @('-NoProfile','-ExecutionPolicy','Bypass','-File',$checkerPath,'-Root',$fixture)
    if ($SkipManifest) { $arguments += '-SkipManifest' }
    $output = @(& $engine @arguments 2>&1)
    if ($LASTEXITCODE -ne $Code) { throw "$Name expected exit $Code, got $LASTEXITCODE`n$($output -join "`n")" }
    $script:passed++; Write-Host "PASS fixture: $Name"
}
try {
    $null = New-Item -ItemType Directory -Path $fixture
    $null = New-Item -ItemType Directory -Path (Join-Path $fixture '.kb'), (Join-Path $fixture 'Knowledge')
    # This initializes only this uniquely owned temporary fixture, never the source repository.
    $null = & git -c "safe.directory=$fixture" -C $fixture init --quiet
    if ($LASTEXITCODE -ne 0) { throw 'Could not initialize temporary fixture.' }
    $healthyAliases = "aliases:`n  Alpha:`n    canonical: Beta`n  Beta:`n    canonical: Beta`n"
    Save '.kb/aliases.yaml' $healthyAliases
    Save 'README.md' "---`nmaturity: L2`n---`n# Home`n"
    Save 'Knowledge/topic.md' "---`nmaturity: L2`n---`n# Topic`n"
    Save-Config (Config); Manifest
    Expect 'healthy with self-terminal alias' 0
    $config = Config; $config.layers = @($config.layers[0]); Save-Config $config
    Expect 'uncovered directory' 1
    $config = Config; $config.layers[1].roots = @('Knowledge','.kb'); Save-Config $config
    Expect 'overlapping roots' 1
    $config = Config; $config.layers[1].roots = @('../Knowledge'); Save-Config $config
    Expect 'escaping root' 1
    $config = Config; $config.contracts.aliases = '../outside.yaml'; Save-Config $config
    Expect 'escaping contract' 1
    $config = Config; $config.layers[1].entrypoints = @('../outside.md'); Save-Config $config
    Expect 'escaping entrypoint' 1
    $config = Config; $config.layers[1].id = 'control'; Save-Config $config
    Expect 'duplicate layer id' 1
    Save-Config (Config)
    Save '.kb/aliases.yaml' "aliases:`n  Alpha:`n    canonical: Beta`n  Beta:`n    canonical: Alpha`n"
    Expect 'alias cycle' 1
    Save '.kb/aliases.yaml' "aliases:`n  Alpha:`n    canonical: Beta`n    unexpected: true`n"
    Expect 'unsupported alias structure' 1
    Save '.kb/aliases.yaml' $healthyAliases
    Save 'Knowledge/topic.md' "---`nmaturity: L2`n---`n# Changed topic`n"
    Expect 'stale manifest bytes' 1
    Expect 'skip stale manifest before rebuild' 0 -SkipManifest
    Manifest
    Save 'Knowledge/topic.md' "---`nmaturity: L3`n---`n# Changed topic`n"
    Expect 'stale manifest maturity' 1
    Save 'Knowledge/topic.md' "---`nmaturity: L2`n---`n# Changed topic`n"
    Manifest
    Save 'Knowledge/topic.md' "---`nmaturity: L2`n---`n# Changed`ntopic`n"
    Expect 'stale manifest lines with unchanged byte count' 1
    Manifest
    Save 'Knowledge/extra.md' '# Extra'
    Expect 'missing manifest path' 1
    # Remove only an exact fixture file created above.
    Remove-Item -LiteralPath (Join-Path $fixture 'Knowledge/extra.md')
    Manifest
    Save '.kb/manifest.yaml' (([IO.File]::ReadAllText((Join-Path $fixture '.kb/manifest.yaml'))) + "unknown: true`n")
    Expect 'unsupported manifest structure' 1
    Manifest
    Expect 'healthy restored' 0
    Write-Host "PASS architecture fixtures: $passed cases ($engine)"
} finally {
    $fullFixture = [IO.Path]::GetFullPath($fixture)
    $tempPrefix = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\','/') + [IO.Path]::DirectorySeparatorChar
    if ($fullFixture.StartsWith($tempPrefix, [StringComparison]::OrdinalIgnoreCase) -and [IO.Path]::GetFileName($fullFixture) -match '^kb-architecture-test-[a-f0-9]{32}$' -and (Test-Path -LiteralPath $fullFixture)) {
        Remove-Item -LiteralPath $fullFixture -Recurse -Force
    }
}
