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

[C++ 语言与标准库契约实验](cpp/README.md)按标准约束、实现观测和运行边界组织可复现用例。

| 目录 | 主题 | 状态 | 关联文档 |
| --- | --- | --- | --- |
| [labs/protobuf-evolution](labs/protobuf-evolution/README.md) | 新旧 schema 的零值 presence、未知字段中转、ProtoJSON 与业务语义反例 | 已执行（protoc 36.0 / Python protobuf 7.36.0，12+12 测试；非 C++ runtime） | [02-网络通信与协议设计](../知识/07-网络与游戏服务端/服务架构与消息通信/02-网络通信与协议设计.md) |
| [labs/cpp-move](labs/cpp-move/README.md) | Copy/Move 计数：扩容、RVO、moved-from | 已执行（MSVC 2022） | [02-Copy-Move与值语义](../知识/01-编程与计算机基础/C%2B%2B语言与对象模型/02-Copy-Move与值语义.md) |
| [labs/atomic-memory-order](labs/atomic-memory-order/README.md) | 原子计数/数据竞争/release-acquire/CAS/内存序开销 | 已执行（MSVC 2022） | [02-Atomic与C++内存模型](../知识/01-编程与计算机基础/并发与同步/02-Atomic与C%2B%2B内存模型.md) |
| [labs/false-sharing](labs/false-sharing/README.md) | False Sharing Benchmark（volatile 与原子槽位） | 已执行（MSVC 2022） | [03-LockFree与FalseSharing](../知识/01-编程与计算机基础/并发与同步/03-LockFree与FalseSharing.md) |
| [server/tick-scheduler](server/tick-scheduler/README.md) | Server Main Loop / Tick 调度模拟 | 已执行（MSVC 2022） | [01-ServerMainLoop与TickScheduler](../知识/07-网络与游戏服务端/运行调度与过载保护/01-ServerMainLoop与TickScheduler.md) |
| [tests/entry-core](tests/entry-core/README.md) | 进入游戏：SHA-256/HMAC-SHA256 对标准向量自校验、票据签发/验签/过期/重放、进入状态机幂等与回滚、DS 租约与栅栏令牌、JIP 状态追赶（乱序重排）与带宽/CPU 对比 | 已执行（MinGW g++ 16.1.0） | [01-角色进入游戏完整链路](../知识/07-网络与游戏服务端/会话身份与在线服务/01-角色进入游戏完整链路.md) |
| [tests/gameplay-core](tests/gameplay-core/README.md) | Gameplay 核心机制：背包事务原子性/幂等、Buff 九类冲突、技能请求管线门禁与确定性重放、属性修正器聚合基准 | 已执行（MinGW g++ 16.1.0） | [05-背包道具完整链路](../知识/05-Gameplay与交互系统/背包装备与存档/05-背包道具完整链路.md)、[03-技能释放完整链路](../知识/05-Gameplay与交互系统/技能战斗与属性结算/03-技能释放完整链路.md)、[04-Buff系统完整链路](../知识/05-Gameplay与交互系统/技能战斗与属性结算/04-Buff系统完整链路.md) |
| [tests/damage-core](tests/damage-core/README.md) | 伤害与属性结算：修正器聚合顺序、五乘区、护甲曲线、真伤、护盾、过量、死亡/免疫拦截、单次上限、DOT 取整、确定性重放 + 结算基准 | 已执行（MinGW g++ 16.1.0） | [11-伤害与属性结算完整链路](../知识/05-Gameplay与交互系统/技能战斗与属性结算/11-伤害与属性结算完整链路.md) |
| [labs/epoll-reactor](labs/epoll-reactor/README.md) | LT/ET 就绪、部分写、半关闭与有界背压 | 已执行（Linux/g++ 14.2，7+20 契约测试；非容量基准） | [01-Socket-Epoll与Reactor](../知识/01-编程与计算机基础/操作系统与系统I-O/01-Socket-Epoll与Reactor.md) |
| [labs/profiling](labs/profiling/README.md) | 性能定位：6 种插桩样式开销（0.33ns 计数 → 310ns 日志）+ 帧预算换算、卡顿检测（中位数阈值+迟滞）、Tick 预算降级状态机 | 已执行（MinGW g++ 16.1.0） | [10-性能问题定位完整链路](../知识/08-工程实践与质量/调试与性能分析/10-性能问题定位完整链路.md) |
| [labs/skinning-contract](labs/skinning-contract/README.md) | 蒙皮空间/inverse bind/palette/权重裁剪数值反例 | 已执行（Python 3.12/Linux，16 项；非 UE 运行） | [07-动画资产与骨骼基础](../知识/04-图形动画与物理仿真/动画求值与角色表现/07-动画资产与骨骼基础.md) |
| [cpp/expected-contract](cpp/expected-contract/README.md) | 内嵌载荷、动态资源、布局与异常传播边界 | 已执行（Linux / GCC14 / libstdc++14；O0/O2 各 28 项，非性能基准） | [异常、类型系统与标准库实现 §3.1](../知识/01-编程与计算机基础/C++语言与对象模型/02-异常、类型系统与标准库实现.md#31-内嵌存储与错误路径的保证边界) |
| [cpp/foundation-contracts](cpp/foundation-contracts/README.md) | 一次性原子发布、固定容量对象池、variant/span/expected 字节解码合同 | 已执行（Linux / GCC14；O0/O2/UBSan 功能测试及3类隔离负例，非性能基准） | [Atomic](../知识/01-编程与计算机基础/并发与同步/02-Atomic与C++内存模型.md)、[对象布局与池](../知识/01-编程与计算机基础/C++语言与对象模型/01-对象布局、虚函数与内存分配.md)、[类型与错误](../知识/01-编程与计算机基础/C++语言与对象模型/02-异常、类型系统与标准库实现.md) |
| labs/page-fault | Page Fault / mmap 实验 | 规划（需 Linux） | W1-10 |
| labs/cache-benchmark | Cache stride / branch / SIMD Benchmark | 规划 | W1-12 |
| [algorithms/astar](algorithms/astar/README.md) | A* 独立 oracle、LRU 故障对照与真实路径返回成本；撤回旧常数计时，旧日志保全 | 已执行（Linux / GCC14 / C++17；功能合同与固定网格观测，非历史项目补测或线上容量） | [07-假人AI完整链路](../知识/06-游戏AI/战斗战术与机器人/07-假人AI完整链路.md)、[11-AI与寻路时间预算](../知识/07-网络与游戏服务端/运行调度与过载保护/11-AI与寻路时间预算.md) |
| [algorithms/aoi](algorithms/aoi/README.md) | AOI Simulator（100/1K/10K Entity，正确率+比较次数+耗时） | 已执行（MSVC 2022） | [05-AOI与InterestManagement](../知识/07-网络与游戏服务端/状态复制与兴趣管理/05-AOI与InterestManagement.md) |
| server/bot-budget | Bot AI/寻路时间预算 | 规划 | W4-01 |

## 使用方式

1. 正文声明 L3 及以上时，在"验证与基准"节链接对应证据目录。
2. 实验先落 `src/` 与 `scripts/`，运行后把原始输出存入 `results/`，再写结论。
3. 性能指标优先记录 P50/P95/P99、CPU、内存、吞吐、带宽中的适用项，并记录测试环境。
4. 每新增 3 篇正文至少产生 1 个 Demo/Test/Benchmark（执行方案风险控制第 1 条）。

## 状态与验收

- 本目录属于维护基础设施，不参与知识成熟度门禁，但每个实验 README 必须满足上述统一格式。
- 新增证据后同步更新本表、[仓库结构](../references/仓库结构.md) 与根 [README](../README.md)。

## A* 队列与重开合同

[A*正确性反例与正文双语言验收](algorithms/astar-contract/README.md)：可变堆键旧错/新对、stale、reopen、目标出队与同键合同；直接抽取正文示例验证，不是性能基准。
