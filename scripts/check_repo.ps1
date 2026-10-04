[CmdletBinding()]
param(
    [string]$Root = '',
    # Optional checkout/install directory containing Engine; never inferred from a historical machine.
    [string]$UeInstallRoot = ''
)

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
$OutputEncoding = [System.Text.UTF8Encoding]::new($false)
if ([string]::IsNullOrWhiteSpace($Root)) { $Root = Split-Path -Parent $PSScriptRoot }
$rootPath = (Resolve-Path -LiteralPath $Root).Path.TrimEnd('\', '/')
$failures = [System.Collections.Generic.List[string]]::new()
$warnings = [System.Collections.Generic.List[string]]::new()
$passes = [System.Collections.Generic.List[string]]::new()
$utf8Strict = [System.Text.UTF8Encoding]::new($false, $true)
$maintenanceRoots = @('references', 'learning', 'scripts')

function Add-Failure([string]$Message) { $script:failures.Add($Message) }
function Add-Warning([string]$Message) { $script:warnings.Add($Message) }
function Add-Pass([string]$Message) { $script:passes.Add($Message) }

function Get-RepoRelative([string]$Path) {
    $full = [System.IO.Path]::GetFullPath($Path)
    $prefix = $rootPath + [System.IO.Path]::DirectorySeparatorChar
    if ($full.StartsWith($prefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        # Preserve the repository-relative convention used by existing filters.
        return $full.Substring($prefix.Length).Replace([System.IO.Path]::DirectorySeparatorChar, [char]'\')
    }
    return $full
}

function Test-MaintenancePath([string]$Path) {
    $relative = Get-RepoRelative $Path
    foreach ($name in $maintenanceRoots) {
        if ($relative -eq $name -or $relative.StartsWith($name + '\', [System.StringComparison]::OrdinalIgnoreCase)) {
            return $true
        }
    }
    return $false
}

function Test-PathUnder([string]$Path, [string]$BasePath) {
    $fullPath = [System.IO.Path]::GetFullPath($Path).TrimEnd('\', '/')
    $fullBase = [System.IO.Path]::GetFullPath($BasePath).TrimEnd('\', '/')
    return $fullPath -eq $fullBase -or $fullPath.StartsWith($fullBase + [System.IO.Path]::DirectorySeparatorChar, [System.StringComparison]::OrdinalIgnoreCase)
}

# These maps preserve pre-migration quality contracts, not topic classification.
$qualityLegacyByCurrent = [Collections.Generic.Dictionary[string,object]]::new([StringComparer]::OrdinalIgnoreCase)
$qualityCurrentByLegacy = [Collections.Generic.Dictionary[string,string]]::new([StringComparer]::OrdinalIgnoreCase)
$qualityNodesByCurrent = [Collections.Generic.Dictionary[string,object]]::new([StringComparer]::OrdinalIgnoreCase)
$knowledgeQualityRoots = @()
$legacyQualityRoots = @('游戏知识', '游戏AI', '游戏服务端', '游戏算法', '00-计算机与工程基础', '游戏测试与质量', '系统实战') | ForEach-Object { Join-Path $rootPath $_ }

function Get-QualityKey([string]$Path) {
    return ([IO.Path]::GetFullPath($Path)).Normalize([Text.NormalizationForm]::FormC)
}

function ConvertTo-KnowledgePath($Relative, [switch]$AllowMissing) {
    if ($Relative -isnot [string] -or [string]::IsNullOrWhiteSpace($Relative) -or
        $Relative -match '[\\:\x00-\x1f\x7f-\x9f]|^/|(^|/)\.{1,2}(/|$)|//|/$' -or
        $Relative -notmatch '(?i)\.md$') { throw "Unsafe knowledge path: $Relative" }
    $full = [IO.Path]::GetFullPath((Join-Path $rootPath $Relative))
    if (-not (Test-PathUnder $full $rootPath)) { throw "Knowledge path outside repository: $Relative" }
    $cursor = $full
    while ($cursor -ne $rootPath) {
        $item = Get-Item -LiteralPath $cursor -Force -ErrorAction SilentlyContinue
        if ($null -ne $item -and ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw "Symlink/reparse point in knowledge path: $Relative"
        }
        $cursor = [IO.Path]::GetDirectoryName($cursor)
    }
    if (-not $AllowMissing -and -not (Test-Path -LiteralPath $full -PathType Leaf)) {
        throw "Missing canonical knowledge file: $Relative"
    }
    return $full
}

function ConvertTo-KnowledgeMapValue($Value) {
    # Use the Windows PowerShell 5.1 ConvertFrom-Json surface. Normalize its
    # PSCustomObjects without relying on the newer -AsHashtable/-Depth options.
    # Unary-comma return preserves empty/singleton arrays instead of letting the
    # PowerShell output pipeline turn them into null or a scalar.
    if ($null -eq $Value) { return $null }
    if ($Value -is [Management.Automation.PSCustomObject]) {
        $mapping = @{}
        foreach ($property in $Value.PSObject.Properties) {
            $mapping[$property.Name] = ConvertTo-KnowledgeMapValue $property.Value
        }
        return $mapping
    }
    if ($Value -is [array]) {
        $items = [Collections.Generic.List[object]]::new()
        foreach ($item in $Value) {
            $items.Add((ConvertTo-KnowledgeMapValue $item))
        }
        return ,($items.ToArray())
    }
    return $Value
}

function Initialize-QualityMapping {
    $graphPath = Join-Path $rootPath '.kb/knowledge-map.json'
    $graphItem = Get-Item -LiteralPath $graphPath -Force -ErrorAction SilentlyContinue
    if ($null -eq $graphItem) { return }
    try {
        $cursor = $graphPath
        while ($cursor -ne $rootPath) {
            $item = Get-Item -LiteralPath $cursor -Force -ErrorAction SilentlyContinue
            if ($null -ne $item -and ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
                throw 'Symlink/reparse point knowledge map is unsupported'
            }
            $cursor = [IO.Path]::GetDirectoryName($cursor)
        }
        if (-not (Test-Path -LiteralPath $graphPath -PathType Leaf)) { throw 'Knowledge map must be a regular file' }
        $parsedGraph = [IO.File]::ReadAllText($graphPath, $utf8Strict) | ConvertFrom-Json
        $graph = ConvertTo-KnowledgeMapValue $parsedGraph
        if ($graph.documents -isnot [array]) { throw 'knowledge-map documents must be an array' }
        if ($graph.domains -isnot [array] -or $graph.domains.Count -eq 0) { throw 'knowledge-map domains must be a nonempty array' }
        $active = [Collections.Generic.Dictionary[string,string]]::new([StringComparer]::OrdinalIgnoreCase)
        $ids = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
        $visible = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
        foreach ($file in $mdFiles) { [void]$visible.Add((Get-QualityKey $file.FullName)) }
        foreach ($node in $graph.documents) {
            if ($node -isnot [Collections.IDictionary] -or $node.id -isnot [string] -or
                [string]::IsNullOrWhiteSpace($node.id) -or -not $ids.Add($node.id.Normalize([Text.NormalizationForm]::FormC))) {
                throw 'Invalid or duplicate knowledge document id'
            }
            $current = ConvertTo-KnowledgePath $node.path
            $key = Get-QualityKey $current
            if ($active.ContainsKey($key)) { throw "Duplicate/case/Unicode-conflicting canonical path: $($node.path)" }
            if (-not $visible.Contains($key)) { throw "Canonical path is not tracked or nonignored Markdown: $($node.path)" }
            $active.Add($key, $current)
            $qualityNodesByCurrent.Add($key, $node)
        }
        foreach ($node in $graph.documents) {
            $current = ConvertTo-KnowledgePath $node.path
            $key = Get-QualityKey $current
            if (-not $node.Contains('legacy_paths')) { continue }
            if ($node.legacy_paths -isnot [array] -or $node.legacy_paths.Count -eq 0) {
                throw "legacy_paths must be a nonempty array: $($node.path)"
            }
            $legacy = [Collections.Generic.List[string]]::new()
            foreach ($relative in $node.legacy_paths) {
                $old = ConvertTo-KnowledgePath $relative -AllowMissing
                $oldKey = Get-QualityKey $old
                if ($active.ContainsKey($oldKey)) { throw "legacy_paths conflicts with active canonical path: $relative" }
                if ($qualityCurrentByLegacy.ContainsKey($oldKey)) { throw "Duplicate/case/Unicode-conflicting legacy_paths: $relative" }
                $qualityCurrentByLegacy.Add($oldKey, $current)
                $legacy.Add($old)
            }
            $qualityLegacyByCurrent.Add($key, @($legacy))
        }
        $roots = [Collections.Generic.List[string]]::new()
        foreach ($domain in @($graph.domains)) {
            if ($domain.entrypoint -isnot [string] -or $domain.entrypoint -notmatch '^知识/[^/]+/README\.md$') {
                throw 'Domain entrypoint must be 知识/<domain-folder>/README.md'
            }
            $entry = ConvertTo-KnowledgePath $domain.entrypoint
            $roots.Add([IO.Path]::GetDirectoryName($entry))
        }
        $script:knowledgeQualityRoots = @($roots | Sort-Object -Unique)
    } catch {
        Add-Failure "知识映射质量兼容配置无效: $($_.Exception.Message)"
        $qualityLegacyByCurrent.Clear()
        $qualityCurrentByLegacy.Clear()
        $qualityNodesByCurrent.Clear()
        $script:knowledgeQualityRoots = @()
    }
}

function Get-QualityPaths([string]$Path) {
    $full = [IO.Path]::GetFullPath($Path)
    $full
    $key = Get-QualityKey $full
    if ($qualityLegacyByCurrent.ContainsKey($key)) { $qualityLegacyByCurrent[$key] }
}

function Test-QualityPathUnder([string]$Path, [string]$BasePath) {
    foreach ($candidate in (Get-QualityPaths $Path)) {
        if (Test-PathUnder $candidate $BasePath) { return $true }
    }
    return $false
}

function Test-NewKnowledgePath([string]$Path) {
    $key = Get-QualityKey $Path
    # An alias is identity history, never an exemption. Only an alias that
    # actually activates a pre-existing quality profile can replace the new
    # generic profile; notes/arbitrary aliases still require generic quality.
    if ($qualityLegacyByCurrent.ContainsKey($key)) {
        foreach ($legacy in $qualityLegacyByCurrent[$key]) {
            foreach ($base in $legacyQualityRoots) {
                if (Test-PathUnder $legacy $base) { return $false }
            }
        }
    }
    foreach ($base in $knowledgeQualityRoots) {
        if (Test-PathUnder $Path $base) { return $true }
    }
    return $false
}

function Test-UnrealTechnologyPath([string]$Path, [switch]$SourceAnalysis) {
    # Technology claims have the same quality requirements with or without
    # migration aliases. A legacy service/notes path cannot bypass UE checks.
    $key = Get-QualityKey $Path
    if (-not $qualityNodesByCurrent.ContainsKey($key)) { return $false }
    $node = $qualityNodesByCurrent[$key]
    return ('unreal-engine' -in @($node.technologies)) -and (-not $SourceAnalysis -or $node.kind -eq 'source-analysis')
}

function Resolve-QualityCanonical([string]$Relative) {
    $old = ConvertTo-KnowledgePath ($Relative.Replace('\', '/')) -AllowMissing
    $key = Get-QualityKey $old
    if ($qualityCurrentByLegacy.ContainsKey($key)) { return $qualityCurrentByLegacy[$key] }
    return $old
}

function Get-NonCodeMarkdownText([string]$Text, [switch]$WithFenceState) {
    # Bounded filtering, not a full CommonMark parser. Remember the direct
    # quote/list containers as well as the delimiter character and length.
    $kept = [Collections.Generic.List[string]]::new()
    $unclosed = [Collections.Generic.List[int]]::new()
    $inFence = $false
    $fenceChar = ''
    $fenceLength = 0
    $fenceOuterQuotes = 0
    $fenceInnerQuotes = 0
    $fenceIndent = 0
    $fenceLine = 0
    $lineNumber = 0
    foreach ($line in ($Text -split "`r?`n")) {
        $lineNumber++
        # Markdown container indentation uses four-column tab stops, not the
        # number of characters in "-`t" or "12.`t". Keep original prose bytes.
        $expandedLine = $line
        if ($line.Contains("`t")) {
            $expanded = [Text.StringBuilder]::new()
            $column = 0
            foreach ($character in $line.ToCharArray()) {
                if ($character -eq [char]9) {
                    $padding = 4 - ($column % 4)
                    [void]$expanded.Append((' ' * $padding))
                    $column += $padding
                } else { [void]$expanded.Append($character); $column++ }
            }
            $expandedLine = $expanded.ToString()
        }
        if ($inFence) {
            $closing = $expandedLine
            $sameContainer = $true
            # Strip exactly the outer quote prefix, not arbitrary code text
            # that happens to start with a greater-than operator.
            for ($q = 0; $q -lt $fenceOuterQuotes; $q++) {
                if ($closing -match '^ {0,3}>[ \t]?(.*)$') { $closing = $Matches[1] }
                else { $sameContainer = $false; break }
            }
            if ($sameContainer -and $fenceIndent -gt 0) {
                if ([string]::IsNullOrWhiteSpace($closing)) { $closing = '' }
                elseif ($closing -match ('^ {' + $fenceIndent + '}(.*)$')) { $closing = $Matches[1] }
                else { $sameContainer = $false }
            }
            if ($sameContainer) {
                for ($q = 0; $q -lt $fenceInnerQuotes; $q++) {
                    if ($closing -match '^ {0,3}>[ \t]?(.*)$') { $closing = $Matches[1] }
                    else { $sameContainer = $false; break }
                }
            }
            if ($sameContainer) {
                if ($closing -match '^ {0,3}(`{3,}|~{3,})[ \t]*$') {
                    $marker = $Matches[1]
                    if ($marker[0].ToString() -eq $fenceChar -and $marker.Length -ge $fenceLength) { $inFence = $false }
                }
                continue
            }
            # A nonblank unindent or a missing quote marker ends the old
            # container. Our explicit-close policy records its unfinished fence,
            # then reprocesses THIS line: a top-level ``` opens a new code block.
            $unclosed.Add($fenceLine)
            $inFence = $false
        }
        $candidate = $expandedLine
        $outerQuotes = 0
        while ($candidate -match '^ {0,3}>[ \t]?(.*)$') {
            $candidate = $Matches[1]
            $outerQuotes++
        }
        $opening = $candidate
        $listIndent = 0
        $innerQuotes = 0
        if ($opening -match '^( {0,3})((?:[-+*]|\d{1,9}[.)])[ \t]+)(.*)$') {
            $listIndent = $Matches[1].Length + $Matches[2].Length
            $opening = $Matches[3]
            while ($opening -match '^ {0,3}>[ \t]?(.*)$') {
                $opening = $Matches[1]
                $innerQuotes++
            }
        }
        if ($opening -match '^ {0,3}(`{3,}|~{3,})(.*)$') {
            $marker = $Matches[1]
            $info = $Matches[2]
            if ($marker[0] -ne [char]96 -or -not $info.Contains('`')) {
                $inFence = $true
                $fenceChar = $marker[0].ToString()
                $fenceLength = $marker.Length
                $fenceOuterQuotes = $outerQuotes
                $fenceInnerQuotes = $innerQuotes
                $fenceIndent = $listIndent
                $fenceLine = $lineNumber
                continue
            }
        }
        # Retain the conservative indented-code exclusion. This is not a
        # general parser for arbitrary nested or lazily continued lists.
        if ($candidate -match '^(?: {4}|\t)') { continue }
        $kept.Add($line)
    }
    if ($inFence) { $unclosed.Add($fenceLine) }
    $nonCode = $kept -join "`n"
    if ($WithFenceState) { return [pscustomobject]@{ Text = $nonCode; UnclosedFences = @($unclosed) } }
    return $nonCode
}

function Get-NonCodeMarkdownLinkText([string]$Text) {
    $nonCode = Get-NonCodeMarkdownText $Text
    # Equal-length runs; a shorter interior run is part of the code span.
    return [regex]::Replace($nonCode, '(?s)(?<!`)(`+)(?!`)(.*?)(?<!`)\1(?!`)', '')
}

function Test-ExternalSourceUrl([string]$Candidate) {
    if ([string]::IsNullOrWhiteSpace($Candidate)) { return $false }
    $candidate = $Candidate.Trim()
    $candidate = [regex]::Replace($candidate, '[.,;:!?，。；：！？]+$', '')
    try {
        $uri = [System.Uri]$candidate
    } catch {
        return $false
    }
    if ($uri.Scheme -notin @('http', 'https')) { return $false }
    $uriHost = $uri.Host.ToLowerInvariant()
    if ([string]::IsNullOrWhiteSpace($uriHost)) { return $false }
    if ($uriHost -eq 'localhost' -or $uriHost -eq '127.0.0.1' -or
        $uriHost -eq 'example.com' -or $uriHost.EndsWith('.example.com')) {
        return $false
    }
    return $true
}

function Get-ExternalSourceUrls([string]$Text) {
    $urls = [System.Collections.Generic.List[string]]::new()
    $seen = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)

    # 先复用 README 的 Markdown 链接解析，确保围栏、缩进代码和行内代码不计入。
    foreach ($target in (Get-LinkTargets $Text)) {
        $candidate = $target.Trim()
        if ((Test-ExternalSourceUrl $candidate) -and $seen.Add($candidate)) {
            $urls.Add($candidate) | Out-Null
        }
    }

    # 同时支持正文中的裸 URL，但仍沿用相同的非代码文本过滤。
    $linkText = Get-NonCodeMarkdownLinkText $Text
    foreach ($match in [regex]::Matches($linkText, '(?i)\bhttps?://[^\s<>()\[\]]+')) {
        $candidate = $match.Value.Trim()
        $candidate = [regex]::Replace($candidate, '[.,;:!?，。；：！？]+$', '')
        if ((Test-ExternalSourceUrl $candidate) -and $seen.Add($candidate)) {
            $urls.Add($candidate) | Out-Null
        }
    }
    return $urls
}

function Get-LinkTargets([string]$Text) {
    $targets = [Collections.Generic.List[string]]::new()
    foreach ($line in ((Get-NonCodeMarkdownLinkText $Text) -split "`r?`n")) {
        $pattern = '(?<!\!)\[[^\]]*\]\((?:<(?<angle>[^>]+)>|(?<plain>[^)\s]+))'
        foreach ($match in [regex]::Matches($line, $pattern)) {
            if ($match.Groups['angle'].Success) { $targets.Add($match.Groups['angle'].Value) }
            else { $targets.Add($match.Groups['plain'].Value) }
        }
    }
    return $targets
}

function Assert-ContainedEvidencePath([string]$Path, [string]$BasePath) {
    $full = [IO.Path]::GetFullPath($Path)
    $base = [IO.Path]::GetFullPath($BasePath).TrimEnd('\', '/')
    # Case-sensitive on Unix; a differently cased sibling is not the selected root.
    $comparison = if ([IO.Path]::DirectorySeparatorChar -eq '/') { [StringComparison]::Ordinal } else { [StringComparison]::OrdinalIgnoreCase }
    if (-not $full.Equals($base, $comparison) -and -not $full.StartsWith($base + [IO.Path]::DirectorySeparatorChar, $comparison)) {
        throw '路径越界'
    }
    $cursor = $full
    while ($cursor) {
        $item = Get-Item -LiteralPath $cursor -Force -ErrorAction SilentlyContinue
        if ($null -ne $item -and ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw '符号链接/重解析点' }
        # Include the root and its ancestors; aliased roots are not safe boundaries.
        $parent = [IO.Path]::GetDirectoryName($cursor)
        if ($parent -eq $cursor) { break }
        $cursor = $parent
    }
    return $full
}

# Changing this pin is a separate authorized baseline change and requires
# independent review with the JSON. No CLI override or automatic refresh exists.
$preservedSourceBaselineSha256 = '197b656e10dc2b1ad275c87af298331b990e5e97aebf4f9b1e67ec2194393a5d'
$preservedSourceDefects = [Collections.Generic.Dictionary[string,object]]::new([StringComparer]::OrdinalIgnoreCase)
$knownSourceDefects = [Collections.Generic.List[string]]::new()

function Get-BytesSha256([byte[]]$Bytes) {
    $hasher = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($hasher.ComputeHash($Bytes))).Replace('-', '').ToLowerInvariant() }
    finally { $hasher.Dispose() }
}

function Initialize-PreservedSourceDefects {
    $baselinePath = Join-Path $rootPath '.kb/preserved-source-defects.json'
    $item = Get-Item -LiteralPath $baselinePath -Force -ErrorAction SilentlyContinue
    if ($null -eq $item) { return }
    try {
        $null = Assert-ContainedEvidencePath $baselinePath $rootPath
        if (-not (Test-Path -LiteralPath $baselinePath -PathType Leaf)) { throw 'baseline must be a regular file' }
        $baselineBytes = [IO.File]::ReadAllBytes($baselinePath)
        if ((Get-BytesSha256 $baselineBytes) -cne $preservedSourceBaselineSha256) { throw 'baseline fingerprint differs from the independently approved pin' }
        $baseline = ConvertTo-KnowledgeMapValue ($utf8Strict.GetString($baselineBytes) | ConvertFrom-Json)
        if ($baseline -isnot [Collections.IDictionary] -or
            ((@($baseline.Keys | Sort-Object) -join '|') -cne 'baseline_commit|defects|version')) { throw 'unexpected baseline fields' }
        if (($baseline.version -isnot [int] -and $baseline.version -isnot [long]) -or $baseline.version -ne 1) { throw 'baseline version must be integer 1' }
        if ($baseline.baseline_commit -isnot [string] -or $baseline.baseline_commit -cne '21b44e8fe732ad76be02024dfa4732238c2443e2') { throw 'unexpected baseline commit' }
        if ($baseline.defects -isnot [array] -or $baseline.defects.Count -eq 0) { throw 'defects must be a nonempty array' }
        $visible = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
        foreach ($file in $mdFiles) { [void]$visible.Add((Get-QualityKey $file.FullName)) }
        foreach ($defect in $baseline.defects) {
            if ($defect -isnot [Collections.IDictionary] -or
                ((@($defect.Keys | Sort-Object) -join '|') -cne 'kind|opening_line|path|sha256')) { throw 'unexpected defect fields' }
            if ($defect.path -isnot [string] -or -not $defect.path.StartsWith('读书笔记/', [StringComparison]::Ordinal) -or
                $defect.path -cne $defect.path.Normalize([Text.NormalizationForm]::FormC)) { throw 'defect path must be a normalized preserved-source path' }
            $full = ConvertTo-KnowledgePath $defect.path
            $key = Get-QualityKey $full
            if (-not $visible.Contains($key)) { throw "orphan baseline: file not in Git-visible Markdown scope: $($defect.path)" }
            if ($preservedSourceDefects.ContainsKey($key)) { throw "duplicate baseline path: $($defect.path)" }
            if ($defect.sha256 -isnot [string] -or $defect.sha256 -cnotmatch '^[a-f0-9]{64}$') { throw 'invalid exact-byte sha256' }
            if ($defect.kind -isnot [string] -or $defect.kind -cne 'unclosed_code_fence') { throw 'unsupported defect kind' }
            if (($defect.opening_line -isnot [int] -and $defect.opening_line -isnot [long]) -or $defect.opening_line -le 0) { throw 'opening_line must be a positive integer' }
            $preservedSourceDefects.Add($key, [pscustomobject]@{
                Path = $defect.path; Sha256 = $defect.sha256; Kind = $defect.kind
                OpeningLine = $defect.opening_line; Observed = $false
            })
        }
    } catch {
        Add-Failure "保护来源缺陷基线无效: $($_.Exception.Message)"
        $preservedSourceDefects.Clear()
    }
}

function Get-UeEvidenceReferences([string]$Text) {
    $seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    $references = [Collections.Generic.List[object]]::new()
    $qualityText = Get-NonCodeMarkdownText $Text
    foreach ($line in ($qualityText -split "`r?`n")) {
        if ($line -match '^\s{4,}') { continue }
        $candidates = [Collections.Generic.List[string]]::new()
        foreach ($match in [regex]::Matches($line, '(?<!`)(`+)(?!`)(.*?)(?<!`)\1(?!`)')) { $candidates.Add($match.Groups[2].Value) }
        $bareLine = [regex]::Replace($line, '(?<!`)(`+)(?!`)(.*?)(?<!`)\1(?!`)', '')
        # Bare paths must have no whitespace. Consume the WHOLE token, including
        # Unicode and punctuation, so an unsupported suffix cannot become a
        # successful check of an existing parent directory. Quote spaces inline.
        foreach ($match in [regex]::Matches($bareLine, '(?i)(?<![\p{L}\p{N}_/:\\])(?:[A-Za-z]:[/\\]|/)[^\s`]*[/\\]Engine[/\\](?:Source|Plugins|Build)(?:[/\\][^\s`]*|(?=$|[\s`]))')) {
            $candidates.Add($match.Value)
        }
        foreach ($match in [regex]::Matches($bareLine, '(?i)(?<![#\p{L}\p{N}_./\\-])Engine[/\\](?:Source|Plugins|Build)(?:[/\\][^\s`]*|(?=$|[\s`]))')) {
            $candidates.Add($match.Value)
        }
        foreach ($candidate in $candidates) {
            $value = $candidate.Trim().Replace('\', '/')
            while ($value.Contains('//')) { $value = $value.Replace('//', '/') }
            $value = [regex]::Replace($value, ':\d+(?:[-~]\d+)?$', '')
            if ($value -notmatch '(?i)(?:^|/)Engine/(?:Source|Plugins|Build)(?:/|$)') { continue }
            if (-not $seen.Add($value)) { continue }
            $isRelative = $value -match '^Engine/'
            $isTemplate = $value -match '(?:^|/)\.{3}(?:/|$)|[<>*?]'
            $isFile = $value -match '(?i)\.(?:h|hpp|hxx|c|cc|cpp|cxx|inl|cs|usf|ush|uplugin|uproject|version|json|ini)$'
            $references.Add([pscustomobject]@{ Value = $value; Relative = $isRelative; Template = $isTemplate; File = $isFile })
        }
    }
    return @($references)
}

function Resolve-LocalTarget([string]$SourceFile, [string]$Target) {
    $decoded = [System.Uri]::UnescapeDataString($Target)
    $pathPart = $decoded.Split('#', 2)[0].Split('?', 2)[0]
    if ([string]::IsNullOrWhiteSpace($pathPart)) { return $null }
    $pathPart = $pathPart.Replace('/', [System.IO.Path]::DirectorySeparatorChar)
    $sourceDir = Split-Path -Parent $SourceFile
    return [System.IO.Path]::GetFullPath((Join-Path $sourceDir $pathPart))
}

$mdFiles = @(& (Join-Path $PSScriptRoot 'get_kb_markdown.ps1') -Root $rootPath)
if ($mdFiles.Count -eq 0) { Add-Failure '没有发现 Markdown 文件' }
Initialize-QualityMapping
Initialize-PreservedSourceDefects

$linkedByFile = @{}
$textByFile = @{}
$fileCount = 0
$readmeCount = 0
$bodyCount = 0

foreach ($file in $mdFiles) {
    $fileCount++
    if ($file.Name -eq 'README.md') { $readmeCount++ } else { $bodyCount++ }
    $relative = Get-RepoRelative $file.FullName
    $bytes = [System.IO.File]::ReadAllBytes($file.FullName)

    if ($bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF) {
        Add-Failure "BOM: $relative"
    }

    try {
        $text = $utf8Strict.GetString($bytes)
    } catch {
        Add-Failure "UTF-8 解码失败: $relative"
        continue
    }
    if ($text.Contains([char]0xFFFD)) { Add-Failure "替换字符 U+FFFD: $relative" }
    $textByFile[$file.FullName] = $text

    $knownKey = Get-QualityKey $file.FullName
    $knownDefect = if ($preservedSourceDefects.ContainsKey($knownKey)) { $preservedSourceDefects[$knownKey] } else { $null }
    $knownBytesMatch = $null -ne $knownDefect -and (Get-BytesSha256 $bytes) -ceq $knownDefect.Sha256
    if ($null -ne $knownDefect -and -not $knownBytesMatch) {
        Add-Failure "保护来源缺陷基线字节变化（须重新授权审核，不自动更新）: $relative"
    }
    $markdownScan = Get-NonCodeMarkdownText $text -WithFenceState
    foreach ($fenceLine in $markdownScan.UnclosedFences) {
        if ($null -ne $knownDefect -and $fenceLine -eq $knownDefect.OpeningLine) {
            $knownDefect.Observed = $true
            if ($knownBytesMatch) {
                $knownSourceDefects.Add("$($knownDefect.Kind)；$($knownDefect.Path):$fenceLine；sha256=$($knownDefect.Sha256)")
                continue
            }
        }
        Add-Failure "未闭合代码围栏（第 $fenceLine 行）: $relative"
    }

    $linked = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
    foreach ($target in (Get-LinkTargets $text)) {
        if ($target -match '^(?:[A-Za-z][A-Za-z0-9+.-]*:|//)') { continue }
        if ($target.StartsWith('#')) { continue }
        try {
            $resolved = Resolve-LocalTarget $file.FullName $target
        } catch {
            Add-Failure "链接路径无法解析 [$target]: $relative"
            continue
        }
        if ($null -eq $resolved) { continue }
        try { $resolved = Assert-ContainedEvidencePath $resolved $rootPath } catch {
            Add-Failure "链接路径不安全 [$target]（$($_.Exception.Message)）: $relative"
            continue
        }
        if (-not (Test-Path -LiteralPath $resolved)) {
            Add-Failure "断链 [$target] -> $(Get-RepoRelative $resolved): $relative"
        } else {
            $linked.Add($resolved) | Out-Null
        }
    }
    $linkedByFile[$file.FullName] = $linked
}

foreach ($defect in $preservedSourceDefects.Values) {
    if (-not $defect.Observed) { Add-Failure "保护来源缺陷基线孤儿（当前未检测到指定缺陷）: $($defect.Path):$($defect.OpeningLine)" }
}

$scopedPaths = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
$directoryFiles = @{}
$directoryChildren = @{}
$directorySet = [Collections.Generic.SortedSet[string]]::new([StringComparer]::Ordinal)
[void]$directorySet.Add($rootPath)
foreach ($file in $mdFiles) {
    [void]$scopedPaths.Add($file.FullName)
    $dir = $file.DirectoryName
    if (-not $directoryFiles.ContainsKey($dir)) { $directoryFiles[$dir] = [Collections.Generic.List[IO.FileInfo]]::new() }
    $directoryFiles[$dir].Add($file)
    while ($dir -ne $rootPath) {
        [void]$directorySet.Add($dir)
        $parent = [IO.Path]::GetDirectoryName($dir)
        if (-not $directoryChildren.ContainsKey($parent)) { $directoryChildren[$parent] = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase) }
        [void]$directoryChildren[$parent].Add($dir)
        $dir = $parent
    }
}
$allDirs = @($directorySet)
foreach ($dir in $allDirs) {
    if (Test-MaintenancePath $dir) { continue }
    $immediateMd = @($directoryFiles[$dir] | Where-Object { $null -ne $_ })
    $readme = Join-Path $dir 'README.md'
    if ($dir -ne $rootPath -and $immediateMd.Count -gt 0 -and -not ($scopedPaths.Contains($readme))) {
        Add-Failure "含 Markdown 的目录缺 README.md: $(Get-RepoRelative $dir)"
        continue
    }
    if (-not ($scopedPaths.Contains($readme))) { continue }
    $readmeLinks = $linkedByFile[$readme]
    if ($null -eq $readmeLinks) { $readmeLinks = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase) }
    foreach ($body in ($immediateMd | Where-Object { $_.Name -ne 'README.md' })) {
        if (-not $readmeLinks.Contains($body.FullName)) {
            Add-Failure "README 文件清单缺少链接: $(Get-RepoRelative $body.FullName)（目录 $(Get-RepoRelative $dir)）"
        }
    }
    foreach ($child in (@($directoryChildren[$dir]) | Where-Object { $null -ne $_ })) {
        if (Test-MaintenancePath $child) { continue }
        $childReadme = Join-Path $child 'README.md'
        if (($scopedPaths.Contains($childReadme)) -and -not $readmeLinks.Contains($childReadme)) {
            Add-Failure "上级 README 缺少子目录导航: $(Get-RepoRelative $childReadme)（上级 $(Get-RepoRelative $dir)）"
        }
    }
}

$rootReadme = Join-Path $rootPath 'README.md'
if ($scopedPaths.Contains($rootReadme)) {
    $rootLinks = $linkedByFile[$rootReadme]
    foreach ($top in (@($directoryChildren[$rootPath]) | Where-Object { $null -ne $_ })) {
        if (Test-MaintenancePath $top) { continue }
        $topReadme = Join-Path $top 'README.md'
        if (($scopedPaths.Contains($topReadme)) -and -not $rootLinks.Contains($topReadme)) {
            Add-Failure "根 README 缺少顶层导航: $(Get-RepoRelative $topReadme)"
        }
    }
}

# 兼容质量规则沿当前/legacy 路径保留原 UE 正文范围；UE 技术栈标签正文不论有无 legacy 都同样受检。
# 路径身份仅用于规则覆盖，真实读取和链接解析始终使用当前 canonical 文件。
$gameKnowledgeRoot = Join-Path $rootPath '游戏知识'
$sourceAnalysisRoot = Join-Path $gameKnowledgeRoot '12-引擎源码分析'
# Keep the lexical join safe even when a caller supplies a foreign provider path.
$ueEngineRoot = [IO.Path]::Combine($ueInstallRoot, 'Engine')
$qualityVersionMissing = 0
$qualityDateMissing = 0
$qualityOfficialLinkMissing = 0
$qualitySourceClaimInvalid = 0
$ueEvidenceStats = @{ Checked = 0; Unchecked = 0; Templates = 0; Foreign = 0 }
$ueRootReady = $false
$ueRootRequested = -not [string]::IsNullOrWhiteSpace($ueInstallRoot)
if ($ueRootRequested) {
    try {
        if (-not [IO.Path]::IsPathRooted($ueInstallRoot)) { throw '必须显式提供绝对 checkout 根' }
        $null = Assert-ContainedEvidencePath $ueEngineRoot $ueEngineRoot
        if (-not (Test-Path -LiteralPath $ueEngineRoot -PathType Container)) { throw 'Engine 目录不存在或不可访问' }
        $ueRootReady = $true
    } catch {
        Add-Failure "源码路径核对未运行（显式源码根无效）: $ueInstallRoot；$($_.Exception.Message)"
    }
}

$gameBodyFiles = @($mdFiles | Where-Object {
    $_.Name -ne 'README.md' -and ((Test-QualityPathUnder $_.FullName $gameKnowledgeRoot) -or (Test-UnrealTechnologyPath $_.FullName))
})
foreach ($file in $gameBodyFiles) {
    if (-not $textByFile.ContainsKey($file.FullName)) { continue }
    $relative = Get-RepoRelative $file.FullName
    $qualityText = Get-NonCodeMarkdownText $textByFile[$file.FullName]

    if ($qualityText -notmatch '版本基准|版本基线') {
        $qualityVersionMissing++
        Add-Failure "质量元数据缺少版本基准/版本基线: $relative"
    }
    if ($qualityText -notmatch '最后更新|更新日期|更新时间') {
        $qualityDateMissing++
        Add-Failure "质量元数据缺少最后更新/更新日期/更新时间: $relative"
    }
    $officialUrls = @(Get-ExternalSourceUrls $textByFile[$file.FullName] | Where-Object {
        $candidateUri = [Uri]$_
        $candidateUri.Scheme -eq 'https' -and $candidateUri.Host -eq 'dev.epicgames.com' -and
        ($candidateUri.AbsolutePath -eq '/documentation' -or $candidateUri.AbsolutePath.StartsWith('/documentation/'))
    })
    if ($officialUrls.Count -eq 0) {
        $qualityOfficialLinkMissing++
        Add-Failure "质量元数据缺少官方链接 https://dev.epicgames.com/documentation: $relative"
    }
}

$sourceBodyFiles = @($gameBodyFiles | Where-Object {
    ((Test-QualityPathUnder $_.FullName $sourceAnalysisRoot) -or (Test-UnrealTechnologyPath $_.FullName -SourceAnalysis)) -and
    -not (@(Get-QualityPaths $_.FullName | ForEach-Object { [IO.Path]::GetFileName($_) }) -contains '19-高优先级源码覆盖路线图.md')
})
foreach ($file in $sourceBodyFiles) {
    if (-not $textByFile.ContainsKey($file.FullName)) { continue }
    $relative = Get-RepoRelative $file.FullName
    $qualityText = Get-NonCodeMarkdownText $textByFile[$file.FullName]
    $evidenceReferences = @(Get-UeEvidenceReferences $textByFile[$file.FullName])
    # Check an explicit completion declaration, not isolated words such as the
    # API concept "预留" or an honest "待补充" limitation. This cannot judge prose truth.
    $completedSourceClaim = $qualityText -match '(?m)^\s*(?:>\s*)?(?:源码核对状态|源码验证状态)\s*[：:]\s*(?:已核对|已完成|已验证)'
    if ($completedSourceClaim -and @($evidenceReferences | Where-Object { $_.File -and -not $_.Template }).Count -eq 0) {
        $qualitySourceClaimInvalid++
        Add-Failure "源码完成声明缺少具体文件定位（占位路径不构成证据）: $relative"
    }
    foreach ($reference in $evidenceReferences) {
        $value = $reference.Value
        # Unsafe segments are rejected even when no source root is available.
        $decoded = [Uri]::UnescapeDataString($value)
        if ($decoded -match '(^|[/\\])\.{1,2}([/\\]|$)|[\x00-\x1f\x7f]') {
            Add-Failure "源码证据路径不安全（路径越界或控制字符）: $value（$relative）"
            continue
        }
        if ($reference.Template) { $ueEvidenceStats.Templates++; continue }
        if (-not $ueRootReady) { $ueEvidenceStats.Unchecked++; continue }
        if ($reference.Relative) {
            $suffix = $decoded.Substring('Engine/'.Length).Replace('/', [IO.Path]::DirectorySeparatorChar)
            $candidate = [IO.Path]::Combine($ueEngineRoot, $suffix)
        } else {
            # Never reinterpret another machine's absolute source label as this
            # checkout. Only native absolute paths within the explicit root apply.
            if (-not [IO.Path]::IsPathRooted($decoded) -or
                ([IO.Path]::DirectorySeparatorChar -eq '/' -and $decoded -match '^[A-Za-z]:/')) {
                $ueEvidenceStats.Foreign++; continue
            }
            $candidate = $decoded.Replace('/', [IO.Path]::DirectorySeparatorChar)
            $comparison = if ([IO.Path]::DirectorySeparatorChar -eq '/') { [StringComparison]::Ordinal } else { [StringComparison]::OrdinalIgnoreCase }
            $prefix = [IO.Path]::GetFullPath($ueEngineRoot).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
            if (-not [IO.Path]::GetFullPath($candidate).StartsWith($prefix, $comparison)) { $ueEvidenceStats.Foreign++; continue }
        }
        try { $candidate = Assert-ContainedEvidencePath $candidate $ueEngineRoot } catch {
            Add-Failure "源码证据路径不安全（$($_.Exception.Message)）: $value（$relative）"
            continue
        }
        $expectedType = if ($reference.File) { 'Leaf' } else { 'Any' }
        if (-not (Test-Path -LiteralPath $candidate -PathType $expectedType)) {
            Add-Failure "源码证据路径不存在: $value（$relative；实际根 $ueEngineRoot）"
        } else { $ueEvidenceStats.Checked++ }
    }
}
if ($sourceBodyFiles.Count -gt 0 -and -not $ueRootRequested) {
    Add-Warning '源码路径核对未运行：未提供 -UeInstallRoot；未确认源码版本、符号或运行行为'
}
if ($ueEvidenceStats.Foreign -gt 0) {
    Add-Warning "源码绝对位置未覆盖：$($ueEvidenceStats.Foreign) 个外部/历史位置不属于本次指定源码根；未自动重映射"
}
if ($ueEvidenceStats.Templates -gt 0) {
    Add-Warning "源码路径模板不作证据：$($ueEvidenceStats.Templates) 个省略/占位定位未检查存在性"
}

# 兼容 P2 质量规则按原路径身份覆盖迁移正文；它不是八域分类。
# 八域中未继承已知旧质量 profile 的正文采用通用 P2 规则；任意 alias 不产生豁免。
# README、围栏代码和维护目录仍不参与。
$domainDefinitions = @(
    [pscustomobject]@{ Name = '游戏AI'; Root = (Join-Path $rootPath '游戏AI') }
    [pscustomobject]@{ Name = '游戏服务端'; Root = (Join-Path $rootPath '游戏服务端') }
    [pscustomobject]@{ Name = '游戏算法'; Root = (Join-Path $rootPath '游戏算法') }
    [pscustomobject]@{ Name = '00-计算机与工程基础'; Root = (Join-Path $rootPath '00-计算机与工程基础') }
    [pscustomobject]@{ Name = '游戏测试与质量'; Root = (Join-Path $rootPath '游戏测试与质量') }
    [pscustomobject]@{ Name = '系统实战'; Root = (Join-Path $rootPath '系统实战') }
)
$domainDefinitions += [pscustomobject]@{ Name = '八域新增正文（通用质量规则）'; Root = (Join-Path $rootPath '知识'); NewOnly = $true }
$domainStats = @{}
$domainBodyFiles = @{}
$domainBaselinePattern = '(?m)^\s*(?:(?:>\s*)|(?:[-+*]\s*)|(?:\|\s*)|(?:#+\s*))*\s*(?:\*\*)?(?:知识基线|版本与规范基线|事实边界)(?:\s*\*\*)?\s*[：:](?:\s*\*\*)?\s*\S+'
$domainTableBaselinePattern = '(?m)^\s*\|\s*(?:知识基线|版本与规范基线|[^|\r\n]*事实边界)\s*\|'
$domainDatePattern = '(?m)^\s*(?:(?:>\s*)|(?:[-+*]\s*)|(?:\|\s*)|(?:#+\s*))*\s*(?:\*\*)?最后更新(?:\s*\*\*)?\s*[：:](?:\s*\*\*)?\s*\S+'
$domainTableDatePattern = '(?m)^\s*\|\s*最后更新\s*\|\s*\S+'
$domainValidationPattern = '验证与基准|验证建议|测试矩阵|基准测试|可复现|回放'
# 现有 P0 文章以“验收”标题作为验证入口；仍优先要求上面的明确关键词。
$domainValidationEntryPattern = '(?m)^\s*(?:#{1,6}\s+|>\s*|[-*+]\s+|\d+[.)]\s+|\|\s*)[^\r\n]*(?:验证与基准|验证建议|测试矩阵|基准测试|可复现|回放|验收)'
$legacyDomainPattern = '(?i)docs\.unrealengine\.com'
$rfc793Pattern = '(?i)(?<![A-Za-z0-9])RFC\s*793(?![A-Za-z0-9])'
$rfc9293Pattern = '(?i)(?<![A-Za-z0-9])RFC\s*9293(?![A-Za-z0-9])'
$rfcCurrentBaselinePattern = '(?is)(?:RFC\s*9293.{0,120}(?:取代|替代|当前基线|现行基线|当前规范|现行规范)|(?:当前基线|现行基线|当前规范|现行规范).{0,120}RFC\s*9293|RFC\s*793.{0,120}(?:取代|替代).{0,120}RFC\s*9293)'

foreach ($domain in $domainDefinitions) {
    $domainStats[$domain.Name] = @{
        BaselineMissing = 0
        DateMissing = 0
        SourceMissing = 0
        ValidationMissing = 0
        LegacyReferenceMissing = 0
    }
    $domainBodyFiles[$domain.Name] = @($mdFiles | Where-Object {
        $_.Name -ne 'README.md' -and
        $(if ($domain.NewOnly) { Test-NewKnowledgePath $_.FullName } else { Test-QualityPathUnder $_.FullName $domain.Root }) -and
        -not (Test-MaintenancePath $_.FullName)
    })
}

foreach ($domain in $domainDefinitions) {
    $stats = $domainStats[$domain.Name]
    foreach ($file in $domainBodyFiles[$domain.Name]) {
        if (-not $textByFile.ContainsKey($file.FullName)) { continue }
        $relative = Get-RepoRelative $file.FullName
        $qualityText = Get-NonCodeMarkdownText $textByFile[$file.FullName]

        if ($qualityText -notmatch $domainBaselinePattern -and
            $qualityText -notmatch $domainTableBaselinePattern) {
            $stats.BaselineMissing++
            Add-Failure "领域质量门禁缺少领域基线行（知识基线/版本与规范基线/事实边界）: $relative"
        }
        if ($qualityText -notmatch $domainDatePattern -and
            $qualityText -notmatch $domainTableDatePattern) {
            $stats.DateMissing++
            Add-Failure "领域质量门禁缺少文档元数据最后更新（最后更新：/最后更新:）: $relative"
        }

        $sourceUrls = @(Get-ExternalSourceUrls $textByFile[$file.FullName])
        if ($sourceUrls.Count -eq 0) {
            $stats.SourceMissing++
            Add-Failure "领域质量门禁缺少非代码外部来源 URL: $relative"
        }

        if ($qualityText -notmatch $domainValidationPattern -and
            $qualityText -notmatch $domainValidationEntryPattern) {
            $stats.ValidationMissing++
            Add-Failure "领域质量门禁缺少验证/可复现入口（验证与基准/验证建议/测试矩阵/基准测试/可复现/回放）: $relative"
        }

        $legacyIssue = $false
        $linkText = Get-NonCodeMarkdownLinkText $textByFile[$file.FullName]
        if ($linkText -match $legacyDomainPattern) {
            $legacyIssue = $true
            Add-Failure "领域质量门禁保留 docs.unrealengine.com 旧域名: $relative"
        }

        $qualityIdentity = (@(Get-QualityPaths $file.FullName | ForEach-Object { Get-RepoRelative $_ }) -join ' ')
        $isServiceOrNetworkDocument = ($domain.Name -eq '游戏服务端') -or
            ($qualityIdentity -match '(?i)服务端|网络|协议|通信|传输|TCP|UDP|HTTP|QUIC|WebSocket|RPC')
        if ($isServiceOrNetworkDocument -and $qualityText -match $rfc793Pattern) {
            $rfcReplacementAllowed = $false
            foreach ($paragraph in ($qualityText -split "`r?`n\s*`r?`n")) {
                if ($paragraph -match $rfc793Pattern -and
                    $paragraph -match $rfc9293Pattern -and
                    $paragraph -match $rfcCurrentBaselinePattern) {
                    $rfcReplacementAllowed = $true
                    break
                }
            }
            if (-not $rfcReplacementAllowed) {
                $legacyIssue = $true
                Add-Failure "领域质量门禁发现 RFC 793，但缺少 RFC 9293 已取代/当前基线说明: $relative"
            }
        }
        if ($legacyIssue) { $stats.LegacyReferenceMissing++ }

        # 系统实战链路额外要素：链路类型、依赖模块、失败路径、Benchmark/Evidence、验证矩阵。
        if ($domain.Name -eq '系统实战') {
            if ($qualityText -notmatch '链路类型|依赖模块|失败路径|Benchmark|Evidence|验证矩阵') {
                Add-Failure "系统实战门禁缺少链路要素（链路类型/依赖模块/失败路径/Benchmark/Evidence/验证矩阵）: $relative"
            }
        }
    }
}

# 知识成熟度门禁（W0-03）：阶段 B——既有正文与本次新增/修改正文缺成熟度均 FAIL；
# L3/L4/L5 关键词只检查最低证据入口，不证明实验已执行、语义真实或覆盖整篇。
# 不依据关键词自动标级；独立内容审查须复核实际链接、范围、输入/结果和原始记录。
# 豁免：README、维护目录（references/learning/scripts）、工作日志/笔记/方案（过程记录与规划）。
$changedFiles = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
$safeDirectoryArg = "safe.directory=$rootPath"
foreach ($cmd in @(
    @('diff', '--name-only'),
    @('diff', '--cached', '--name-only'),
    @('ls-files', '--others', '--exclude-standard'))) {
    # 仓库可能由不同 Windows 用户创建；仅对本次调用声明局部 safe.directory，绝不写全局配置。
    # ErrorActionPreference=Stop 会把原生 git 的普通 stderr（例如 CRLF 转换提示）
    # 提升为 terminating NativeCommandError；临时重定向 stderr，确保 stdout 与退出码独立。
    $stderrPath = [System.IO.Path]::GetTempFileName()
    try {
        $previousErrorActionPreference = $ErrorActionPreference
        $ErrorActionPreference = 'Continue'
        $gitOutput = @(& git -C $rootPath -c $safeDirectoryArg -c core.quotepath=false @cmd 2> $stderrPath)
        $gitExitCode = $LASTEXITCODE
        $ErrorActionPreference = $previousErrorActionPreference
        $gitStderr = if (Test-Path -LiteralPath $stderrPath) { Get-Content -LiteralPath $stderrPath -Raw } else { '' }
    } finally {
        if ($null -ne $previousErrorActionPreference) { $ErrorActionPreference = $previousErrorActionPreference }
        Remove-Item -LiteralPath $stderrPath -Force -ErrorAction SilentlyContinue
    }
    if ($gitExitCode -ne 0) {
        $detail = ((@($gitStderr) + @($gitOutput) | ForEach-Object { $_.ToString().Trim() } | Where-Object { $_ }) -join ' ')
        if ($detail.Length -gt 240) { $detail = $detail.Substring(0, 240) }
        Add-Warning "Git 变更扫描失败（退出码 $gitExitCode，命令 git $($cmd -join ' ')）: $detail"
        continue
    }
    foreach ($name in $gitOutput) {
        $nameText = $name.ToString().Trim()
        if (-not [string]::IsNullOrWhiteSpace($nameText)) {
            try {
                $changedFiles.Add([System.IO.Path]::GetFullPath((Join-Path $rootPath $nameText))) | Out-Null
            } catch {
                Add-Warning "Git 变更路径无法解析: $nameText"
            }
        }
    }
}

$maturityMissing = 0
$maturityMissingChanged = 0
$maturityStats = @{ L0 = 0; L1 = 0; L2 = 0; L3 = 0; L4 = 0; L5 = 0 }
foreach ($file in $mdFiles) {
    if ($file.Name -eq 'README.md') { continue }
    if (Test-MaintenancePath $file.FullName) { continue }
    $relative = Get-RepoRelative $file.FullName
    # 工作日志/笔记是过程记录与速查笔记，方案/是建设规划，读书笔记/是外部来源材料层（sources），均不参与知识成熟度门禁。
    if ($relative -like '工作日志\*' -or $relative -like '笔记\*' -or $relative -like '方案\*' -or $relative -like '读书笔记\*') { continue }
    if (-not $textByFile.ContainsKey($file.FullName)) { continue }
    $qualityText = Get-NonCodeMarkdownText $textByFile[$file.FullName]
    $m = [regex]::Match($qualityText, '知识成熟度\s*[：:]\s*L([0-5])')
    if (-not $m.Success) {
        if ($changedFiles.Contains($file.FullName)) {
            $maturityMissingChanged++
            Add-Failure "本次新增/修改正文缺少知识成熟度: $relative"
        } else {
            $maturityMissing++
            Add-Failure "既有正文缺少知识成熟度（阶段 B 门禁）: $relative"
        }
        continue
    }
    $level = [int]$m.Groups[1].Value
    $maturityStats["L$level"]++
    if ($level -ge 3 -and $qualityText -notmatch 'Evidence|证据|Demo|演示|实验|可运行') {
        Add-Failure "标注 L$level 但缺少 Evidence/Demo 入口: $relative"
    }
    if ($level -ge 4 -and $qualityText -notmatch '原始结果|results/|P50|P95|P99|验证矩阵|测试矩阵|压测数据|基准数据') {
        Add-Failure "标注 L$level 但缺少 Benchmark/Test 证据: $relative"
    }
    if ($level -ge 5 -and $qualityText -notmatch '工作日志|复盘|Postmortem|生产证据|线上事故|故障复盘') {
        Add-Failure "标注 L5 但缺少工作日志/项目复盘证据: $relative"
    }
}
if ($maturityMissing -gt 0 -or $maturityMissingChanged -gt 0) {
    Add-Warning "正文缺少知识成熟度：既有 $maturityMissing 篇、本次新增/修改 $maturityMissingChanged 篇（阶段 B：均 FAIL）"
}

# P3 UE Dedicated Server 专项门禁：检查十一篇已登记专题（四篇核心 + 七篇扩展）、质量门禁说明和网络同步旧路径。
# 这里的锚点检查只证明正文覆盖了必要概念，不等于实际执行了命令或通过了运行验收。
$dsStats = [ordered]@{
    RequiredFileMissing = 0
    SourceAnchorMissing = 0
    BuildAnchorMissing = 0
    PlatformAnchorMissing = 0
    TestAnchorMissing = 0
    ExtendedAnchorMissing = 0
    GateTextMissing = 0
    ActorChannelResidual = 0
}
$dsDocumentDefinitions = @(
    [pscustomobject]@{
        Name = '源码专题'
        Relative = '游戏知识\12-引擎源码分析\32-UE Dedicated Server启动与监听源码.md'
        Counter = 'SourceAnchorMissing'
        Anchors = @(
            [pscustomobject]@{ Label = 'UE_SERVER'; Patterns = @('UE_SERVER', 'Dedicated Server') }
            [pscustomobject]@{ Label = 'WITH_SERVER_CODE'; Patterns = @('WITH_SERVER_CODE', '服务端代码', '服务端规则', '服务器进程') }
            [pscustomobject]@{ Label = 'UWorld::Listen'; Patterns = @('UWorld::Listen') }
            # UE5.8 本机专题以 UIpNetDriver::InitListen 记录实际 IP 实现；作为该监听概念锚点的已核对别名。
            [pscustomobject]@{ Label = 'UNetDriver::InitListen'; Patterns = @('UNetDriver::InitListen', 'UIpNetDriver::InitListen') }
            [pscustomobject]@{ Label = 'TickDispatch'; Patterns = @('TickDispatch') }
            [pscustomobject]@{ Label = 'TickFlush'; Patterns = @('TickFlush') }
            [pscustomobject]@{ Label = 'PreLogin'; Patterns = @('PreLogin') }
            [pscustomobject]@{ Label = 'PostLogin'; Patterns = @('PostLogin') }
            # 源码专题已核对的验证入口使用 Resolve-Path/Test-NetConnection；保留 Test-Path 作为规范锚点并接受实际等价路径检查写法。
            [pscustomobject]@{ Label = 'Test-Path/路径验证'; Patterns = @('Test-Path', 'Resolve-Path', 'Test-NetConnection') }
            [pscustomobject]@{ Label = 'rg'; Patterns = @('rg\s+-') }
        )
    }
    [pscustomobject]@{
        Name = '构建专题'
        Relative = '游戏知识\08-工具链与打包发布\09-UE Dedicated Server构建烘焙与运行.md'
        Counter = 'BuildAnchorMissing'
        Anchors = @(
            [pscustomobject]@{ Label = 'TargetType.Server'; Patterns = @('TargetType\.Server') }
            [pscustomobject]@{ Label = 'BuildCookRun'; Patterns = @('BuildCookRun') }
            [pscustomobject]@{ Label = 'ServerDefaultMap'; Patterns = @('ServerDefaultMap') }
            [pscustomobject]@{ Label = 'serverconfig'; Patterns = @('serverconfig') }
            [pscustomobject]@{ Label = 'serverplatform'; Patterns = @('serverplatform', 'servertargetplatform') }
            [pscustomobject]@{ Label = 'iostore'; Patterns = @('iostore') }
            [pscustomobject]@{ Label = 'pak'; Patterns = @('pak') }
        )
    }
    [pscustomobject]@{
        Name = '平台专题'
        Relative = '游戏服务端\05-UE Dedicated Server平台化\01-UE Dedicated Server实例生命周期与平台化.md'
        Counter = 'PlatformAnchorMissing'
        Anchors = @(
            [pscustomobject]@{ Label = 'Booting'; Patterns = @('Booting') }
            [pscustomobject]@{ Label = 'Ready'; Patterns = @('Ready') }
            [pscustomobject]@{ Label = 'Allocated'; Patterns = @('Allocated') }
            [pscustomobject]@{ Label = 'Draining'; Patterns = @('Draining') }
            [pscustomobject]@{ Label = 'Terminating'; Patterns = @('Terminating') }
            [pscustomobject]@{ Label = 'instanceId'; Patterns = @('instanceId') }
            [pscustomobject]@{ Label = 'buildId'; Patterns = @('buildId') }
            [pscustomobject]@{ Label = 'sessionTicket'; Patterns = @('sessionTicket') }
            [pscustomobject]@{ Label = 'readiness'; Patterns = @('readiness') }
            [pscustomobject]@{ Label = 'heartbeat'; Patterns = @('heartbeat') }
            [pscustomobject]@{ Label = 'Agones'; Patterns = @('Agones') }
            [pscustomobject]@{ Label = 'GameLift'; Patterns = @('GameLift') }
        )
    }
    [pscustomobject]@{
        Name = '测试专题'
        Relative = '游戏测试与质量\06-UE Dedicated Server联机验收与Gauntlet.md'
        Counter = 'TestAnchorMissing'
        Anchors = @(
            [pscustomobject]@{ Label = 'Gauntlet'; Patterns = @('Gauntlet') }
            [pscustomobject]@{ Label = 'RunUnreal'; Patterns = @('RunUnreal') }
            [pscustomobject]@{ Label = 'Server/Client'; Patterns = @('Server', 'Client') }
            [pscustomobject]@{ Label = '网络仿真/Network Emulation'; Patterns = @('网络仿真', 'Network Emulation') }
            [pscustomobject]@{ Label = '丢包'; Patterns = @('丢包') }
            [pscustomobject]@{ Label = 'Soak/长稳'; Patterns = @('Soak', '长稳') }
            [pscustomobject]@{ Label = 'Trace/日志'; Patterns = @('Trace', '日志') }
        )
    }
)

# 七篇扩展专题：运行调优、内容裁剪、Linux 部署、会话重连、日志观测、机器人压测、UNetDriver 源码。
$dsExtendedDefinitions = @(
    [pscustomobject]@{
        Name = '运行调优专题'
        Relative = '游戏知识\08-工具链与打包发布\10-UE Dedicated Server运行参数与性能调优.md'
        Counter = 'ExtendedAnchorMissing'
        Anchors = @(
            [pscustomobject]@{ Label = 'NetServerMaxTickRate'; Patterns = @('NetServerMaxTickRate') }
            [pscustomobject]@{ Label = 'FixedFrameRate'; Patterns = @('bUseFixedFrameRate', 'FixedFrameRate') }
            [pscustomobject]@{ Label = 'MaxClientRate'; Patterns = @('MaxClientRate') }
            [pscustomobject]@{ Label = 'PktLag/PktLoss'; Patterns = @('PktLag', 'PktLoss') }
            [pscustomobject]@{ Label = 'DDoS'; Patterns = @('DDoS') }
        )
    }
    [pscustomobject]@{
        Name = '内容裁剪专题'
        Relative = '游戏知识\08-工具链与打包发布\11-DS内容裁剪与服务器资源预算.md'
        Counter = 'ExtendedAnchorMissing'
        Anchors = @(
            [pscustomobject]@{ Label = 'UE_SERVER'; Patterns = @('UE_SERVER') }
            [pscustomobject]@{ Label = 'Cook'; Patterns = @('Cook') }
            [pscustomobject]@{ Label = 'World Partition/Streaming'; Patterns = @('World Partition', 'Level Streaming') }
            [pscustomobject]@{ Label = '内存预算'; Patterns = @('内存预算') }
        )
    }
    [pscustomobject]@{
        Name = 'Linux部署专题'
        Relative = '游戏服务端\05-UE Dedicated Server平台化\02-Linux DS部署与容器实战.md'
        Counter = 'ExtendedAnchorMissing'
        Anchors = @(
            [pscustomobject]@{ Label = 'Linux'; Patterns = @('Linux') }
            [pscustomobject]@{ Label = 'systemd/容器'; Patterns = @('systemd', '容器', 'Docker') }
            [pscustomobject]@{ Label = 'SIGTERM/drain'; Patterns = @('SIGTERM', 'drain', '优雅关服') }
            [pscustomobject]@{ Label = '非root'; Patterns = @('非 root', '非root') }
            [pscustomobject]@{ Label = '崩溃/符号'; Patterns = @('core dump', '崩溃', '符号') }
        )
    }
    [pscustomobject]@{
        Name = '会话重连专题'
        Relative = '游戏服务端\05-UE Dedicated Server平台化\03-DS会话注册与重连实现.md'
        Counter = 'ExtendedAnchorMissing'
        Anchors = @(
            [pscustomobject]@{ Label = 'ConnectionTimeout'; Patterns = @('ConnectionTimeout', 'InitialConnectTimeout') }
            [pscustomobject]@{ Label = '重连窗口'; Patterns = @('重连窗口', '重连') }
            [pscustomobject]@{ Label = 'JIP'; Patterns = @('JIP') }
            [pscustomobject]@{ Label = 'ticket/票据'; Patterns = @('ticket', '票据') }
            [pscustomobject]@{ Label = '平台会话/游戏内会话'; Patterns = @('平台会话', '游戏内会话') }
        )
    }
    [pscustomobject]@{
        Name = '日志观测专题'
        Relative = '游戏服务端\05-UE Dedicated Server平台化\04-DS日志崩溃与可观测性实战.md'
        Counter = 'ExtendedAnchorMissing'
        Anchors = @(
            [pscustomobject]@{ Label = '脱敏'; Patterns = @('脱敏') }
            [pscustomobject]@{ Label = '符号化'; Patterns = @('符号化', '符号') }
            [pscustomobject]@{ Label = 'Trace'; Patterns = @('Trace') }
            [pscustomobject]@{ Label = 'SLO'; Patterns = @('SLO') }
            [pscustomobject]@{ Label = '告警'; Patterns = @('告警') }
        )
    }
    [pscustomobject]@{
        Name = '机器人压测专题'
        Relative = '游戏测试与质量\07-UE DS机器人压测与容量评估.md'
        Counter = 'ExtendedAnchorMissing'
        Anchors = @(
            [pscustomobject]@{ Label = '机器人/Bot'; Patterns = @('机器人', 'Bot') }
            [pscustomobject]@{ Label = '容量'; Patterns = @('容量') }
            [pscustomobject]@{ Label = '拐点'; Patterns = @('拐点') }
            [pscustomobject]@{ Label = '阶梯加压/Ramp'; Patterns = @('阶梯加压', 'Ramp') }
            [pscustomobject]@{ Label = '场景混合'; Patterns = @('场景混合', '行为') }
        )
    }
    [pscustomobject]@{
        Name = 'UNetDriver源码专题'
        Relative = '游戏知识\12-引擎源码分析\33-UNetDriver与连接通道源码.md'
        Counter = 'ExtendedAnchorMissing'
        Anchors = @(
            [pscustomobject]@{ Label = 'InitBase'; Patterns = @('UNetDriver::InitBase', 'InitBase') }
            [pscustomobject]@{ Label = 'ReceivedRawPacket'; Patterns = @('ReceivedRawPacket') }
            [pscustomobject]@{ Label = 'ReceivedBunch'; Patterns = @('UControlChannel::ReceivedBunch', 'ReceivedBunch') }
            [pscustomobject]@{ Label = 'ConnectionTimeout'; Patterns = @('ConnectionTimeout', 'InitialConnectTimeout') }
            [pscustomobject]@{ Label = 'ServerTravel'; Patterns = @('UWorld::ServerTravel', 'ServerTravel') }
        )
    }
)

foreach ($definition in @($dsDocumentDefinitions + $dsExtendedDefinitions)) {
    $fullPath = Resolve-QualityCanonical $definition.Relative
    if (-not (Test-Path -LiteralPath $fullPath -PathType Leaf)) {
        $dsStats.RequiredFileMissing++
        Add-Failure "DS 专项门禁必需文件缺失: $($definition.Relative)"
        continue
    }

    $documentText = $textByFile[$fullPath]
    foreach ($anchor in $definition.Anchors) {
        $matched = $false
        foreach ($pattern in $anchor.Patterns) {
            if ($documentText -match $pattern) {
                $matched = $true
                break
            }
        }
        if (-not $matched) {
            $dsStats[$definition.Counter]++
            Add-Failure "DS 专项门禁锚点缺失 [$($definition.Name)/$($anchor.Label)]: $($definition.Relative)"
        }
    }
}

$dsQualityGateRelative = '游戏知识\08-工具链与打包发布\08-全栈质量门禁与灰度回滚.md'
$dsQualityGatePath = Resolve-QualityCanonical $dsQualityGateRelative
if (-not (Test-Path -LiteralPath $dsQualityGatePath -PathType Leaf)) {
    $dsStats.RequiredFileMissing++
    Add-Failure "DS 专项门禁质量文档缺失: $dsQualityGateRelative"
} else {
    $dsQualityGateText = $textByFile[$dsQualityGatePath]
    if ($dsQualityGateText -notmatch '概念覆盖不等于执行验证' -or
        $dsQualityGateText -notmatch '占位命令') {
        $dsStats.GateTextMissing++
        Add-Failure "DS 专项门禁说明缺少概念/执行验证边界: $dsQualityGateRelative"
    }
}

$networkSyncRoot = Join-Path $gameKnowledgeRoot '06-网络同步'
# The historical directory may no longer exist after migration; identity still applies.
& {
    $networkSyncFiles = @($mdFiles | Where-Object { Test-QualityPathUnder $_.FullName $networkSyncRoot })
    foreach ($file in $networkSyncFiles) {
        if ($textByFile[$file.FullName] -match '(?i)ActorChannel\.cpp') {
            $dsStats.ActorChannelResidual++
            Add-Failure "DS 专项门禁发现网络同步旧路径 ActorChannel.cpp: $(Get-RepoRelative $file.FullName)"
        }
    }
}

Write-Host "Markdown: $fileCount（正文 $bodyCount，README $readmeCount）"
Write-Host '质量检查边界：机械 lint 不判定教学正确性、证据真实性或整篇成熟度；需独立内容审查'
Write-Host "源码路径存在性：已检查 $($ueEvidenceStats.Checked)、未检查 $($ueEvidenceStats.Unchecked)、外部绝对位置 $($ueEvidenceStats.Foreign)、路径模板 $($ueEvidenceStats.Templates)；不验证版本/符号/运行行为"
Write-Host '兼容质量规则覆盖（旧路径身份，不代表八域分类）：'
foreach ($domainName in @('00-计算机与工程基础', '游戏知识', '游戏服务端', '游戏算法', '游戏AI', '游戏测试与质量', '系统实战')) {
    $domainRoot = Join-Path $rootPath $domainName
    $domainFiles = @($mdFiles | Where-Object { Test-QualityPathUnder $_.FullName $domainRoot })
    $domainBody = @($domainFiles | Where-Object { $_.Name -ne 'README.md' }).Count
    $domainReadme = $domainFiles.Count - $domainBody
    Write-Host "  $domainName：正文 $domainBody / README $domainReadme"
}
Write-Host "成熟度分布：L0 $($maturityStats.L0)、L1 $($maturityStats.L1)、L2 $($maturityStats.L2)、L3 $($maturityStats.L3)、L4 $($maturityStats.L4)、L5 $($maturityStats.L5)"
Write-Host "PASS: $($passes.Count + 1) 项基础检查已执行"
Write-Host "质量元数据：版本缺失 $qualityVersionMissing、日期缺失 $qualityDateMissing、官方链接缺失 $qualityOfficialLinkMissing、源码完成声明无定位 $qualitySourceClaimInvalid"
$domainTotals = @{
    BaselineMissing = 0
    DateMissing = 0
    SourceMissing = 0
    ValidationMissing = 0
    LegacyReferenceMissing = 0
}
$domainMetricKeys = @('BaselineMissing', 'DateMissing', 'SourceMissing', 'ValidationMissing', 'LegacyReferenceMissing')
$domainBodyTotal = 0
Write-Host '兼容及新增正文质量门禁统计：'
foreach ($domain in $domainDefinitions) {
    $stats = $domainStats[$domain.Name]
    $domainBodyTotal += $domainBodyFiles[$domain.Name].Count
    foreach ($metric in $domainMetricKeys) { $domainTotals[$metric] += $stats[$metric] }
    Write-Host "$($domain.Name)：领域基线缺失 $($stats.BaselineMissing)、日期缺失 $($stats.DateMissing)、来源缺失 $($stats.SourceMissing)、验证入口缺失 $($stats.ValidationMissing)、旧规范引用缺失 $($stats.LegacyReferenceMissing)"
}
Write-Host "领域质量门禁合计（正文 $domainBodyTotal）：领域基线缺失 $($domainTotals.BaselineMissing)、日期缺失 $($domainTotals.DateMissing)、来源缺失 $($domainTotals.SourceMissing)、验证入口缺失 $($domainTotals.ValidationMissing)、旧规范引用缺失 $($domainTotals.LegacyReferenceMissing)"
Write-Host "DS 专项门禁统计：必需文件缺失 $($dsStats.RequiredFileMissing)、源码锚点缺失 $($dsStats.SourceAnchorMissing)、构建锚点缺失 $($dsStats.BuildAnchorMissing)、平台锚点缺失 $($dsStats.PlatformAnchorMissing)、测试锚点缺失 $($dsStats.TestAnchorMissing)、扩展锚点缺失 $($dsStats.ExtendedAnchorMissing)、门禁说明缺失 $($dsStats.GateTextMissing)、ActorChannel.cpp 旧路径残留 $($dsStats.ActorChannelResidual)"
if ($warnings.Count -gt 0) {
    Write-Host "WARN: $($warnings.Count)"
    $warnings | ForEach-Object { Write-Host "WARN $_" }
} else {
    Write-Host 'WARN: 0'
}
Write-Host "KNOWN_SOURCE_DEFECT: $($knownSourceDefects.Count)（保留原件的已知欠账，不代表缺陷已修复）"
$knownSourceDefects | ForEach-Object { Write-Host "KNOWN_SOURCE_DEFECT $_" }
if ($failures.Count -gt 0) {
    Write-Host "FAIL: $($failures.Count)"
    $failures | ForEach-Object { Write-Host "FAIL $_" }
    Write-Host 'RESULT: FAIL'
    exit 1
}
if ($knownSourceDefects.Count -gt 0) {
    Write-Host 'UNEXPECTED_FAILURES: 0'
    Write-Host "RESULT: PASS_WITH_KNOWN_SOURCE_DEFECTS（已知来源缺陷 $($knownSourceDefects.Count)；不是零缺陷或全质量通过）"
} else {
    Write-Host 'FAIL: 0'
    Write-Host 'RESULT: PASS'
}
exit 0
