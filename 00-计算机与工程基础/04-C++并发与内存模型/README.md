# 04-C++并发与内存模型 · 分类

> 定位：线程、原子、内存序、无锁与缓存竞争的底层原理，配本机可复现实验（evidence/labs/atomic-memory-order、evidence/labs/false-sharing）。

## 文件列表

| 文件 | 简介 | 成熟度 |
| --- | --- | --- |
| [02-线程同步与锁](02-线程同步与锁.md) | mutex、读写锁、条件变量、死锁与竞争基准 | L2 |
| [02-Atomic与C++内存模型](02-Atomic与C++内存模型.md) | 原子操作、六种 memory_order、happens-before、release/acquire 消息传递、CAS；含本机实验 | L4 |
| [03-LockFree与FalseSharing](03-LockFree与FalseSharing.md) | 缓存一致性/RFO、false sharing、无锁栈与 ABA、对齐与每线程缓冲；含本机 Benchmark | L4 |
| [04-线程池、任务调度与协程调度](04-线程池、任务调度与协程调度.md) | 线程池、工作窃取、任务依赖与协程调度 | L2 |
| [05-进程间通信与跨进程同步](05-进程间通信与跨进程同步.md) | 管道、socket、共享内存、事件通知、权限与恢复 | L2 |

## 后续提升

- 为线程同步、任务调度与 IPC 补充可归档的跨平台运行证据，达到 L3。

## 学习顺序

1. [02-Atomic与C++内存模型](02-Atomic与C++内存模型.md)（原子与内存序）→ 2. [03-LockFree与FalseSharing](03-LockFree与FalseSharing.md)（缓存竞争与无锁）→ 3. [04-线程池、任务调度与协程调度](04-线程池、任务调度与协程调度.md) → 4. [05-进程间通信与跨进程同步](05-进程间通信与跨进程同步.md)。

## 与 UE/服务端的对接

- UE：`TAtomic`（Templates/Atomic.h）、`TLockFreePointerListLIFO`（Containers/LockFreeList.h）、`FMallocBinned`（HAL/MallocBinned.h）。
- 服务端：多线程分片、每线程缓冲、热路径计数 relaxed 原子。
- 线程同步与 IPC 专题分别覆盖进程内共享状态和跨进程通信边界。
