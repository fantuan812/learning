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

系统整理与深度优化“00-计算机与工程基础”知识体系：从孤立专题升级为网状贯通的底层工程底座。建立清晰的“底座原理 → 机制设计 → 游戏工程落地（UE5客户端/服务端/算法/质量）”推导链路；完善领域级 Domain MOC、总 README 与 16 个子域 README；补充跨专题互链以消除 33 篇知识孤岛；保持物理路径与 OKF 元数据稳定，确保全部仓库与格式门禁 100% 通过。

# Scope（当前）

- 领域导航升级：将 `00_Index/domains/计算机与工程基础.md` 升级为具备 16 子域 40 专题全景图、五大工程角色学习路径（客户端/引擎、服务端、算法/AI、性能与质量、分布式系统）、跨域工程映射网格及证据链索引的权威 Domain MOC。
- 领域根入口升级：更新 `00-计算机与工程基础/README.md`，给出三层推导架构图、完整的 40 篇填充矩阵与成熟度/类型分布、子域边界与编号稳定性说明。
- 16 子域 README 深度标准化：将 16 个二级 README 从简易清单升级为规范的子域工程手册（定位与核心问题、主题矩阵与状态、逻辑学习顺序、游戏与引擎工程映射、前置/后置跨域依赖）。
- 消除正文知识孤岛：为 33 篇缺乏内部链接的正文补充结构化的“关联知识与前置/后置专题”小节，建立底座内与跨域的网状互链。
- 控制面与工具链同步：更新 `.kb/taxonomy.yaml`、`.kb/decisions.md`（KD-017）、重建 `.kb/manifest.yaml`，运行 `check_okf.ps1` 与 `check_repo.ps1` 校验。
- 边界与约束：依据 KD-016，保持现有 40 篇正文物理文件名和路径稳定（避免断链与 Git 历史断裂），不变更各篇已审查的 maturity/verified，不修改用户 `.obsidian`。

# Current State

- 发布基线：`main` 提交 `9beb653aa267a4a0dbcf44a32fa21ce205cc3ce5` 已推送到 `origin/main`；发布后双方 SHA 相同，ahead/behind 为 `0/0`。
- 发布提交包含经独立审核的 399 个路径；369 篇 legacy 只增加最小 frontmatter，提交父节点、路径/blob manifest 与 staged diff 指纹一致。
- 发布后 Obsidian 自动规范化工作区 `00_Index/Knowledge.base`，该变化未进入上一提交；本阶段从该工作区语义版本继续扩展。
- 物理目录仍按六大知识域、系统实战、evidence 和工作流/控制面组织，主干层级可用；已证问题集中在少数跨域主题和导航不足，而非整棵目录树失效。
- `.obsidian` 仍是用户未跟踪内容，workspace 动态状态继续忽略。

# Findings（当前）

- 基础底座已具备 16 个子域、40 篇正文（均 ≥300 行），覆盖 C++、OS、体系结构、网络、编译、分布式、数学、安全与工程效能，具备深厚的知识积累。
- 导航层次严重不足：原 `00_Index/domains/计算机与工程基础.md`（仅 51 行）仅含 16 个二级 README 链接，缺乏 40 篇专题展开、角色学习路径与跨域知识映射。
- 子域 README 高度骨架化：08、09、10、11、14、16 等多个子域 README 仅 14~19 行，缺乏结构化的专题表格、成熟度标注、游戏研发映射和依赖说明。
- 正文孤岛现象严重：40 篇正文中多达 33 篇内部 Markdown 链接数为 0，未形成知识网络。
- 编号与命名存在历史缺口或冲突：04 并发存在两个 02 且缺 01；07 系统编程存在两个 03 且缺 02；08 缺 02；13、14、15、16 存在无编号与带编号混用。依据 KD-016 决策，物理路径保持稳定以防止全库断链，通过 MOC 与 README 提供规范的语义学习序列。

# Proposed Taxonomy Changes

- 保持 16 子域物理目录与 40 篇正文文件名不变，保持 Canonical 路径权威性。
- 在 `00_Index/domains/计算机与工程基础.md` 和各子域 README 中提供规范化的语义主题全景和多维度学习路径。
- 同步 `.kb/taxonomy.yaml` 中的子域描述，确保与实际覆盖完全一致。

# File Operations（当前）

| Action | Paths | Confidence | Reason |
| --- | --- | ---: | --- |
| Extend | `00_Index/domains/计算机与工程基础.md` | 0.98 | 升级为包含 16 子域 40 专题、五大角色路径、跨域映射和证据链的 Domain MOC |
| Extend | `00-计算机与工程基础/README.md` | 0.98 | 升级三层架构图、填充状态表、成熟度分布与子域边界规范 |
| Extend | `00-计算机与工程基础/*/README.md` (16 篇) | 0.96 | 全面标准化 16 个子域 README：定位、专题矩阵、学习顺序、工程落地、跨域依赖 |
| Extend | `00-计算机与工程基础/**/*.md` (33 篇孤岛正文) | 0.95 | 补齐“关联知识与前置/后置专题”小节，消除知识孤岛，织密知识网络 |
| UpdateMetadata | `.kb/decisions.md` | 0.95 | 记录 KD-017 计算机与工程基础知识体系整理优化决策 |
| Rebuild | `.kb/manifest.yaml` | 0.99 | 机械同步所有被更新文件的 lines、bytes 与校验项 |

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

1. [x] 全面盘点“00-计算机与工程基础”16 个子域、40 篇核心专题，梳理完整知识依赖图谱与跨域工程映射。
2. [x] 升级 `00_Index/domains/计算机与工程基础.md` 为权威 Domain MOC（160 行，覆盖 40 专题矩阵、五大角色学习拓扑、业务映射网格及证据链索引）。
3. [x] 升级 `00-计算机与工程基础/README.md` 领域总入口（三层推导架构、完整填充矩阵与物理路径稳定性规范）。
4. [x] 体系化升级 16 个子域的 `README.md`，统一结构为核心定位、专题矩阵、学习顺序、工程落地与跨域导航。
5. [x] 深度排查发现并彻底重写 02-01（对象布局虚函数）与 03-01（Concepts 与 Ranges）中残留的历史重复占位内容，恢复为 370+ 行深度实战技术专著。
6. [x] 为全量 40 篇正文补齐结构化“关联知识与工程落地”小节，消除 33 篇知识孤岛，使每篇均具备 5~14 条前后置与业务互链。
7. [x] 登记架构决策 KD-017 到 `.kb/decisions.md`。
8. [x] 机械重建 `.kb/manifest.yaml`（404 篇），全量通过 Changed、Strict 及 check_repo 质量门禁。

# Final Validation（当前）

- `check_okf Changed`：Scanned 59 / Conformant 59 / FAIL 0 / RESULT: PASS。
- `check_okf Strict`：Scanned 402 / Conformant 402 / Excluded 2 / FAIL 0 / RESULT: PASS。
- `check_repo.ps1`：404 个 Markdown（正文 330，README 74），FAIL 0 / RESULT: PASS。
- 质量元数据：版本缺失 0、日期缺失 0、官方链接缺失 0、源码占位 0。
- 领域质量门禁：基线缺失 0、日期缺失 0、来源缺失 0、验证入口缺失 0、旧规范引用缺失 0。
- 相对链接检查：全库涉及“00-计算机与工程基础”的相对路径 100% 存在，断链数为 0。
- 正文深度：基础域 40 篇正文行数均 ≥300 行（真实技术内容，机械重复归零）。
- 孤岛清零：基础域 40 篇正文内部 Markdown 互链数均在 5~14 条，零外链文档数清零。

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
