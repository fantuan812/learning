---
type: Mechanism
title: "02-Atomic与C++内存模型"
status: stable
verified: []
maturity: L4
updated: 2026-10-04
---
# 02-Atomic与C++内存模型

> 知识基线：C++20/23 语言规则；具体 ABI 与编译器行为以所列一手资料和工具链为界。
> 最后更新：2026-10-04。
> 验证与基准：本轮 Test 覆盖见[基础合同实验](../../../evidence/cpp/foundation-contracts/README.md)，安全程序三种构建各45项；不将功能测试当性能基准或UE运行验收。

> 知识成熟度：L4 是本页沿用的证据层级，不意味着每种平台、每个无锁算法已经证明。历史 Windows 基准保留；本次新增 Linux C++23 功能实验，未运行 ARM 或 TSan。
> 版本基线：主体采用 C++20/23 的同步规则。实现与标准分开；GCC 的 consume 映射以其官方说明为准。最后核对：2026-10-04。

## 1. 概述

设服务器的加载线程准备了一份配置，Tick 线程见到 `ready` 后就读取它。真正的问题不是“CPU 会不会重排”，而是：**这一次普通读取，凭什么排在对应写入之后？对象是否还活着？写者是否已经开始覆盖下一份数据？**

先区分三件事：原子操作不被其他线程观察成撕裂的中间状态；每个原子对象有自己的修改序；跨对象的数据发布需要额外的同步关系。把标志换成 atomic，只回答第一部分，不能自动让整个配置成为线程安全对象。

读完本篇应能给一段发布代码画出 happens-before 链，识别复合更新丢失，并解释为什么“压测没出错”不能证明程序合法。

## 2. 核心概念

| 概念 | 用于回答的问题 | 不能由此推出 |
|---|---|---|
| 原子 load/store | 单次访问标志能否撕裂 | load 后计算再 store 是一个原子事务 |
| RMW（fetch_add/CAS） | 读取与更新能否作为一个操作参与竞争 | 周围所有普通对象都受到保护 |
| 修改序 | 同一个原子对象的各次修改怎样排序 | 不同原子对象天然共享一个顺序 |
| sequenced-before | 一个线程内哪些求值先发生 | 所有源码行都形成跨线程同步 |
| synchronizes-with | 哪次发布被哪次接收观察到 | 两个线程只要都用了 acquire/release 就配对 |
| happens-before | 冲突访问是否有顺序保证 | 无锁、无饥饿或实时期限保证 |

数据竞争涉及不同线程中的冲突访问、至少一个非原子访问、且没有要求的 happens-before 次序。普通数据可以安全地共享：只读初始化数据、互斥锁、线程 join 或有效的原子发布协议都可能建立所需边界。不能把“共享变量不是 atomic”直接判错。[标准工作草案 intro.races](https://eel.is/c++draft/intro.races)

## 3. 原理详解

### 3.1 为什么需要内存模型：两重重排

硬件缓存一致性、指令执行次序和语言层正确性不是同一个问题。编译器与 CPU 都可以有实现自由，但正确 C++ 程序依靠语言合同，而非猜测某台机器通常怎样执行。x86、ARM 各自还有指令种类、内存类型和屏障细节，不能用“x86 不重排 / ARM 任意重排”代替推导。

先证明没有数据竞争，再看生成指令和测量。发生 UB 的程序，不承诺只出现“旧值”或“少加几次”；换优化级别可能使原来观察到的现象消失。

### 3.2 happens-before 与释放序列

一次性消息发布的因果链是：

~~~text
写 payload → ready.store(true, release)
                      ↓ acquire-load 读到这次发布
                读到 true → 读 payload
~~~

同步必须绑定具体读取来源。消费者若仍读到初始 false，不能据此使用 payload。某些 RMW 可把同步延续在释放序列上，但其定义有标准版本差异；基础代码优先让 load 直接读对应 store，不把复杂释放序列当通用捷径。[原子排序规则](https://eel.is/c++draft/atomics.order)

如果这里的 payload 是普通对象，而两端标志都改成 relaxed，发布链消失，普通写读构成数据竞争 UB。不是“只有 ARM 才错”。如果 payload 本身也是 atomic，则可以构造无数据竞争但缺少所需跨对象顺序的反例；这和普通 payload 的 UB 必须分开讲。

### 3.3 六种 memory_order 语义与用途

| 顺序 | 合适的操作 | 需要先回答的问题 |
|---|---|---|
| relaxed | load/store/RMW | 只需要这个原子对象的值，还是在借它发布别的数据？ |
| acquire | load、RMW | 是否读到对应 release 发布的值或适用释放序列？ |
| release | store、RMW | 发布前的数据是否已初始化，之后是否还会并发覆盖？ |
| acq_rel | RMW | 这次读和写分别承担什么同步责任？ |
| seq_cst | load/store/RMW、适用栅栏 | 是否需要 SC 操作的一致全序？它不自动把所有普通访问纳入总序 |
| consume | C++20/23 中的依赖相关规则 | 不应当成 relaxed；本库新示例直接用 acquire 便于审查 |

**实现事实：GCC 的 `__ATOMIC_CONSUME` 当前按更强的 acquire 实现，不是 relaxed。** 该事实不代表所有版本、所有编译器都按同样方式降级；也不是“没有同步语义”。[GCC 官方说明](https://gcc.gnu.org/onlinedocs/gcc/_005f_005fatomic-Builtins.html)

默认 seq_cst 是合理的起点，并非代码气味。只有能说明为何更弱的顺序仍满足协议时才削弱；“relaxed 更快”不能作为正确性理由。load 不能用 release/acq_rel，store 不能用 acquire/acq_rel；不要把顺序当可任意指定的强度数字。

### 3.4 CAS 与无锁正确性

~~~cpp
std::atomic<int> counter{0};
int current = counter.load(std::memory_order_relaxed);
while (!counter.compare_exchange_weak(current, current + 1,
                                      std::memory_order_relaxed,
                                      std::memory_order_relaxed)) {
    // 失败会把实际观察值写回 current；必须重新计算 desired。
}
~~~

此例还要求计数范围不使普通表达式 `current + 1` 溢出。失败路径是一次读取，不能采用 release/acq_rel。weak 可伪失败，适合循环；strong 是否更贵取决于平台，不能给普适性能结论。CAS 比较值，不保证节点仍存活，也不解决 ABA；安全回收和逻辑身份属于另一个协议。

### 3.5 UE 对照与版本核验边界

历史参考位置包括 Core 的 `Templates/Atomic.h`、`HAL/PlatformAtomics.h` 和无锁容器。这里仅作查找起点，本次没有本机 UE 树可核验，不沿用未核对行号或对 Binned 每线程列表的猜测。`TAtomic` 不会因为使用 UE 包装就自动获得反射/序列化集成；应按目标引擎版本及既有接口选型。

队列是否 SPSC/MPSC、是否拥有元素、销毁前怎样停止生产者，比“容器名带 LockFree”更重要。见[并发与高性能](../../07-网络与游戏服务端/运行调度与过载保护/05-并发与高性能.md)。

### 3.6 双重检查锁（DCLP）与静态单例

在锁外普通读取 `instance`，而另一线程在锁内写同一个普通指针，不能因写方加锁就安全。一次性初始化通常选 C++11 局部静态或 `std::call_once`。原子指针发布可使用 release/acquire，但还必须解决对象销毁和谁拥有它；SC 也不能修复一个被提前释放的实例。

### 3.7 原子误用模式速查

| 错误 | 具体后果 | 应补的合同 |
|---|---|---|
| atomic load + 1 + store 当自增 | 两个读者都读0、都写1，可合法丢更新 | 一个 fetch_add 或 CAS 循环 |
| 普通 payload + relaxed ready | 无发布边，存在数据竞争 UB | 明确的同步与所有权 |
| 重复覆盖一次性发布槽 | 读者读上一份时写者开始写下一份 | 握手、代次和独占时段 |
| 引用计数等于对象线程安全 | 活着不等于内容无竞争 | 对象状态另有同步 |
| volatile 当同步 | 不提供一般 C++ 线程同步 | atomic/锁/消息机制 |
| 每个对象都是 atomic 就认定算法正确 | 多步不变量仍可被打破 | 状态机及线性化点 |

## 4. 示例：本机实验

下面是新增测试中的一次性发布核心，置于函数内并包含 `<atomic> <thread> <string>`。两个线程结束前，所有引用对象保持活着；消费者写 observed，主线程只在 join 后读它：

~~~cpp
    struct Payload { int sequence; std::string text; };
    Payload payload{};
    std::atomic<bool> ready{false};
    bool observed = false; // Written by consumer, read only after join.
    std::thread producer([&] {
        payload = {42, "inventory-ready"};
        ready.store(true, std::memory_order_release);
    });
    std::thread consumer([&] {
        while (!ready.load(std::memory_order_acquire)) std::this_thread::yield();
        observed = payload.sequence == 42 && payload.text == "inventory-ready";
    });
    producer.join();
    consumer.join();
~~~

这里没有“reset ready”操作，不能直接改成消息队列。要复用槽位，消费者必须先完成读取再交还所有权，生产者才能覆盖；可用双向握手或带代次的有界队列，而不是加一个 sleep。

### 4.1 复现与修改指南

在仓库根执行：

~~~sh
python3 -B evidence/cpp/foundation-contracts/run.py --cxx g++-14
~~~

完整[源码、报告和边界](../../../evidence/cpp/foundation-contracts/README.md)包括一次性发布、合法拆分 load/store 丢更新模型、CAS 失败写回和等总工作量计数。不要把安全例改为普通数据竞争后仍以“PASS 次数”验收。

## 5. 实验结果与结论

原有[atomic-memory-order 实验](../../../evidence/labs/atomic-memory-order/README.md)与原始 Windows 输出保留，用作历史观察，不冒称本次重跑。旧稿曾将这些数字过度泛化：

| 旧观测 | 可得结论 | 不可得结论 |
|---|---|---|
| 单线程 81.4/81.6/82.1 ms | 该轮三个样本相近 | 所有线程数差异均小于5% |
| 8线程 1815/2739/2766 ms | 该轮样本差异明显，需重复及控制实验 | 三种顺序成本相同 |
| 每线程固定2000万次，线程数1→8 | 总操作数也变8倍 | 墙钟比就是同负载的竞争惩罚 |
| 多轮发布观察成功 | 该程序在该次运行通过 | 证明所有架构、所有调度正确 |

### 5.1 与 false sharing 实验的联动解读

共享同一计数器的竞争与多个逻辑独立计数器落在同一缓存行的 false sharing 不同。比较布局前先固定总工作量、线程数、CPU亲和/频率条件、预热和重复次数，报告墙钟、吞吐和离散程度。本次新增实验只检查总计数，不测耗时，不产出“加速多少倍”的结论。

### 5.2 标准演进备忘

C++11 提供基本原子模型；C++17 有 `is_always_lock_free`；C++20 有 `atomic_ref` 和 wait/notify。atomic_ref 的对齐与访问约束必须单独满足；notify 负责唤醒，发布可见性仍取决于原子操作的顺序与读值来源。标准草案会演进，不能把最新版段号当固定 C++20 原文。

## 6. 最佳实践

- 审查先写不变量、所有者、发布/接收点和停止协议，再选 memory_order。
- 独立统计可用 relaxed，其他用途也可在完整协议证明下使用；统计数达到阈值不自动发布对应普通数组内容。
- 引用计数递减常用 release 并在最后一位销毁前 acquire fence，或适用的 acq_rel；必须连同整个引用协议证明，不照搬一条口诀。智能指针本身也不保护载荷的并发写。
- `std::atomic<T>` 是否无锁取决于类型、目标和实现，查询 `is_lock_free()`/`is_always_lock_free`；“原子”不等于“无需内部锁”。
- TSan 是动态工具，执行到的错误可成为证据，漏报不能证明无竞争；ASan 不能代替 TSan。本次没有运行 TSan，旧MSVC环境也不能套用虚构的 `/fsanitize=thread`。

## 7. FAQ

**relaxed 不安全吗？** 它可以正确实现独立计数；错误通常在于让它承担没有提供的发布功能。

**seq_cst 能修所有并发问题吗？** 不能。非法生命周期、两次操作之间的逻辑竞态、没有遵守协议的普通访问仍然可能错误。

**为什么 x86 多次成功不能证明程序？** C++ 规则先决定程序是否有定义；硬件样本只覆盖一次具体编译与执行。

**atomic 的 ++ 与 fetch_add 完全相同吗？** 都是RMW，但返回值语义不同：前置++返回新值，后置++及fetch_add返回旧值；默认内存序也要与显式参数区分。

**volatile 是否强制“写进内存”？** 可移植含义是 volatile 访问的可观察性要求，不是通用缓存刷新、原子性或跨线程同步承诺。

## 8. 验证与基准

新增45项组合功能测试见证据页，其中本章12项；[本次原始结果](../../../evidence/cpp/foundation-contracts/observed-gcc14-2026-10-04/report.json)逐条记录真实退出码。O0/O2/UBSan检查与语言关系推导互补，不能穷举内存模型。现有历史UB实验只作旧观察保留，默认新增runner不运行故意数据竞争。ARM、MSVC、TSan、UE实际线程任务/容器与性能容量均未在本轮验证。

### 8.1 并发代码审查清单

1. 所有普通冲突访问是否存在可说明的 happens-before？
2. acquire 到底读到哪次 release 的值？是否存在提前覆盖或提前回收？
3. 复合事务是否需要RMW或更高层锁？
4. 停止/取消时线程、队列元素和载荷的生命周期如何收口？
5. 性能对比是否同工作量、同编译条件，且没有把UB当优化方案？

### 8.2 快速决策树

独立计数→原子RMW；发布普通对象→同步+所有权；复合可变结构→先用锁或成熟并发容器；生命周期→RAII+明确线程结束；再按真实瓶颈考虑减弱顺序和分片。

## 9. 关联阅读

- [LockFree与FalseSharing](03-LockFree与FalseSharing.md)：进展保证、回收和缓存行布局
- [对象生命周期与RAII](../C%2B%2B语言与对象模型/01-C%2B%2B对象生命周期与RAII.md)：存储与所有权
- [编程与计算机基础路线](../../../00_Index/学习路线/编程与计算机基础.md)
- [本轮可运行合同](../../../evidence/cpp/foundation-contracts/README.md)
