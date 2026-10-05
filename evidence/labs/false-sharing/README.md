---
type: Evidence
title: "Evidence · false-sharing：缓存行竞争 Benchmark"
status: stable
verified: []
maturity: L2
updated: 2026-10-05
sources:
  - id: cpp17-memory
    title: "N4659 · C++ memory model and data races"
    resource: "https://timsong-cpp.github.io/cppwp/n4659/intro.races"
  - id: linux-false-sharing
    title: "Linux kernel documentation · False Sharing"
    resource: "https://docs.kernel.org/kernel-hacking/false-sharing.html"
---
# Evidence · false-sharing：缓存行竞争 Benchmark

> 历史状态：保存 2026-08-12 的 Windows x64 / MSVC 运行记录。旧文注记“虚拟化/受限调度”，但原输出没有足够拓扑/落核证据认证它对结果的影响。
> 本次校订：2026-10-05，静态核对现存源码、runner、两份 raw 与解释；未重跑旧性能基准，未修改旧源/脚本/原始结果。
> 知识成熟度：L2，主要承诺为历史材料的静态核对与有限解释，原 L0 改为与此范围相符的等级；不代表所有旧源码输入/失败路径经过验证，也不把历史耗时当作整篇机制证明。`verified: []` 保持不变。

## 问题与可检验假设

1. 同一操作与工作量下，每线程独立槽位的相邻布局与隔离布局耗时是否有差异？
2. 多线程更新同一原子计数器的 true sharing，与独立原子槽位如何比较？
3. 普通/volatile 与原子版本还改变了什么，现有样本能支持哪些结论？

不同数据共行且访问含写时，可能有不必要的相干交互，既包括写/写，也包括写/读。对齐、填充和减少写频率是候选手段；`64` 是这里的布局假设，不是全平台常数。原子 RMW 不保证每轮都发 RFO，不保证竞争必然放大。换成原子会改变工作量与指令，不能作为单一因果诊断。

## 源码访问合同

[false_sharing.cpp](src/false_sharing.cpp) 在启动线程前构造 `slots`，每个 lambda 按值捕获唯一的 `i`，只访问 `slots[i].v`，期间不改变容器结构，最后逐线程 `join`。在合法输入、资源成功和无算术溢出等前提下，不同普通标量槽位没有同位置冲突，**不会因为在同一缓存行就出现 data race**。[C++17 的不同元素规则](https://timsong-cpp.github.io/cppwp/n4659/container.requirements.dataraces) 需与 [内存位置定义](https://timsong-cpp.github.io/cppwp/n4659/intro.memory) 一起使用，不能推广到相邻非零位域或 `vector<bool>`。

`volatile` 不是该访问合法的理由，也不提供线程同步/原子性；它不意味着绕过缓存到 DRAM 或每个 `++` 固定成一条内存 RMW。普通独占计数循环的化简可由 as-if 规则解释，不需要 data-race UB。原源码相关旧注释保留为历史材料，本 README 不沿用；`run_padded` 实际先局部累加、再写回一次，其“不是局部累加”的旧注释也有误。现存源码只打印耗时，未核最终值，本次静态核对不等于对所有输入及异常路径的认证。

## 历史环境与用法

- 系统：Windows x64（逻辑处理器 16）
- 编译器：MSVC 14.44.35207，`cl /O2 /std:c++17 /EHsc /utf-8 /W4`
- 用法：`false_sharing.exe [每线程次数=20000000] [线程数=4]`
- [旧 build_run.ps1](scripts/build_run.ps1) 依赖其写明的 Windows BuildTools 路径，并会覆盖四线程 raw；本次未执行。新复验应保护旧文件，将新日志写到独立位置

## 指标和原始结果

现存源码的组别为 aligned / packed（volatile 直写）、shared_atomic（单计数器）、atomic_packed / atomic_aligned（独立原子槽）、局部累加对照。只有四线程 raw 同时记录这五组；不能把源码组别投射到缺少组别的旧输出。

[results/false_sharing_win_x64_msvc.txt](results/false_sharing_win_x64_msvc.txt)（4 线程 × 2000 万次）的历史摘要原样保留：

```text
run 1: aligned=38.47  packed=48.08  shared_atomic=1288.93  atomic_packed=798.64  atomic_aligned=119.69
        packed/aligned=1.2x  atomic_packed/atomic_aligned=6.7x
run 2: aligned=37.14  packed=49.98  shared_atomic=1284.00  atomic_packed=388.02  atomic_aligned=114.91
        packed/aligned=1.3x  atomic_packed/atomic_aligned=3.4x
run 3: aligned=35.29  packed=53.08  shared_atomic=1270.43  atomic_packed=923.64  atomic_aligned=131.92
        packed/aligned=1.5x  atomic_packed/atomic_aligned=7.0x
对照（每线程局部累加再写回）: aligned≈6.3ms  packed≈6.3ms
```

摘要中的局部对照约 6.3ms 是旧近似写法；raw 实值是两轮 aligned 6.63/6.27ms、packed 6.32/6.37ms，不是每轮恰好 6.3。两份 raw 标题写“3 次取平均”，实际下面逐轮列值，没有输出均值；这是旧标签错误，不能把原始记录改写成新计算结果。

另：[results/false_sharing_16t_win_x64_msvc.txt](results/false_sharing_16t_win_x64_msvc.txt)（16 线程 × 500 万次）中，aligned 为 32.09/30.58/32.58ms，packed 为 38.48/39.01/38.71ms，packed/aligned 逐轮为 **1.2、1.3、1.2x**；shared_atomic 为 1103.99/1106.31/1148.56ms（旧摘要 1103~1149ms）。局部对照为 aligned 25.84/26.90ms、packed 26.80/24.45ms。

十六线程 raw **没有 atomic_packed / atomic_aligned 两组**，因此撤回旧版“同四线程结论”。其输出格式与现存源码不同，现有材料未认证二者来自同一程序修订版；不能补造缺组或用当前源码替旧二进制背书。

## 能支持的解释

1. **四线程原子槽布局对照**：atomic_packed/atomic_aligned 为 3.4~7.0x，原“均值约 5x”只是粗略概述，不能替代三轮原值。这是该历史样本的时间差，未记录实际地址/PMU，不能据此排除全部混杂因素或保证其他机器必现
2. **混杂的真实共享参照**：shared_atomic 1284ms 对 aligned 37ms（约 34x）同时改变原子性与共享方式，不能归为单独 true-sharing 代价；后续应在相同原子操作下尽量比较 shared/packed/aligned
3. **volatile 直写样本**：四线程约 1.2~1.5x；没有实际落核记录，无法宣称线程大多在同一物理核心、没有跨核失效或差异被低估。原子组同样会受调度/频率等影响
4. **局部累加对照**：约 6ms 是旧实现观察，减少写回频率并可能被编译器化简；不是所有版本的绝对下界，也不代表写回完全没有缓存行交互

## 局限与下一次复验

- 本次没有旧程序最终计数断言、硬件拓扑/地址/PMU 记录或完整构建溯源；三个耗时样本也不能证明 P99 或无锁算法正确性
- `alignas(64)` 的作用还要结合字段偏移、类型大小、实际地址与目标硬件；数组基址对齐不等于每个元素隔离
- 本轮未运行旧 MSVC 基准、TSan、perf/VTune、物理亲和或 UE。正文另外给出的普通 unsigned 独占槽位、共享 atomic 计数和布局短例，仅验证其自身功能/布局，不是本旧程序重跑
- 下一次先核最终值，再保持同种操作与工作量，记录编译选项、代码 revision、目标硬件、线程分布、预热和原始多轮结果。可按需使用亲和/PMU，不预定收益方向；撤回“通常 10~30x”门槛

## 关联知识文档

- [03-LockFree与FalseSharing](../../../知识/01-编程与计算机基础/并发与同步/03-LockFree与FalseSharing.md)：访问模型、布局、CAS/ABA、完整短例和本次真实验证范围
- [02-Atomic与C++内存模型](../../../知识/01-编程与计算机基础/并发与同步/02-Atomic与C%2B%2B内存模型.md)：原子操作不等于关联普通对象的发布保证
