# 04-C++并发与内存模型 · 分类

> 定位：线程、原子、内存序、无锁与缓存竞争的底层原理，配本机可复现实验（evidence/labs/atomic-memory-order、evidence/labs/false-sharing）。

## 文件列表

| 文件 | 简介 | 成熟度 |
| --- | --- | --- |
| [02-Atomic与C++内存模型](02-Atomic与C++内存模型.md) | 原子操作、六种 memory_order、happens-before、release/acquire 消息传递、CAS；含本机实验 | L4 |
| [03-LockFree与FalseSharing](03-LockFree与FalseSharing.md) | 缓存一致性/RFO、false sharing、无锁栈与 ABA、对齐与每线程缓冲；含本机 Benchmark | L4 |

## 规划

- `01-线程同步与锁.md`（W1-06，规划）：mutex/rwlock/condition variable/deadlock/contention。
- 本分类为 [W1 第一批](../../方案/知识体系完善执行方案.md) 的一部分；01 完成后同步登记。

## 学习顺序

1. [02-Atomic与C++内存模型](02-Atomic与C++内存模型.md)（原子与内存序）→ 2. [03-LockFree与FalseSharing](03-LockFree与FalseSharing.md)（缓存竞争与无锁）→ 3. `01-线程同步与锁`（规划）。

## 与 UE/服务端的对接

- UE：`TAtomic`（Templates/Atomic.h）、`TLockFreePointerListLIFO`（Containers/LockFreeList.h）、`FMallocBinned`（HAL/MallocBinned.h）。
- 服务端：多线程分片、每线程缓冲、热路径计数 relaxed 原子。
