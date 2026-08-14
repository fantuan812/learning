# 当前执行计划

> 知识成熟度：L2（执行计划模板，结构性变更前填写）。

> 结构性变更前必须填写本节；执行过程中持续更新；完成并审计后归档。

# Goal

执行第四轮审计（R4）遗留 review-queue 四项结构性操作（2026-08-14 用户批准）：R4-OBS-01 性能笔记归位、R2-SPLIT-01 AI 03-01 拆分、R2-MOVE-01 GameplayDebugger 归属、R2-GATE-01 成熟度门禁阶段 B。

# Scope

- R4-OBS-01：笔记/插桩测试 + 笔记/perf性能分析 收敛为 canonical——新建 `00-计算机与工程基础/07-Linux系统编程/03-性能工具：插桩与perf采样.md`（≥300 行，L2 成熟度，含领域门禁五项要素），笔记保留为速查并加"正式知识见 canonical"指向；同步 07/00 README、references/仓库结构.md、manifest。
- R2-SPLIT-01：游戏AI/03-评测与安全/01-AI评测回放与LLM安全.md（2036 行双主题）拆分为《AI 评测与回放》（01 收窄）+《LLM NPC 安全》（新篇 03，原 §七 逐字迁移）；§四 回放与算法 04-05 互链已补；同步 AI/03 README、游戏AI README、19 路线图、全库链接。
- R2-MOVE-01：游戏知识/07-UI与性能优化/05-GameplayDebugger与运行时调试.md 归属错位——采用低风险方案：07 README 补"调试工具"覆盖声明（不搬文件，避免跨分类链接重构）。
- R2-GATE-01：check_repo.ps1 成熟度门禁阶段 B——既有正文缺成熟度由 WARN 升级为 FAIL；当前全库缺成熟度正文为 0（R4 验证），升级后应保持 FAIL 0。
- 控制面：review-queue 四项 Status 更新；decisions.md 登记 KD-012；plans 执行进度与最终验证。

# Current State

- R4 审计与 P1/P2/三条 DUP 修复已落地（36 文件变更）；review-queue 剩余 4 项 Pending 待执行。
- 仓库 039a990 + 未提交变更，工作树含 R4 修复。

# Findings

见 .kb/audit.md 与 review-queue 各项。四项均为结构性操作：1 项新建文档、1 项拆分迁移、1 项 README 声明、1 项门禁升级。

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

# Final Validation

- check_repo RESULT PASS / FAIL 0（阶段 B 门禁全量复跑）。
- 拆分迁移：§七 394 行逐字迁移，SHA-256 去空行归一后一致；7.x → 1.x 重编号完整。
- 全库断链 0；旧文件名 `01-AI评测回放与LLM安全` 残留仅 .kb 控制面（记录性提及）。
- manifest 321 条目（320 → +2 新增 −1 改名净 +1 = 321）；与磁盘 321 个 .md 一致。
- git diff --check 通过；无 BOM（check_repo 覆盖）。
- review-queue 剩余 Pending：DEDUP-01（KD-004 维持现状，已决策）、R2-MANIFEST-01（R3 已执行但条目状态标注滞后，已补注记）、R2-DEDUP-01（KD-004 维持现状）、RENAME-01（KD-005 已执行，旧条目状态滞后）——均为历史记录性条目，实质已闭环。
