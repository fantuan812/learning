---
type: Plan
title: "当前执行计划"
description: "知识库结构性变更的当前计划、范围、风险与验证状态。"
tags:
  - knowledge-base
  - governance
  - okf
status: stable
verified: []
maturity: L2
updated: 2026-08-20
---

# 当前执行计划

> 知识成熟度：L2（执行计划模板，结构性变更前填写）。

> 结构性变更前必须填写本节；执行过程中持续更新；完成并审计后归档。

# Goal（当前）

已将 369 篇 legacy Markdown 分域迁移到本库最小 OKF frontmatter 合同，并使 profile 范围通过 Strict；正文、路径、链接与成熟度证据保持不变，迁移任务不写入用户 `.obsidian`。

# Scope（当前）

- 元数据：每篇只补 `type`、首个 H1 对应的 `title`、`status: stable`、`verified: []` 与已有 `maturity`；没有显式成熟度的支持文档记为 L0。
- 事实边界：不自动补 `description`、`tags`、`sources`、`generated` 或 `updated`，不把迁移本身当作验证事件。
- 类型：README 默认 `Index`；Evidence 实验 README 为 `Evidence`；系统实战为 `Project`；工作日志为 `Experience`；笔记为 `Reference`；知识正文按分域审查映射，含糊项保守使用 `Concept`。
- 批次：支持/证据 39；基础 57；算法+AI 47；服务端 54；游戏知识 172。每批验证后再进入下一批。
- 排除：保留 `.agents/skills/**/SKILL.md` 的 Skill frontmatter 合同与 `learning/log.md` 的只增不改历史格式，共 2 篇 profile 排除项。
- 控制面：更新 decision、review queue、audit、bundle log、learning log 与 manifest；不移动 taxonomy、正文或链接。

# Current State

- 继续迁移基线：HEAD `32c35ddbd968563fd3fd3f9b152a9fa205bd77db`，分支 `main`；上一轮 OKF/Obsidian 未提交改动保持在工作树，本轮基线已另存仓库外。
- 全库 392 个 Markdown；profile 范围 Scanned 390、Conformant 390、Legacy 0、Excluded 2。
- 五批实际完成 39 + 57 + 47 + 54 + 172 = 369 篇；每篇只新增 7 行最小 frontmatter。
- 迁移内容初验 Changed/Strict PASS / FAIL 0；`check_repo.ps1` PASS / FAIL 0 / WARN 13。manifest 将在控制面收口后做最终 392/392 重建。
- `.obsidian` 三个稳定未跟踪配置保持原哈希；`workspace.json` 是已忽略的动态本机状态，不纳入迁移。

# Findings（当前）

- 六个知识域和支持目录均已完成迁移；369 篇的首个 H1 已安全写入 `title`，正文已有成熟度原样进入 YAML，未由迁移者重评或升级。
- `status: stable` 只表示现有条目可被消费，不表示已验证；`verified: []` 明确没有验证事件。
- YAML title 使用双引号安全转义；正文采用字节前缀方式插入，避免归一化原文件换行或源码附录。
- 评审无 P0；P1 的分批、逐批门禁和可回滚范围均已执行。

# Proposed Taxonomy Changes

- 不新增、移动或重命名任何知识域；Domain → Subdomain → Topic 与 canonical 路径完全保持。
- 本轮只提高文档级可交换性与 Obsidian Properties 覆盖率；细化 `Concept` 类型可在后续触碰时进行。

# File Operations（当前）

| Action | Paths | Confidence | Reason |
| --- | --- | ---: | --- |
| UpdateMetadata（已完成） | 369 篇 legacy Markdown | 0.90 | 分域补最小 OKF frontmatter，正文与路径不变 |
| Extend（收口中） | `.kb/decisions.md`、`review-queue.md`、`audit.md`、`plans/current.md`、`log.md` | 0.95 | 记录授权、批次、边界与验证事实 |
| Rebuild（待最终重建） | `.kb/manifest.yaml` | 0.95 | 每批机械同步 paths/bytes/lines/maturity，最终做 392/392 精确校验 |

# Merge Plan

无正文合并；只在文件开头插入元数据。

# Split Plan

无正文拆分、移动或重命名。

# Needs Review（当前）

- 用户已明确“继续做迁移”，`OKF-MIGRATION-01` 已按分域方案执行；低置信度类型统一降级为 `Concept`，未做结构动作。
- 是否共享更多 `.obsidian` 配置仍未授权，本轮不接管未跟踪配置。

# Risks（已控制）

- 369 文件的大 diff 已拆为 5 个不重叠批次，每批保留路径/type/title/maturity 计数与门禁结果。
- H1 中的引号、冒号和特殊字符统一转义，Changed/Strict 已通过。
- 330 篇在批次前保存正文哈希并全量复核一致；先执行的 39 篇由严格 7 行前缀、`+7/-0` 和 OKF 门禁证明正文未删改。
- 无成熟度的支持文档使用 L0 代表“未形成知识成熟度证据”，不等同于内容错误；不得据此降低已有 L1–L5。

# Audit Result（当前）

主线程与独立只读终验均完成：Windows PowerShell 5.1 与 `pwsh` 的 Changed/Strict/Audit 均为 Scanned 390、Conformant 390、Legacy 0、Excluded 2、FAIL 0、PASS；`check_repo` PASS / FAIL 0 / WARN 13，369 篇均为 `+7/-0`，330 篇正文去前缀哈希与基线一致。独立验收提出的历史快照易误读项已通过显式 supersede/note 收口。

# Execution Progress（当前）

1. [x] 冻结上一轮未提交工作树与 `.obsidian` 基线。
2. [x] 盘点 369 篇 legacy，并完成分域 type/title/maturity 只读分析。
3. [x] 完成批次化方案评审；确认无 P0，吸收 YAML 转义与 L0 语义边界。
4. [x] 执行支持/证据、基础、算法+AI、服务端、游戏知识五批迁移。
5. [x] 更新 profile、decision、review queue、现行规则、audit/log，并重建最终 manifest。
6. [x] 完成 Changed/Audit/Strict、双 PowerShell、正文哈希、全库门禁与独立审计。

# Final Validation（当前）

- profile 范围：Scanned 390、Conformant 390、Legacy 0、Excluded 2；Changed/Audit/Strict 均无 FAIL。
- Windows PowerShell 5.1 与 `pwsh` 都通过 Changed 和 Strict。
- `check_repo.ps1` PASS；内部链接、UTF-8/BOM、围栏、`git diff --check` 无新增错误。
- manifest 与磁盘 392/392，逐条 bytes/lines/maturity 一致；正文去前缀哈希抽样/全量验证一致。
- `.obsidian` 稳定配置哈希保持，workspace 动态状态原样保留；无 commit、无 push。

---

# Historical Snapshot（2026-08-20 早前：计算机基础审查）

- R4 四项结构性操作已执行完毕（KD-012）。
- Lyra 专项审查（R4-LYRA）修复完毕；覆盖率审查（R4-LYRA-COVERAGE）完成，补篇 49/50 已执行（覆盖率 32.4%→35.2%）。
- 批次 1+3 已执行完毕（2026-08-14 用户批准）：51 新篇 + 42/43/49 补深挖 + 边界声明；覆盖率 35.4%→**40.5%**（286/707）；check_repo PASS。
- 批次 2 已执行完毕（2026-08-14 用户指示）：52 交互系统新建 + 40 GameFeatureAction/41 动画实例/49 CommonGame UI 补深挖；覆盖率 40.5%→**42.9%**（303/707）；check_repo PASS；边界声明"批次 2 已执行、暂无已登记待补项"。
- 2026-08-18 核心覆盖扩展已执行：新增 53-56 四篇，均含真实 C++ 片段与项目文件全文附录；覆盖矩阵将新增文件标为 L2，运行态验证保留为 L3；ShooterCore 核心玩法已解除“全量范围外”口径，但剩余资产/模式仍明确待补。
- 当前审查基线为 `aca16b8`；本轮修复在该提交之后进行，提交前不得把工作树状态写成已推送事实。

## Historical Findings（R4 四项）

见 .kb/audit.md 与 review-queue 各项。四项均为结构性操作：1 项新建文档、1 项拆分迁移、1 项 README 声明、1 项门禁升级。
- R2-GATE-01：check_repo.ps1 成熟度门禁阶段 B——既有正文缺成熟度由 WARN 升级为 FAIL；当前全库缺成熟度正文为 0（R4 验证），升级后应保持 FAIL 0。
- 控制面：review-queue 四项 Status 更新；decisions.md 登记 KD-012；plans 执行进度与最终验证。

## Historical File Operations

- R4-OBS-01：新建 00-07/03-性能工具：插桩与perf采样.md；笔记两篇头部加 canonical 指向；07 README 文件列表 + 规划更新；00 README 填充状态表（07 篇数 1→2）；references/仓库结构.md（07 章 5→6 篇）；manifest 回填新条目。
- R2-SPLIT-01：01-AI评测回放与LLM安全.md 删 §七（LLM 安全约 395 行）改指路节 + H1 收窄；新建 03-LLM-NPC安全.md（§七 逐字迁移 + 完整元数据）；AI/03 README 文件列表；游戏AI README 篇数口径；全库 grep 旧引用清理。
- R2-MOVE-01：07 README 补"调试与运行时诊断"覆盖声明小节。
- R2-GATE-01：check_repo.ps1 成熟度 WARN→FAIL 升级（保留豁免类：工作日志/笔记/方案/README/维护目录）。

## Historical Risks

- 拆分必须逐字迁移（SHA-256 切片比对），旧文件名链接全库 grep 清理；AI/03 篇数口径同步（19 路线图若引用）。
- 新建 canonical 需满足领域门禁（基线行/最后更新/外部来源/验证入口）与 ≥300 行，否则 check_repo FAIL。
- 门禁升级后立即全量复跑，确认既有正文无缺成熟度（R4 已证为 0）。
- 每批执行后跑 check_repo，FAIL 归零再进入下一批。

## Historical Execution Progress

1. 用户批准执行剩余四项（2026-08-14）。
2. R2-SPLIT-01 已执行：01 改名收窄《AI评测回放》（1649 行）+ 新建 03-LLM-NPC安全（419 行，§七 逐字迁移 SHA-256 一致）；13 处引用 + AI/03 README + 游戏AI README 同步；check_repo PASS。
3. R2-MOVE-01 已执行：07 README 补"运行时调试"覆盖声明（L13-16 覆盖范围节），文件不迁移。
4. R2-GATE-01 已执行：check_repo.ps1 成熟度门禁阶段 A→B（既有缺成熟度 WARN→FAIL），全库复跑 FAIL 0；脚本 BOM 已修复（Windows PowerShell 5.1 解析要求）。
5. R4-OBS-01 已执行：新建 00-07/03-性能工具：插桩与perf采样.md（326 行 L2，含验证与基准/故障案例节）；笔记两篇加 canonical 指向；07/00 README、references/仓库结构.md、manifest（321 条目）同步。
6. Lyra 专项审查（R4-LYRA）完成：报告写入 audit.md，候选登记 review-queue。
7. Lyra 修复已执行（2026-08-14 用户批准）：LYRA-DUP-01（46 §八 分工声明+转发）、LYRA-LINK-01/02/03（19 路线图、40-43 补链、44 去重补链）、LYRA-LOG-01（40-42/47 日志同步）、P3（45/46 补链 48、47 术语/失败模式清理）；check_repo PASS，review-queue 状态更新。
8. Lyra 覆盖率补篇已执行（2026-08-14 用户批准）：49-Lyra-UI控件与表现源码（2 写作子代理产出，558 行正文 + 11 附录逐字一致）、50-Lyra-设置系统与GameSettings源码（342 行正文 + 10 附录行数全 OK）；LYRA-COV-03（39 插件地图 16 插件补全、41/42/44 空心标注）；12 README/19 路线图/39 总览篇数 47→49、manifest 325 条目同步；覆盖率 32.4%→35.4%；check_repo PASS。
9. Lyra 批次 1+3 已执行（2026-08-14 用户批准）：新建 51-Lyra-GAS扩展与能力费用源码（551 行正文 + 附录 A 22 文件逐字一致）；42 篇追加武器实例章节（+5 附录）、43 篇追加 VerbMessage 章节（+5 附录）、49 篇追加 NumberPop/ContextEffects 章节（+6 附录，MeshText.cpp 尾随空行修复）；12 README/19 路线图边界声明 + 篇数口径 49→50；manifest 326 条目同步（清理 R3 垃圾前缀 + 补录 LLM-NPC 条目）；覆盖率 35.4%→**40.5%**（286/707）；check_repo PASS。
10. Lyra 批次 2 已执行（2026-08-14 用户指示）：新建 52-Lyra-交互系统源码（600 行正文 + 17 附录逐字一致，附录路径补全 Source/LyraGame 前缀）；40 篇追加 GameFeatureAction 家族（529 行 + 6 附录）、49 篇追加 CommonGame UI 管理层（约 390 行 + 6 附录）、41 篇追加动画实例基类（149 行 + 2 附录）；12 README/19 路线图/39 总览篇数 50→51、边界声明批次 2 完成；manifest 327 条目同步；覆盖率 40.5%→**42.9%**（303/707）；check_repo PASS（52 篇"预留"→"保留"占位词清零）。

11. 2026-08-17 审查收口：修复终端品质进阶道具条件、39-52 导航/完成清单/日期口径、控制面旧快照和 review queue 状态；manifest 重新计算；源码附录统一代码围栏内的行尾及缩进空白并补充格式归一说明。
12. 2026-08-18 核心源码覆盖：新增 53（生成/移动/状态）、54（ReplicationGraph/GFCM/GameFeatures/ModularGameplayActors）、55（输入重映射/Latency Marker/AimAssist）、56（ShooterCore TDM/淘汰消息/Accolade/GAS 命中上下文）；同步 12 README、19 路线图和 L0-L3 覆盖矩阵。
13. 2026-08-18 前置专题源码证据：20-31 逐篇补入 UE 5.8 实际函数，覆盖 Iris、Mass/StateTree、WorldPartition、Landscape/Foliage、Sequencer/MoviePipeline、Enhanced Input、CommonUI、MVVM、GameplayTasks、Trace、Lumen/MegaLights 和 ProceduralVegetationEditor；原有示意代码保留为概念说明。

## Historical Final Validation

- check_repo RESULT PASS / FAIL 0（阶段 B 门禁全量复跑；新增 53-56 后复跑 PASS）。
- 批次 2 附录：40（6 文件）+ 41（2 文件）+ 49（6 文件）+ 52（17 文件）= 31 文件程序化逐字一致；四篇章节编号连续（40:一~三十三、41:一~四十、49:一~十七、52:一~十六）。新增 53-56 的 31 个项目/插件文件全文附录和引擎真实节选均通过围栏、UTF-8 与标记检查。
- 拆分迁移：§七 394 行逐字迁移，SHA-256 去空行归一后一致；7.x → 1.x 重编号完整。
- 全库断链 0；旧文件名 `01-AI评测回放与LLM安全` 残留仅 .kb 控制面（记录性提及）。
- manifest 331 条目；与磁盘 331 个 `.md` 一致，bytes/lines 重新校正。
- git diff --check 通过；源码附录代码字符、注释、条件编译和文件尾换行保留，仅代码围栏内的行尾及缩进空白统一；无 BOM（check_repo 覆盖）。
- review-queue 有效条目无 Pending；DEDUP-01/R2-DEDUP-01 标为已决策，R2-MANIFEST-01/RENAME-01 标为已执行，历史 Current/Suggested 快照保留但不可重复触发。
- 核心边界：53-56 提升的是 L1/L2 静态源码证据，不宣称 PIE、Dedicated Server、手柄设备矩阵或资产接线已经完成；外观/反馈/性能/回放/Hotfix 等剩余目录继续按覆盖矩阵标记为 L0/L1 待补。
- 前置专题边界：20-31 的新增代码块是本机 UE 5.8 函数节选，不等于整份引擎文件全文收录；运行时、平台和实验性插件条件仍按各篇正文边界验证。
