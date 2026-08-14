# Needs Review

> 知识成熟度：L2（低置信度操作队列，人工确认后执行）。

低于 0.75 confidence 的结构性操作登记在此，人工确认后才执行。

## DEDUP-01（39-47 附录去重）

Current: 39-47 九篇附录跨篇重复粘贴 16 个源码文件（7 对文章；LyraGameInstance、LyraAbilitySet 各出现 3 次）。

Suggested: 附录改为"单篇收录 + 他篇链接"，或抽取共享附录文件。

Confidence: 0.9（重复事实）/ 0.5（是否执行）

Reason: 用户此前明确要求"每篇都出现完整源码，而不是指向源码在哪"；去重方案与该要求冲突，需用户确认后才可执行。

Suggested action: 保持现状或按用户新决定执行

Status: Pending

> 2026-08-13 决策：**已执行**（KD-009）。用户"继续处理剩余事项"确认后，01-38 H1 统一为"UE 引擎源码分析 NN：主题"（33 个文件；05/06/07/38 原已达标），39-48 Lyra 系列保持"UE5.8 Lyra 源码解析 NN：主题"；全库 grep 无旧标题残留。

---

## R2-SPLIT-01（AI 03-01 拆分）

Current: 游戏AI/03-评测与安全/01-AI评测回放与LLM安全.md（2036 行双主题）。

Suggested: 拆为《AI 评测与回放》《LLM NPC 安全》两篇；§四 回放与确定性压缩为指路节并与算法 04-05 双向互链。

Confidence: 0.75

Reason: §七（LLM NPC 安全，约 395 行）可独立成篇；§四 与算法 04-05 实质重叠仅单向链。

Suggested action: 拆分（需迁移计划）

Status: Pending

## R2-MOVE-01（07-05 调试工具归属）

Current: 游戏知识/07-UI与性能优化/05-GameplayDebugger与运行时调试.md。

Suggested: 迁至引擎基础/独立调试分类，或 README 补覆盖声明（33KB 通用运行时调试内容，README 覆盖范围未提调试主题）。

Confidence: 0.7

Reason: 分类名与内容错位；迁移涉及跨分类导航与链接更新。

Suggested action: 迁移 / 声明

Status: Pending

## R2-GATE-01（成熟度字段入门禁 + 批量补标）

Current: 全库约 129 篇正文缺"知识成熟度"行；check_repo.ps1 不检查该字段，只查知识基线/最后更新/官方链接。

Suggested: check_repo 增加"本次新增/修改正文必须带成熟度行"（已有逻辑）扩展为"既有正文缺成熟度按目录分批 FAIL 阈值"；同时按 L0-L5 口径批量补标约 129 篇。

Confidence: 0.9（门禁缺口事实）/ 0.7（是否本轮执行）

Reason: 写作规范 §2 要求每篇标注；门禁与规范脱节导致欠账长期不可见。

Suggested action: 先补标后加固门禁（或反向，需用户定序）

Status: Pending

## R2-MANIFEST-01（manifest 索引填充）

Current: .kb/manifest.yaml 为空骨架（documents: {}）。

Suggested: 由 kb_scanner 回填文档索引（路径/域/成熟度/大小），作为 RAG 与后续审计基线。

Confidence: 0.8

Reason: Phase 1 要求维护 manifest；空索引使控制面无法回答"仓库当前有哪些文档"。

Suggested action: 填充（机械生成，主线程执行）

Status: Pending

> 2026-08-14 决策：**已执行**（R3）。机械回填 320 个文档（kind/maturity/bytes/lines），版本升至 2；check_repo 复跑 PASS。

## R2-DEDUP-01（概念层双写收敛）

Current: 01-07/08/09 与 01-04、02-03/04/08/10、04-01/02、04-00 FAQ/清单双源、08-08 与服务端 04 系列等多组概念层重复详述。

Suggested: 主责篇保留详述，他篇收敛为摘要+链接；04-00 问答/清单改为单一来源（分篇）并注明同步规则。

Confidence: 0.7-0.95（按组）

Reason: 收敛涉及正文删改与互链，属于结构性去重；与"每篇完整可读"的既有要求需平衡。

Suggested action: 分批去重（先 exact 级：01-08/09 对比表、04-00 FAQ）

Status: Pending

> 2026-08-13 决策：**维持现状**（KD-004）。用户此前明确要求"每篇都出现完整源码"；去重方案与该要求冲突，不执行。跨篇重复作为已知成本记录，后续若用户改变要求再执行。

## SPLIT-01（47 篇拆分）

Current: 47-Lyra-调试工具与扩展源码.md（正文 1522 行，20+ 组件）。

Suggested: 拆分"Lyra 扩展插件"为 48 号独立篇，或声明为目录级速查篇。

Confidence: 0.6

Reason: 粒度远粗于 40-43 单一链路文章；拆分影响系列编号与导航。

Suggested action: 拆分 / 维持

Status: Pending

## SPLIT-02（算法 04-01 拆分）

Current: 游戏算法/04-确定性与基准工程/01-动态寻路确定性与基准测试.md（1832 行）。

Suggested: 按 §一~§六 拆为 3-4 篇（动态寻路 / NavMesh 工程 / 避障与数值鲁棒 / 确定性基准）。

Confidence: 0.65

Reason: 单篇承担 4-6 个独立篇目的工作量，分类名盖不住内容。

Suggested action: 拆分 / 改名"寻路工程与确定性"

Status: Pending

## SPLIT-03（服务端 04-01 拆分）

Current: 游戏服务端/04-平台与可靠性/01-鉴权限流幂等灾备与可观测性.md（1134 行八主题）。

Suggested: 按方案 W7-10.1 拆分，主题归还 02-数据与业务 系列。

Confidence: 0.6

Reason: 八主题合辑与 02 系列单主题篇粒度不一致；已有规划 TODO（P1 未动）。

Suggested action: 拆分

Status: Pending

## RENAME-01（46 文件名）

Current: 46-Lyra-AI队伍与调试源码.md，H1 为"AI 机器人与队伍系统"（调试主题实际在 47）。

Suggested: 文件名改为 46-Lyra-AI机器人与队伍源码.md，或标题补充调试范围。

Confidence: 0.55

Reason: 文件名与标题/内容范围不一致；改文件名需同步 39/44/README/19 路线图引用。

Suggested action: 改名 / 维持

Status: Pending

> 2026-08-13 决策：**已执行**（KD-005）。`46-Lyra-AI队伍与调试源码.md` → `46-Lyra-AI机器人与队伍源码.md`，13 处引用（39/40/41/42/43/44/45/47、12 README、19 路线图、03 README、46 自查命令、.kb/audit.md）全部同步。

## SPLIT-01/02/03 与 STYLE-01（拆分与标题模板）

> 2026-08-13 决策：**延后**（KD-006）。三项拆分与 H1 模板统一属于大规模结构变更，需独立计划（内容迁移清单、链接影响面、导航同步）后分批执行，避免仓促拆分破坏正文；其中 SPLIT-03 已挂靠方案 W7-10.1 的既有 TODO。恢复执行时从本队列取回。

> 2026-08-13 更新：SPLIT-01/02/03 已按 .kb/plans/current.md 迁移计划执行完毕（KD-007）：47 拆出 48 插件篇；算法 04 拆为 01-总览 + 02-05 四篇；服务端 04 按 W7-10.1 拆为 00-总览 + 01-08 八篇；正文逐字迁移并完成全库链接/导航同步。STYLE-01（H1 模板统一）仍延后。

## STYLE-01（12 目录标题模板）

Current: 01-38 的 H1 存在 4+ 套命名模板（"NN · 主题" / "UE 引擎源码分析 NN：主题" / 裸标题 / 编号内嵌）。

Suggested: 统一为"UE 引擎源码分析 NN：主题"（20-35 补编号）。

Confidence: 0.6

Reason: 影响系列归属辨识与检索；只改标题不改文件名，但涉及 20+ 文件。

Suggested action: 批量统一

Status: Pending

<!-- 模板（有内容时取消注释并填写）：
## KB000XXX

Current:

    Inbox/xxx.md

Suggested:

    Knowledge/<Domain>/<Subdomain>/<Topic>/

Confidence:

    0.68

Reason:

The document contains both general knowledge and project-specific configuration.

Suggested action:

Split

Status:

Pending
-->
