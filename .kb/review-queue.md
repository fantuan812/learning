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
