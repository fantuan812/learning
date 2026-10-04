---
type: Evidence
title: "Evidence · tick-scheduler：Server Main Loop 模拟"
status: stable
verified: []
maturity: L0
---
# Evidence · tick-scheduler：Server Main Loop 模拟

> 状态：已执行（2026-08-12，Windows x64 / MSVC）

## 问题

1. 固定步长主循环（accumulator 模式）在不同实体规模下的 Tick 成本分布（P50/P95/P99）？
2. 过载时 catch-up（补帧，有上限）与 drop（丢帧）两种策略的效果差异？
3. 实体数量对单 Tick 成本的影响（100 / 1000 / 10000）？

## 假设

- Tick 成本 = 实体数 × 单位成本（带抖动）的模型量，线性放大。
- catch-up 保持 Tick 总数（世界时间一致）但产生突发帧；drop 保持帧节奏但世界时间变慢（Tick 减少）。
- 60Hz 下 Tick 预算 ≈ 16667us；负载系数 1.0/1.3/1.5 模拟正常/过载/严重过载。

## 环境

- 系统：Windows x64；编译器：MSVC 14.44.35207，`cl /O2 /std:c++17 /EHsc /utf-8 /W4`
- 模型参数：6000 帧，单位成本 0.5us/实体，抖动 ±30%，catch-up 上限 3 帧。

## 指标

- ticks（实际执行 Tick 数）、Tick 成本 P50/P95/P99（us）、drops（丢帧/清积压次数）、catch（补帧帧数）。

## 原始结果

`results/tick_scheduler_win_x64_msvc.txt`（2026-08-12）：

```text
场景                             |  ticks |      P50 |      P95 |      P99 |  drops |  catch | frames
100 entities x0.5us load=1.0     |   6000 |     50.1 |     63.6 |     64.7 |      0 |      0 |   6000
1000 entities x0.5us load=1.0    |   6000 |    501.2 |    635.5 |    647.0 |      0 |      0 |   6000
10000 entities x0.5us load=1.0   |   6000 |   5012.3 |   6355.2 |   6470.4 |      0 |      0 |   6000
10000 x0.5us load=1.3 catch-up   |   7800 |   5005.6 |   6362.0 |   6473.2 |      0 |   1800 |   6000
10000 x0.5us load=1.3 drop       |   6000 |   5012.3 |   6355.2 |   6470.4 |   1500 |      0 |   6000
20000 x0.5us load=1.5 catch-up   |   9000 |  10003.6 |  12718.1 |  12943.1 |      0 |   3000 |   6000
20000 x0.5us load=1.5 drop       |   6000 |  10024.6 |  12710.4 |  12940.8 |   3000 |      0 |   6000
```

## 结论

1. 实体数线性放大 Tick 成本（100→50us，1000→500us，10000→5000us）；10000 实体时单 Tick 已占 60Hz 预算约 30%。
2. 过载 1.3 时：catch-up 执行 7800 个 Tick（补 1800 帧，无丢帧），世界时间保持真实速度但帧内突发；drop 只执行 6000 个 Tick、丢 1500 次积压，世界时间变慢 30%。
3. 严重过载 1.5 时差距扩大（9000 vs 6000 ticks，3000 次丢/补）；P95/P99 由抖动决定（±30% 抖动 → P99≈1.29×P50）。
4. 策略选择 = 一致性 vs 节奏：需要确定性/回放/权威结算选 catch-up（限上限防雪崩），需要帧节奏稳定选 drop（世界减速），实际系统常组合使用。

## 局限

- 模型实验：成本为线性模型量，未建模分配、网络发送、AI/寻路的非线性放大；真实服务器需用压测（机器人压测）校准预算。
- 未建模"补帧导致单帧执行多个 Tick"时的峰值延迟（catch-up 突发帧对下游网络批量的影响）。

## 关联知识文档

- [01-ServerMainLoop与TickScheduler](../../../知识/07-网络与游戏服务端/运行调度与过载保护/01-ServerMainLoop与TickScheduler.md)
- [11-AI与寻路时间预算](../../../知识/07-网络与游戏服务端/运行调度与过载保护/11-AI与寻路时间预算.md)（已落地）
