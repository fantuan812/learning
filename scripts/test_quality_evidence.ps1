# All writes and Git initialization are confined to uniquely owned temporary fixtures.
# These tests validate the lint contract, never teaching correctness or a real UE run.
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$checker = Join-Path $PSScriptRoot 'check_repo.ps1'
$okfChecker = Join-Path $PSScriptRoot 'check_okf.ps1'
$pwsh = (Get-Process -Id $PID).Path
$fixture = Join-Path ([IO.Path]::GetTempPath()) ('kb-quality-evidence-' + [guid]::NewGuid().ToString('N'))
$engineFixture = Join-Path ([IO.Path]::GetTempPath()) ('kb-source-fixture-' + [guid]::NewGuid().ToString('N'))
$outsideFixture = Join-Path ([IO.Path]::GetTempPath()) ('kb-outside-fixture-' + [guid]::NewGuid().ToString('N'))
$utf8 = [Text.UTF8Encoding]::new($false)
$cases = 0
foreach ($directory in @($fixture, $engineFixture, $outsideFixture)) { [void][IO.Directory]::CreateDirectory($directory) }
function Require([bool]$Condition, [string]$Message) { if (-not $Condition) { throw $Message } }
function Write-Fixture([string]$Relative, [string]$Text) {
    $path = Join-Path $fixture $Relative
    [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($path))
    [IO.File]::WriteAllText($path, $Text, $utf8)
}
function Expect([string]$Name, [int]$Code, [string]$Contains, [string]$Absent = '', [string[]]$Arguments = @(), [switch]$Okf) {
    $scriptPath = if ($Okf) { $okfChecker } else { $checker }
    $extra = if ($Okf) { @('-Mode', 'Strict') } else { @() }
    $output = (& $pwsh -NoLogo -NoProfile -File $scriptPath -Root $fixture @extra @Arguments 2>&1 | Out-String)
    $actual = $LASTEXITCODE
    $script:lastFixtureOutput = $output
    Require ($actual -eq $Code) "$Name expected exit $Code, got $actual`n$output"
    Require ($output.Contains($Contains)) "$Name missing diagnostic [$Contains]`n$output"
    if ($Absent) { Require (-not $output.Contains($Absent)) "$Name unexpected diagnostic [$Absent]`n$output" }
    $script:cases++
    Write-Output "PASS quality fixture: $Name (exit $actual)"
}
& git -c "safe.directory=$fixture" -C $fixture init --quiet
if ($LASTEXITCODE -ne 0) { throw 'Fixture git init failed' }
# Import the actual DS requirements, not a weaker imitation of the aggregate gate.
$tokens = $null; $parseErrors = $null
$ast = [Management.Automation.Language.Parser]::ParseFile($checker, [ref]$tokens, [ref]$parseErrors)
Require ($parseErrors.Count -eq 0) 'Checker must parse before fixtures run'
foreach ($variable in @('$dsDocumentDefinitions', '$dsExtendedDefinitions', '$dsQualityGateRelative')) {
    $assignment = @($ast.FindAll({ param($node) $node -is [Management.Automation.Language.AssignmentStatementAst] }, $true) | Where-Object { $_.Left.Extent.Text -eq $variable })
    Require ($assignment.Count -eq 1) "Expected one DS contract assignment: $variable"
    . ([scriptblock]::Create($assignment[0].Extent.Text))
}
$common = @'
---
type: Concept
status: draft
verified: []
maturity: L2
updated: 2026-10-04
---
# Isolated lint fixture

> 知识成熟度：L2
> 版本基准：UE synthetic-fixture-version；不是实际引擎版本或运行记录。
> 最后更新：2026-10-04
> 知识基线：本页仅构造检查器输入，不构成教学或引擎证据。

[官方入口](https://dev.epicgames.com/documentation/en-us/unreal-engine/)

## 验证与基准

仅验证机械 lint；命令和原始输出由本测试进程产生。
'@
# Normalize only these owned template strings, never checked source/baseline bytes.
$common = $common.Replace("`r`n", "`n")
foreach ($definition in @($dsDocumentDefinitions + $dsExtendedDefinitions)) {
    $body = $common + "`n"
    foreach ($anchor in $definition.Anchors) { $body += $anchor.Patterns[0].Replace('\s+', ' ').Replace('\.', '.') + "`n" }
    Write-Fixture $definition.Relative $body
}
Write-Fixture $dsQualityGateRelative ($common + "`n概念覆盖不等于执行验证；占位命令不代表执行。`n")
$probe = '游戏算法/probe.md'
$sourceProbe = '游戏知识/12-引擎源码分析/quality-probe.md'
$short = @'
---
type: Concept
status: draft
verified: []
maturity: L2
updated: 2026-10-04
sources:
  - resource: https://www.rfc-editor.org/rfc/rfc9293
---
# 短文机械合同

> 知识成熟度：L2
> 知识基线：RFC 9293（2022），这里只示范检查器能识别的元数据。
> 最后更新：2026-10-04

动机：区分记录边界与字节流；本 fixture 不声称已运行网络实验。
因果：记录编码先写长度，接收方积累足够字节后再交付一条记录。
正确例：长度 3 与 abc 到齐才交付 abc；反例：只收到 a 就交付会截断记录。
失败边界：长度超限应拒绝，断流应报告不完整；具体协议仍需独立审稿。

## 可复现追踪

手工输入 [3,a] 再输入 [b,c]，分别预期“不交付”和“abc”。
'@
$short = $short.Replace("`r`n", "`n")
$static = $common + "`n源码核对状态：未核对；来源文档已静态阅读，源码待补充，未运行 UE。`n保留字段用于预留扩展，不是已完成实现。`n"
Write-Fixture $probe $short
Write-Fixture $sourceProbe $static
function Build-Navigation {
    $directories = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($file in Get-ChildItem -LiteralPath $fixture -Recurse -Filter '*.md' -File) {
        $dir = $file.DirectoryName
        while ($dir.StartsWith($fixture, [StringComparison]::OrdinalIgnoreCase)) {
            [void]$directories.Add($dir)
            if ($dir -eq $fixture) { break }
            $dir = [IO.Path]::GetDirectoryName($dir)
        }
    }
    foreach ($dir in $directories) { [IO.File]::WriteAllText((Join-Path $dir 'README.md'), "---`ntype: Index`n---`n# Fixture`n", $utf8) }
    foreach ($dir in $directories) {
        $lines = @('---', 'type: Index', '---', '# Fixture', '')
        foreach ($file in Get-ChildItem -LiteralPath $dir -File -Filter '*.md' | Where-Object Name -ne 'README.md') { $lines += "[$($file.Name)](<./$($file.Name)>)" }
        foreach ($child in Get-ChildItem -LiteralPath $dir -Directory) {
            if (Test-Path -LiteralPath (Join-Path $child.FullName 'README.md')) { $lines += "[$($child.Name)](<./$($child.Name)/README.md>)" }
        }
        [IO.File]::WriteAllText((Join-Path $dir 'README.md'), ($lines -join "`n") + "`n", $utf8)
    }
}
Build-Navigation
Require (($short -split "`n").Count -lt 100) 'Positive short fixture must really be short'
Expect 'short complete non-UE article, honest static-only boundary' 0 'RESULT: PASS' '正文少于 300 行'
Expect 'short metadata passes actual OKF checker' 0 'FAIL: 0' '' @() -Okf
Expect 'no source root explicitly remains unverified' 0 '源码路径核对未运行：未提供 -UeInstallRoot' '源码路径存在性：已检查 1'

$padding = ((1..310 | ForEach-Object { 'Filler is not evidence.' }) -join "`n")
Write-Fixture $probe ($short.Replace('type: Concept', '') + "`n" + $padding)
Expect 'long filler cannot replace required type' 1 'type missing/empty' '' @() -Okf
Write-Fixture $probe ($short.Replace('> 最后更新：2026-10-04', '').Replace('https://www.rfc-editor.org/rfc/rfc9293', '') + "`n" + $padding)
Expect 'long filler cannot replace date or source' 1 '领域质量门禁缺少非代码外部来源 URL'
Write-Fixture $probe $short

Require ($short.Contains('sources:' + "`n" + '  - resource: https://www.rfc-editor.org/rfc/rfc9293')) 'Positive template must contain the real provenance being removed'
$withoutSource = $short.Replace('sources:' + "`n" + '  - resource: https://www.rfc-editor.org/rfc/rfc9293', '')
Require ($withoutSource -notmatch '(?im)^sources\s*:' -and $withoutSource -notmatch '(?i)https?://') 'Code-only source negatives must have no real frontmatter or prose URL left in their seed'
Write-Fixture $probe ($withoutSource + "`n" + '```markdown' + "`n[Fake source](https://www.rfc-editor.org/rfc/rfc9293)`n" + '```' + "`n")
Expect 'fenced fake source is not evidence' 1 '领域质量门禁缺少非代码外部来源 URL'
Write-Fixture $probe ($withoutSource + "`n    https://www.rfc-editor.org/rfc/rfc9293`n")
Expect 'indented fake source is not evidence' 1 '领域质量门禁缺少非代码外部来源 URL'
Write-Fixture $probe ($withoutSource + "`n" + '`https://www.rfc-editor.org/rfc/rfc9293`' + "`n")
Expect 'inline-code fake source is not evidence' 1 '领域质量门禁缺少非代码外部来源 URL'
Write-Fixture $probe $short
$fourFence = @'
````markdown
```text
https://www.rfc-editor.org/rfc/rfc9293
[Not a real repository link](./absent-in-code.md)
```
````
'@
$quotedFence = @'
> ```text
> https://www.rfc-editor.org/rfc/rfc9293
> [Not a real repository link](./absent-in-code.md)
> ```
'@
$nestedQuoteFence = @'
> > ~~~~text
> > ~~~
> > https://www.rfc-editor.org/rfc/rfc9293
> > ~~~
> > ~~~~
'@
$listFence = @'
- ````markdown
  ```text
  https://www.rfc-editor.org/rfc/rfc9293
  ```
  ````
'@
$tildeFence = @'
~~~~text
~~~
https://www.rfc-editor.org/rfc/rfc9293
~~~
~~~~~
'@
$trailingInfoFence = @'
```text
```not-a-close
https://www.rfc-editor.org/rfc/rfc9293
```
'@
foreach ($case in @(
    @{ Name = 'four-backtick outer fence'; Text = $fourFence },
    @{ Name = 'blockquote code fence'; Text = $quotedFence },
    @{ Name = 'nested quote and tilde fence'; Text = $nestedQuoteFence },
    @{ Name = 'direct list-item fence'; Text = $listFence },
    @{ Name = 'long tilde fence'; Text = $tildeFence },
    @{ Name = 'closing marker with trailing info stays in code'; Text = $trailingInfoFence }
)) {
    Write-Fixture $probe ($withoutSource + "`n`n" + $case.Text + "`n")
    Expect ($case.Name + ' cannot supply a source') 1 '领域质量门禁缺少非代码外部来源 URL' '未闭合代码围栏'
    Write-Fixture $probe ($withoutSource + "`n`n" + $case.Text + "`n[Real source after fence](https://www.rfc-editor.org/rfc/rfc9293)`n")
    Expect ($case.Name + ' retains real source after closure') 0 'RESULT: PASS' '断链'
}
Write-Fixture $probe ($withoutSource + "`n> [Real quoted source](https://www.rfc-editor.org/rfc/rfc9293)`n")
Expect 'ordinary blockquote source remains valid' 0 'RESULT: PASS'
Write-Fixture $probe ($withoutSource + "`n" + '``an interior ` https://www.rfc-editor.org/rfc/rfc9293``' + "`n")
Expect 'multi-backtick inline code cannot supply a source' 1 '领域质量门禁缺少非代码外部来源 URL'
Write-Fixture $probe $short

$listUnindent = @'
- ```text
  hidden sample
```
https://www.rfc-editor.org/rfc/rfc9293
'@
$orderedListUnindent = @'
12. ~~~~text
    hidden sample
~~~~
https://www.rfc-editor.org/rfc/rfc9293
'@
$tabListUnindent = "-`t" + '```text' + "`n    hidden sample`n  " + '```' + "`nhttps://www.rfc-editor.org/rfc/rfc9293`n"
$tabOrderedUnindent = "12.`t~~~~text`n    hidden sample`n  ~~~~`nhttps://www.rfc-editor.org/rfc/rfc9293`n"
$quoteListUnindent = @'
> - ```text
>   hidden sample
> ```
> https://www.rfc-editor.org/rfc/rfc9293
'@
$listQuoteUnindent = @'
- > ```text
  > hidden sample
> ```
> https://www.rfc-editor.org/rfc/rfc9293
'@
foreach ($case in @(
    @{ Name = 'direct list unindent'; Text = $listUnindent },
    @{ Name = 'ordered list unindent'; Text = $orderedListUnindent },
    @{ Name = 'tab-expanded bullet list unindent'; Text = $tabListUnindent },
    @{ Name = 'tab-expanded ordered list unindent'; Text = $tabOrderedUnindent },
    @{ Name = 'quote containing list unindent'; Text = $quoteListUnindent },
    @{ Name = 'list containing quote unindent'; Text = $listQuoteUnindent }
)) {
    Write-Fixture $probe ($withoutSource + "`n`n" + $case.Text + "`n")
    Expect ($case.Name + ' opens a new fence and cannot supply a source') 1 '领域质量门禁缺少非代码外部来源 URL'
    Require ($script:lastFixtureOutput.Contains('未闭合代码围栏')) 'Container exit must preserve explicit-close failure'
}
$orderedListClosed = @'
12. ~~~~text
    hidden sample
    ~~~~
[Real source after ordered list](https://www.rfc-editor.org/rfc/rfc9293)
'@
$tabListClosed = "-`t" + '```text' + "`n    hidden sample`n    " + '```' + "`n[Real](https://www.rfc-editor.org/rfc/rfc9293)`n"
$tabOrderedClosed = "12.`t~~~~text`n    hidden sample`n    ~~~~`n[Real](https://www.rfc-editor.org/rfc/rfc9293)`n"
$quotedListClosed = @'
> - ```text
>   hidden sample
>   ```
> [Real source after list](https://www.rfc-editor.org/rfc/rfc9293)
'@
$listQuotedClosed = @'
- > ```text
  > hidden sample
  > ```

[Real source after list](https://www.rfc-editor.org/rfc/rfc9293)
'@
$listBlankClosed = @'
- ```text
  hidden sample

  still code
  ```
[Real source after list](https://www.rfc-editor.org/rfc/rfc9293)
'@
foreach ($case in @(
    @{ Name = 'ordered list correctly closed'; Text = $orderedListClosed },
    @{ Name = 'tab-expanded bullet list correctly closed'; Text = $tabListClosed },
    @{ Name = 'tab-expanded ordered list correctly closed'; Text = $tabOrderedClosed },
    @{ Name = 'quote containing correctly closed list'; Text = $quotedListClosed },
    @{ Name = 'list containing correctly closed quote'; Text = $listQuotedClosed },
    @{ Name = 'blank list-code line does not end container'; Text = $listBlankClosed }
)) {
    Write-Fixture $probe ($withoutSource + "`n`n" + $case.Text + "`n")
    Expect ($case.Name + ' retains real source') 0 'RESULT: PASS' '未闭合代码围栏'
}
Write-Fixture $probe $short

$missingVersion = $static.Replace('> 版本基准：UE synthetic-fixture-version；不是实际引擎版本或运行记录。', '')
Write-Fixture $sourceProbe ($missingVersion + "`n" + '```markdown' + "`n版本基准：UE fake`n" + '```' + "`n")
Expect 'fenced fake version does not satisfy UE metadata' 1 '质量元数据缺少版本基准/版本基线'
$withoutOfficial = $static.Replace('[官方入口](https://dev.epicgames.com/documentation/en-us/unreal-engine/)', '')
Write-Fixture $sourceProbe ($withoutOfficial + "`n" + '`https://dev.epicgames.com/documentation/en-us/unreal-engine/`' + "`n")
Expect 'inline-code fake official source is not evidence' 1 '质量元数据缺少官方链接'
Write-Fixture $sourceProbe ($withoutOfficial + "`n[Wrong host](https://dev.epicgames.com.example.invalid/documentation/)`n")
Expect 'official-looking hostname is not an official source' 1 '质量元数据缺少官方链接'
Write-Fixture $sourceProbe $static

Write-Fixture $probe ($short + "`n[Broken](./absent.md)`n")
Expect 'broken repository link fails' 1 '断链'
[IO.File]::WriteAllText((Join-Path $outsideFixture 'outside.txt'), 'Only temporary test data.', $utf8)
$outsideRelative = '../' + [IO.Path]::GetFileName($outsideFixture) + '/outside.txt'
# From the root README, this target exists outside the repository.
$rootReadme = [IO.File]::ReadAllText((Join-Path $fixture 'README.md'), $utf8)
Write-Fixture 'README.md' ($rootReadme + "`n[Escape]($outsideRelative)`n")
Write-Fixture $probe $short
Expect 'existing out-of-root link fails containment' 1 '链接路径不安全'
Write-Fixture 'README.md' ($rootReadme + "`n[Encoded escape]($($outsideRelative.Replace('../','%2e%2e/')))`n")
Expect 'percent-encoded out-of-root link fails containment' 1 '链接路径不安全'
Write-Fixture 'README.md' $rootReadme

[IO.File]::WriteAllBytes((Join-Path $fixture $probe), [byte[]]@(0xc3, 0x28))
Expect 'invalid UTF-8 remains fatal' 1 'UTF-8 解码失败'
[IO.File]::WriteAllText((Join-Path $fixture $probe), $short, [Text.UTF8Encoding]::new($true))
Expect 'BOM remains fatal' 1 'BOM:'
Write-Fixture $probe ($short + [char]0xfffd)
Expect 'replacement character remains fatal' 1 '替换字符 U+FFFD'
Write-Fixture $probe ($short + "`n" + '```cpp' + "`n")
Expect 'unclosed fence remains fatal' 1 '未闭合代码围栏'
Write-Fixture $probe $short

Write-Fixture '.kb/knowledge-map.json' '{"documents":[{"id":"fake","path":"知识/missing.md"}],"domains":[{"entrypoint":"知识/demo/README.md"}]}'
Expect 'fake missing canonical remains fatal' 1 'Missing canonical knowledge file'
Write-Fixture '.kb/knowledge-map.json' '{"documents":[{"id":"escape","path":"../outside.md"}],"domains":[{"entrypoint":"知识/demo/README.md"}]}'
Expect 'canonical traversal remains fatal' 1 'Unsafe knowledge path'
Remove-Item -LiteralPath (Join-Path $fixture '.kb/knowledge-map.json')

$sourceDir = Join-Path $engineFixture 'Engine/Source/Fixture'
[void][IO.Directory]::CreateDirectory($sourceDir)
[void][IO.Directory]::CreateDirectory((Join-Path $engineFixture 'Engine/Build'))
[IO.File]::WriteAllText((Join-Path $sourceDir 'Sample.cpp'), '// Synthetic fixture, not private UE source.', $utf8)
[IO.File]::WriteAllText((Join-Path $engineFixture 'Engine/Build/Build.version'), '{"MajorVersion":99,"MinorVersion":7,"PatchVersion":3,"BranchName":"synthetic-test"}', $utf8)
$actualSource = $common + "`n源码核对状态：已核对（仅本测试的临时文件存在性）。`n" + '`Engine/Source/Fixture/Sample.cpp`' + "`n" + '`Engine/Build/Build.version`' + "`n未运行 UE；临时树不构成真实引擎证据。`n"
Write-Fixture $sourceProbe $actualSource
Expect 'alternate platform checkout validates explicit relative files' 0 '源码路径存在性：已检查 2' '源码路径核对未运行' @('-UeInstallRoot', $engineFixture)
$nativeSource = (Join-Path $sourceDir 'Sample.cpp').Replace('\', '/')
Write-Fixture $sourceProbe ($actualSource.Replace('Engine/Source/Fixture/Sample.cpp', $nativeSource))
Expect 'native absolute evidence under the selected checkout is checked' 0 '源码路径存在性：已检查 2' '源码绝对位置未覆盖' @('-UeInstallRoot', $engineFixture)
Write-Fixture $sourceProbe ($common + "`n原生裸文件位置：$nativeSource`n")
Expect 'bare native absolute existing file is checked' 0 '源码路径存在性：已检查 1' '源码绝对位置未覆盖' @('-UeInstallRoot', $engineFixture)
Write-Fixture $sourceProbe ($common + "`n原生裸文件位置：$($nativeSource.Replace('Sample.cpp', 'Missing.cpp'))`n")
Expect 'bare native absolute missing file fails' 1 '源码证据路径不存在' '' @('-UeInstallRoot', $engineFixture)
[IO.File]::WriteAllText((Join-Path $sourceDir '样例[1].cpp'), '// Unicode literal-path fixture.', $utf8)
Write-Fixture $sourceProbe ($common + "`nEngine/Source/Fixture/样例[1].cpp`n")
Expect 'whole bare Unicode path with brackets is checked literally' 0 '源码路径存在性：已检查 1' '源码证据路径不存在' @('-UeInstallRoot', $engineFixture)
Write-Fixture $sourceProbe ($common + "`nEngine/Source/Fixture/不存在.cpp`n")
Expect 'missing bare Unicode file cannot be truncated to its existing directory' 1 '源码证据路径不存在: Engine/Source/Fixture/不存在.cpp' '' @('-UeInstallRoot', $engineFixture)
Require ($script:lastFixtureOutput.Contains('源码路径存在性：已检查 0')) 'Unicode negative must not count its parent directory as checked evidence'
$unicodeAbsolute = (Join-Path $sourceDir '样例[1].cpp').Replace('\', '/')
Write-Fixture $sourceProbe ($common + "`n$unicodeAbsolute`n")
Expect 'whole bare absolute Unicode file is checked' 0 '源码路径存在性：已检查 1' '源码证据路径不存在' @('-UeInstallRoot', $engineFixture)
Write-Fixture $sourceProbe ($common + "`n$($unicodeAbsolute.Replace('样例[1].cpp', '不存在.cpp'))`n")
Expect 'missing bare absolute Unicode file fails' 1 '源码证据路径不存在' '' @('-UeInstallRoot', $engineFixture)
Require ($script:lastFixtureOutput.Contains('源码路径存在性：已检查 0')) 'Absolute Unicode negative must not check a truncated prefix'
Write-Fixture $sourceProbe ($static + "`n" + '> ```text' + "`n> $($nativeSource.Replace('Sample.cpp', 'Missing.cpp'))`n" + '> ```' + "`n")
Expect 'quoted code source path stays outside path-check coverage' 0 '源码路径存在性：已检查 0' '源码证据路径不存在' @('-UeInstallRoot', $engineFixture)
Write-Fixture $sourceProbe ($actualSource.Replace('Sample.cpp', 'Missing.cpp'))
Expect 'provided root with a missing file fails' 1 '源码证据路径不存在' '' @('-UeInstallRoot', $engineFixture)
Write-Fixture $sourceProbe ($actualSource.Replace('Engine/Source/Fixture/Sample.cpp', 'Engine/Source/../../../escape.cpp'))
Expect 'source traversal fails even without a root' 1 '源码证据路径不安全（路径越界或控制字符）'
Write-Fixture $sourceProbe ($actualSource.Replace('Engine/Source/Fixture/Sample.cpp', 'Engine/Source/%2e%2e/%2e%2e/escape.cpp'))
Expect 'encoded source traversal fails' 1 '源码证据路径不安全（路径越界或控制字符）' '' @('-UeInstallRoot', $engineFixture)
Write-Fixture $sourceProbe $actualSource
Expect 'explicit nonexistent source root fails, never reports verified' 1 '源码路径核对未运行（显式源码根无效）' '' @('-UeInstallRoot', (Join-Path $engineFixture 'not-present'))

Write-Fixture $sourceProbe ($common + "`n源码核对状态：已完成`n" + '`Engine/Source/...`' + "`n")
Expect 'completion claim with only a placeholder fails' 1 '源码完成声明缺少具体文件定位'
Write-Fixture $sourceProbe ($static + "`n" + '`Engine/Source/...`' + "`n")
Expect 'honest unfinished placeholder stays explicitly unverified' 0 '源码路径模板不作证据' '源码完成声明缺少具体文件定位'
$foreignPath = if ([IO.Path]::DirectorySeparatorChar -eq '/') { 'Z:/DifferentMachine/UE/Engine/Source/Fixture/Sample.cpp' } else { '/different-machine/UE/Engine/Source/Fixture/Sample.cpp' }
Write-Fixture $sourceProbe ($static + "`n" + '`' + $foreignPath + '`' + "`n")
Expect 'foreign absolute source is not remapped to this checkout' 0 '源码绝对位置未覆盖：1' '源码路径存在性：已检查 1' @('-UeInstallRoot', $engineFixture)

# Directory junctions on Windows avoid requiring file-symlink privileges.
$linkType = if ([IO.Path]::DirectorySeparatorChar -eq '/') { 'SymbolicLink' } else { 'Junction' }
$sourceLink = Join-Path $engineFixture 'Engine/Source/Escaped'
$null = New-Item -ItemType $linkType -Path $sourceLink -Target $outsideFixture
[IO.File]::WriteAllText((Join-Path $outsideFixture 'Outside.cpp'), '// Temporary external fixture.', $utf8)
Write-Fixture $sourceProbe ($actualSource.Replace('Engine/Source/Fixture/Sample.cpp', 'Engine/Source/Escaped/Outside.cpp'))
Expect 'source symlink or junction fails closed' 1 '符号链接/重解析点' '' @('-UeInstallRoot', $engineFixture)
$aliasRoot = Join-Path $outsideFixture 'engine-alias'
$null = New-Item -ItemType $linkType -Path $aliasRoot -Target $engineFixture
Write-Fixture $sourceProbe $actualSource
Expect 'symlink or junction selected as the source root fails closed' 1 '源码路径核对未运行（显式源码根无效）' '' @('-UeInstallRoot', $aliasRoot)
[void][IO.Directory]::CreateDirectory((Join-Path $fixture 'assets'))
$null = New-Item -ItemType $linkType -Path (Join-Path $fixture 'assets/escape') -Target $outsideFixture
Write-Fixture $sourceProbe $static
Write-Fixture 'README.md' ($rootReadme + "`n[Linked escape](assets/escape/outside.txt)`n")
Expect 'repository link through symlink or junction fails closed' 1 '链接路径不安全'
Write-Fixture 'README.md' $rootReadme
Expect 'healthy fixture restored, no line-count gate' 0 'RESULT: PASS' '正文少于 300 行'

# The production pin has no override. To exercise the identical validator with
# synthetic source bytes, this isolated checker copy changes only that one pin.
$originalChecker = $checker
$originalCheckerText = [IO.File]::ReadAllText($checker, $utf8)
$pinPattern = '(?m)^\$preservedSourceBaselineSha256 = ''[^'']+''(?=\r?$)'
$pinMatches = [regex]::Matches($originalCheckerText, $pinPattern)
Require ($pinMatches.Count -eq 1) 'Exactly one production baseline pin must exist'
$crlfCheckerText = $originalCheckerText.Replace("`r`n", "`n").Replace("`n", "`r`n")
Require ([regex]::Matches($crlfCheckerText, $pinPattern).Count -eq 1) 'Pin detection must work in a CRLF script checkout without changing protected source hashing'
$fixtureTools = Join-Path ([IO.Path]::GetTempPath()) ('kb-quality-tools-' + [guid]::NewGuid().ToString('N'))
[void][IO.Directory]::CreateDirectory($fixtureTools)
$checker = Join-Path $fixtureTools 'check_repo.ps1'
[IO.File]::Copy((Join-Path $PSScriptRoot 'get_kb_markdown.ps1'), (Join-Path $fixtureTools 'get_kb_markdown.ps1'))
function Fixture-Sha256([byte[]]$Bytes) {
    $hash = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($hash.ComputeHash($Bytes))).Replace('-', '').ToLowerInvariant() }
    finally { $hash.Dispose() }
}
function Save-TestBaseline([object[]]$Entries, $BaselineCommit = '21b44e8fe732ad76be02024dfa4732238c2443e2') {
    $baseline = [ordered]@{ version = 1; baseline_commit = $BaselineCommit; defects = @($Entries) }
    Write-Fixture '.kb/preserved-source-defects.json' ((($baseline | ConvertTo-Json -Depth 8) + "`n").Replace("`r`n", "`n"))
    $fingerprint = Fixture-Sha256 ([IO.File]::ReadAllBytes((Join-Path $fixture '.kb/preserved-source-defects.json')))
    $replacement = '$preservedSourceBaselineSha256 = ' + "'$fingerprint'"
    $patched = $originalCheckerText.Replace($pinMatches[0].Value, $replacement)
    Require ($patched.Replace($replacement, $pinMatches[0].Value) -ceq $originalCheckerText) 'Fixture checker may change only the baseline pin'
    [IO.File]::WriteAllText($checker, $patched, $utf8)
}
$knownPath = '读书笔记/known.md'
$knownText = ($short + "`n" + '```text' + "`nPreserved unfinished source sample.`n").Replace("`r`n", "`n")
$knownOpenLine = [array]::IndexOf(($knownText -split "`r?`n"), '```text') + 1
$knownEntry = [ordered]@{ path = $knownPath; sha256 = (Fixture-Sha256 ($utf8.GetBytes($knownText))); kind = 'unclosed_code_fence'; opening_line = $knownOpenLine }
Write-Fixture $knownPath $knownText
Build-Navigation
Save-TestBaseline -Entries @($knownEntry)
Expect 'pinned exact source defect remains an explicit known defect' 0 'RESULT: PASS_WITH_KNOWN_SOURCE_DEFECTS' '未闭合代码围栏'
Require ($script:lastFixtureOutput.Contains('KNOWN_SOURCE_DEFECT: 1') -and $script:lastFixtureOutput.Contains('KNOWN_SOURCE_DEFECT unclosed_code_fence')) 'Known defects require a count and an itemized diagnostic'

Write-Fixture $knownPath ($knownText + ' ')
Expect 'one-byte change cannot inherit a preserved-source exception' 1 '保护来源缺陷基线字节变化'
Write-Fixture $knownPath $knownText
Write-Fixture $knownPath ($knownText.Replace("`n", "`r`n"))
Expect 'CRLF conversion changes exact source bytes and fails' 1 '保护来源缺陷基线字节变化'
Write-Fixture $knownPath $knownText
Write-Fixture '读书笔记/new-defect.md' $knownText
Build-Navigation
Expect 'new article with the same defect still fails' 1 '未闭合代码围栏'
Remove-Item -LiteralPath (Join-Path $fixture '读书笔记/new-defect.md')
Build-Navigation

$badLinkText = $knownText.Replace('```text', "[Missing](./absent.md)`n" + '```text')
Write-Fixture $knownPath $badLinkText
$badLinkEntry = [ordered]@{ path = $knownPath; sha256 = (Fixture-Sha256 ($utf8.GetBytes($badLinkText))); kind = 'unclosed_code_fence'; opening_line = $knownOpenLine + 1 }
Save-TestBaseline -Entries @($badLinkEntry)
Expect 'known fence exception cannot suppress another link error' 1 '断链'
Require ($script:lastFixtureOutput.Contains('KNOWN_SOURCE_DEFECT: 1')) 'The link negative must actually match the known fence'
Write-Fixture $knownPath $knownText
[IO.File]::WriteAllBytes((Join-Path $fixture $knownPath), ([byte[]]@(0xef, 0xbb, 0xbf) + $utf8.GetBytes($knownText)))
$bomEntry = [ordered]@{ path = $knownPath; sha256 = (Fixture-Sha256 ([IO.File]::ReadAllBytes((Join-Path $fixture $knownPath)))); kind = 'unclosed_code_fence'; opening_line = $knownOpenLine }
Save-TestBaseline -Entries @($bomEntry)
Expect 'known fence exception cannot suppress BOM encoding failure' 1 'BOM:'
Require ($script:lastFixtureOutput.Contains('KNOWN_SOURCE_DEFECT: 1')) 'Encoding negative must actually match the known fence'
$missingTypeText = $knownText.Replace('type: Concept', '')
Write-Fixture $knownPath $missingTypeText
$missingTypeEntry = [ordered]@{ path = $knownPath; sha256 = (Fixture-Sha256 ($utf8.GetBytes($missingTypeText))); kind = 'unclosed_code_fence'; opening_line = $knownOpenLine }
Save-TestBaseline -Entries @($missingTypeEntry)
Expect 'known source remains subject to the independent OKF type gate' 1 'type missing/empty' '' @() -Okf
Write-Fixture $knownPath $knownText

$closedText = $knownText + '```' + "`n"
Write-Fixture $knownPath $closedText
$orphanEntry = [ordered]@{ path = $knownPath; sha256 = (Fixture-Sha256 ($utf8.GetBytes($closedText))); kind = 'unclosed_code_fence'; opening_line = $knownOpenLine }
Save-TestBaseline -Entries @($orphanEntry)
Expect 'orphan baseline with no corresponding defect fails' 1 '保护来源缺陷基线孤儿'
Write-Fixture $knownPath $knownText
Save-TestBaseline -Entries @($knownEntry, $knownEntry)
Expect 'duplicate baseline entries fail' 1 'duplicate baseline path'
Save-TestBaseline -Entries @($knownEntry)
[IO.File]::AppendAllText((Join-Path $fixture '.kb/preserved-source-defects.json'), ' ', $utf8)
Expect 'any unapproved baseline byte change fails the production pin' 1 'baseline fingerprint differs'
Save-TestBaseline -Entries @($knownEntry)
$unapprovedExtra = [ordered]@{ version = 1; baseline_commit = '21b44e8fe732ad76be02024dfa4732238c2443e2'; defects = @($knownEntry, $knownEntry) }
Write-Fixture '.kb/preserved-source-defects.json' ($unapprovedExtra | ConvertTo-Json -Depth 8)
Expect 'adding a baseline entry without an independently changed pin fails' 1 'baseline fingerprint differs'
Save-TestBaseline -Entries @($knownEntry)
$baselineLf = [IO.File]::ReadAllText((Join-Path $fixture '.kb/preserved-source-defects.json'), $utf8)
Write-Fixture '.kb/preserved-source-defects.json' ($baselineLf.Replace("`n", "`r`n"))
Expect 'CRLF conversion of the pinned control JSON fails' 1 'baseline fingerprint differs'
Save-TestBaseline -Entries @($knownEntry)
$missingEntry = [ordered]@{ path = '读书笔记/absent.md'; sha256 = $knownEntry.sha256; kind = 'unclosed_code_fence'; opening_line = $knownOpenLine }
Save-TestBaseline -Entries @($missingEntry)
Expect 'orphan baseline for a missing file fails' 1 'Missing canonical knowledge file'
$unknownKind = [ordered]@{ path = $knownPath; sha256 = $knownEntry.sha256; kind = 'any_error'; opening_line = $knownOpenLine }
Save-TestBaseline -Entries @($unknownKind)
Expect 'baseline cannot broaden the defect kind' 1 'unsupported defect kind'
$arrayKind = [ordered]@{ path = $knownPath; sha256 = $knownEntry.sha256; kind = @('unclosed_code_fence'); opening_line = $knownOpenLine }
Save-TestBaseline -Entries @($arrayKind)
Expect 'array-valued kind cannot bypass exact scalar validation' 1 'unsupported defect kind'
Save-TestBaseline -Entries @($knownEntry) -BaselineCommit @('21b44e8fe732ad76be02024dfa4732238c2443e2')
Expect 'array-valued baseline commit is invalid' 1 'unexpected baseline commit'
Save-TestBaseline -Entries @($knownEntry)
Expect 'restored pinned fixture reports known debt without claiming zero defects' 0 'RESULT: PASS_WITH_KNOWN_SOURCE_DEFECTS' 'FAIL: 0'

# Restore the production checker; no baseline pin is changed in the source tree.
$checker = $originalChecker
Remove-Item -LiteralPath (Join-Path $fixture '.kb/preserved-source-defects.json')
Remove-Item -LiteralPath (Join-Path $fixture $knownPath)
Build-Navigation
Expect 'production checker restored after isolated pin tests' 0 'RESULT: PASS' 'KNOWN_SOURCE_DEFECT: 1'
Write-Output "Pinned-checker fixture retained: $fixtureTools"

Write-Output "PASS quality evidence fixtures: $cases cases"
Write-Output "Fixtures retained: $fixture ; $engineFixture ; $outsideFixture"
