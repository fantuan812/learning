# 当前执行计划

> 知识成熟度：L2（执行计划模板，结构性变更前填写）。

> 结构性变更前必须填写本节；执行过程中持续更新；完成并审计后归档。

# Goal（当前）

收口 Lyra 39-52 批次后的审查问题（2026-08-17）：修正文档口径与导航，更新控制面和 manifest，保留逐字源码附录的证据边界，并完成全库复验。

# Scope（当前）

- 文档口径：工作日志的进阶道具完备性按“有出边的品质”表达；Lyra 系列统一为 39-52，根 README、分类 README、路线图和交叉阅读同步。
- 控制面：current plan、audit、review queue 只把当前状态作为可执行事实，历史发现保留为历史记录；已执行/已决策条目不再标记 Pending。
- 索引：重新计算 327 个 Markdown 条目的 bytes/lines，确保 `.kb/manifest.yaml` 与磁盘一致。
- 空白策略：源码附录完整保留代码字符、注释、条件编译和文件尾换行；仅统一代码围栏内的行尾及缩进空白以通过 diff 门禁，并在附录说明格式归一；普通 Markdown 继续执行 whitespace 检查。
- 验证：运行 `scripts/check_repo.ps1`、全库链接/围栏检查、manifest 统计和 `git diff --check`。

# Current State

- R4 四项结构性操作已执行完毕（KD-012）。
- Lyra 专项审查（R4-LYRA）修复完毕；覆盖率审查（R4-LYRA-COVERAGE）完成，补篇 49/50 已执行（覆盖率 32.4%→35.2%）。
- 批次 1+3 已执行完毕（2026-08-14 用户批准）：51 新篇 + 42/43/49 补深挖 + 边界声明；覆盖率 35.4%→**40.5%**（286/707）；check_repo PASS。
- 批次 2 已执行完毕（2026-08-14 用户指示）：52 交互系统新建 + 40 GameFeatureAction/41 动画实例/49 CommonGame UI 补深挖；覆盖率 40.5%→**42.9%**（303/707）；check_repo PASS；边界声明"批次 2 已执行、暂无已登记待补项"。
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

# Final Validation

- check_repo RESULT PASS / FAIL 0（阶段 B 门禁全量复跑；批次 2 复跑同样 PASS）。
- 批次 2 附录：40（6 文件）+ 41（2 文件）+ 49（6 文件）+ 52（17 文件）= 31 文件程序化逐字一致；四篇章节编号连续（40:一~三十三、41:一~四十、49:一~十七、52:一~十六）。
- 拆分迁移：§七 394 行逐字迁移，SHA-256 去空行归一后一致；7.x → 1.x 重编号完整。
- 全库断链 0；旧文件名 `01-AI评测回放与LLM安全` 残留仅 .kb 控制面（记录性提及）。
- manifest 327 条目；与磁盘 327 个 `.md` 一致，bytes/lines 重新校正。
- git diff --check 通过；源码附录代码字符、注释、条件编译和文件尾换行保留，仅代码围栏内的行尾及缩进空白统一；无 BOM（check_repo 覆盖）。
- review-queue 有效条目无 Pending；DEDUP-01/R2-DEDUP-01 标为已决策，R2-MANIFEST-01/RENAME-01 标为已执行，历史 Current/Suggested 快照保留但不可重复触发。
