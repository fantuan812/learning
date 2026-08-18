# 06-世界模拟与运行时 · 知识库（首批 11 篇已落地）

> 定位：MMO / 实时游戏服务器 Runtime 层——服务器如何"把世界跑起来"。重点不是 Web 后端，而是 Main Loop、Tick、Entity、Scene、AOI、同步裁剪、跨服迁移与时间预算。
> 与算法层分工：[游戏算法](../../游戏算法/01-寻路与图论/README.md) 讲"AOI 是什么"（九宫格/十字链表/Quadtree），本层讲"AOI 在真实服务器 Tick、Scene 与 send 流程里怎么工作"，两者不合并。
> 与平台层分工：[05-UE Dedicated Server平台化](<../05-UE Dedicated Server平台化/README.md>) 讲实例生命周期与平台契约，本层讲进程内部的世界模拟。
> 状态：首批 11 篇正文已落地（01/02/03/04/05/07/08/11/12/13/14）；06 SpatialQuery、09 动态分线、10 大规模战斗仍保留为下一批规划。

## 规划主题

| 主题 | 说明 |
| --- | --- |
| 服务器 Main Loop / Tick Scheduler / Timer | 帧循环、固定步长、时间片、优先级 |
| Timer / Timing Wheel | 时间源、时间轮/最小堆、取消、重试、任务预算与关服 Drain |
| Entity 生命周期 | 创建/销毁、组件、Entity 池、Ghost/Mirror Entity |
| Scene / Map / Zone | 场景归属、区域划分、加载与卸载 |
| AOI 与 Interest Management | Enter/Leave View、Update List、限流、网络批量 |
| Spatial Partition | 空间分区在 Tick 与同步流程中的位置 |
| Entity Ownership | 所有权归属、fencing、迁移与客户端预测边界 |
| Cross Zone Migration / 跨服迁移 / 分线 | 跨场景/跨服迁移协议、断线重连边界 |
| 动态负载均衡 | 分线扩容、场景迁移、玩家分布调整 |
| 大规模战斗 / 降级策略 | 战斗结算批处理、视野裁剪、技能降级 |
| 时间预算 | Tick 预算、AI Tick Budget、服务器寻路预算 |
| 世界时间 / GameClock | wall/monotonic/game/tick 时间分类、暂停/减速、跨服对齐 |
| 背压与过载保护 | 队列积压、高水位、降级阶梯、滞回恢复 |
| 世界 Snapshot / 故障恢复 | 快照+增量事件、崩溃恢复状态机、客户端 Reconcile、事件日志幂等 |

> 编号映射与 [方案](../../方案/知识体系完善执行方案.md) W2 保持一致：02=Timer、06=SpatialQuery（规划）、07=Entity Ownership、08=跨 Zone 迁移、09=动态分线（规划）、10=大规模战斗/降级（规划）；已落地篇为 01/02/03/04/05/07/08/11/12/13/14。

## 核心链路示例（AOI）

```text
Player 进入 Scene → 加入 AOI Cell → 计算 Interest Set → EnterView/LeaveView
→ Update List → 限流 → Network Batch → 客户端 Replication
```

## 与现有知识库的引用

- 算法基础：[游戏算法/01-寻路与图论](../../游戏算法/01-寻路与图论/README.md)（AOI、空间索引、寻路）。
- 平台层：[05-UE Dedicated Server平台化](<../05-UE Dedicated Server平台化/README.md>)。
- 客户端同步：[游戏知识/06-网络同步](../../游戏知识/06-网络同步/README.md)。
- 压测与验收：[游戏测试与质量](../../游戏测试与质量/README.md)。

## 填充状态

| 主题 | 状态 | 篇数 | 成熟度分布 |
| --- | --- | --- | --- |
| [Main Loop / Tick Scheduler](01-ServerMainLoop与TickScheduler.md) | 已落地 | 1 | L4 |
| [Timer / Timing Wheel](02-Timer时间轮与延迟任务.md) | 已落地 | 1 | L2 |
| [Entity 生命周期](03-Entity生命周期与组件模型.md) | 已落地 | 1 | L3 |
| [Scene / Map / Zone](04-Scene-Map-Zone与实例管理.md) | 已落地 | 1 | L3 |
| [AOI 与 Interest Management](05-AOI与InterestManagement.md) | 已落地 | 1 | L4 |
| SpatialQuery / 空间索引 | 规划 | 0 | - |
| [Entity Ownership / Authority](07-EntityOwnership与Authority.md) | 已落地 | 1 | L2 |
| [跨 Zone / 跨服迁移](08-跨Zone与跨服迁移.md) | 已落地 | 1 | L2 |
| 动态分线 / 负载均衡 | 规划 | 0 | - |
| 大规模战斗 / 降级策略 | 规划 | 0 | - |
| [时间预算 / AI Tick](11-AI与寻路时间预算.md) | 已落地 | 1 | L4 |
| [世界时间 / GameClock](12-世界时间确定性与GameClock.md) | 已落地 | 1 | L2 |
| [世界 Snapshot / 故障恢复](13-世界Snapshot与故障恢复.md) | 已落地 | 1 | L3 |
| [背压与过载保护](14-运行时背压与过载保护.md) | 已落地 | 1 | L4 |

## 文件列表

| 文件 | 简介 | 成熟度 |
| --- | --- | --- |
| [01-ServerMainLoop与TickScheduler](01-ServerMainLoop与TickScheduler.md) | 固定步长主循环、accumulator、catch-up/drop、时间预算、UE5.8 对照；含本机模拟 | L4 |
| [02-Timer时间轮与延迟任务](02-Timer时间轮与延迟任务.md) | Tick/monotonic/business time、时间轮/堆选型、取消/重试、owner queue、过载与恢复 | L2 |
| [03-Entity生命周期与组件模型](03-Entity生命周期与组件模型.md) | Entity ID/世代、Spawn/Despawn、组件模型、实体池、Ghost/Mirror、悬垂防护 | L3 |
| [04-Scene-Map-Zone与实例管理](04-Scene-Map-Zone与实例管理.md) | Map/Scene/Zone 三层、玩家归属、实例分配、Zone 迁移、动态加载、线程/进程分布 | L3 |
| [05-AOI与InterestManagement](05-AOI与InterestManagement.md) | Move→Cell Change→Interest Diff→Enter/Leave→限流→批量；含本机模拟器（100/1K/10K 实体） | L4 |
| [07-EntityOwnership与Authority](07-EntityOwnership与Authority.md) | 单写者、generation、owner/fence、客户端预测、迁移封存、故障接管与审计 | L2 |
| [08-跨Zone与跨服迁移](08-跨Zone与跨服迁移.md) | 冻结快照、delta、路由 epoch、重连、Timer/AOI 衔接、回滚与故障注入 | L2 |
| [11-AI与寻路时间预算](11-AI与寻路时间预算.md) | 三层频率/预算、可中断寻路、分帧更新、降级阶梯；复用 A*/Tick 实验证据 | L4 |
| [12-世界时间确定性与GameClock](12-世界时间确定性与GameClock.md) | 五类时间边界、GameClock 单一权威、暂停/减速、跨服对齐、worldLag 监控 | L2 |
| [13-世界Snapshot与故障恢复](13-世界Snapshot与故障恢复.md) | 快照+增量事件、崩溃恢复状态机、客户端 Reconcile、事件日志幂等 | L3 |
| [14-运行时背压与过载保护](14-运行时背压与过载保护.md) | 背压传导、降级优先级、四类过载阈值、滞回恢复；复用 Tick/AOI 实验证据 | L4 |

## 验收门禁

- 正文 ≥ 300 行、UTF-8 无 BOM；满足领域门禁：知识基线、最后更新、非代码外部来源、验证与基准入口。
- 每篇给出：数据结构 + Tick 流程 + Mermaid 数据流 + 边界/失败路径 + 验证建议（容量、带宽、CPU、p99）。
- 完成 ≥1 篇后，同步更新本 README 填充状态表、[服务端 README](../README.md)、[仓库结构](../../references/仓库结构.md) 与根 [README](../../README.md)。
