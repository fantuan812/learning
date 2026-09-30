---
type: Plan
title: "知识库与 Agent 架构重构"
description: "统一内容落点、控制面事实源、Agent 执行权限和可重复架构验收。"
status: stable
verified: []
maturity: L2
updated: 2026-09-08
---

# 当前执行计划

> 知识成熟度：L2（以仓库文件审计为依据，运行验收单独记录）。

## Goal

完成知识库和 Agent 整体架构重构：保留正文路径，消除归类与权限规则冲突，提供可执行的一致性门禁。

## Scope

用户已授权工作树内架构重构。无提交、推送、永久删除授权。主线程担任协调者和共享控制面单一整合者，内容执行与验证分配独立角色。

## Current State

- 仓库：当前 Git 根；分支 main；HEAD ca551a33e9a077138500180c00a6cf2e51df6a67。
- dirty 基线：README.md、references/仓库结构.md、读书笔记/（含 PDF 与 Markdown）。
- status、tracked/staged patch、全部 dirty 文件 SHA256 存于仓库外临时目录 kb-architecture-20260907-170249。
- 原有 dirty 内容保留；2026-09-08 为满足根导航检查，在 README 只新增两个入口并验证移除新增段后与修改前内容一致。执行中发现外部并行改动：读书笔记、.gitignore、翻译脚本及缓存发生变化；保留现场，不恢复旧 hash。恢复基线另存于仓库外 kb-architecture-resume-20260908-100509。
- 上一轮计划原文保存在 [2026-09-03-combat-ai.md](2026-09-03-combat-ai.md)，其历史验收结论不代表本轮状态。

## Findings

1. 根 AGENTS 强制通用内容进入 Knowledge，与已有领域 Canonical 路由冲突。
2. 多处角色表不同步；角色文件固定模型，缺少明确内容执行/整合/验证配置。
3. 大量写作细则常驻根 AGENTS，项目 Skill 又重复全套流程，默认上下文过重。
4. aliases 存在 Inventory 与背包系统、Determinism 与确定性的非终止环。
5. 物理目录将领域、来源、证据、导航、治理并列，需要明确责任层和机械检查。
6. 基线 check_repo：429 Markdown，29 FAIL、23 WARN；已有正文元数据/证据缺失与读书笔记成熟度缺失。不得把这些既有失败当成本次引入，也不得仅为全绿自动提高成熟度。

## Proposed Taxonomy Changes

保留六个主题领域与全部正文路径。以架构清单区分 content、practice、sources、evidence、navigation、governance、automation；taxonomy 继续负责领域内部分类。Knowledge 是领域外的通用主题扩展入口，Projects 只收明确项目约束；来源型读书笔记保留原地。

## File Operations

| Action | Exact scope | Owner | Confidence |
| --- | --- | --- | --- |
| Refactor | AGENTS.md; references/agent协作与发布规则.md; references/知识组织规则.md | Agent 执行者 | 0.95 |
| Refactor | .agents/README.md; .agents/skills/knowledge-base-organizer/SKILL.md; .codex/config.toml; .codex/agents/*.toml; .codex/README.md | Agent 执行者（现有角色及三个明确新增角色） | 0.95 |
| Create | .kb/architecture.json; references/知识库架构.md | 单一整合者 | 0.95 |
| Update | .kb/taxonomy.yaml; .kb/aliases.yaml; .kb/README.md; .kb/decisions.md; .kb/plans/current.md; .kb/plans/README.md | 单一整合者 | 0.95 |
| Archive | .kb/plans/2026-09-03-combat-ai.md | 单一整合者 | 1.0 |
| Update | Knowledge/README.md; Projects/README.md; 00_Index/README.md; 00_Index/MOC.md; 00_Index/axes/知识边界与生命周期.md; index.md | 单一整合者 | 0.95 |
| Create | scripts/check_architecture.ps1; scripts/test_architecture.ps1 | 门禁执行者，完成后由单一整合者接入统一范围 | 0.95 |
| Fix | scripts/rebuild_manifest.ps1 | 单一整合者（统一文件范围、Git 失败即停止、确定性排序） | 0.99 |
| Rebuild | .kb/manifest.yaml | 单一整合者，最后机械生成 | 1.0 |
| Extend | README.md | 单一整合者，仅新增架构与 Agent 导航；保留原有内容及改动 | 0.99 |
| Refactor | scripts/check_repo.ps1; scripts/check_okf.ps1; scripts/get_kb_markdown.ps1; scripts/test_kb_scope.ps1 | 扫描范围执行者，统一 Git 可见 Markdown 范围；保留所有内容规则 | 0.99 |

新增范围必须先更新此清单。分析/验证角色只读。共享路径只由其指定写者写入；所有角色禁止更改 Git index/history/remote。

## Merge Plan / Split Plan

仅拆分控制面规则职责，知识正文无合并、拆分、迁移或删除。旧规则中有效约束通过链接保留，历史决策与学习日志不回写。

## Needs Review

- 架构审查确认权威规则无矛盾、来源材料不成为第二份 Canonical。
- Agent 配置按官方文档及当前宿主能力校验，不宣称当前任务已加载新角色。
- 新架构脚本在 Windows PowerShell 5.1 与 pwsh 运行并执行负向 fixture 测试。

## Risks

用户 dirty 内容不接管。2026-09-08 验收发现新 .codex README 缺根导航，根 README 仅追加两个入口并校验原文完整保留；仓库结构说明仍保持不变。现有正文 FAIL 单独记录；Git 已忽略的缓存与依赖不应作为知识文件扫描，统一文件范围不更改正文门禁。

## Audit Result

两路只读架构审查确认路由冲突、角色冲突和别名环，采用保留正文路径的责任分层方案。主体与扫描范围补充修改均获独立只读审查 PASS_WITH_WARNINGS；最终审查者另实跑完整架构检查，584 个路径与磁盘指标全部吻合。

## Execution Progress

- [x] 恢复执行工具，核实仓库与 dirty 基线
- [x] 执行全库基线检查并保存上一轮计划
- [x] 完成架构与 Agent 规则重构
- [x] 接入架构检查、统一扫描范围与 manifest 重建
- [x] 完成主体独立复核与实际门禁执行

## Final Validation

2026-09-08 验收快照：

| 检查 | 实际结果 |
| --- | --- |
| 架构检查 | PowerShell 7 与 Windows PowerShell 5.1 均通过；目录/入口/契约有效，35 个别名无环，584 个 manifest 路径与磁盘指标一致 |
| 架构负向回归 | 两个 PowerShell 环境各 16/16 用例通过，包含越界、重复归属、别名环及清单漂移 |
| 文件范围回归 | 两个 PowerShell 环境均通过 9 项语义断言；忽略缓存排除，已跟踪且匹配 ignore 的正文仍纳入 |
| manifest 可重复性 | 同一快照在两个 PowerShell 环境生成的 SHA256 相同；本记录更新后重新生成 |
| Agent 配置 | 8 个 TOML 角色及项目配置解析通过；角色唯一、必需字段/报告字段齐全，模型继承与 sandbox 默认符合合同 |
| Skill | 当前简单 frontmatter、名称/描述、UTF8、AGENTS 链接检查通过；官方 quick_validate 缺 PyYAML 未运行，不宣称等价通用 YAML 验证 |
| OKF | Changed、Audit、Strict 均通过；Strict 为 Scanned 582 / Conformant 582 / Excluded 2 / FAIL 0 |
| 内容质量 | check_repo 为 584 Markdown / WARN 28 / FAIL 219；无本任务架构文件失败 |
| Git | HEAD 与任务基线一致，index 空；本任务 diff 单独核对，未提交、未推送 |
| 文件范围与编码 | 39 个本任务文件全部 UTF8 无 BOM，本任务 allowlist 的 diff --check 退出 0 |

内容失败由 11 项原有技术正文问题，以及 208 项外部读书笔记问题构成：读书笔记包含 164 项缺成熟度、42 项代码围栏未闭合、2 项 README 清单缺链接。未改这些正文，也未通过降低规则或自动提高成熟度消除失败。来源内容由并行任务持续导入，以上数量仅代表本次运行快照。

原有 check_repo 含中文且为 UTF8 无 BOM；PowerShell 7 可直接执行，Windows PowerShell 5.1 直接 -File 仍有既有编码解析问题。5.1 使用临时 UTF8 BOM 副本、同源 helper 和显式 Root 验证，得到相同的 584/219 结果；不能将此写成源文件直接 -File 通过。

恢复时在仓库外保存了新的 status、diff 和外部文件 hash。README 移除本次新增两个链接后与编辑前原文一致；references/仓库结构.md、.gitignore 和翻译脚本未写入。四个读书笔记 README 在恢复期间被外部任务更新，保留当前内容，不回退旧 hash。全仓 diff --check 仅发现外部 .gitignore 末尾空行，保留其改动。

构建日志与基线在系统临时目录 kb-architecture-resume-20260908-100509；扫描双环境日志在 kb-scope-check-20260908-101250；5.1 兼容加载日志在 kb-ps51-utf8-20260908-101342。代码、文档和机器清单在仓库内，临时日志不是知识正文。

配置已通过静态检查，但未实测当前宿主加载新角色；不把文件存在视作工具权限或运行时已生效。
