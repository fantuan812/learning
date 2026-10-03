# Regression fixtures are isolated; never edit the caller's repository.
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$scriptPath = Join-Path $PSScriptRoot 'check_repo.ps1'
$fixture = Join-Path ([IO.Path]::GetTempPath()) ('kb-repo-paths-' + [guid]::NewGuid().ToString('N'))
[void][IO.Directory]::CreateDirectory($fixture)
$rootPath = $fixture.TrimEnd('\', '/')
$maintenanceRoots = @('references', 'learning', 'scripts')
$tokens = $null
$errors = $null
$ast = [Management.Automation.Language.Parser]::ParseFile($scriptPath, [ref]$tokens, [ref]$errors)
if ($errors.Count) { throw 'check_repo.ps1 must parse' }
foreach ($name in @('Get-RepoRelative', 'Test-MaintenancePath', 'Test-PathUnder', 'ConvertTo-KnowledgeMapValue')) {
    $definition = @($ast.FindAll({ param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] }, $false) | Where-Object Name -eq $name)
    if ($definition.Count -ne 1) { throw "Expected one definition: $name" }
    . ([scriptblock]::Create($definition[0].Extent.Text))
}
function Require([bool]$Condition, [string]$Message) { if (-not $Condition) { throw $Message } }
# Exercise the compatibility normalization on the actual parser surface used
# by the checker. These are shape guarantees, not a claim of a 5.1 host run.
$jsonShape = '{"documents":[{"id":"one","legacy_paths":["old/a.md"]}],"empty":[],"null_value":null,"scalar":"old/b.md","object":{},"nested":[[],["x"],null],"flag":false,"zero":0}' | ConvertFrom-Json
$normalized = ConvertTo-KnowledgeMapValue $jsonShape
Require ($normalized -is [Collections.IDictionary]) 'JSON root must normalize to a dictionary'
Require ($normalized.documents -is [array] -and $normalized.documents.Count -eq 1) 'Singleton documents must remain an array'
Require ($normalized.documents[0] -is [Collections.IDictionary]) 'Nested objects must normalize to dictionaries'
Require ($normalized.documents[0].legacy_paths -is [array] -and $normalized.documents[0].legacy_paths.Count -eq 1) 'Singleton aliases must remain arrays'
Require ($normalized.empty -is [array] -and $normalized.empty.Count -eq 0) 'Empty array must remain an empty array'
Require ($normalized.Contains('null_value') -and $null -eq $normalized.null_value) 'Null member must remain present and null'
Require ($normalized.scalar -is [string]) 'A scalar alias must not become an array'
Require ($normalized.object -is [Collections.IDictionary] -and $normalized.object.Count -eq 0) 'Empty object must remain a dictionary'
Require ($normalized.nested -is [array] -and $normalized.nested.Count -eq 3) 'Nested array dimensions must remain stable'
Require ($normalized.nested[0] -is [array] -and $normalized.nested[0].Count -eq 0) 'Nested empty array must not disappear'
Require ($normalized.nested[1] -is [array] -and $normalized.nested[1].Count -eq 1) 'Nested singleton array must not flatten'
Require ($null -eq $normalized.nested[2]) 'Null array element must not disappear'
Require ($normalized.flag -is [bool] -and -not $normalized.flag -and $normalized.zero -eq 0) 'Boolean and numeric scalar values must remain intact'
$emptyDocuments = ConvertTo-KnowledgeMapValue ('{"documents":[]}' | ConvertFrom-Json)
Require ($emptyDocuments.documents -is [array] -and $emptyDocuments.documents.Count -eq 0) 'Empty documents array must preserve its shape'
Write-Output 'PASS: JSON dictionary normalization preserves empty/singleton/nested arrays, nulls, and scalars'
# Exercise the actual evidence-root assignment with a drive that cannot exist.
# Join-Path can throw on a missing provider drive even though evidence is optional.
$evidenceAssignment = @($ast.FindAll({ param($node) $node -is [Management.Automation.Language.AssignmentStatementAst] -and $node.Left.Extent.Text -eq '$ueEngineRoot' }, $true))
Require ($evidenceAssignment.Count -eq 1) 'Expected one engine evidence root assignment'
$ueInstallRoot = ('MissingEvidence' + [guid]::NewGuid().ToString('N') + ':\UE')
. ([scriptblock]::Create($evidenceAssignment[0].Extent.Text))
Require ($ueEngineRoot -ceq [IO.Path]::Combine($ueInstallRoot, 'Engine')) 'Foreign evidence path must be joined lexically without a provider lookup'
$topic = Join-Path $rootPath 'topic'
Require (Test-PathUnder $topic $topic) 'Same path must be under itself'
Require (Test-PathUnder (Join-Path $topic 'child.md') $topic) 'Child must be in scope'
Require (-not (Test-PathUnder (Join-Path $rootPath 'topic-other/child.md') $topic)) 'Sibling prefix must not be in scope'
Require (-not (Test-PathUnder $rootPath $topic)) 'Parent must not be in child scope'
Require ((Get-RepoRelative (Join-Path $rootPath 'references/child.md')) -ceq 'references\child.md') 'Stable relative path convention'
Require (Test-MaintenancePath (Join-Path $rootPath 'references/child.md')) 'Maintenance descendant exemption'
Require (Test-MaintenancePath (Join-Path $rootPath 'scripts')) 'Maintenance root exemption'
Require (-not (Test-MaintenancePath (Join-Path $rootPath 'references-other/child.md'))) 'Sibling must not receive exemption'
$utf8 = [Text.UTF8Encoding]::new($false)
function Write-Fixture([string]$Relative, [string]$Text) {
    $path = Join-Path $fixture $Relative
    [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($path))
    [IO.File]::WriteAllText($path, $Text, $utf8)
}
& git -c "safe.directory=$fixture" -C $fixture init --quiet
if ($LASTEXITCODE -ne 0) { throw 'Fixture git init failed' }
# Deliberately invalid canonical/domain pages must fail, but sources and
# maintenance must retain their original narrow maturity exemptions.
Write-Fixture '游戏服务端/probe.md' '# Missing domain quality metadata'
Write-Fixture 'topic/probe.md' '# Missing maturity'
Write-Fixture 'references/probe.md' '# Maintenance'
Write-Fixture 'references-other/probe.md' '# Not maintenance'
Write-Fixture '读书笔记/probe.md' '# Source material'
$pwsh = (Get-Process -Id $PID).Path
$output = (& $pwsh -NoLogo -NoProfile -File $scriptPath -Root $fixture 2>&1 | Out-String)
$exitCode = $LASTEXITCODE
Require ($exitCode -ne 0) 'Invalid fixture must fail'
Require ($output.Contains('领域质量门禁缺少领域基线行（知识基线/版本与规范基线/事实边界）: 游戏服务端\probe.md')) 'Domain quality check must actually inspect the page'
Require ($output.Contains('本次新增/修改正文缺少知识成熟度: topic\probe.md')) 'Missing maturity must fail'
Require ($output.Contains('本次新增/修改正文缺少知识成熟度: references-other\probe.md')) 'Sibling exemption must not weaken maturity'
Require (-not $output.Contains('缺少知识成熟度: references\probe.md')) 'Maintenance maturity exemption must remain'
Require (-not $output.Contains('缺少知识成熟度: 读书笔记\probe.md')) 'Source maturity exemption must remain'
Require (-not ($output -match '(?m)目录缺 README\.md: references\r?$')) 'Maintenance navigation exemption must remain'
Write-Output 'PASS: native paths, scope boundaries, narrow exemptions, domain coverage, missing-maturity negative controls'
Write-Output "Fixture retained: $fixture"

# A second, fully healthy isolated fixture physically moves every required DS
# canonical document. The historical directories never exist in this fixture.
$fixture = Join-Path ([IO.Path]::GetTempPath()) ('kb-repo-migration-' + [guid]::NewGuid().ToString('N'))
[void][IO.Directory]::CreateDirectory($fixture)
$rootPath = $fixture.TrimEnd('\', '/')
& git -c "safe.directory=$fixture" -C $fixture init --quiet
if ($LASTEXITCODE -ne 0) { throw 'Migration fixture git init failed' }
foreach ($variable in @('$dsDocumentDefinitions', '$dsExtendedDefinitions', '$dsQualityGateRelative')) {
    $assignment = @($ast.FindAll({ param($node) $node -is [Management.Automation.Language.AssignmentStatementAst] }, $true) | Where-Object { $_.Left.Extent.Text -eq $variable })
    Require ($assignment.Count -eq 1) "Expected one assignment: $variable"
    . ([scriptblock]::Create($assignment[0].Extent.Text))
}
$common = @'
---
type: Concept
maturity: L2
---
# Fixture

> 知识成熟度：L2

版本基准：UE 5.8
最后更新：2026-10-03
知识基线：fixture contract

[官方来源](https://dev.epicgames.com/documentation/en-us/unreal-engine/)

## 验证与基准

Fixture validation.
'@
$newDomain = '知识/07-网络与游戏服务端'
$newDirectory = "$newDomain/迁移测试"
$nodes = [Collections.Generic.List[object]]::new()
$index = 0
foreach ($definition in @($dsDocumentDefinitions + $dsExtendedDefinitions)) {
    $index++
    $relative = "$newDirectory/ds-$index.md"
    $body = $common + "`n"
    foreach ($anchor in $definition.Anchors) {
        $body += ($anchor.Patterns[0].Replace('\s+', ' ').Replace('\.', '.')) + "`n"
    }
    Write-Fixture $relative $body
    $nodes.Add([ordered]@{ id = "ds-$index"; path = $relative; domain = 'online'; kind = 'concept'; legacy_paths = @($definition.Relative.Replace('\', '/')) })
}
$sourcePath = $nodes[0].path
$sourceText = [IO.File]::ReadAllText((Join-Path $fixture $sourcePath), $utf8)
$gatePath = "$newDirectory/quality.md"
Write-Fixture $gatePath ($common + "`n概念覆盖不等于执行验证。占位命令不代表执行。`n")
$nodes.Add([ordered]@{ id = 'quality'; path = $gatePath; domain = 'online'; kind = 'concept'; legacy_paths = @($dsQualityGateRelative.Replace('\', '/')) })
$networkPath = "$newDirectory/network.md"
Write-Fixture $networkPath $common
$nodes.Add([ordered]@{ id = 'network'; path = $networkPath; domain = 'online'; kind = 'concept'; legacy_paths = @('游戏知识/06-网络同步/probe.md') })
$servicePath = "$newDirectory/service.md"
Write-Fixture $servicePath $common
$nodes.Add([ordered]@{ id = 'service'; path = $servicePath; domain = 'online'; kind = 'concept'; legacy_paths = @('游戏服务端/probe.md') })
$uePath = "$newDirectory/ue.md"
Write-Fixture $uePath $common
$nodes.Add([ordered]@{ id = 'ue'; path = $uePath; domain = 'online'; kind = 'concept'; legacy_paths = @('游戏知识/probe.md') })
$newUePath = "$newDirectory/new-ue.md"
Write-Fixture $newUePath $common
$nodes.Add([ordered]@{ id = 'new-ue'; path = $newUePath; domain = 'online'; kind = 'concept'; technologies = @('unreal-engine') })
$newGenericPath = "$newDirectory/new-generic.md"
Write-Fixture $newGenericPath $common
$nodes.Add([ordered]@{ id = 'new-generic'; path = $newGenericPath; domain = 'online'; kind = 'concept' })

function Save-MigrationGraph {
    $graph = [ordered]@{
        version = 1
        domains = @(@{ id = 'online'; title = '网络'; entrypoint = "$newDomain/README.md" })
        documents = @($nodes)
        concepts = @()
        relations = @()
    }
    Write-Fixture '.kb/knowledge-map.json' ($graph | ConvertTo-Json -Depth 30)
}
function Build-FixtureNavigation {
    $directories = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($file in Get-ChildItem -LiteralPath $fixture -Recurse -Filter '*.md' -File) {
        $directory = $file.DirectoryName
        while ($directory -and (Test-PathUnder $directory $fixture)) {
            [void]$directories.Add($directory)
            if ($directory -eq $fixture) { break }
            $directory = [IO.Path]::GetDirectoryName($directory)
        }
    }
    foreach ($directory in $directories) { [IO.File]::WriteAllText((Join-Path $directory 'README.md'), '# Fixture', $utf8) }
    foreach ($directory in $directories) {
        $links = @('# Fixture', '')
        foreach ($file in Get-ChildItem -LiteralPath $directory -File -Filter '*.md' | Where-Object Name -ne 'README.md') {
            $links += "[$($file.Name)](./$($file.Name))"
        }
        foreach ($child in Get-ChildItem -LiteralPath $directory -Directory) {
            if (Test-Path -LiteralPath (Join-Path $child.FullName 'README.md')) {
                $links += "[$($child.Name)](./$($child.Name)/README.md)"
            }
        }
        [IO.File]::WriteAllText((Join-Path $directory 'README.md'), (($links -join "`n") + "`n"), $utf8)
    }
}
$migrationCases = 0
function Expect-Migration([string]$Name, [int]$Code, [string]$Contains = '', [string]$Absent = '') {
    Save-MigrationGraph
    $output = (& $pwsh -NoLogo -NoProfile -File $scriptPath -Root $fixture 2>&1 | Out-String)
    $actual = $LASTEXITCODE
    Require ($actual -eq $Code) "$Name expected exit $Code, got $actual`n$output"
    if ($Contains) { Require ($output.Contains($Contains)) "$Name missing diagnostic [$Contains]`n$output" }
    if ($Absent) { Require (-not $output.Contains($Absent)) "$Name unexpected diagnostic [$Absent]`n$output" }
    $script:migrationCases++
    Write-Output "PASS migration fixture: $Name"
}
Build-FixtureNavigation
Require (-not (Test-Path -LiteralPath (Join-Path $fixture '游戏知识'))) 'Historical UE tree must not exist in migration fixture'
Expect-Migration 'all twelve DS/gate documents resolve to moved canonical files' 0 'RESULT: PASS' 'DS 专项门禁必需文件缺失'

# Migration must neither lose the original profile nor accidentally apply the
# new generic profile to historical UE prose that never had those requirements.
Write-Fixture $uePath ($common.Replace('知识基线：fixture contract', '').Replace('## 验证与基准', '## Notes'))
Expect-Migration 'moved UE article keeps only its original quality profile' 0 'RESULT: PASS'
Write-Fixture $uePath $common
$serviceNode = @($nodes | Where-Object { $_.id -eq 'service' })[0]
$serviceNode.technologies = @('unreal-engine')
Write-Fixture $servicePath ($common.Replace('版本基准：UE 5.8', '').Replace('https://dev.epicgames.com/documentation/en-us/unreal-engine/', 'https://www.rfc-editor.org/rfc/rfc9293'))
Expect-Migration 'UE technology claim requires UE metadata even with service legacy' 1 "质量元数据缺少版本基准/版本基线: $($servicePath.Replace('/', '\'))"
$serviceNode.Remove('technologies')
Expect-Migration 'moved untagged service article retains its original quality profile' 0 'RESULT: PASS'
Write-Fixture $servicePath $common

Write-Fixture $servicePath ($common.Replace('知识基线：fixture contract', ''))
Expect-Migration 'moved service article still requires baseline' 1 "领域质量门禁缺少领域基线行（知识基线/版本与规范基线/事实边界）: $($servicePath.Replace('/', '\'))"
Write-Fixture $servicePath $common

$missingUe = $common.Replace('版本基准：UE 5.8', '').Replace('[官方来源](https://dev.epicgames.com/documentation/en-us/unreal-engine/)', '')
Write-Fixture $uePath $missingUe
Expect-Migration 'moved UE article still requires version and official source' 1 "质量元数据缺少官方链接 https://dev.epicgames.com/documentation: $($uePath.Replace('/', '\'))"
Write-Fixture $uePath $common

Write-Fixture $sourcePath ($sourceText.Replace('UWorld::Listen', 'MissingListener'))
Expect-Migration 'moved DS source still requires exact anchors' 1 'DS 专项门禁锚点缺失 [源码专题/UWorld::Listen]' 'DS 专项门禁必需文件缺失'
Write-Fixture $sourcePath $sourceText

Write-Fixture $networkPath ($common + "`nActorChannel.cpp`n")
Expect-Migration 'moved network check survives absent historical directory' 1 'DS 专项门禁发现网络同步旧路径 ActorChannel.cpp'
Write-Fixture $networkPath $common

Write-Fixture $newUePath $missingUe
Expect-Migration 'new UE-tagged article requires UE quality metadata' 1 "质量元数据缺少版本基准/版本基线: $($newUePath.Replace('/', '\'))"
Write-Fixture $newUePath $common
Write-Fixture $newGenericPath ($common.Replace('知识基线：fixture contract', ''))
Expect-Migration 'new no-legacy eight-domain article requires generic baseline' 1 "领域质量门禁缺少领域基线行（知识基线/版本与规范基线/事实边界）: $($newGenericPath.Replace('/', '\'))"
$genericNode = @($nodes | Where-Object { $_.id -eq 'new-generic' })[0]
$genericNode.legacy_paths = @('archive/arbitrary.md')
Expect-Migration 'arbitrary alias cannot exempt generic quality' 1 "领域质量门禁缺少领域基线行（知识基线/版本与规范基线/事实边界）: $($newGenericPath.Replace('/', '\'))"
$genericNode.legacy_paths = @('笔记/old-note.md')
Expect-Migration 'notes alias cannot exempt generic quality' 1 "领域质量门禁缺少领域基线行（知识基线/版本与规范基线/事实边界）: $($newGenericPath.Replace('/', '\'))"
$genericNode.Remove('legacy_paths')
Write-Fixture $newGenericPath $common

$originalLegacy = $nodes[0].legacy_paths
$nodes[0].legacy_paths = @('../escape.md')
Expect-Migration 'alias traversal fails closed' 1 'Unsafe knowledge path'
$nodes[0].legacy_paths = @($nodes[1].path)
Expect-Migration 'alias cannot shadow an active canonical path' 1 'legacy_paths conflicts with active canonical path'
$nodes[0].legacy_paths = @($nodes[1].legacy_paths[0].ToUpperInvariant())
Expect-Migration 'alias case conflict fails closed' 1 'conflicting legacy_paths'
$nodes[0].legacy_paths = @()
Expect-Migration 'empty alias array fails closed' 1 'legacy_paths must be a nonempty array'
$nodes[0].legacy_paths = 'old/probe.md'
Expect-Migration 'scalar alias fails closed' 1 'legacy_paths must be a nonempty array'
$secondLegacy = $nodes[1].legacy_paths
$nodes[0].legacy_paths = @('archive/café.md')
$nodes[1].legacy_paths = @('archive/cafe' + [char]0x0301 + '.md')
Expect-Migration 'Unicode normalization alias collision fails closed' 1 'conflicting legacy_paths'
$nodes[1].legacy_paths = $secondLegacy
$nodes[0].legacy_paths = $originalLegacy
$originalPath = $nodes[0].path
$nodes[0].path = "$newDirectory/missing.md"
Expect-Migration 'alias cannot excuse a missing canonical document' 1 'Missing canonical knowledge file'
$nodes[0].path = $originalPath
Expect-Migration 'healthy migrated fixture restored' 0 'RESULT: PASS'
Write-Output "PASS migration fixtures: $migrationCases cases"
Write-Output "Migration fixture retained: $fixture"
