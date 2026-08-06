[CmdletBinding()]
param(
    [string]$Root = 'C:\project\git'
)

$ErrorActionPreference = 'Stop'
$rootPath = (Resolve-Path -LiteralPath $Root).Path.TrimEnd('\')
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
    $prefix = $rootPath + '\'
    if ($full.StartsWith($prefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        return $full.Substring($prefix.Length)
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
    $fullPath = [System.IO.Path]::GetFullPath($Path).TrimEnd('\')
    $fullBase = [System.IO.Path]::GetFullPath($BasePath).TrimEnd('\')
    return $fullPath -eq $fullBase -or $fullPath.StartsWith($fullBase + '\', [System.StringComparison]::OrdinalIgnoreCase)
}

function Get-NonCodeMarkdownText([string]$Text) {
    $kept = [System.Collections.Generic.List[string]]::new()
    $inFence = $false
    $fenceChar = ''
    foreach ($line in ($Text -split "`r?`n")) {
        if ($line -match '^\s*(```|~~~)') {
            $markerChar = $Matches[1].Substring(0, 1)
            if (-not $inFence) {
                $inFence = $true
                $fenceChar = $markerChar
            } elseif ($fenceChar -eq $markerChar) {
                $inFence = $false
                $fenceChar = ''
            }
            continue
        }
        if (-not $inFence) { $kept.Add($line) }
    }
    return ($kept -join "`n")
}

function Get-NonCodeMarkdownLinkText([string]$Text) {
    $kept = [System.Collections.Generic.List[string]]::new()
    $nonCode = Get-NonCodeMarkdownText $Text
    foreach ($line in ($nonCode -split "`r?`n")) {
        # 与 README 链接解析保持一致：缩进代码块和行内代码不作为来源文本。
        if ($line -match '^\s{4,}') { continue }
        $kept.Add([regex]::Replace($line, '`[^`]*`', ''))
    }
    return ($kept -join "`n")
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
        if (Test-ExternalSourceUrl $candidate -and $seen.Add($candidate)) {
            $urls.Add($candidate) | Out-Null
        }
    }

    # 同时支持正文中的裸 URL，但仍沿用相同的非代码文本过滤。
    $linkText = Get-NonCodeMarkdownLinkText $Text
    foreach ($match in [regex]::Matches($linkText, '(?i)\bhttps?://[^\s<>()\[\]]+')) {
        $candidate = $match.Value.Trim()
        $candidate = [regex]::Replace($candidate, '[.,;:!?，。；：！？]+$', '')
        if (Test-ExternalSourceUrl $candidate -and $seen.Add($candidate)) {
            $urls.Add($candidate) | Out-Null
        }
    }
    return $urls
}

function Get-LinkTargets([string]$Text) {
    $targets = [System.Collections.Generic.List[string]]::new()
    $inFence = $false
    $fenceChar = ''
    $lines = $Text -split "`r?`n"
    foreach ($line in $lines) {
        if ($line -match '^\s*(```|~~~)') {
            $marker = $Matches[1]
            $markerChar = $marker.Substring(0, 1)
            if (-not $inFence) {
                $inFence = $true
                $fenceChar = $markerChar
            } elseif ($fenceChar -eq $markerChar) {
                $inFence = $false
                $fenceChar = ''
            }
            continue
        }
        if ($inFence) { continue }
        # Indented code blocks and inline-code spans are not Markdown links.
        if ($line -match '^\s{4,}') { continue }
        $line = [regex]::Replace($line, '`[^`]*`', '')

        $pattern = '(?<!\!)\[[^\]]*\]\((?:<(?<angle>[^>]+)>|(?<plain>[^)\s]+))'
        foreach ($match in [regex]::Matches($line, $pattern)) {
            if ($match.Groups['angle'].Success) {
                $targets.Add($match.Groups['angle'].Value)
            } else {
                $targets.Add($match.Groups['plain'].Value)
            }
        }
    }
    return $targets
}

function Resolve-LocalTarget([string]$SourceFile, [string]$Target) {
    $decoded = [System.Uri]::UnescapeDataString($Target)
    $pathPart = $decoded.Split('#', 2)[0].Split('?', 2)[0]
    if ([string]::IsNullOrWhiteSpace($pathPart)) { return $null }
    $pathPart = $pathPart.Replace('/', [System.IO.Path]::DirectorySeparatorChar)
    $sourceDir = Split-Path -Parent $SourceFile
    return [System.IO.Path]::GetFullPath((Join-Path $sourceDir $pathPart))
}

$mdFiles = @(Get-ChildItem -LiteralPath $rootPath -Recurse -File -Filter '*.md' |
    Where-Object { $_.FullName -notmatch '\\.git(\\|$)' })
if ($mdFiles.Count -eq 0) { Add-Failure '没有发现 Markdown 文件' }

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

    $inFence = $false
    $fenceChar = ''
    $fenceLine = 0
    $lineNumber = 0
    foreach ($line in ($text -split "`r?`n")) {
        $lineNumber++
        if ($line -match '^\s*(```|~~~)') {
            $markerChar = $Matches[1].Substring(0, 1)
            if (-not $inFence) {
                $inFence = $true
                $fenceChar = $markerChar
                $fenceLine = $lineNumber
            } elseif ($fenceChar -eq $markerChar) {
                $inFence = $false
                $fenceChar = ''
                $fenceLine = 0
            }
        }
    }
    if ($inFence) { Add-Failure "未闭合代码围栏（第 $fenceLine 行）: $relative" }

    if ($file.Name -ne 'README.md' -and -not (Test-MaintenancePath $file.FullName)) {
        $lineCount = @($text -split "`r?`n").Count
        if ($lineCount -lt 300) { Add-Warning "正文少于 300 行（$lineCount 行）: $relative" }
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
        if (-not (Test-Path -LiteralPath $resolved)) {
            Add-Failure "断链 [$target] -> $(Get-RepoRelative $resolved): $relative"
        } else {
            $linked.Add($resolved) | Out-Null
        }
    }
    $linkedByFile[$file.FullName] = $linked
}

$allDirs = @($rootPath) + @(Get-ChildItem -LiteralPath $rootPath -Recurse -Directory |
    Where-Object { $_.FullName -notmatch '\\.git(\\|$)' } | ForEach-Object { $_.FullName })
foreach ($dir in $allDirs) {
    if (Test-MaintenancePath $dir) { continue }
    $immediateMd = @(Get-ChildItem -LiteralPath $dir -File -Filter '*.md')
    $readme = Join-Path $dir 'README.md'
    if ($dir -ne $rootPath -and $immediateMd.Count -gt 0 -and -not (Test-Path -LiteralPath $readme -PathType Leaf)) {
        Add-Failure "含 Markdown 的目录缺 README.md: $(Get-RepoRelative $dir)"
        continue
    }
    if (-not (Test-Path -LiteralPath $readme -PathType Leaf)) { continue }
    $readmeLinks = $linkedByFile[$readme]
    if ($null -eq $readmeLinks) { $readmeLinks = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase) }
    foreach ($body in ($immediateMd | Where-Object { $_.Name -ne 'README.md' })) {
        if (-not $readmeLinks.Contains($body.FullName)) {
            Add-Failure "README 文件清单缺少链接: $(Get-RepoRelative $body.FullName)（目录 $(Get-RepoRelative $dir)）"
        }
    }
    foreach ($child in (Get-ChildItem -LiteralPath $dir -Directory)) {
        if (Test-MaintenancePath $child.FullName) { continue }
        $childReadme = Join-Path $child.FullName 'README.md'
        if ((Test-Path -LiteralPath $childReadme -PathType Leaf) -and -not $readmeLinks.Contains($childReadme)) {
            Add-Failure "上级 README 缺少子目录导航: $(Get-RepoRelative $childReadme)（上级 $(Get-RepoRelative $dir)）"
        }
    }
}

$rootReadme = Join-Path $rootPath 'README.md'
if (Test-Path -LiteralPath $rootReadme -PathType Leaf) {
    $rootLinks = $linkedByFile[$rootReadme]
    foreach ($top in (Get-ChildItem -LiteralPath $rootPath -Directory)) {
        if (Test-MaintenancePath $top.FullName) { continue }
        $topReadme = Join-Path $top.FullName 'README.md'
        if ((Test-Path -LiteralPath $topReadme -PathType Leaf) -and -not $rootLinks.Contains($topReadme)) {
            Add-Failure "根 README 缺少顶层导航: $(Get-RepoRelative $topReadme)"
        }
    }
}

# 语义质量门禁只作用于游戏知识正文；路线图、维护日志和代码示意不参与源码质量判定。
$gameKnowledgeRoot = Join-Path $rootPath '游戏知识'
$sourceAnalysisRoot = Join-Path $gameKnowledgeRoot '12-引擎源码分析'
$ueInstallRoot = 'C:\Program Files\Epic Games\UE_5.8'
$ueEngineRoot = Join-Path $ueInstallRoot 'Engine'
$absoluteEvidencePattern = '(?i)(?<![#A-Za-z0-9])C:\\+Program Files\\+Epic Games\\+UE_5\.8\\+Engine(?:\\+[A-Za-z0-9_+.\-]+)*'
$relativeEvidencePattern = '(?i)(?<![#A-Za-z0-9_./-])Engine/(?:Source|Plugins)(?:/[A-Za-z0-9_+.\-]+)*(?:/)?'
$qualityVersionMissing = 0
$qualityDateMissing = 0
$qualityOfficialLinkMissing = 0
$qualitySourcePlaceholder = 0
$uePathWarningEmitted = $false

$gameBodyFiles = @($mdFiles | Where-Object {
    $_.Name -ne 'README.md' -and (Test-PathUnder $_.FullName $gameKnowledgeRoot)
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
    if ($qualityText -notmatch 'https://dev\.epicgames\.com/documentation') {
        $qualityOfficialLinkMissing++
        Add-Failure "质量元数据缺少官方链接 https://dev.epicgames.com/documentation: $relative"
    }
}

$sourceBodyFiles = @($gameBodyFiles | Where-Object {
    (Test-PathUnder $_.FullName $sourceAnalysisRoot) -and
    $_.Name -ne '19-高优先级源码覆盖路线图.md'
})
foreach ($file in $sourceBodyFiles) {
    if (-not $textByFile.ContainsKey($file.FullName)) { continue }
    $relative = Get-RepoRelative $file.FullName
    $qualityText = Get-NonCodeMarkdownText $textByFile[$file.FullName]
    $placeholder = [regex]::Match($qualityText, '预留|待补充|学习骨架')
    if ($placeholder.Success) {
        $qualitySourcePlaceholder++
        Add-Failure "源码占位词 [$($placeholder.Value)]: $relative"
    }

    $absoluteEvidence = [regex]::Matches($qualityText, $absoluteEvidencePattern)
    $relativeEvidence = [regex]::Matches($qualityText, $relativeEvidencePattern)
    if ($absoluteEvidence.Count -eq 0 -and $relativeEvidence.Count -eq 0) { continue }

    if (-not (Test-Path -LiteralPath $ueEngineRoot -PathType Container)) {
        if (-not $uePathWarningEmitted) {
            Add-Warning "源码证据路径未验证（UE 安装根不存在）: $ueInstallRoot"
            $uePathWarningEmitted = $true
        }
        continue
    }

    # 相对 Engine/... 路径可能是模块/目录示意；只对明确的 UE 绝对路径判定明显不存在。
    $checkedAbsolute = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
    foreach ($match in $absoluteEvidence) {
        $evidencePath = $match.Value
        while ($evidencePath.Contains('\\')) { $evidencePath = $evidencePath.Replace('\\', '\') }
        if (-not $checkedAbsolute.Add($evidencePath)) { continue }
        if (-not (Test-Path -LiteralPath $evidencePath)) {
            Add-Failure "源码绝对证据路径不存在: $evidencePath（$relative）"
        }
    }
}

# P2 领域质量门禁：只检查三个顶层领域的正文，README、围栏代码和维护目录不参与判定。
$domainDefinitions = @(
    [pscustomobject]@{ Name = '游戏AI'; Root = (Join-Path $rootPath '游戏AI') }
    [pscustomobject]@{ Name = '游戏服务端'; Root = (Join-Path $rootPath '游戏服务端') }
    [pscustomobject]@{ Name = '游戏算法'; Root = (Join-Path $rootPath '游戏算法') }
)
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
        (Test-PathUnder $_.FullName $domain.Root) -and
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

        $isServiceOrNetworkDocument = ($domain.Name -eq '游戏服务端') -or
            ($relative -match '(?i)服务端|网络|协议|通信|传输|TCP|UDP|HTTP|QUIC|WebSocket|RPC')
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
    }
}

Write-Host "Markdown: $fileCount（正文 $bodyCount，README $readmeCount）"
Write-Host "PASS: $($passes.Count + 1) 项基础检查已执行"
Write-Host "质量元数据：版本缺失 $qualityVersionMissing、日期缺失 $qualityDateMissing、官方链接缺失 $qualityOfficialLinkMissing、源码占位 $qualitySourcePlaceholder"
$domainTotals = @{
    BaselineMissing = 0
    DateMissing = 0
    SourceMissing = 0
    ValidationMissing = 0
    LegacyReferenceMissing = 0
}
$domainMetricKeys = @('BaselineMissing', 'DateMissing', 'SourceMissing', 'ValidationMissing', 'LegacyReferenceMissing')
$domainBodyTotal = 0
Write-Host '领域质量门禁统计：'
foreach ($domain in $domainDefinitions) {
    $stats = $domainStats[$domain.Name]
    $domainBodyTotal += $domainBodyFiles[$domain.Name].Count
    foreach ($metric in $domainMetricKeys) { $domainTotals[$metric] += $stats[$metric] }
    Write-Host "$($domain.Name)：领域基线缺失 $($stats.BaselineMissing)、日期缺失 $($stats.DateMissing)、来源缺失 $($stats.SourceMissing)、验证入口缺失 $($stats.ValidationMissing)、旧规范引用缺失 $($stats.LegacyReferenceMissing)"
}
Write-Host "领域质量门禁合计（正文 $domainBodyTotal）：领域基线缺失 $($domainTotals.BaselineMissing)、日期缺失 $($domainTotals.DateMissing)、来源缺失 $($domainTotals.SourceMissing)、验证入口缺失 $($domainTotals.ValidationMissing)、旧规范引用缺失 $($domainTotals.LegacyReferenceMissing)"
if ($warnings.Count -gt 0) {
    Write-Host "WARN: $($warnings.Count)"
    $warnings | ForEach-Object { Write-Host "WARN $_" }
} else {
    Write-Host 'WARN: 0'
}
if ($failures.Count -gt 0) {
    Write-Host "FAIL: $($failures.Count)"
    $failures | ForEach-Object { Write-Host "FAIL $_" }
    Write-Host 'RESULT: FAIL'
    exit 1
}
Write-Host 'FAIL: 0'
Write-Host 'RESULT: PASS'
exit 0
