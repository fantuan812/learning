# 00-计算机与工程基础 · 知识库（建设中）

> 定位：游戏客户端与服务端共同的底层底座。本层回答"为什么"——cache miss 为什么慢、false sharing 是什么、acquire/release 保证什么、移动构造为什么能优化 TArray 扩容、allocator 为什么影响服务器性能、virtual memory 与 page fault 怎么影响游戏卡顿、epoll 的 LT/ET 区别、系统调用为什么贵、NUMA 对 MMO 服务器有什么影响。
> 状态：骨架（目录与规划已建立），正文按优先级分批填充。
> 成熟度：正文顶部按 [写作规范](../references/写作规范.md) 标注知识成熟度 L0~L5。

## 为什么需要这一层

现有知识树从 UE 引擎（UObject、Actor、GAS、Replication、Rendering）和服务端（Server、Database、Distributed System）直接开始，缺少作为"根"的系统整理：

```text
UE 客户端链路：C++ → UE 源码 → 游戏客户端（UObject/GC/容器内存/TaskGraph/渲染）
服务端链路：  C++ → Linux → 网络/并发 → 游戏服务端（Main Loop/Tick/AOI/allocator/epoll/NUMA）
```

没有这一层时，`TArray`、`TSharedPtr`、`FRunnable`、`TaskGraph`、`FMallocBinned` 只能"记住结论"，无法回答性能与正确性问题；UE 源码开发与高性能游戏服务端最终都会汇合到这里。

## 规划分类（10 个）

| 分类 | 主题 |
| --- | --- |
| 01-C++核心 | 对象生命周期、RAII、copy/move、值语义、虚函数与多态 |
| 02-C++对象模型与内存 | 对象布局、vtable、alignment、placement new、内存池与 allocator |
| 03-现代C++与泛型 | 模板、变参、SFINAE/concepts、移动语义与完美转发、STL 容器实现 |
| 04-C++并发与内存模型 | thread、atomic、memory ordering、lock-free、false sharing |
| 05-数据结构与复杂度 | 复杂度分析、容器选型、缓存局部性 |
| 06-操作系统 | 进程/线程、虚拟内存与 page fault、系统调用代价、调度 |
| 07-Linux系统编程 | socket、epoll LT/ET、IO 多路复用、信号、性能工具 |
| 08-计算机体系结构与性能 | CPU cache、branch prediction、SIMD、NUMA、perf 分析方法 |
| 09-计算机网络基础 | TCP/UDP/QUIC 原理、拥塞控制、网络边界（与服务端网络分层） |
| 10-工程设计与调试 | 调试器、sanitizer、profiler、错误处理与日志 |

## 优先级（第一批）

对象生命周期 / RAII / copy-move / 模板 / STL 容器实现 / allocator / 虚函数与对象布局 / alignment / cache line / atomic 与 memory ordering / thread 与 process / virtual memory / socket 与 epoll / CPU cache / branch prediction / SIMD / perf

## 与现有知识库的分工

- 本层讲原理与实现机制；[游戏知识/12-引擎源码分析](../游戏知识/12-引擎源码分析/README.md) 讲 UE 对它们的工程化（FMallocBinned、TaskGraph、TArray 源码）。
- 本层讲 Linux 系统编程；[游戏服务端/01-架构与网络](../游戏服务端/01-架构与网络/README.md) 讲游戏服务器对它的使用（epoll 封装、线程模型）。
- 本层讲数据结构与复杂度；[游戏算法](../游戏算法/README.md) 讲游戏领域算法（寻路、空间索引、AOI）。

## 填充状态

| 分类 | 状态 | 篇数 | 成熟度分布 |
| --- | --- | --- | --- |
| 01-C++核心 | 规划 | 0 | - |
| 02-C++对象模型与内存 | 规划 | 0 | - |
| 03-现代C++与泛型 | 规划 | 0 | - |
| 04-C++并发与内存模型 | 规划 | 0 | - |
| 05-数据结构与复杂度 | 规划 | 0 | - |
| 06-操作系统 | 规划 | 0 | - |
| 07-Linux系统编程 | 规划 | 0 | - |
| 08-计算机体系结构与性能 | 规划 | 0 | - |
| 09-计算机网络基础 | 规划 | 0 | - |
| 10-工程设计与调试 | 规划 | 0 | - |

## 验收门禁

- 正文 ≥ 300 行、UTF-8 无 BOM，按 [写作规范](../references/写作规范.md) 标注版本边界与知识成熟度。
- 涉及内核/标准结论时给出可验证来源；性能结论尽量给出实验数据或明确标注"待验证"。
- 每个分类完成 ≥1 篇后，同步更新本 README 的填充状态表、[仓库结构](../references/仓库结构.md) 与根 [README](../README.md)。
