# 当前执行计划

> 知识成熟度：L2（执行计划模板，结构性变更前填写）。

> 结构性变更前必须填写本节；执行过程中持续更新；完成并审计后归档。

# Goal（当前）

收口计算机与工程基础体系全面审查问题（2026-08-20）：修复格式与技术表述、校准成熟度证据、将 13 篇短专题补至 300 行以上，并同步导航、taxonomy、manifest 与审计记录。

# Scope（当前）

- 内容：基础层 16 分类、40 篇正文；修复审查发现的 C++、PageFault、Paxos、Quorum、TLS、QUIC 与 io_uring 表述，并补齐独立虚拟化专题。
- 深度：13 篇低于 300 行的新增专题补齐最小示例、配置、预期输出、Benchmark、故障矩阵和验收记录。
- 证据：7 篇没有实际 Evidence 的 L3 文档降为 L2，并明确命令计划与运行证据的边界。
- 控制面：同步基础层状态表、taxonomy、manifest、README 与 audit；历史 Lyra 记录继续作为历史快照保留。
- 验证：运行 `scripts/check_repo.ps1`、UTF-8/BOM/围栏/字面换行检查、manifest 集合与 bytes/lines 比对、`git diff --check`。

# Current State

- 2026-08-20 基础层正文 40 篇、分类 README 17 篇；13 篇审查短文及新增虚拟化专题均已达到 300 行以上。
- 5 篇字面 `` `r`n `` 元数据问题已清零；C++、PageFault、Paxos/Quorum、TLS/QUIC、io_uring 表述已修订。
- 无真实 Evidence 的 7 篇 L3 已降为 L2；运行结果落盘后再按证据升级。
- 控制面同步与最终门禁已完成：全库 386 个 Markdown（正文 315、README 71），基础域正文 40 篇；`check_repo.ps1` PASS、FAIL 0，12 条 WARN 均为既有控制面/日志/笔记/路线图短文；以下 Lyra/R4 项目状态作为历史记录保留。

- R4 四项结构性操作已执行完毕（KD-012）。
- Lyra 专项审查（R4-LYRA）修复完毕；覆盖率审查（R4-LYRA-COVERAGE）完成，补篇 49/50 已执行（覆盖率 32.4%→35.2%）。
- 批次 1+3 已执行完毕（2026-08-14 用户批准）：51 新篇 + 42/43/49 补深挖 + 边界声明；覆盖率 35.4%→**40.5%**（286/707）；check_repo PASS。
- 批次 2 已执行完毕（2026-08-14 用户指示）：52 交互系统新建 + 40 GameFeatureAction/41 动画实例/49 CommonGame UI 补深挖；覆盖率 40.5%→**42.9%**（303/707）；check_repo PASS；边界声明"批次 2 已执行、暂无已登记待补项"。
- 2026-08-18 核心覆盖扩展已执行：新增 53-56 四篇，均含真实 C++ 片段与项目文件全文附录；覆盖矩阵将新增文件标为 L2，运行态验证保留为 L3；ShooterCore 核心玩法已解除“全量范围外”口径，但剩余资产/模式仍明确待补。
- 当前审查基线为 `aca16b8`；本轮修复在该提交之后进行，提交前不得把工作树状态写成已推送事实。

# Findings（历史：R4 四项）

见 .kb/audit.md 与 review-queue 各项。四项均为结构性操作：1 项新建文档、1 项拆分迁移、1 项 README 声明、1 项门禁升级。
- R2-GATE-01：check_repo.ps1 成熟度门禁阶段 B——既有正文缺成熟度由 WARN 升级为 FAIL；当前全库缺成熟度正文为 0（R4 验证），升级后应保持 FAIL 0。
- 控制面：review-queue 四项 Status 更新；decisions.md 登记 KD-012；plans 执行进度与最终验证。

# File Operations

- R4-OBS-01：新建 00-07/03-性能工具：插桩与perf采样.md；笔记两篇头部加 canonical 指向；07 README 文件列表 + 规划更新；00 README 填充状态表（07 篇数 1→2）；references/仓库结构.md（07 章 5→6 篇）；manifest 回填新条目。
- R2-SPLIT-01：01-AI评测回放与LLM安全.md 删 §七（LLM 安全约 395 行）改指路节 + H1 收窄；新建 03-LLM-NPC安全.md（§七 逐字迁移 + 完整元数据）；AI/03 README 文件列表；游戏AI README 篇数口径；全库 grep 旧引用清理。
- R2-MOVE-01：07 README 补"调试与运行时诊断"覆盖声明小节。
- R2-GATE-01：check_repo.ps1 成熟度 WARN→FAIL 升级（保留豁免类：工作日志/笔记/方案/README/维护目录）。

# Risks

- 拆分必须逐字迁移（SHA-256 切片比对），旧文件名链接全库 grep 清理；AI/03 篇数口径同步（19 路线图若引用）。
- 新建 canonical 需满足领域门禁（基线行/最后更新/外部来源/验证入口）与 ≥300 行，否则 check_repo FAIL。
- 门禁升级后立即全量复跑，确认既有正文无缺成熟度（R4 已证为 0）。
- 每批执行后跑 check_repo，FAIL 归零再进入下一批。

# Execution Progress

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

# Final Validation

- check_repo RESULT PASS / FAIL 0（阶段 B 门禁全量复跑；新增 53-56 后复跑 PASS）。
- 批次 2 附录：40（6 文件）+ 41（2 文件）+ 49（6 文件）+ 52（17 文件）= 31 文件程序化逐字一致；四篇章节编号连续（40:一~三十三、41:一~四十、49:一~十七、52:一~十六）。新增 53-56 的 31 个项目/插件文件全文附录和引擎真实节选均通过围栏、UTF-8 与标记检查。
- 拆分迁移：§七 394 行逐字迁移，SHA-256 去空行归一后一致；7.x → 1.x 重编号完整。
- 全库断链 0；旧文件名 `01-AI评测回放与LLM安全` 残留仅 .kb 控制面（记录性提及）。
- manifest 331 条目；与磁盘 331 个 `.md` 一致，bytes/lines 重新校正。
- git diff --check 通过；源码附录代码字符、注释、条件编译和文件尾换行保留，仅代码围栏内的行尾及缩进空白统一；无 BOM（check_repo 覆盖）。
- review-queue 有效条目无 Pending；DEDUP-01/R2-DEDUP-01 标为已决策，R2-MANIFEST-01/RENAME-01 标为已执行，历史 Current/Suggested 快照保留但不可重复触发。
- 核心边界：53-56 提升的是 L1/L2 静态源码证据，不宣称 PIE、Dedicated Server、手柄设备矩阵或资产接线已经完成；外观/反馈/性能/回放/Hotfix 等剩余目录继续按覆盖矩阵标记为 L0/L1 待补。
- 前置专题边界：20-31 的新增代码块是本机 UE 5.8 函数节选，不等于整份引擎文件全文收录；运行时、平台和实验性插件条件仍按各篇正文边界验证。
