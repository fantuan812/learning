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
foreach ($name in @('Get-RepoRelative', 'Test-MaintenancePath', 'Test-PathUnder')) {
    $definition = @($ast.FindAll({ param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] }, $false) | Where-Object Name -eq $name)
    if ($definition.Count -ne 1) { throw "Expected one definition: $name" }
    . ([scriptblock]::Create($definition[0].Extent.Text))
}
function Require([bool]$Condition, [string]$Message) { if (-not $Condition) { throw $Message } }
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
