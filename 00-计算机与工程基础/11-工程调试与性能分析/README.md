---
type: Index
title: "11-工程调试与性能分析 · 分类"
status: stable
verified: []
maturity: L2
updated: 2026-08-20
---

# 11-工程调试与性能分析 · 分类

> 定位：系统化建立从故障复现、现场保全、证据分析到根因修复的排障工程闭环；覆盖 GDB / LLDB / WinDbg 调试器、Core Dump 核心转储逆向分析、LLVM Sanitizers（ASan / TSan / UBSan）动态内存检测、Profiler 性能剖析与火焰图分析。

---

## 1. 专题矩阵与当前状态

| 专题文件与 Canonical 路径 | 标题与核心范畴 | 知识类型 | 成熟度 | 核心工程问题与回答 |
| :--- | :--- | :---: | :---: | :--- |
| [01-调试与性能分析方法论](01-调试与性能分析方法论.md) | 结构化调试排障循环、GDB/LLDB 核心转储栈帧还原、AddressSanitizer（UAF/越界读写检测）、ThreadSanitizer 数据竞争检测、UndefinedBehaviorSanitizer、Tracy/VTune 性能采样、指标-日志-追踪三位一体闭环 | BestPractice | L2 | 提供工业级高难度 C++ 崩溃（野指针、踩内存、死锁、偶发微卡顿）的标准定位与根因切除排障流程。 |

---

## 2. 核心排障武器库与工具矩阵

| 排障维度 | 适用场景与代表性工具 | 核心检测能力与开销特征 |
| :--- | :--- | :--- |
| **内存破坏检测** | AddressSanitizer (ASan) | 捕获 Use-After-Free、堆栈越界（Buffer Overflow）、全局变量溢出；内存增加约 2x，CPU 慢约 1.5~2x，适合测试服全量开启。 |
| **多线程竞态检测** | ThreadSanitizer (TSan) | 捕获无锁保护的数据竞争（Data Race）、锁次序反转（潜在死锁）；内存与 CPU 开销较大（5~10x），适合并发单元测试。 |
| **线上崩溃后向分析** | GDB / LLDB / WinDbg + Core Dump | 结合未剥离符号表（Debug Symbols）还原崩溃发生瞬间所有线程的寄存器、局部变量与调用栈（Call Stack）。 |
| **宏观性能与火焰图** | Linux perf / Tracy Profiler / Unreal Insights | 采样 CPU 周期消耗、缓存缺失率与系统调用频次，横轴采样占比、纵轴调用深度生成 FlameGraph 直观暴露热点。 |

---

## 3. 游戏研发与工程落地对接

- **Unreal Engine (UE5)**：
  - Unreal Insights：集成于引擎底层的轻量级微秒级追踪系统，实时监控 Frame Tick、RenderThread、GPU 开销与资产加载停顿；
  - 崩溃报告客户端（CrashReportClient）与 Symbol Server：线上客户端崩溃自动打包 Minidump 与调用栈并上传至符号服务器自动化聚类。
- **游戏服务端生产排障**：
  - Dedicated Server 发生 SIGSEGV / SIGABRT 时的 Core Dump 自动生成策略（`ulimit -c unlimited` 与 `core_pattern`）；
  - 针对死锁与高 CPU 占用进程：使用 `gdb -p <pid>` 执行 `thread apply all bt` 快速打印全线程调用栈定位死锁等待链。

---

## 4. 跨域与相关导航

- [00-计算机与工程基础 总索引](../README.md)
- [计算机与工程基础 Domain MOC](../../00_Index/domains/计算机与工程基础.md)
- [04-C++并发与内存模型/02-线程同步与锁](../04-C++并发与内存模型/02-线程同步与锁.md)
- [07-Linux系统编程/03-性能工具：插桩与perf采样](../07-Linux系统编程/03-性能工具：插桩与perf采样.md)
- [15-软件工程与构建/测试静态分析Fuzz与持续交付](../15-软件工程与构建/测试静态分析Fuzz与持续交付.md)
