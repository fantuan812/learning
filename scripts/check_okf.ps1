# Scope: only this validator script; reads repository state and never mutates files, index, history, or remotes.
# This is a lightweight lint for the supported OKF YAML subset, not a general YAML parser.
[CmdletBinding()]
param(
    [string]$Root = '',
    [ValidateSet('Changed','Audit','Strict')][string]$Mode = 'Changed'
)

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
$OutputEncoding = [System.Text.UTF8Encoding]::new($false)
if ([string]::IsNullOrWhiteSpace($Root)) { $Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path) }
$rootPath = (Resolve-Path -LiteralPath $Root).Path

function Invoke-Git([string[]]$GitArgs) {
    $old = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    $out = & git -c "safe.directory=$rootPath" -c core.quotepath=false -C $rootPath $GitArgs 2>$null
    if ($LASTEXITCODE -ne 0) { $ErrorActionPreference=$old; throw "git failed: $($GitArgs -join ' ')" }
    $ErrorActionPreference = $old
    return $out
}

function Get-ChangedMarkdown {
    $items = New-Object System.Collections.Generic.List[string]
    # Explicit Git glob avoids native wildcard expansion of array arguments.
    $diff = @(Invoke-Git @('diff','--name-only','--diff-filter=ACMRTUXB','HEAD','--',':(glob)**/*.md'))
    foreach ($x in $diff) { if ($x -and ([string]$x -notmatch '(?i)^\.agents[\\/]skills[\\/].*[\\/]SKILL\.md$') -and ([string]$x -notmatch '(?i)^learning[\\/]log\.md$')) { [void]$items.Add((Join-Path $rootPath ([string]$x))) } }
    $untracked = @(Invoke-Git @('ls-files','--others','--exclude-standard','--',':(glob)**/*.md'))
    foreach ($p in $untracked) {
        if ($p -and ($p -notmatch '(?i)^\.agents[\\/]skills[\\/].*[\\/]SKILL\.md$') -and ($p -notmatch '(?i)^learning[\\/]log\.md$') -and (Test-Path -LiteralPath (Join-Path $rootPath $p))) { [void]$items.Add((Join-Path $rootPath $p)) }
    }
    return @($items | Sort-Object -Unique)
}

$allMarkdown = @(& (Join-Path $PSScriptRoot 'get_kb_markdown.ps1') -Root $rootPath)
function Test-OperationalExclusion([string]$Path) {
    return ($Path -match '(?i)[\\/]\.agents[\\/]skills[\\/].*[\\/]SKILL\.md$' -or $Path -match '(?i)[\\/]learning[\\/]log\.md$')
}
function Get-MarkdownFiles {
    param([string]$ScanMode)
    $changedSet = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    if ($ScanMode -eq 'Changed') {
        foreach ($path in (Get-ChangedMarkdown)) { [void]$changedSet.Add($path) }
    }
    foreach ($file in $allMarkdown) {
        if (Test-OperationalExclusion $file.FullName) { continue }
        if ($ScanMode -ne 'Changed' -or $changedSet.Contains($file.FullName)) { $file.FullName }
    }
}
function Get-FrontmatterField {
    param([string[]]$FrontmatterLines, [string]$Name)
    for ($i = 0; $i -lt $FrontmatterLines.Count; $i++) {
        $line = [string]$FrontmatterLines[$i]
        if ($line -match ("^" + [regex]::Escape($Name) + "\s*:\s*(.*?)\s*$")) {
            $inline = $Matches[1].Trim()
            $block = New-Object System.Collections.Generic.List[string]
            for ($j = $i + 1; $j -lt $FrontmatterLines.Count; $j++) {
                $next = [string]$FrontmatterLines[$j]
                if ($next -match '^\S') { break }
                [void]$block.Add($next)
            }
            return @{ Found = $true; Inline = $inline; Block = @($block) }
        }
    }
    return @{ Found = $false; Inline = ''; Block = @() }
}

function ConvertFrom-InlineMap {
    param([string]$Value)
    $trimmed = $Value.Trim()
    if ($trimmed -notmatch '^\{(.*)\}$') { return $null }
    $map = @{}
    $body = $Matches[1].Trim()
    if ([string]::IsNullOrWhiteSpace($body)) { return $map }
    foreach ($part in @($body -split ',')) {
        if ($part -notmatch '^\s*([A-Za-z_][\w-]*)\s*:\s*(.*?)\s*$') { return $null }
        $map[$Matches[1].ToLowerInvariant()] = $Matches[2].Trim().Trim('''','"')
    }
    return $map
}

function Test-Iso8601DateTime {
    param([string]$Value)
    $v = $Value.Trim().Trim('''','"')
    if ($v -notmatch '^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(?:\.\d+)?(?:Z|[+-]\d{2}:\d{2})$') { return $false }
    $parsed = [DateTimeOffset]::MinValue
    return [DateTimeOffset]::TryParse($v, [Globalization.CultureInfo]::InvariantCulture, [Globalization.DateTimeStyles]::RoundtripKind, [ref]$parsed)
}

function Test-Actor {
    param([string]$Value)
    $v = $Value.Trim().Trim('''','"')
    return (-not [string]::IsNullOrWhiteSpace($v) -and ($v -match '^human:.+$' -or $v -match '^process:.+$' -or $v -match '^[^\s/]+/[^\s/]+$'))
}

function Test-VerificationEvent {
    param([hashtable]$Event)
    if ($null -eq $Event -or -not $Event.ContainsKey('by') -or -not (Test-Actor $Event['by'])) { return 'verified event missing/invalid by' }
    if (-not $Event.ContainsKey('at') -or -not (Test-Iso8601DateTime $Event['at'])) { return 'verified event missing/invalid at' }
    return $null
}

function ConvertFrom-BlockMap {
    param([string[]]$Lines, [bool]$AllowDash)
    $map = @{}
    foreach ($line in $Lines) {
        if ([string]::IsNullOrWhiteSpace($line)) { continue }
        $candidate = [string]$line
        if ($candidate -match '^\s*-\s*(.*)$') {
            if (-not $AllowDash) { return $null }
            $candidate = $Matches[1]
        }
        if ($candidate -notmatch '^\s*([A-Za-z_][\w-]*)\s*:\s*(.*?)\s*$') { return $null }
        $map[$Matches[1].ToLowerInvariant()] = $Matches[2].Trim().Trim('''','"')
    }
    return $map
}

function Test-VerifiedField {
    param($Field)
    $inline = [string]$Field.Inline
    if ($inline -eq '[]') { return $null }
    if ($inline.StartsWith('{')) {
        $event = ConvertFrom-InlineMap $inline
        if ($null -eq $event) { return 'invalid verified mapping' }
        return Test-VerificationEvent $event
    }
    if ($inline.StartsWith('[')) {
        $matches = [regex]::Matches($inline, '\{[^{}]*\}')
        if ($matches.Count -eq 0) { return 'invalid verified inline list' }
        $remainder = [regex]::Replace($inline, '\{[^{}]*\}', '')
        if ($remainder -notmatch '^\[\s*(?:,\s*)*\]$') { return 'invalid verified inline list' }
        foreach ($match in $matches) {
            $event = ConvertFrom-InlineMap $match.Value
            if ($null -eq $event) { return 'invalid verified inline list event' }
            $reason = Test-VerificationEvent $event
            if ($reason) { return $reason }
        }
        return $null
    }
    if (-not [string]::IsNullOrWhiteSpace($inline)) { return 'verified must be an event mapping, event list, or []' }

    $events = New-Object System.Collections.Generic.List[hashtable]
    $current = $null
    $sawList = $false
    foreach ($line in @($Field.Block)) {
        if ([string]::IsNullOrWhiteSpace($line)) { continue }
        if ($line -match '^\s*-\s*(.*)$') {
            $sawList = $true
            if ($null -ne $current) { [void]$events.Add($current) }
            $current = @{}
            $rest = $Matches[1].Trim()
            if ($rest.StartsWith('{')) {
                $event = ConvertFrom-InlineMap $rest
                if ($null -eq $event) { return 'invalid verified list event' }
                [void]$events.Add($event)
                $current = $null
            } elseif (-not [string]::IsNullOrWhiteSpace($rest)) {
                if ($rest -notmatch '^([A-Za-z_][\w-]*)\s*:\s*(.*?)\s*$') { return 'invalid verified list event' }
                $current[$Matches[1].ToLowerInvariant()] = $Matches[2].Trim().Trim('''','"')
            }
            continue
        }
        if ($line -notmatch '^\s+([A-Za-z_][\w-]*)\s*:\s*(.*?)\s*$') { return 'invalid verified block' }
        if ($null -eq $current) { $current = @{} }
        $current[$Matches[1].ToLowerInvariant()] = $Matches[2].Trim().Trim('''','"')
    }
    if ($null -ne $current) { [void]$events.Add($current) }
    if ($events.Count -eq 0) { return 'verified block has no events' }
    if (-not $sawList -and $events.Count -ne 1) { return 'invalid verified mapping' }
    foreach ($event in $events) {
        $reason = Test-VerificationEvent $event
        if ($reason) { return $reason }
    }
    return $null
}

function Test-GeneratedField {
    param($Field)
    $map = $null
    $inline = [string]$Field.Inline
    if ($inline.StartsWith('{')) { $map = ConvertFrom-InlineMap $inline }
    elseif ([string]::IsNullOrWhiteSpace($inline)) { $map = ConvertFrom-BlockMap @($Field.Block) $false }
    else { return 'generated must be a mapping' }
    if ($null -eq $map -or -not $map.ContainsKey('by') -or -not (Test-Actor $map['by'])) { return 'generated missing/invalid by' }
    if ($map.ContainsKey('at') -and -not (Test-Iso8601DateTime $map['at'])) { return 'generated invalid at' }
    return $null
}

function Test-Note($Path) {
    $name = [IO.Path]::GetFileName($Path)
    $isRootIndex = [string]::Equals([IO.Path]::GetFullPath($Path), (Join-Path $rootPath 'index.md'), [StringComparison]::OrdinalIgnoreCase)
    $reserved = ($name -ieq 'index.md' -or $name -ieq 'log.md')
    $lines = @(Get-Content -LiteralPath $Path -ErrorAction Stop)
    $has = ($lines.Count -gt 0 -and $lines[0].Trim() -eq '---')
    if (-not $has) {
        if ($reserved) { if($name -ieq 'log.md' -and -not ($lines -match '^##\s+\d{4}-\d{2}-\d{2}')) { return @{State='FAIL';Reason='log heading missing'} }; return @{ State='PASS'; Reason='reserved' } }
        return @{ State=($(if ($Mode -eq 'Audit') {'WARN'} else {'FAIL'})); Reason='missing frontmatter' }
    }
    $close = -1
    for ($i=1; $i -lt $lines.Count; $i++) { if ($lines[$i].Trim() -eq '---') { $close=$i; break } }
    if ($close -lt 0) { return @{State='FAIL';Reason='unclosed frontmatter'} }
    $fm = @{}; $sourceBlocks = @(); $inSources=$false; $itemActive=$false; $itemHasResource=$false; $missingSourceResources=0
    for ($i=1; $i -lt $close; $i++) {
        $l=[string]$lines[$i]
        if ($inSources) {
            if ($l -match '^\S') { if ($itemActive -and -not $itemHasResource) {$missingSourceResources++}; $inSources=$false; $itemActive=$false }
            elseif ($l -match '^\s*-\s*') { if ($itemActive -and -not $itemHasResource) {$missingSourceResources++}; $itemActive=$true; $itemHasResource=$false; if ($l -match 'resource\s*:\s*(\S+)') {$itemHasResource=$true}; continue }
            elseif ($l -match '^\s+resource\s*:\s*(\S+)') { if ($itemActive) {$itemHasResource=$true}; continue }
            else { continue }
        }
        if ($l -match '^sources\s*:\s*(.*?)\s*$') { $inSources=($Matches[1].Trim() -ne '[]'); continue }
        if ($l -match '^([A-Za-z_][\w-]*)\s*:\s*(.*?)\s*$') { $fm[$Matches[1].ToLowerInvariant()]=$Matches[2].Trim().Trim('''','"') }
    }
    if ($inSources -and $itemActive -and -not $itemHasResource) {$missingSourceResources++}
    $frontmatterLines = if ($close -gt 1) { @($lines[1..($close-1)]) } else { @() }
    $errs=New-Object System.Collections.Generic.List[string]
    if ($name -ieq 'index.md' -and -not $isRootIndex) { [void]$errs.Add('non-root index frontmatter forbidden') }
    if ($name -ieq 'index.md' -and $isRootIndex) {
        foreach($k in $fm.Keys){if($k -ne 'okf_version'){[void]$errs.Add('root index only okf_version')}}
        if ($fm.ContainsKey('okf_version') -and $fm['okf_version'] -ne '0.2') { [void]$errs.Add('root index okf_version must be 0.2') }
    }
    if ($name -ieq 'log.md') { [void]$errs.Add('log.md must not have frontmatter') }
    if ($name -ine 'index.md' -or -not $isRootIndex) { if (-not $fm.ContainsKey('type') -or [string]::IsNullOrWhiteSpace($fm['type'])) { [void]$errs.Add('type missing/empty') } }
    if ($fm.ContainsKey('status') -and $fm['status'] -notin @('draft','stable','deprecated')) { [void]$errs.Add('invalid status') }
    $verifiedField = Get-FrontmatterField $frontmatterLines 'verified'
    if ($verifiedField.Found) { $reason = Test-VerifiedField $verifiedField; if ($reason) { [void]$errs.Add($reason) } }
    $generatedField = Get-FrontmatterField $frontmatterLines 'generated'
    if ($generatedField.Found) { $reason = Test-GeneratedField $generatedField; if ($reason) { [void]$errs.Add($reason) } }
    foreach ($k in @('updated','stale_after')) { if ($fm.ContainsKey($k) -and $fm[$k] -notmatch '^(\d{4}-\d{2}-\d{2}|\{\{date:YYYY-MM-DD\}\})$') { [void]$errs.Add("invalid $k") } }
    if ($missingSourceResources -gt 0) { [void]$errs.Add('source item resource missing') }
    if ($errs.Count) { return @{State='FAIL';Reason=($errs -join '; ')} }
    return @{State='PASS';Reason='conformant'}
}

$files = @(Get-MarkdownFiles $Mode)
$excluded = @($allMarkdown | Where-Object { Test-OperationalExclusion $_.FullName }).Count
$warn=0; $fail=0; $legacy=0; $ok=0
foreach ($f in $files) {
    try { $r=Test-Note $f; if ($r.State -eq 'PASS'){$ok++} elseif($r.State -eq 'WARN'){$warn++;$legacy++} else{$fail++}; if($r.State -ne 'PASS'){ Write-Output ("{0}: {1} [{2}]" -f ($f.Substring($rootPath.Length+1)),$r.Reason,$r.State) } }
    catch { $fail++; Write-Output ("{0}: read error [{1}]" -f $f,'FAIL') }
}
$result = if ($fail -gt 0) {'FAIL'} elseif ($warn -gt 0) {'WARN'} else {'PASS'}
Write-Output "Mode: $Mode"
Write-Output "Scanned: $($files.Count)"
Write-Output "Conformant: $ok"
Write-Output "Legacy: $legacy"
Write-Output "Excluded: $excluded"
Write-Output "WARN: $warn"
Write-Output "FAIL: $fail"
Write-Output "RESULT: $result"
exit $(if($result -eq 'FAIL'){1}else{0})
