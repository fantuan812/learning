# Git mutations are restricted to the newly created temporary fixture.
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$helper = Join-Path $PSScriptRoot 'get_kb_markdown.ps1'
$fixture = Join-Path ([IO.Path]::GetTempPath()) ('kb-scope-' + [guid]::NewGuid().ToString('N'))
[void][IO.Directory]::CreateDirectory($fixture)
$utf8 = [Text.UTF8Encoding]::new($false)
function Write-Fixture([string]$Relative, [string]$Text) {
    $path = Join-Path $fixture $Relative
    [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($path))
    [IO.File]::WriteAllText($path, $Text, $utf8)
}
function Invoke-FixtureGit([string[]]$Arguments) {
    & git -c "safe.directory=$fixture" -C $fixture @Arguments 2>$null | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "Fixture Git failed: $Arguments" }
}
Invoke-FixtureGit @('init','--quiet')
Write-Fixture 'tracked.md' 'tracked'
Write-Fixture 'deleted.md' 'deleted'
Invoke-FixtureGit @('add','--','tracked.md','deleted.md')
# This exact newly-created fixture file is safe to remove without recursion.
Remove-Item -LiteralPath (Join-Path $fixture 'deleted.md')
Write-Fixture '.gitignore' "cache/`ntracked.md`n"
Write-Fixture 'new.md' 'new'
Write-Fixture 'cache/ignored.md' 'ignored'
Write-Fixture '.img-work/excluded.md' 'excluded'
Write-Fixture 'references/UnrealEngine-5.8-Docs/excluded.md' 'excluded'
$unicodeName = ([string][char]0x4E2D + [char]0x6587) + '/note.md'
Write-Fixture $unicodeName 'unicode'
$files = @(& $helper -Root $fixture)
$names = @($files | ForEach-Object { $_.FullName.Substring($fixture.Length + 1).Replace('\','/') })
$expected = [string[]]@('new.md', 'tracked.md', $unicodeName)
[Array]::Sort($expected, [StringComparer]::Ordinal)
if (($names -join '|') -cne ($expected -join '|')) { throw "Scope mismatch: $($names -join '|')" }
if (@($files | Where-Object { $_ -isnot [IO.FileInfo] }).Count) { throw 'Expected FileInfo output' }
$nonRepository = Join-Path ([IO.Path]::GetTempPath()) ('kb-no-git-' + [guid]::NewGuid().ToString('N'))
[void][IO.Directory]::CreateDirectory($nonRepository)
$failed = $false
try { $null = @(& $helper -Root $nonRepository) } catch { $failed = $_.Exception.Message -match 'git ls-files failed' }
if (-not $failed) { throw 'Non-repository scope must fail explicitly' }
Write-Output 'PASS: untracked, ignored, tracked-ignore, deleted, Unicode, ordering, exclusions, FileInfo, Git failure'
Write-Output "Fixture retained: $fixture"
