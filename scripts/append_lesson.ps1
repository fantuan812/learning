[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$Lesson,

    [string]$File = ''
)

$ErrorActionPreference = 'Stop'
$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = Split-Path -Parent $scriptRoot
if ([string]::IsNullOrWhiteSpace($File)) {
    $File = Join-Path $repoRoot 'learning\log.md'
}

$parent = Split-Path -Parent $File
if (-not (Test-Path -LiteralPath $parent -PathType Container)) {
    New-Item -ItemType Directory -Force -Path $parent | Out-Null
}

$utf8NoBom = [System.Text.UTF8Encoding]::new($false)
$content = if (Test-Path -LiteralPath $File -PathType Leaf) {
    [System.IO.File]::ReadAllText($File, $utf8NoBom)
} else {
    "# 维护经验日志`n`n本文件只追加，不回写历史记录。`n"
}

$timestamp = Get-Date -Format 'yyyy-MM-dd HH:mm:ss zzz'
$content = $content.TrimEnd("`r", "`n") + "`n- $timestamp：$Lesson`n"
[System.IO.File]::WriteAllText($File, $content, $utf8NoBom)
Write-Output "APPENDED $File"

