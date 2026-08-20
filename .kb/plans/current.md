---
type: Plan
title: "当前执行计划"
description: "知识库结构性变更的当前计划、范围、风险与验证状态。"
tags:
  - knowledge-base
  - governance
  - okf
  - taxonomy
  - navigation
status: stable
verified: []
maturity: L2
updated: 2026-08-20
---

# 当前执行计划

> 知识成熟度：L2（执行计划模板，结构性变更前填写）。

> 结构性变更前必须填写本节；执行过程中持续更新；完成并审计后归档。

# Goal（当前）

在已发布的 OKF 全库迁移之上重整知识结构：先建立可审计的领域 MOC、跨域 Canonical 关系、生命周期路由和 Obsidian 属性视图，再决定是否需要物理目录迁移。第一阶段保护现有正文路径、成熟度、验证事件和链接。

# Scope（当前）

- 创建 `00_Index/domains/` 下六个 Domain MOC，按真实目录组织学习路径和领域边界。
- 创建 `00_Index/axes/` 下“跨域主题”“知识边界与生命周期”两个横向视图，明确唯一 Primary Canonical 与提升流程。
- 将 `00_Index/Knowledge.base` 扩展为 All / Review / Evidence / Project 四个视图；Bases 只消费 Properties，不成为新事实源。
- 更新 Global MOC、根索引、README、taxonomy、结构/Obsidian/Agent 协作规则和控制面；最终机械重建 manifest。
- 不移动、重命名、拆分或合并知识正文；不改正文 H1、`type`、`maturity`、`verified`、`sources` 与既有 Canonical 路径。
- 不修改或纳入用户 `.obsidian`；Obsidian 对 `.base` 的自动规范化按外部工作区变化保留并语义审查。

# Current State

- 发布基线：`main` 提交 `9beb653aa267a4a0dbcf44a32fa21ce205cc3ce5` 已推送到 `origin/main`；发布后双方 SHA 相同，ahead/behind 为 `0/0`。
- 发布提交包含经独立审核的 399 个路径；369 篇 legacy 只增加最小 frontmatter，提交父节点、路径/blob manifest 与 staged diff 指纹一致。
- 发布后 Obsidian 自动规范化工作区 `00_Index/Knowledge.base`，该变化未进入上一提交；本阶段从该工作区语义版本继续扩展。
- 物理目录仍按六大知识域、系统实战、evidence 和工作流/控制面组织，主干层级可用；已证问题集中在少数跨域主题和导航不足，而非整棵目录树失效。
- `.obsidian` 仍是用户未跟踪内容，workspace 动态状态继续忽略。

# Findings（当前）

- 六大领域的根 README 与全部二级目录 README 均存在；主知识树保持“Domain → Subdomain → Topic”即可承载当前内容。
- `Knowledge/` 与 `Projects/` 仍是骨架，不应把六大领域机械搬入；只有无既有领域归属的 universal 内容或明确项目身份才启用。
- NavMesh、行为树、GAS/Buff、网络复制、Dedicated Server、性能证据和背包存在真实跨域关系，适合用 Primary/Secondary 链接表达。
- `00-计算机与工程基础` 的编号有缺口或重复，但路径稳定；学习顺序应由 MOC 表达，当前不重编号。
- 大规模物理移动会同时影响相对链接、README 清单、manifest、Obsidian backlinks 和 Git 历史；第一阶段没有足够证据承担该成本。

# Proposed Taxonomy Changes

- taxonomy 升级为 virtual-first 导航：保留物理 Canonical 路径，通过 Domain MOC、横向 axes 与 Base 形成三轴视图。
- 三轴分别是 Canonical Domain、OKF knowledge type、scope/lifecycle；目录不再承担全部学习顺序和跨域关系。
- 物理重构状态保持 `gated`；必须先完成小域或单一主题族的链接影响和回滚审计。

# File Operations（当前）

| Action | Paths | Confidence | Reason |
| --- | --- | ---: | --- |
| CreateMOC | `00_Index/domains/*.md`、`00_Index/axes/*.md` | 0.94 | 建立虚拟领域和横向关系，不复制正文 |
| Extend | `00_Index/MOC.md`、`README.md`、`index.md`、`00_Index/README.md` | 0.96 | 暴露新的导航入口 |
| Extend | `00_Index/Knowledge.base` | 0.90 | 使用官方支持的 filter、groupBy 和 folder 过滤构建四个视图 |
| UpdateMetadata | `.kb/taxonomy.yaml` | 0.92 | 登记 virtual-first 导航契约和物理试点闸门 |
| CreatePolicy | `references/agent协作与发布规则.md` | 0.95 | 补齐 Skill 所需的协作与发布速查入口 |
| CreateTool | `scripts/rebuild_manifest.ps1` | 0.96 | 将 manifest 的机械重算固化为可重复命令 |
| Rebuild | `.kb/manifest.yaml` | 0.98 | 收口后机械同步 Markdown 路径、bytes、lines、maturity |

# Merge Plan

第一阶段无正文合并。重叠主题只指定 Primary/Secondary 职责并互链。

# Split Plan

第一阶段无正文拆分、移动或重命名。mixed 内容的 Split 只作为后续准入规则，不在本阶段执行。

# Needs Review（当前）

- `TAXONOMY-PILOT-01`：是否在虚拟导航稳定后选择一个小域或单一主题族做物理迁移试点。
- `OBSIDIAN-CONFIG-01`：是否选择性共享稳定 `.obsidian` 配置仍未授权；本轮继续不接管。
- Lyra 46/47、DS 构建和笔记归属保留为后续语义治理候选，不与本轮 MOC 创建混合执行。

# Risks（当前）

- MOC 变成第二份正文：索引只写职责、顺序和链接，不复制主题内容。
- Canonical 冲突：跨域主题必须标一个 Primary，并把其余页面限定为实现、证据、生产或案例视角。
- Base 语法或自动规范化漂移：使用 Obsidian 官方语法，保留运行时规范化结果并检查语义 diff。
- 导航与实际路径漂移：所有新增本地链接逐一解析，manifest 最终从磁盘机械重建。
- 提前物理重构：taxonomy 明确 `gated`，没有旧→新映射、入链清单和回滚方案时禁止 Move/Rename/Split/Merge。

# Audit Result（当前）

执行前与执行后只读审计均已完成：目录、领域边界、基础学习顺序、跨域重叠、Knowledge/Projects/evidence/工作流边界和 Obsidian Base 官方语法由独立 Agent 交叉核对；Phase A 语义审计 P0/P1/P2/P3 均为 0。新增导航目标全部存在，知识正文路径/内容、maturity 与 verified 未改变。

# Execution Progress（当前）

1. [x] 提交并推送 OKF 全库迁移；远端 SHA 与本地 `9beb653` 一致。
2. [x] 并行盘点目录、领域边界、跨域主题和 Obsidian Bases 官方语法。
3. [x] 选择 virtual-first：冻结物理 Canonical 路径，拒绝立即全量搬迁。
4. [x] 创建六个 Domain MOC、两个横向 axes 和四个 Base 视图。
5. [x] 同步 taxonomy、全局导航、协作规范、decision/review queue/audit/log。
6. [x] 重建 manifest，运行 OKF、仓库、链接、编码和差异门禁。
7. [x] 独立审核第一阶段结果，形成物理试点准入清单。

# Final Validation（当前）

- `check_okf Changed`：Scanned 22 / Conformant 22 / FAIL 0；Strict：Scanned 401 / Conformant 401 / Excluded 2 / FAIL 0。
- `check_repo.ps1`：403 个 Markdown，PASS / FAIL 0 / WARN 21；新增 WARN 均为短 MOC/axes 导航，属于用途预期。
- manifest 与磁盘 403/403，路径、kind、bytes、lines、maturity 差异均为 0；Windows PowerShell 5.1 与 pwsh 生成结果完全相同。
- 本地链接、UTF-8/BOM、`git diff --check` 均通过；tracked 知识正文变化 0，tracked `.obsidian` 变化 0。
- 独立语义审计 P0–P3 为 0；`TAXONOMY-PILOT-01` 保持 Pending，不在 Phase A 偷跑物理迁移。
- 发布状态：OKF 迁移提交已推送；本结构整理阶段尚未提交、尚未推送。

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
