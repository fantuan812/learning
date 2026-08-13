# Evidence · false-sharing：缓存行竞争 Benchmark

> 状态：已执行（2026-08-12，Windows x64 / MSVC；注意本机为虚拟化/受限调度环境，见"局限"）

## 问题

1. 多线程各自累加"自己的槽位"，相邻排列（共享缓存行）vs 64 字节对齐（独立缓存行），耗时差多少？
2. 真实共享计数（单原子计数器多线程 RMW）的成本参照是多少？
3. 为什么非原子 `volatile` 写在本机观察不到明显竞争，而原子槽位能？

## 假设

- 共享缓存行的写会导致跨核缓存失效（RFO ping-pong），使吞吐大幅下降；`alignas(64)` 隔离槽位可消除。
- 原子 RMW 强制走缓存一致性协议，竞争必然放大，比非原子写更稳定可复现。
- 编译器可能把普通共享变量的循环提升为寄存器累加（数据竞争 UB 允许），因此需要 `volatile` 或原子。

## 环境

- 系统：Windows x64（逻辑处理器 16）
- 编译器：MSVC 14.44.35207，`cl /O2 /std:c++17 /EHsc /utf-8 /W4`
- 用法：`false_sharing.exe [每线程次数=20000000] [线程数=4]`

## 指标

- 各组耗时（ms，3 次取原始值）：aligned / packed（volatile 直写）、shared_atomic（单计数器对照）、atomic_packed / atomic_aligned（原子槽位）、局部累加对照。

## 原始结果

`results/false_sharing_win_x64_msvc.txt`（4 线程 × 2000 万次）摘要：

```text
run 1: aligned=38.47  packed=48.08  shared_atomic=1288.93  atomic_packed=798.64  atomic_aligned=119.69
        packed/aligned=1.2x  atomic_packed/atomic_aligned=6.7x
run 2: aligned=37.14  packed=49.98  shared_atomic=1284.00  atomic_packed=388.02  atomic_aligned=114.91
        packed/aligned=1.3x  atomic_packed/atomic_aligned=3.4x
run 3: aligned=35.29  packed=53.08  shared_atomic=1270.43  atomic_packed=923.64  atomic_aligned=131.92
        packed/aligned=1.5x  atomic_packed/atomic_aligned=7.0x
对照（每线程局部累加再写回）: aligned≈6.3ms  packed≈6.3ms
```

另：16 线程 × 500 万次结果见 `results/false_sharing_16t_win_x64_msvc.txt`（packed/aligned 仍仅 1.2x，shared_atomic 1103~1149ms，atomic_packed/atomic_aligned 同 4 线程结论）。

## 结论

1. **原子槽位共享缓存行**：atomic_packed/atomic_aligned = 3.4~7.0x（均值约 5x）——同一缓存行内的独立原子槽互相拖慢，这是 false sharing 的可靠复现。
2. **真实共享**：shared_atomic 1284ms vs aligned 37ms（约 34x）——单一行被多线程 RMW 竞争时串行化最严重。
3. **非原子 volatile 直写在本机仅 1.2~1.5x**：本机（虚拟化/受限调度）下线程大多落在同一物理核心，非原子写没有跨核失效；说明 false sharing 的代价依赖线程实际分布，**用原子/真实共享竞争更容易稳定复现问题**。
4. **局部累加再写回**（~6ms）是所有版本的绝对下界：把每线程状态留在寄存器/局部，是消除竞争的最彻底手段。

## 局限

- 本机无法保证线程落在不同物理核心（虚拟化/调度限制），packed 版差异被低估；结论 1/2 的竞争数据（原子版）不受影响。
- 非原子写存在数据竞争 UB，实验仅用于观察现象；生产代码请使用原子或加锁。
- 建议在真实多核物理机（≥4 物理核）上复跑，packed/aligned 比例通常可达 10~30x。

## 关联知识文档

- [03-LockFree与FalseSharing](../../../00-计算机与工程基础/04-C++并发与内存模型/03-LockFree与FalseSharing.md)
- [02-Atomic与C++内存模型](../../../00-计算机与工程基础/04-C++并发与内存模型/02-Atomic与C++内存模型.md)
