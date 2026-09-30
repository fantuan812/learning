# Shared read-only Markdown scope. Git failures are fatal; never scan recursively.
[CmdletBinding()]
param([Parameter(Mandatory = $true)][string]$Root)

$ErrorActionPreference = 'Stop'
$gitCommand = Get-Command git -CommandType Application -ErrorAction Stop | Select-Object -First 1
$rootPath = (Resolve-Path -LiteralPath $Root).Path.TrimEnd('\', '/')
$prefix = $rootPath + [IO.Path]::DirectorySeparatorChar
$oldEncoding = [Console]::OutputEncoding
$oldPreference = $ErrorActionPreference
try {
    [Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
    $ErrorActionPreference = 'Continue'
    $paths = @(& $gitCommand.Source -c "safe.directory=$rootPath" -c core.quotepath=false -C $rootPath ls-files --cached --others --exclude-standard -- '*.md' 2>$null)
    $gitExit = $LASTEXITCODE
} finally {
    [Console]::OutputEncoding = $oldEncoding
    $ErrorActionPreference = $oldPreference
}
if ($gitExit -ne 0) { throw "git ls-files failed for Markdown scope (exit $gitExit): $rootPath" }
$unique = [Collections.Generic.SortedSet[string]]::new([StringComparer]::Ordinal)
foreach ($entry in $paths) {
    $relative = [string]$entry
    if ([string]::IsNullOrWhiteSpace($relative)) { continue }
    # Quoted names contain unsupported control characters, not ordinary Unicode.
    if ($relative.StartsWith('"') -or [IO.Path]::IsPathRooted($relative)) { throw "Unsafe Git path: $relative" }
    if ($relative -match '(?i)(^|[\\/])\.img-work([\\/]|$)' -or
        $relative -match '(?i)(^|[\\/])\.workbuddy([\\/]|$)' -or
        $relative -match '(?i)(^|[\\/])references[\\/]UnrealEngine-5\.8-Docs([\\/]|$)') { continue }
    $full = [IO.Path]::GetFullPath((Join-Path $rootPath $relative))
    if (-not $full.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) { throw "Git path outside root: $relative" }
    # Check ancestors before testing existence, including dangling junctions.
    $cursor = $full
    while ($cursor -and $cursor.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
        $item = Get-Item -LiteralPath $cursor -Force -ErrorAction SilentlyContinue
        if ($null -ne $item -and ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw "Reparse point in Git path: $relative" }
        $cursor = [IO.Path]::GetDirectoryName($cursor)
    }
    if (Test-Path -LiteralPath $full -PathType Leaf) { [void]$unique.Add($full) }
}
foreach ($full in $unique) { Get-Item -LiteralPath $full -Force }
