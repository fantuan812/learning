[CmdletBinding()]
param([string]$Root, [switch]$SkipManifest)
$ErrorActionPreference = 'Stop'
if (-not $Root) { $Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path) }
$utf8 = [System.Text.UTF8Encoding]::new($false, $true)
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$OutputEncoding = [System.Text.Encoding]::UTF8
$rootPath = [IO.Path]::GetFullPath($Root).TrimEnd('\', '/')
$prefix = $rootPath + [IO.Path]::DirectorySeparatorChar
function Read-Utf8([string]$Path) { return [IO.File]::ReadAllText($Path, $utf8) }
function Require($Condition, [string]$Message) { if (-not $Condition) { throw $Message } }
function Safe-Path([string]$Path, [switch]$Directory) {
    Require (-not [string]::IsNullOrWhiteSpace($Path)) 'Empty path.'
    Require (-not [IO.Path]::IsPathRooted($Path) -and $Path -notmatch '(^|[\\/])\.\.([\\/]|$)|:') "Unsafe path: $Path"
    $full = [IO.Path]::GetFullPath((Join-Path $rootPath $Path))
    Require ($full.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) "Path outside root: $Path"
    $cursor = $full
    while ($cursor.Length -gt $rootPath.Length) {
        if (Test-Path -LiteralPath $cursor) {
            Require (((Get-Item -LiteralPath $cursor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) -eq 0) "Reparse point is unsupported: $Path"
        }
        $cursor = Split-Path -Parent $cursor
    }
    $kind = if ($Directory) { 'Container' } else { 'Leaf' }
    Require (Test-Path -LiteralPath $full -PathType $kind) "Missing $kind`: $Path"
    return $full
}
function Fields($Object, [string[]]$Expected) {
    Require ($null -ne $Object) 'Missing object.'
    $actual = @($Object.PSObject.Properties.Name)
    Require (@(Compare-Object $actual $Expected).Count -eq 0) "Unexpected fields; expected: $($Expected -join ', ')"
}
function Yaml-String([string]$Value) {
    if ($Value.StartsWith('"')) {
        Require ($Value -match '^"(?:[^"\\]|\\[\\"])*"$') "Unsupported YAML string: $Value"
        return [regex]::Replace($Value.Substring(1, $Value.Length - 2), '\\([\\"])', '$1')
    }
    Require ($Value -match '^[^:#\[\]{}&*!|>''"\s][^:#\[\]{}&*!|>''"]*$') "Unsupported YAML scalar: $Value"
    return $Value.TrimEnd()
}
function Maturity([string]$Text) {
    $match = [regex]::Match($Text, '(?m)^maturity:\s*(L[0-5])\s*$')
    if (-not $match.Success) { $match = [regex]::Match($Text, '\u77E5\u8BC6\u6210\u719F\u5EA6[\uFF1A:]\s*\*{0,2}(L[0-5])') }
    if ($match.Success) { return $match.Groups[1].Value }; return 'L0'
}
$failures = 0
try {
    $config = (Read-Utf8 (Safe-Path '.kb/architecture.json')) | ConvertFrom-Json
    Fields $config @('version','layers','excluded_roots','contracts')
    Require (($config.version -is [int] -or $config.version -is [long]) -and $config.version -eq 1) 'Unsupported architecture version.'
    Require ($config.layers -is [array] -and $config.layers.Count -gt 0) 'layers must be a nonempty array.'
    Require ($config.excluded_roots -is [array]) 'excluded_roots must be an array.'
    Require ($config.contracts -is [pscustomobject]) 'contracts must be an object.'
    $ids = @{}; $roots = @{}
    foreach ($layer in $config.layers) {
        Fields $layer @('id','roots','entrypoints')
        Require ($layer.id -is [string] -and $layer.id -match '^[a-z][a-z0-9_-]*$' -and -not $ids.ContainsKey($layer.id)) 'Invalid or duplicate layer id.'
        $ids[$layer.id] = $true
        Require ($layer.roots -is [array] -and $layer.entrypoints -is [array]) 'roots and entrypoints must be arrays.'
        foreach ($name in $layer.roots) {
            Require ($name -is [string] -and $name -notmatch '[\\/:]' -and $name -notin @('.', '..') -and -not $roots.ContainsKey($name)) "Invalid or overlapping root: $name"
            $null = Safe-Path $name -Directory; $roots[$name] = $layer.id
        }
        foreach ($entry in $layer.entrypoints) { Require ($entry -is [string]) 'Entrypoint must be a string.'; $null = Safe-Path $entry }
    }
    foreach ($name in $config.excluded_roots) {
        Require ($name -is [string] -and $name -notmatch '[\\/:]' -and $name -notin @('.', '..') -and -not [string]::IsNullOrWhiteSpace($name) -and -not $roots.ContainsKey($name)) "Invalid or overlapping exclusion: $name"
        $roots[$name] = 'excluded'
    }
    foreach ($dir in Get-ChildItem -LiteralPath $rootPath -Directory -Force) { Require ($roots.ContainsKey($dir.Name)) "Unclassified root: $($dir.Name)" }
    foreach ($contract in $config.contracts.PSObject.Properties) { Require ($contract.Value -is [string]) 'Contract must be a path string.'; $null = Safe-Path $contract.Value }
    Write-Host 'PASS architecture: schema, roots, entrypoints, contracts'
} catch { Write-Host "FAIL architecture: $($_.Exception.Message)"; $failures++ }
try {
    $aliases = New-Object 'System.Collections.Generic.Dictionary[string,string]' ([StringComparer]::Ordinal)
    $started = $false; $pending = $null
    foreach ($line in ((Read-Utf8 (Safe-Path '.kb/aliases.yaml')) -split '\r?\n')) {
        if ($line -match '^\s*(#.*)?$') { continue }
        if (-not $started) { Require ($line -ceq 'aliases:') 'Expected aliases header.'; $started = $true; continue }
        if ($line -match '^  (\S.*):$') {
            Require ($null -eq $pending) 'Alias missing canonical.'
            $pending = Yaml-String $Matches[1]
            Require (-not $aliases.ContainsKey($pending)) "Duplicate alias: $pending"
        } elseif ($line -match '^    canonical: (.+)$') {
            Require ($null -ne $pending) 'Canonical without alias.'
            $aliases.Add($pending, (Yaml-String $Matches[1])); $pending = $null
        } else { throw "Unsupported aliases structure: $line" }
    }
    Require ($started -and $null -eq $pending) 'Incomplete aliases structure.'
    foreach ($alias in $aliases.Keys) {
        $seen = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::Ordinal)
        $current = $alias
        while ($aliases.ContainsKey($current)) {
            Require ($seen.Add($current)) "Alias cycle from: $alias"
            $next = $aliases[$current]; if ($next -ceq $current) { break }; $current = $next
        }
    }
    Write-Host "PASS aliases: $($aliases.Count) mappings, no cycles"
} catch { Write-Host "FAIL aliases: $($_.Exception.Message)"; $failures++ }
if (-not $SkipManifest) {
    try {
        $lines = @((Read-Utf8 (Safe-Path '.kb/manifest.yaml')) -split '\r?\n' | Where-Object { $_ -notmatch '^\s*(#.*)?$' })
        $headers = @('version: 4','okf:','  profile: ".kb/okf-profile.yaml"','  compatibility: migrated','  strict_conformance: true','  strict_scope: repository_profile','  official_parser_conformance: not_claimed','documents:')
        Require ($lines.Count -ge $headers.Count) 'Incomplete manifest.'
        for ($i = 0; $i -lt $headers.Count; $i++) { Require ($lines[$i] -ceq $headers[$i]) "Unsupported manifest header at line $i" }
        $entries = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::Ordinal)
        for ($i = $headers.Count; $i -lt $lines.Count; $i += 5) {
            Require ($i + 4 -lt $lines.Count -and $lines[$i] -match '^  (".*"):$') 'Unsupported manifest document structure.'
            $path = Yaml-String $Matches[1]; Require ($entries.Add($path)) "Duplicate manifest path: $path"
            $full = Safe-Path $path
            $kind = if ([IO.Path]::GetFileName($full) -eq 'README.md') { 'README' } else { 'doc' }
            $bytes = [IO.File]::ReadAllBytes($full); $text = $utf8.GetString($bytes)
            $expected = @("    kind: $kind", "    maturity: $(Maturity $text)", "    bytes: $($bytes.Length)", "    lines: $([IO.File]::ReadAllLines($full, $utf8).Count)")
            for ($j = 0; $j -lt 4; $j++) { Require ($lines[$i + $j + 1] -ceq $expected[$j]) "Stale or unsupported manifest field for $path`: $($lines[$i + $j + 1])" }
        }
        $scriptDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
        $files = @(& (Join-Path $scriptDirectory 'get_kb_markdown.ps1') -Root $rootPath)
        $actual = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::Ordinal)
        foreach ($file in $files) { $null = $actual.Add($file.FullName.Substring($prefix.Length).Replace('\', '/')) }
        Require ($entries.SetEquals($actual)) 'Manifest paths differ from the shared Git-visible Markdown scope.'
        Write-Host "PASS manifest: $($entries.Count) exact paths and disk metrics"
    } catch { Write-Host "FAIL manifest: $($_.Exception.Message)"; $failures++ }
} else { Write-Host 'SKIP manifest: requested before rebuild' }
if ($failures) { exit 1 }; exit 0
