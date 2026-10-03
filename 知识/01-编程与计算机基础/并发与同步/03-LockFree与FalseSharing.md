---
type: Mechanism
title: "03-LockFree与FalseSharing"
status: stable
verified: []
maturity: L4
---
# 03-LockFree与FalseSharing

> 知识基线：C++11 内存模型与无锁原语；x86 缓存一致性（MESI 族）与 RFO；编译器基准 MSVC 2022（v14.44.35207）/ x64；UE 对照以本机 UE5.8 源码为准。
> 版本基准：C++11 引入 `std::atomic` 与 CAS；`std::atomic_ref` 为 C++20；`alignas` 自 C++11。
> 适用范围：多线程热路径（任务队列、统计、状态发布）与高性能服务器；读者应先读 [02-Atomic与C++内存模型](02-Atomic与C++内存模型.md)。
> 官方参考：[cppreference - std::atomic](https://en.cppreference.com/w/cpp/atomic/atomic)、[cppreference - compare_exchange](https://en.cppreference.com/w/cpp/atomic/atomic/compare_exchange)、[cppreference - alignas](https://en.cppreference.com/w/cpp/language/alignas)。
> 最后更新：2026-08-12（首版，含本机 Benchmark）。
> 知识成熟度：L4（Benchmark + 原始结果，见 [evidence/labs/false-sharing](../../../evidence/labs/false-sharing/README.md)）。

## 1. 概述

两个现象经常被混为一谈，但机制完全不同：

1. **Lock-free（无锁）**：不用锁也能保证"至少一个线程推进"的并发数据结构——正确性难题（CAS/ABA），与性能无关；
2. **False sharing（伪共享）**：多个线程各自写"逻辑上独立"的变量，却因为落在同一条缓存行而互相拖慢——纯性能问题，与正确性无关。

本文回答：

- 缓存一致性协议如何让"写相邻变量"变慢（RFO ping-pong）？
- 为什么原子 RMW 的 false sharing 一定可复现，而非原子写可能观察不到？
- 无锁栈怎么做，ABA 是什么，怎么破？
- 游戏客户端/服务端里哪些真实代码正在吃 false sharing 的亏，怎么改？

## 2. 核心概念

| 术语 | 中文 | 一句话定义 |
| --- | --- | --- |
| Cache line | 缓存行 | 缓存一致性最小单位（x86 通常 64 字节） |
| MESI / MOESI | 缓存一致性协议 | 缓存行状态机：Modified/Exclusive/Shared/Invalid(+Owned) |
| RFO | 读归属 | 写共享缓存行前先获取独占权的总线事务 |
| False sharing | 伪共享 | 不同线程写同一缓存行的不同字节，互相触发 RFO |
| Padding / alignment | 填充/对齐 | 用 `alignas(64)` 等把热变量隔离到独立缓存行 |
| Lock-free | 无锁 | 数据结构保证至少一个线程总能完成操作 |
| Wait-free | 无等待 | 每个线程都在有限步内完成（更强） |
| CAS loop | CAS 循环 | `compare_exchange` 乐观重试模式 |
| ABA | ABA 问题 | CAS 比较的值经历 A→B→A，身份已变却被判未变 |
| Hazard pointer / epoch | 危险指针/代际 | 延迟回收被并发线程可能引用的内存，防 ABA/悬垂 |

## 3. 原理详解

### 3.1 缓存一致性与 RFO：为什么"写相邻变量"会慢

```text
线程 A 写 slot[0]（行内字节 0）        线程 B 写 slot[1]（同一行内字节 8）
  A 需要该行 Exclusive/M 状态             B 需要该行 Exclusive/M 状态
  → 向其他核发 Invalidate                 → A 的行被作废，B 获得独占
  → 行在核间来回传递（RFO ping-pong）      → 下一次 A 写又要抢回
```

当 4 个线程各自写同一条缓存行的不同槽位时，每次写都触发一次全核失效与归属转移。**逻辑上互不干扰，物理上互相踩脚**——这就是 false sharing。

消除方法：

- **`alignas(64)`**：让每个热槽独占缓存行（实验的 atomic_aligned 版本）；
- **每线程独立缓冲**：写进自己的局部/每线程存储，最后合并；
- **只读共享 + 写私有**：共享数据只读，写数据放线程私有区。

### 3.2 为什么"原子槽位"一定可复现，普通写不一定

本机实验给出一个反直觉结论（详见第 5 节）：

- 普通 `volatile long long` 相邻槽位（packed）相对独立行（aligned）只慢 1.2~1.5 倍；
- 换成原子槽位（每线程 `fetch_add` 自己的槽），packed/aligned 达 3.4~7.0 倍；
- 单原子计数器 4 线程竞争（shared_atomic）比 aligned 慢约 34 倍。

原因：

1. **普通写可以被缓冲/合并**：非原子 store 经过存储缓冲，若线程被调度到同一物理核心（虚拟化/受限环境常见），缓存行始终留在该核 L1，几乎没有跨核失效。
2. **原子 RMW 强制走一致性协议**：`lock` 前缀指令要求缓存行处于独占状态，每轮都发 RFO，竞争必然显现。
3. **编译器可能把普通共享变量的循环提升进寄存器**（数据竞争是 UB，编译器有权这么做），实验里 `volatile` 才保住内存访问。

工程含义：**怀疑 false sharing 时，用原子槽位/真实共享复现最可靠；修完后用非原子热路径验证收益**。

### 3.3 无锁栈（Treiber Stack）与 ABA

```cpp
template <typename T>
class LockFreeStack {
    struct Node { T value; Node* next; };
    std::atomic<Node*> head_{nullptr};
public:
    void push(T v) {
        Node* n = new Node{std::move(v), nullptr};
        Node* h = head_.load(std::memory_order_relaxed);
        do {
            n->next = h;
        } while (!head_.compare_exchange_weak(h, n, std::memory_order_release));
    }
    // pop 需要处理 ABA 与内存回收（下方说明），此处省略
};
```

`push` 的正确性依赖 CAS 循环：`head_` 变了就重试。`pop` 的经典陷阱：

```text
线程 P 读取 head=A
另一线程弹出 A、释放 A、再压入新节点恰好复用地址 A
P 的 CAS(A→A.next) 成功 —— 但 A 已经不是原来那个节点（ABA）
```

对策：

- **Tagged pointer**：指针低位带代际计数，ABA 时计数不同（`compare_exchange` 比较"指针+计数"）；
- **Hazard pointer**：读节点前登记，回收前检查是否被引用；
- **Epoch / 垃圾延迟回收**：按代回收，保证 CAS 窗口内节点不释放（UE 的 `TLockFreePointerListLIFO` 内部即用类似机制）。

### 3.4 与 UE 的关联（UE5.8 本机源码）

- **`TLockFreePointerListLIFO`**：`Engine\Source\Runtime\Core\Public\Containers\LockFreeList.h`（第 842/899 行附近）——任务图（TaskGraph）与渲染线程传递对象的无锁容器；内部用原子节点与延迟回收，用户不要自己重复造。
- **`FMallocBinned`**：`Engine\Source\Runtime\Core\Public\HAL\MallocBinned.h`（第 109 行附近）——按大小分桶 + 每线程空闲列表，把全局锁竞争降到每线程，本质就是"每线程缓冲 + 原子合并"的工程形态。
- **多线程渲染**：GameThread 写、RenderThread 读的共享数据（如变换缓冲、实例数据）按"帧双缓冲 + 原子发布"组织；热路径上把每个渲染线程独立的槽位 `alignas` 对齐是常见优化。
- **服务器**：实体状态按 Zone/线程分片，避免所有线程写同一张全局表；统计计数用 `relaxed` 原子累加，定期合并。

### 3.5 常见 false sharing 现场与检查方法

最容易踩的三种现场：

1. **每线程槽位数组**：`long long perThreadCounters[NUM_THREADS]`——相邻排列，必然共享缓存行；修复：`alignas(64)` 或 `std::hardware_destructive_interference_size`（C++17 起）对齐。
2. **对象内的相邻热字段**：`struct Stats { long long hits; long long misses; };` 两个线程各写一个字段——加 `alignas(64)` 或拆成独立对象。
3. **队列头尾指针**：单生产者单消费者队列的 `head`/`tail` 放在同一结构体内，写冲突互相拖慢——用 padding 隔离（经典 DPDK/游戏网络队列实践）。

验证布局的代码模式：

```cpp
struct Stats { alignas(64) long long hits; alignas(64) long long misses; };
static_assert(sizeof(Stats) >= 128, "hits/misses 必须隔离到不同缓存行");
```

检查方法：

- **布局检查**：打印 `sizeof(T)` 与字段偏移（`offsetof`），确认热字段是否落在同一条 64 字节区间；
- **对齐声明**：`alignas(std::hardware_destructive_interference_size)` 或直接 `alignas(64)`（x86 缓存行 64 字节；ARM 常见 64，可查 `sysconf(_SC_LEVEL1_DCACHE_LINESIZE)`）；
- **复现实验**：把槽位改成原子 RMW 复测（本实验的 atomic_packed 版），快速确认是否缓存行问题；
- **工具**：Linux `perf c2c`（false sharing 检测）、Intel VTune 的 "Analysis of memory access"；Windows 可先看 PMU 的缓存未命中计数。

### 3.6 x86 缓存层次与行大小

理解 false sharing 前先确认目标 CPU 的缓存参数（Windows 查询：`wmic cpu get L2CacheSize,L3CacheSize` 或直接查型号规格）：

| 层级 | 典型容量 | 延迟 | 一致性粒度 |
| --- | ---: | ---: | --- |
| L1 | 32~64KB/核 | ~4 cycles | 64 字节行 |
| L2 | 0.5~2MB/核 | ~12 cycles | 64 字节行 |
| L3 | 8~64MB 共享 | ~40 cycles | 64 字节行 |

缓存行是**一致性协议的最小单位**：任何一行的写归属转移都让整行（64 字节）失效。所以"8 字节槽位 + 56 字节无用数据"依然与邻居互相踩脚——只有把热槽隔到不同行才有效。

## 4. 示例：本机 Benchmark

实验源码 `evidence/labs/false-sharing/src/false_sharing.cpp`，五组对照（4 线程 × 2000 万次/线程，3 轮）：

- `aligned` / `packed`：`volatile long long` 槽位，独立行 vs 相邻排列；
- `shared_atomic`：单原子计数器（真实共享，参照）；
- `atomic_packed` / `atomic_aligned`：每线程原子槽位，同线 vs 独立线；
- 局部累加对照：每线程寄存器累加后写回一次（绝对下界）。

### 4.1 复现与修改指南

- 直接复现：`powershell -NoProfile -ExecutionPolicy Bypass -File evidence/labs/false-sharing/scripts/build_run.ps1`；
- 改线程数/次数：`false_sharing.exe 10000000 8`（第二参数为线程数）；
- 物理机复验：给 `run_padded_direct` 的线程加 `SetThreadAffinityMask` 绑定到不同物理核（Windows）或用 `taskset`（Linux），packed/aligned 比例预期 10~30x；
- 观察编译器影响：分别用 `/O2` 与 `/Od` 编译，对比 packed 版差异（`/Od` 下竞争更明显，因为寄存器提升消失）。

## 5. 实验结果与结论

环境：Windows x64（16 逻辑处理器，虚拟化/受限调度），MSVC 14.44.35207，`cl /O2 /std:c++17 /EHsc`，2026-08-12。完整原始输出见 [evidence/labs/false-sharing/README.md](../../../evidence/labs/false-sharing/README.md)。

| 版本 | run1 | run2 | run3 | 说明 |
| --- | ---: | ---: | ---: | --- |
| aligned（volatile，独立行） | 38.5 | 37.1 | 35.3 ms | 基线 |
| packed（volatile，同行） | 48.1 | 50.0 | 53.1 ms | 仅 1.2~1.5x（本机受限） |
| shared_atomic（单计数器） | 1288.9 | 1284.0 | 1270.4 ms | 约 34x——真实共享竞争 |
| atomic_packed（同行原子槽） | 798.6 | 388.0 | 923.6 ms | 3.4~7.0x——false sharing 可靠复现 |
| atomic_aligned（独立行原子槽） | 119.7 | 114.9 | 131.9 ms | 原子槽基线 |
| 局部累加对照 | ~6.3 ms | ~6.3 ms | —— | 绝对下界（无内存竞争） |

结论：

1. **false sharing 真实存在且可观**：同一缓存行内的独立原子槽比独立行慢 3.4~7.0 倍；真实共享慢约 34 倍。
2. **`alignas(64)` 隔离有效**：atomic_aligned 相对 atomic_packed 稳定快 3~7 倍。
3. **非原子写在受限环境可能"骗过"你**：packed 仅 1.2~1.5x——线程落核、编译器优化都会掩盖问题；用原子复现、用真实场景验收。
4. **最彻底的方案是"不共享"**：局部累加 6ms 远低于任何内存竞争版本——每线程状态留在寄存器/局部是王道。

### 5.1 数据解读：为什么 run2 的 atomic_packed 只有 388ms

三次运行 atomic_packed 为 799/388/924ms（波动大），而 shared_atomic 稳定在 ~1280ms。原因：

- atomic_packed 是"4 个原子槽竞争 1 条行"，每个 RMW 等待行归属；行归属转移的延迟受线程调度影响，波动大；
- shared_atomic 是"4 线程竞争同一变量"，`lock xadd` 串行化更彻底，波动小；
- 结论：**竞争数据的单次值不可靠，取多轮并报告范围**；修复前后对比要在同一机器、同一负载、多轮取中位数。
- 补充：run1/run3 的 atomic_packed（799/924ms）接近 shared_atomic（~1280ms），说明行归属转移的延迟在多数情况下已接近"真共享"的串行化成本。

## 6. 最佳实践

1. **热路径共享写先问"能不能不共享"**：每线程缓冲 → 定期合并；读多写少 → 只读共享。
2. **必须共享的槽位 `alignas(64)`**：每线程槽、队列头尾、统计累加器按 64 字节隔离。
3. **用原子复现竞争**：怀疑 false sharing 时先把槽位改成原子 RMW 复测，确认后再优化。
4. **无锁优先用现成容器**（UE `TLockFreePointerListLIFO` 等）；手写必须处理 ABA 与回收。
5. **不要用 `volatile` 当并发工具**：它只阻止优化，不提供原子性与顺序。
6. **性能验收看 P50/P95/P99**：false sharing 的代价是吞吐下降与延迟尖峰，单测平均值会掩盖问题；在真实多核物理机上复验（本实验在受限环境低估了 packed 差异）。
7. **CI 常驻 TSan**：无锁代码的正确性靠消毒器与压力测试，不靠"跑了很多次没崩"。
8. **写代码前先定"谁写谁读"**：共享数据按"单写多读/多写/只读"分类；多写数据优先考虑每线程副本 + 合并，而不是对齐硬扛。
9. **不要把 `alignas(64)` 到处撒**：每个 64 字节 padding 都在放大内存占用与缓存占用；只对"跨线程写 + 热路径"字段使用，并用 `sizeof` 验证。
10. **锁与无锁之间先测再选**：竞争模型（读写比例、线程数、临界区长度）决定谁快；给两种实现都做 P99 对比再定。

## 7. FAQ

**Q1：`volatile` 为什么不能修并发？**
`volatile` 强制每次访问走内存（防编译器优化），但多线程同时读写的"原子性"与"顺序"它都不管，仍是数据竞争 UB。

**Q2：`alignas(64)` 一定能消除 false sharing 吗？**
只要每个热槽的 64 字节区间不与其他线程写的字节重叠即可；注意数组/容器元素按元素大小排布，`vector<Slot>` 中 `alignas(64)` 的结构体会自动按 64 对齐。

**Q3：lock-free 一定比加锁快吗？**
不一定。竞争激烈时 CAS 重试、缓存行 ping-pong 可能比锁更慢；lock-free 的价值是"不阻塞/无死锁/优先级反转少"，性能要按场景 Benchmark。

**Q4：ABA 在什么场景才真正危险？**
内存回收型 CAS（无锁栈 pop、无锁链表删除）——地址复用导致误判。数值型 CAS（计数器、序号）通常无 ABA 风险。

**Q5：UE 里我该用什么做线程间队列？**
`TLockFreePointerListLIFO`/`TQueue`（多生产者单消费者等变体），或 TaskGraph 的任务依赖；不要自己写无锁队列，除非性能数据证明必要。

**Q6：为什么我的 false sharing 实验测不出来？**
三个常见原因：线程被调度到同一物理核心（虚拟化/亲和性）、编译器把循环提升进寄存器、测量没取 P99/没跑足够长。对照本实验：改用原子槽位复现，或 `SetThreadAffinityMask` 强制分散线程。

**Q7：`shared_atomic` 34x 和 false sharing 是一回事吗？**
不完全：`shared_atomic` 是"真共享"（同一变量的合法竞争），false sharing 是"假共享"（不同变量的非法相互干扰）。两者都是缓存行竞争，但修复手段不同：前者要减少写频率/改数据结构，后者只需对齐隔离。

**Q8：`std::hardware_destructive_interference_size` 是什么？**
C++17 提供的"同缓存行会互相干扰"的保守大小（通常 64），用于对齐隔离；配套 `hardware_constructive_interference_size` 表示"应尽量放同一行"的大小（用于共享只读数据）。

**Q9：无锁容器在游戏里真有必要吗？**
有：TaskGraph 任务投递、渲染线程与游戏线程之间、服务器线程间的对象传递都是高频 + 短临界区，锁会让临界区竞争放大。UE 已提供 `TLockFreePointerListLIFO` 等成熟实现，直接使用即可。

**Q10：为什么我的基准在 Release 和 Debug 差别巨大？**
Debug 无优化时编译器不会把共享变量提升进寄存器、也不会做向量化，false sharing 反而"更容易"暴露；Release 优化后可能被掩盖或改变。结论：性能结论只在 Release 下成立，正确性结论两个配置都成立。

**Q11：无锁队列的"内存回收"为什么和 ABA 绑在一起？**
无锁 pop 从共享链表中摘下节点后，其他线程可能仍持有指向它的指针（CAS 窗口内）；直接 `delete` 会造成悬垂 CAS。所以回收必须延迟到"确认无人引用"——hazard pointer/epoch 本质都是"延迟回收"，而延迟回收又让地址复用成为可能，ABA 与回收是一枚硬币的两面。

**Q12：`std::atomic` 的 `fetch_add` 和 `lock xadd` 是一回事吗？**
在 x86-64 MSVC/Clang/GCC 上，`fetch_add`（任意 order）都会编译成 `lock xadd`；`lock` 前缀本身保证 RMW 原子性并隐式充当全屏障。这就是实验里三种 order 耗时几乎相同的原因——x86 上真正的内存序成本在 store 侧（`seq_cst store` 需 `mfence`/`xchg`），RMW 侧三者等价。

**Q13：为什么"每线程缓冲 + 定期合并"通常比对齐更好？**
对齐只是"不互相踩脚"，每线程缓冲连共享读都省了（写完全私有，只在合并点碰一次共享）。代价是合并逻辑与最终一致性；两者可组合：每线程缓冲 + 合并点对齐。

## 8. 验证与基准

- 本机实验：`powershell -NoProfile -ExecutionPolicy Bypass -File evidence/labs/false-sharing/scripts/build_run.ps1`，原始输出 `results/false_sharing_win_x64_msvc.txt`（4 线程）与 `results/false_sharing_16t_win_x64_msvc.txt`（16 线程）。
- 复验条件：真实多核物理机（≥4 物理核）；可加 `SetThreadAffinityMask` 把线程绑到不同核心，packed/aligned 比例预期 10~30x。
- 正确性验证：无锁栈/队列用 TSan + 多线程压力测试；UE 侧可用 `Automation` 并发用例。
- 标准依据：[cppreference - std::atomic](https://en.cppreference.com/w/cpp/atomic/atomic)、[cppreference - alignas](https://en.cppreference.com/w/cpp/language/alignas)。

复验与工具（Linux/CI 参考）：

```bash
# TSan 无锁容器压力测试
clang++ -std=c++20 -fsanitize=thread -O1 -g lockfree_test.cpp -o lockfree_tsan
# perf c2c 检测 false sharing（需 >= 4.10 内核）
perf c2c record ./false_sharing 20000000 4
perf c2c report
```

Windows 对照：使用 `SetThreadAffinityMask` 把线程绑定到不同物理核心后重跑 `build_run.ps1`，packed/aligned 比例预期显著上升（本机受限环境见实验 README 的"局限"）。

### 8.1 无锁代码审查清单

| 检查项 | 判定 |
| --- | --- |
| 每个共享槽位是否与别的线程写隔离（对齐/独立行） | 相邻热槽 = FAIL |
| CAS 循环是否处理 ABA（tagged/hazard/epoch） | 内存回收型无锁无对策 = FAIL |
| 内存序是否明确（不依赖默认 seq_cst 碰巧正确） | 全部默认且说不清 = 风险项 |
| 是否用现成无锁容器（UE `TLockFreePointerListLIFO`） | 手写需评审 |
| 是否在真实多核物理机复验过 P50/P95/P99 | 只在受限环境测过 = 风险项 |
| TSan 压力测试是否常驻 CI | 无 = 风险项 |

> 本文档的 L4 证据来自受限环境（虚拟化/调度），packed 差异被低估；真实多核物理机复验（第 8 节命令）是升级 L5 的验收项。

### 8.2 快速决策树

```text
多线程写同一缓存行？
├─ 逻辑上独立的数据 → alignas(64) / 每线程缓冲（消除 false sharing）
├─ 真实共享的单一计数 → 减少写频率 / 分片汇总（原子 relaxed 累加）
├─ 需要无锁结构 → TLockFreePointerListLIFO 等现成实现 + ABA/回收评审
└─ 不确定 → 先按"每线程私有 + 定期合并"设计，再按需共享
```

## 9. 关联阅读

- [02-Atomic与C++内存模型](02-Atomic与C++内存模型.md)：原子语义与内存序（本文的前置）。
- [01-C++对象生命周期与RAII](../C%2B%2B语言与对象模型/01-C%2B%2B对象生命周期与RAII.md)：所有权与回收（ABA 对策的背景）。
- [00-计算机与工程基础](../../../00-计算机与工程基础/README.md)：本层规划与门禁。
- [游戏知识/12-引擎源码分析](../../../游戏知识/12-引擎源码分析/README.md)：TaskGraph 无锁队列与渲染线程数据流的 UE 实现。
- [游戏服务端/06-世界模拟与运行时](../../../游戏服务端/06-世界模拟与运行时/README.md)：服务器多线程分片与 Tick 调度。
- [游戏服务端/01-架构与网络/05-并发与高性能](../../../游戏服务端/01-架构与网络/05-并发与高性能.md)：服务端应用层的并发模型与缓存竞争实践（篇级互链；分工：本文=语言层/硬件层）。
- [04-C++并发与内存模型 README](../../../00-计算机与工程基础/04-C%2B%2B并发与内存模型/README.md)：本分类导航与规划。
- `01-线程同步与锁`（规划）：锁与无锁的适用边界。
- [02-Atomic与C++内存模型](02-Atomic与C++内存模型.md)：原子语义与内存序的完整展开。
- [游戏知识/12-引擎源码分析](../../../游戏知识/12-引擎源码分析/README.md)：UE 线程模型与任务系统的源码实现。
