# All file/index/history mutations belong to a fresh temporary fixture.
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$checker = Join-Path $PSScriptRoot 'check_okf.ps1'
$fixture = Join-Path ([IO.Path]::GetTempPath()) ('kb-okf-changed-' + [guid]::NewGuid().ToString('N'))
[void][IO.Directory]::CreateDirectory($fixture)
$utf8 = [Text.UTF8Encoding]::new($false)
function Write-Fixture([string]$Relative, [string]$Text) {
    $path = Join-Path $fixture $Relative
    [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($path))
    [IO.File]::WriteAllText($path, $Text, $utf8)
}
function Git-Fixture([string[]]$Arguments) {
    & git -c "safe.directory=$fixture" -c user.name=fixture -c user.email=fixture@example.invalid -C $fixture @Arguments | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "Fixture git failed: $Arguments" }
}
function Require([bool]$Condition, [string]$Message) { if (-not $Condition) { throw $Message } }
$pwsh = (Get-Process -Id $PID).Path
$valid = "---`ntype: Note`n---`n# Valid`n"
Git-Fixture @('init', '--quiet')
Write-Fixture 'README.md' $valid
Write-Fixture 'index.md' "---`nokf_version: 0.2`n---`n# Root`n"
Write-Fixture 'nested/unstaged.md' $valid
Write-Fixture 'nested/staged.md' $valid
Git-Fixture @('add', '--', 'README.md', 'index.md', 'nested/unstaged.md', 'nested/staged.md')
Git-Fixture @('commit', '--quiet', '-m', 'Fixture baseline')
# CWD deliberately contains *.md so native array-globbing regressions surface.
Push-Location $fixture
try {
    $output = (& $pwsh -NoLogo -NoProfile -File $checker -Root $fixture -Mode Strict 2>&1 | Out-String)
    Require ($LASTEXITCODE -eq 0) "Valid root index and notes must pass Strict: $output"
    Require ($output.Contains('Scanned: 4')) 'Strict must inspect all four fixture files'
    Write-Fixture 'index.md' "---`nokf_version: 0.2`n---`n# Changed root`n"
    Write-Fixture 'nested/unstaged.md' '# Missing frontmatter, unstaged'
    Write-Fixture 'nested/staged.md' '# Missing frontmatter, staged'
    Git-Fixture @('add', '--', 'nested/staged.md')
    Write-Fixture 'nested/untracked.md' '# Missing frontmatter, untracked'
    $output = (& $pwsh -NoLogo -NoProfile -File $checker -Root $fixture -Mode Changed 2>&1 | Out-String)
    Require ($LASTEXITCODE -eq 1) 'Changed invalid nested notes must fail'
    Require ($output.Contains('Scanned: 4')) "Changed must inspect root plus all three nested changes: $output"
    Require ($output.Contains('FAIL: 3')) 'Exactly three missing-frontmatter negatives must fail'
    foreach ($name in @('unstaged.md', 'staged.md', 'untracked.md')) {
        Require ($output.Contains($name + ': missing frontmatter [FAIL]')) "Missing negative control: $name"
    }
    Write-Fixture 'nested/index.md' "---`nokf_version: 0.2`n---`n# Illegal nested index`n"
    $output = (& $pwsh -NoLogo -NoProfile -File $checker -Root $fixture -Mode Changed 2>&1 | Out-String)
    Require ($LASTEXITCODE -eq 1) 'Nested index with frontmatter must still fail'
    Require ($output.Contains('non-root index frontmatter forbidden')) 'Root exception must not leak to nested index'
} finally { Pop-Location }
Write-Output 'PASS: root index, tracked unstaged/staged, untracked nested changes, CWD wildcard trap, negative controls'
Write-Output "Fixture retained: $fixture"
