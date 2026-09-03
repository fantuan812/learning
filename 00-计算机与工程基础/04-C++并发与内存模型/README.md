---
type: Index
title: "04-C++并发与内存模型 · 分类"
status: stable
verified: []
maturity: L2
updated: 2026-08-20
---

# 04-C++并发与内存模型 · 分类

> 定位：多线程并发编程、锁与无锁机制、C++ 内存模型（Memory Order）、CPU 缓存竞争与跨进程同步的底层原理。配有本机可复现实验（`evidence/labs/atomic-memory-order`、`evidence/labs/false-sharing`）。

---

## 1. 专题矩阵与当前状态

> 路径说明：物理文件名遵循仓库稳定契约保留，逻辑序列由本表第一列“逻辑序号”规范呈现。

| 逻辑序号 | 专题文件与 Canonical 路径 | 知识类型 | 成熟度 | 核心范畴与工程解答 |
| :---: | :--- | :---: | :---: | :--- |
| **01** | [02-线程同步与锁](02-线程同步与锁.md) | Mechanism | L2 | 互斥锁（mutex/shared_mutex）、RAII 持锁守卫、条件变量虚假唤醒、死锁避免拓扑序与锁粒度优化。 |
| **02** | [02-Atomic与C++内存模型](02-Atomic与C++内存模型.md) | Mechanism | L4 | std::atomic、6 种 memory_order、happens-before、Release-Acquire 消息传递语义与 CAS 硬件实现；**含本机实测实验**。 |
| **03** | [03-LockFree与FalseSharing](03-LockFree与FalseSharing.md) | Mechanism | L4 | CPU 缓存一致性（MESI）、False Sharing 伪共享根因与 64B 对齐消除、无锁单向栈与 ABA 问题；**含本机实测 Benchmark**。 |
| **04** | [04-线程池、任务调度与协程调度](04-线程池、任务调度与协程调度.md) | Architecture | L2 | 线程池设计模式、Work-Stealing 工作窃取算法、任务有向无环图（DAG）依赖调度、C++20 无栈协程集成。 |
| **05** | [05-进程间通信与跨进程同步](05-进程间通信与跨进程同步.md) | Mechanism | L2 | 匿名/命名管道、Unix Domain Socket、POSIX 共享内存、跨进程互斥量与进程崩溃恢复机制。 |

---

## 2. 逻辑学习顺序与依赖关系

```text
01. 线程同步与锁 (02-线程同步与锁.md)
  └─→ 02. 原子操作与内存模型 (02-Atomic与C++内存模型.md)
        └─→ 03. 伪共享规避与无锁并发 (03-LockFree与FalseSharing.md)
              └─→ 04. 任务调度系统 (04-线程池、任务调度与协程调度.md)
                    └─→ 05. 进程间通信与跨进程同步 (05-进程间通信与跨进程同步.md)
```

1. **第一阶段：受控临界区与共享锁**：先读 [02-线程同步与锁](02-线程同步与锁.md)，掌握何时必须独占锁，何时可用读写锁，防范死锁与锁竞争瓶颈。
2. **第二阶段：进入硬件微观内存序**：精读 [02-Atomic与C++内存模型](02-Atomic与C++内存模型.md)，理解为何编译器和 CPU 会重排指令，掌握 `acquire` 和 `release` 建立的同步屏障。
3. **第三阶段：攻克无锁与缓存竞争陷阱**：研读 [03-LockFree与FalseSharing](03-LockFree与FalseSharing.md)，识别看似并发实则总线乒乓的伪共享，掌握无锁队列与 ABA 消除。
4. **第四阶段：宏观调度与多进程架构**：研读 [04-线程池、任务调度与协程调度](04-线程池、任务调度与协程调度.md) 与 [05-进程间通信与跨进程同步](05-进程间通信与跨进程同步.md)，构建工业级任务执行器与跨进程服务集群。

---

## 3. 游戏研发与工程落地对接

- **Unreal Engine (UE5)**：
  - UE 并发核心机制：`FCriticalSection`、`FRWLock`、`FEvent`；
  - `TAtomic` 原语在对象引用计数与渲染命令队列中的使用；
  - UE 任务图系统（`TaskGraph` 与 UE5 `Tasks::Launch`）：基于工作窃取与任务依赖图的高效异步执行底座；
  - 无锁单向链表 `TLockFreePointerListLIFO`。
- **游戏服务端**：
  - 多线程主循环架构：网络 IO 线程 ↔ 逻辑分片线程 ↔ 异步存盘线程之间的数据交换；
  - 热路径监控指标统计：采用 `relaxed` 原子累加避免全局锁阻塞；
  - 任务投递队列：无锁单生产者单消费者（SPSC）或多生产者单消费者（MPSC）环形队列；
  - DS 进程与守护 Agent 之间基于 Unix Domain Socket 或共享内存的高速跨进程心跳与数据通道。

---

## 4. 本地可复现实验证据

- **原子内存序验证**：[`evidence/labs/atomic-memory-order/`](../../evidence/labs/atomic-memory-order/README.md)（已执行 MSVC 2022 实测）
- **False Sharing 伪共享基准**：[`evidence/labs/false-sharing/`](../../evidence/labs/false-sharing/README.md)（已执行基准测试并提供火焰图证据）

---

## 5. 跨域与相关导航

- [00-计算机与工程基础 总索引](../README.md)
- [计算机与工程基础 Domain MOC](../../00_Index/domains/计算机与工程基础.md)
- [06-操作系统/01-进程线程虚拟内存与系统调用](../06-操作系统/01-进程线程虚拟内存与系统调用.md)
- [08-计算机体系结构与性能/01-处理器存储层次与性能工程](../08-计算机体系结构与性能/01-处理器存储层次与性能工程.md)
- [游戏服务端/01-架构与网络/05-并发与高性能](../../游戏服务端/01-架构与网络/05-并发与高性能.md)
