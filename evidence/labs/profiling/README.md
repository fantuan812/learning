---
type: Evidence
title: "性能定位可运行证据（插桩开销 / 卡顿检测 / Tick 预算与降级）"
description: "量化常见插桩方式的开销，并验证卡顿检测与预算降级状态机的判定规则。"
tags:
  - evidence
  - profiling
  - performance
  - gameplay
status: stable
verified: []
maturity: L0
updated: 2026-09-11
---

# 性能定位可运行证据

> 证据范围：本机可编译运行的 2 个程序，**17 条断言全部通过**，含每调用开销（ns）与帧预算换算；不涉及 UE Insights/Trace、Linux perf 与线上环境。按本仓库约定，`evidence/` 属维护基础设施，其 README 的 maturity 字段不参与知识成熟度门禁。

性能定位的日常其实只有两件事：**在热路径上留得下证据**，以及**把"卡了一下"变成可判定的数据**。本目录把这两件事量化：插桩本身要花多少、什么写法会把帧预算吃光、卡顿怎么判定才不误报、预算超了之后降级状态机怎么升怎么降。

## 问题

1. 一次计数器自增、一次原子自增、一次作用域计时、一次格式化日志，各自要多少钱？
2. "在热路径上打日志"这句话到底有多贵？在什么调用量级上会直接吃掉帧预算？
3. 采样（每 N 次采一次）能省多少，代价是什么？
4. 一段帧时间里怎么判定"卡顿事件"？如何避免把一次 3 帧的连续卡顿数成 3 次、又如何避免抖动误报？
5. Tick 超预算后降级该怎么升、怎么降？怎样避免"降级/恢复"反复抖动？
6. 预算记账本身的开销是多少（它是否值得每 Tick 都做）？

## 假设

- 帧预算按 60 fps 计 = 16.6 ms；"热路径调用量"分为两种视角：单系统 1 000 次/帧、大世界逐实体 100 000 次/帧。
- 卡顿判定采用**中位数**基准而非 p99：p99 本身会被卡顿污染，用它算阈值会让阈值越抬越高，最后检测不到卡顿。
- 降级需要迟滞（hysteresis）：连续 N 次超预算才升级，连续 M 次达标记才降级。
- 预算记账要求单 Tick 开销可忽略，否则"为了观测而观测"本身会成为开销。

## 环境

| 项目 | 值 |
| --- | --- |
| 主机 | Windows（MINGW64_NT-10.0-26200，x86_64，16 逻辑核） |
| 工具链 | MSYS2 MinGW-w64 `g++` 16.1.0，`-std=c++17 -O2` |
| 依赖 | 仅 C++ 标准库 |
| 未使用 | UE Insights / Trace、UnrealStats、Linux perf、DS 环境 |

## 运行方式

```powershell
& (Join-Path $RepoRoot 'evidence/labs/profiling/scripts/build_run.ps1')
```

```bash
bash evidence/labs/profiling/scripts/run_all.sh
```

脚本重新编译并把**未经修改的原始输出**写入 `results/`。

## 输入

- 开销基准：每个样式 200 轮 × 20 000 次调用，取 p50 ns/次。
- 卡顿检测：30 秒 60 fps 序列（1 800 帧），基线 7–9 ms；注入 12 次卡顿（25–80 ms），其中 3 次为连续 2–3 帧的"级联卡顿"；另有纯抖动序列作为误报对照组。
- 预算降级：4 个系统（ai 2 ms / physics 3 ms / network 2 ms / gameplay 4 ms），构造"连续超预算"、"交替好坏"、"持续恢复"三类输入。
- 记账基准：200 000 Tick × 4 系统。

## 指标与原始结果

**断言：`profiling_overhead` pass=7 fail=0；`hitch_and_budget` pass=10 fail=0**

每调用开销（p50）：

```text
1 bare counter increment          0.33 ns
2 atomic relaxed increment        4.01 ns
3 scoped steady_clock timer      50.03 ns
4 strcmp lookup per call          5.68 ns
5 snprintf label per call       274.25 ns
6 snprintf + fwrite per call    309.90 ns
7 sampled timer (1/100 calls)     0.96 ns
```

帧预算换算（热路径 100 000 次/帧）：

```text
bare counter          0.032 ms/frame    0.19%
atomic counter        0.384 ms/frame    2.31%
scoped timer          5.003 ms/frame   30.14%
snprintf label       27.467 ms/frame  165.46%
snprintf + fwrite    30.846 ms/frame  185.82%
sampled timer(1/100)  0.096 ms/frame    0.58%
```

卡顿检测与预算记账：

```text
threshold = 24.0 ms (2.5 x median, floor 1.5 frame)
12 injected hitch events -> 12 detected events, 18/18 hitching frames, 0 false positives
budget accounting: 1.7 ns/tick, 0.4 ns/system
```

原始输出：[profiling_overhead.txt](results/profiling_overhead.txt) ｜ [hitch_and_budget.txt](results/hitch_and_budget.txt)

## 结论

1. **计数器几乎免费，日志几乎致命**：裸自增 0.33 ns，而"格式化 + 缓冲写" 309.9 ns——相差约 **940 倍**。在 100 000 次/帧的热路径上，前者占预算 0.19%，后者占 **185.8%**。这就是"热路径别打日志"的量化版本：它不是风格问题，是能不能跑的问题。
2. **原子自增比裸自增贵约 12 倍**（4.01 ns vs 0.33 ns），但仍在可接受范围；真正要警惕的是**每调用一次字符串查找**（5.68 ns，约 17 倍）与**每次重新格式化**。
3. **作用域计时器是两个时钟读**（50 ns），在 100 000 次/帧时已吃掉 **30% 预算**。所以"给每个实体都加一个 scope timer"同样不可取；**按系统计时 + 采样**才是正解。
4. **1/100 采样把开销压到 0.96 ns**（相对全量计时约 **1/52**），在 100 000 次/帧下只占 0.58% 预算。代价是只得到统计分布而非逐次明细，且采样计数必须是确定性的（不能引入随机时序）。
5. **卡顿检测必须用中位数而不是 p99**：p99 会被卡顿本身污染，阈值越抬越高最终漏检。用 `max(2.5 × median, 1.5 帧)` 的阈值配合迟滞（连续 2 帧恢复到阈值 60% 以下才结束一次卡顿），实测 12 次卡顿（含级联）判定为**恰好 12 个事件**、18 帧全部命中召回、纯抖动序列 **0 误报**。
6. **降级必须有迟滞**：连续 3 次超预算升一级（上限 3 级），连续 30 次达标降一级。交替"超/不超"的输入在无迟滞时会每 Tick 抖一次；实测该输入下**级别变化 0 次**。
7. **预算记账本身可以忽略**：1.7 ns/Tick、每系统 0.4 ns；即使每 Tick 记 4 个系统，20 Hz 下开销也在噪声量级。所以"怕记账有开销"不能成为不记账的理由。

## 局限

- **不是 UE Insights/Trace**：未覆盖 Unreal Insights 的 Trace 通道、Stats 系统、ProfileGPU、Stat NamedEvents；本节只量化"自建插桩"的开销量级。
- **单机单线程、无真实渲染/物理**：数字是纯 CPU 逻辑开销，不含缓存竞争、上下文切换、磁盘 IO 与真实 DS 负载；`fwrite` 测的是 1 MiB 缓冲下的顺序写。
- **编译与 CPU 相关**：`-O2` 与 16 核 x86_64 环境下的 p50；不同编译器/架构绝对值会变，**结论应看比例与量级而非绝对值**。
- **卡顿检测为简化模型**：未覆盖 GPU 等待、加载卡顿、GC 卡顿的分型，也未做多阈值分档（微卡顿/大卡顿）。
- **预算模型为静态预算**：真实项目里预算随玩法/场景变化，需要按帧型（战斗/大世界/UI）分档。
- 未覆盖：采样偏差的统计置信区间、Trace 落盘 IO 影响、线上采样率配置策略。

## 关联知识文档

- [系统实战/10-性能问题定位完整链路](../../../系统实战/10-性能问题定位完整链路.md)（本证据的主要使用者）
- [笔记/插桩测试](../../../笔记/插桩测试.md)、[笔记/perf性能分析](../../../笔记/perf性能分析.md)（方法速查）
- [00-计算机与工程基础/07-Linux系统编程/03-性能工具：插桩与perf采样](../../../00-计算机与工程基础/07-Linux系统编程/03-性能工具：插桩与perf采样.md)
- [游戏知识/07-UI与性能优化/03-性能分析工具与Profiling](../../../游戏知识/07-UI与性能优化/03-性能分析工具与Profiling.md)
- [游戏服务端/06-世界模拟与运行时/14-运行时背压与过载保护](../../../游戏服务端/06-世界模拟与运行时/14-运行时背压与过载保护.md)（预算与降级的上层策略）
- [evidence/tests/damage-core](../../tests/damage-core/README.md)、[evidence/tests/gameplay-core](../../tests/gameplay-core/README.md)（同样使用"分批计时再归一"的度量方法）
