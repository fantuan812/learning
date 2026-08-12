# 06-世界模拟与运行时 · 知识库（建设中）

> 定位：MMO / 实时游戏服务器 Runtime 层——服务器如何"把世界跑起来"。重点不是 Web 后端，而是 Main Loop、Tick、Entity、Scene、AOI、同步裁剪、跨服迁移与时间预算。
> 与算法层分工：[游戏算法](../../游戏算法/01-寻路与图论/README.md) 讲"AOI 是什么"（九宫格/十字链表/Quadtree），本层讲"AOI 在真实服务器 Tick、Scene 与 send 流程里怎么工作"，两者不合并。
> 与平台层分工：[05-UE Dedicated Server平台化](<../05-UE Dedicated Server平台化/README.md>) 讲实例生命周期与平台契约，本层讲进程内部的世界模拟。
> 状态：骨架（规划已建立），正文按优先级分批填充。

## 规划主题

| 主题 | 说明 |
| --- | --- |
| 服务器 Main Loop / Tick Scheduler / Timer | 帧循环、固定步长、时间片、优先级 |
| Entity 生命周期 | 创建/销毁、组件、Entity 池、Ghost/Mirror Entity |
| Scene / Map / Zone | 场景归属、区域划分、加载与卸载 |
| AOI 与 Interest Management | Enter/Leave View、Update List、限流、网络批量 |
| Spatial Partition | 空间分区在 Tick 与同步流程中的位置 |
| Entity Ownership | 所有权归属、迁移、客户端预测边界 |
| Cross Zone Migration / 跨服迁移 / 分线 | 跨场景/跨服迁移协议、断线重连边界 |
| 动态负载均衡 | 分线扩容、场景迁移、玩家分布调整 |
| 大规模战斗 / 降级策略 | 战斗结算批处理、视野裁剪、技能降级 |
| 时间预算 | Tick 预算、AI Tick Budget、服务器寻路预算 |

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
| Main Loop / Tick Scheduler / Timer | 规划 | 0 | - |
| Entity 生命周期 | 规划 | 0 | - |
| Scene / Map / Zone | 规划 | 0 | - |
| AOI 与 Interest Management | 规划 | 0 | - |
| Spatial Partition | 规划 | 0 | - |
| Entity Ownership | 规划 | 0 | - |
| 跨服迁移 / 分线 | 规划 | 0 | - |
| 动态负载均衡 | 规划 | 0 | - |
| 大规模战斗 / 降级策略 | 规划 | 0 | - |
| 时间预算 / AI Tick | 规划 | 0 | - |

## 验收门禁

- 正文 ≥ 300 行、UTF-8 无 BOM；满足领域门禁：知识基线、最后更新、非代码外部来源、验证与基准入口。
- 每篇给出：数据结构 + Tick 流程 + Mermaid 数据流 + 边界/失败路径 + 验证建议（容量、带宽、CPU、p99）。
- 完成 ≥1 篇后，同步更新本 README 填充状态表、[服务端 README](../README.md)、[仓库结构](../../references/仓库结构.md) 与根 [README](../../README.md)。
