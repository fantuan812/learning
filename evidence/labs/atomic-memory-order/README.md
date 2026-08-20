---
type: Evidence
title: "Evidence · atomic-memory-order：Atomic 与 C++ 内存模型实验"
status: stable
verified: []
maturity: L0
---
# Evidence · atomic-memory-order：Atomic 与 C++ 内存模型实验

> 状态：已执行（2026-08-12，Windows x64 / MSVC）

## 问题

1. `std::atomic` 的 RMW（fetch_add）多线程累计是否正确？
2. 普通 `int` 多线程 `++` 会发生什么（数据竞争）？
3. release/acquire 能否正确传递"数据 + 就绪标志"（message passing）？
4. CAS 无锁计数是否正确？
5. relaxed / acq_rel / seq_cst 在 x86 上的开销差异有多大？

## 假设

- 原子 RMW 与 CAS 在任意线程数下结果精确；普通 `int` 竞争是 UB，结果不确定，可能丢更新。
- x86 上三种 `memory_order` 的 RMW 均编译为 `lock` 前缀指令，开销差异小；多线程竞争（缓存行）是主成本。
- release/acquire 足以同步"先写数据、再置标志"的模式（happens-before 传递）。

## 环境

- 系统：Windows，x64（逻辑处理器 16）
- 编译器：MSVC 2022 Build Tools（`VC\Tools\MSVC\14.44.35207`），`cl /O2 /std:c++17 /EHsc /utf-8 /W4`

## 运行方式

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build_run.ps1
```

## 输入与指标

- 8 线程 × 100 万次 fetch_add / CAS（A、D）；8 线程 × 500 万次 × 5 轮普通 `++`（B）；release/acquire 消息传递 500 万轮（C）。
- 指标：计数正确性、丢更新轮数、fetch_add 耗时（1/8 线程 × relaxed/acq_rel/seq_cst，每线程 2000 万次）。

## 原始结果

`results/atomic_memory_order_win_x64_msvc.txt`（2026-08-12）摘要：

```text
A) atomic fetch_add x8000000: got=8000000 expect=8000000 -> PASS
B) plain ++ x40000000: 5 轮全部丢更新（got≈19M~22M，expect=40M）
C) message passing (release/acquire) x5000000: PASS
D) CAS loop x8000000: got=8000000 expect=8000000 -> PASS
E) fetch_add 耗时（ms，iters=20000000/线程）
   threads | relaxed | acq_rel | seq_cst
         1 |   81.38 |   81.64 |   82.12
         8 | 1814.89 | 2739.05 | 2766.33
```

## 结论

1. 原子 RMW 与 CAS 计数精确（A/D PASS）；普通 `int` 竞争 5 轮全丢更新，验证"数据竞争 = UB，结果不确定"。
2. release/acquire 消息传递 500 万轮无失败（C PASS），是"数据 + 标志"同步的正确模式。
3. x86 上三种 `memory_order` 开销几乎相同（1 线程 ~82ms）；8 线程竞争时 relaxed 与 acq_rel/seq_cst 差异仍远小于"竞争本身"的成本（~1.8~2.8s，约 22~34 倍于单线程）。

## 局限

- x86（TSO）无法直接演示 relaxed 在弱序平台（ARM）上可能出现的乱序失效；弱序验证需在 ARM 设备复跑（见关联文档的验证计划）。
- B 组丢更新比例与机器、编译器、调度相关，本机为 5/5；换环境可能不同，但"普通共享变量多线程写不可依赖"的结论不变。
- 本机为虚拟化/受限调度环境，线程可能聚集在同一物理核心；竞争数据见 false-sharing 实验的对照。

## 关联知识文档

- [02-Atomic与C++内存模型](../../../00-计算机与工程基础/04-C++并发与内存模型/02-Atomic与C++内存模型.md)
- [03-LockFree与FalseSharing](../../../00-计算机与工程基础/04-C++并发与内存模型/03-LockFree与FalseSharing.md)
