[CmdletBinding()]
param(
    [string]$Root = (Split-Path -Parent $PSScriptRoot),
    [string]$GeneratedAt = (Get-Date -Format 'yyyy-MM-dd'),
    [string]$Note = 'paths, bytes, lines, and maturity mechanically recalculated'
)

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$OutputEncoding = [System.Text.Encoding]::UTF8
$rootPath = [System.IO.Path]::GetFullPath($Root)
$manifestPath = Join-Path $rootPath '.kb\manifest.yaml'
$utf8NoBom = [System.Text.UTF8Encoding]::new($false, $true)

function Get-RelativePath([string]$BasePath, [string]$FullPath) {
    $baseUri = New-Object System.Uri(($BasePath.TrimEnd('\', '/') + [System.IO.Path]::DirectorySeparatorChar))
    $fileUri = New-Object System.Uri($FullPath)
    return [System.Uri]::UnescapeDataString($baseUri.MakeRelativeUri($fileUri).ToString()).Replace('\', '/')
}

function Get-Maturity([string]$Text) {
    $yaml = [regex]::Match($Text, '(?m)^maturity:\s*(L[0-5])\s*$')
    if ($yaml.Success) { return $yaml.Groups[1].Value }

    # Keep the script source ASCII-safe so Windows PowerShell 5.1 can read it
    # without a UTF-8 BOM while still matching the Chinese maturity label.
    $body = [regex]::Match($Text, '\u77E5\u8BC6\u6210\u719F\u5EA6[\uFF1A:]\s*\*{0,2}(L[0-5])')
    if ($body.Success) { return $body.Groups[1].Value }

    return 'L0'
}

function Quote-Yaml([string]$Value) {
    return '"' + $Value.Replace('\', '\\').Replace('"', '\"') + '"'
}

$scriptDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
$files = @(& (Join-Path $scriptDirectory 'get_kb_markdown.ps1') -Root $rootPath)
$orderedPaths = New-Object System.Collections.Generic.List[string]
foreach ($file in $files) {
    $orderedPaths.Add((Get-RelativePath $rootPath $file.FullName))
}
$ordered = $orderedPaths.ToArray()
[System.Array]::Sort($ordered, [System.StringComparer]::Ordinal)

$lines = New-Object System.Collections.Generic.List[string]
$lines.Add('# Knowledge Base Manifest')
$lines.Add('# Index only; canonical content remains in each Markdown document.')
$lines.Add("# Generated: $GeneratedAt ($Note).")
$lines.Add('version: 4')
$lines.Add('')
$lines.Add('okf:')
$lines.Add('  profile: ".kb/okf-profile.yaml"')
$lines.Add('  compatibility: migrated')
$lines.Add('  strict_conformance: true')
$lines.Add('  strict_scope: repository_profile')
$lines.Add('  official_parser_conformance: not_claimed')
$lines.Add('')
$lines.Add('documents:')

foreach ($relative in $ordered) {
    $fullFilePath = Join-Path $rootPath $relative
    $bytes = [System.IO.File]::ReadAllBytes($fullFilePath)
    $text = $utf8NoBom.GetString($bytes)
    $lineCount = [System.IO.File]::ReadAllLines($fullFilePath, $utf8NoBom).Count
    $fileName = [System.IO.Path]::GetFileName($fullFilePath)
    $kind = if ($fileName -eq 'README.md') { 'README' } else { 'doc' }
    $maturity = Get-Maturity $text

    $lines.Add("  $(Quote-Yaml $relative):")
    $lines.Add("    kind: $kind")
    $lines.Add("    maturity: $maturity")
    $lines.Add("    bytes: $($bytes.Length)")
    $lines.Add("    lines: $lineCount")
}

$content = [string]::Join("`n", $lines) + "`n"
[System.IO.File]::WriteAllText($manifestPath, $content, $utf8NoBom)
Write-Host "Manifest rebuilt: $($ordered.Count) Markdown files -> $manifestPath"
