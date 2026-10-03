---
type: Index
title: "Evidence · 实验与基准证据库"
status: stable
verified: []
maturity: L0
---
# Evidence · 实验与基准证据库

> 定位：为 L3~L5 知识文档提供可验证证据。原则：**保存原始数据，禁止只留结论**；公司代码无法公开时使用脱敏 Demo、伪数据、相对性能数据与可复现实验替代。
> 约定：每个证据目录统一结构 `<topic>/README.md + src/ + tests/ + scripts/ + data/ + results/`；README 必须包含：问题、假设、环境、运行方式、输入、指标、原始结果、结论、局限、关联知识文档。
> 规则：只有实际运行并保存了原始输出的实验才可被 L4 文档引用；未运行的实验必须在 README 标注"待执行"。

## 证据目录

| 目录 | 主题 | 状态 | 关联文档 |
| --- | --- | --- | --- |
| [labs/protobuf-evolution](labs/protobuf-evolution/README.md) | 新旧 schema 的零值 presence、未知字段中转、ProtoJSON 与业务语义反例 | 已执行（protoc 36.0 / Python protobuf 7.36.0，12+12 测试；非 C++ runtime） | [02-网络通信与协议设计](../游戏服务端/01-架构与网络/02-网络通信与协议设计.md) |
| [labs/cpp-move](labs/cpp-move/README.md) | Copy/Move 计数：扩容、RVO、moved-from | 已执行（MSVC 2022） | [02-Copy-Move与值语义](../00-计算机与工程基础/01-C++核心/02-Copy-Move与值语义.md) |
| [labs/atomic-memory-order](labs/atomic-memory-order/README.md) | 原子计数/数据竞争/release-acquire/CAS/内存序开销 | 已执行（MSVC 2022） | [02-Atomic与C++内存模型](../00-计算机与工程基础/04-C++并发与内存模型/02-Atomic与C++内存模型.md) |
| [labs/false-sharing](labs/false-sharing/README.md) | False Sharing Benchmark（volatile 与原子槽位） | 已执行（MSVC 2022） | [03-LockFree与FalseSharing](../00-计算机与工程基础/04-C++并发与内存模型/03-LockFree与FalseSharing.md) |
| [server/tick-scheduler](server/tick-scheduler/README.md) | Server Main Loop / Tick 调度模拟 | 已执行（MSVC 2022） | [01-ServerMainLoop与TickScheduler](../游戏服务端/06-世界模拟与运行时/01-ServerMainLoop与TickScheduler.md) |
| [tests/entry-core](tests/entry-core/README.md) | 进入游戏：SHA-256/HMAC-SHA256 对标准向量自校验、票据签发/验签/过期/重放、进入状态机幂等与回滚、DS 租约与栅栏令牌、JIP 状态追赶（乱序重排）与带宽/CPU 对比 | 已执行（MinGW g++ 16.1.0） | [01-角色进入游戏完整链路](../系统实战/01-角色进入游戏完整链路.md) |
| [tests/gameplay-core](tests/gameplay-core/README.md) | Gameplay 核心机制：背包事务原子性/幂等、Buff 九类冲突、技能请求管线门禁与确定性重放、属性修正器聚合基准 | 已执行（MinGW g++ 16.1.0） | [05-背包道具完整链路](../系统实战/05-背包道具完整链路.md)、[03-技能释放完整链路](../系统实战/03-技能释放完整链路.md)、[04-Buff系统完整链路](../系统实战/04-Buff系统完整链路.md) |
| [tests/damage-core](tests/damage-core/README.md) | 伤害与属性结算：修正器聚合顺序、五乘区、护甲曲线、真伤、护盾、过量、死亡/免疫拦截、单次上限、DOT 取整、确定性重放 + 结算基准 | 已执行（MinGW g++ 16.1.0） | [11-伤害与属性结算完整链路](../系统实战/11-伤害与属性结算完整链路.md) |
| labs/epoll-reactor | epoll LT/ET + Reactor 最小实现 | 代码已建，待 Linux 执行 | [01-Socket-Epoll与Reactor](../00-计算机与工程基础/07-Linux系统编程/01-Socket-Epoll与Reactor.md) |
| [labs/profiling](labs/profiling/README.md) | 性能定位：6 种插桩样式开销（0.33ns 计数 → 310ns 日志）+ 帧预算换算、卡顿检测（中位数阈值+迟滞）、Tick 预算降级状态机 | 已执行（MinGW g++ 16.1.0） | [10-性能问题定位完整链路](../系统实战/10-性能问题定位完整链路.md) |
| labs/page-fault | Page Fault / mmap 实验 | 规划（需 Linux） | W1-10 |
| labs/cache-benchmark | Cache stride / branch / SIMD Benchmark | 规划 | W1-12 |
| [algorithms/astar](algorithms/astar/README.md) | A* 二叉堆 vs 线性扫描 + 路径缓存（补齐工作日志缺失项） | 已执行（MSVC 2022） | [07-假人AI完整链路](../系统实战/07-假人AI完整链路.md) |
| [algorithms/aoi](algorithms/aoi/README.md) | AOI Simulator（100/1K/10K Entity，正确率+比较次数+耗时） | 已执行（MSVC 2022） | [05-AOI与InterestManagement](../游戏服务端/06-世界模拟与运行时/05-AOI与InterestManagement.md) |
| server/bot-budget | Bot AI/寻路时间预算 | 规划 | W4-01 |

## 使用方式

1. 正文声明 L3 及以上时，在"验证与基准"节链接对应证据目录。
2. 实验先落 `src/` 与 `scripts/`，运行后把原始输出存入 `results/`，再写结论。
3. 性能指标优先记录 P50/P95/P99、CPU、内存、吞吐、带宽中的适用项，并记录测试环境。
4. 每新增 3 篇正文至少产生 1 个 Demo/Test/Benchmark（执行方案风险控制第 1 条）。

## 状态与验收

- 本目录属于维护基础设施，不参与知识成熟度门禁，但每个实验 README 必须满足上述统一格式。
- 新增证据后同步更新本表、[仓库结构](../references/仓库结构.md) 与根 [README](../README.md)。
