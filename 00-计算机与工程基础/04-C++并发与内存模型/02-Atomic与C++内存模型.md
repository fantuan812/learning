# 02-Atomic与C++内存模型
> 验证与基准：按文中命令执行最小实验，记录结果与边界。

> 知识基线：C++11 内存模型（`std::atomic`、六种 `memory_order`、happens-before）；C++20 保留语义（`consume` 无编译器增强，按 relaxed 处理）；编译器基准 MSVC 2022（v14.44.35207）/ x64；UE 对照以本机 UE5.8 源码为准。
> 版本基准：C++11 引入内存模型；C++17 起 `atomic` 的 `is_always_lock_free` 等查询可用；`std::atomic_ref` 为 C++20。
> 适用范围：多线程客户端/服务端热路径（统计计数、任务队列、引用计数、状态发布）；无锁数据结构见 [03-LockFree与FalseSharing](03-LockFree与FalseSharing.md)。
> 官方参考：[cppreference - std::memory_order](https://en.cppreference.com/w/cpp/atomic/memory_order)、[cppreference - std::atomic](https://en.cppreference.com/w/cpp/atomic/atomic)、[cppreference - happens-before](https://en.cppreference.com/w/cpp/atomic/memory_order#Happens-before)。
> 最后更新：2026-08-12（首版，含本机实验）。
> 知识成熟度：L4（Benchmark/Test + 原始结果，见 [evidence/labs/atomic-memory-order](../../evidence/labs/atomic-memory-order/README.md)）。

## 1. 概述

多线程正确性不只是"加锁"：**原子操作决定"单个变量的读改写是否不可分割"，内存序决定"其他线程能看到什么顺序"**。两者合起来构成 C++ 内存模型。游戏客户端与服务端到处是原子计数（引用计数、任务队列、统计、单例初始化），写错内存序会在极端负载下偶发错误——且只在特定 CPU/编译器下复现。

本文回答：

1. `std::atomic` 的 RMW 与 CAS 为什么可靠，普通变量为什么不行？
2. `relaxed / acquire / release / acq_rel / seq_cst` 各自保证什么，什么时候用哪个？
3. "先写数据、再置标志"（message passing）为什么必须用 release/acquire？
4. x86 上不同内存序的开销差异有多大，什么才是真正的成本？

## 2. 核心概念

| 术语 | 中文 | 一句话定义 |
| --- | --- | --- |
| Atomic operation | 原子操作 | 不可分割的读/写/RMW，其他线程要么看到之前、要么看到之后 |
| RMW | 读-改-写 | 如 `fetch_add`、`compare_exchange`，在硬件层不可分割 |
| Modification order | 修改序 | 每个原子对象上所有写操作的全序（C++ 保证存在） |
| Happens-before | 先于发生 | 跨线程的传递性因果序：A 先于 B 且 B 先于 C ⇒ A 先于 C |
| Sequenced-before | 程序内序 | 同一线程内按语句顺序的因果序 |
| `memory_order_relaxed` | 宽松序 | 只保证原子性，不保证任何顺序 |
| `acquire` / `release` | 获取/释放 | 一对同步点：release 之前写的数据，acquire 之后可见 |
| `seq_cst` | 顺序一致 | 所有原子操作存在一个全局一致序（默认值） |
| Lock-free | 无锁 | 至少一个线程总能推进，不依赖锁 |
| CAS | 比较交换 | `compare_exchange_weak/strong`，无锁循环的基本原语 |
| ABA 问题 | ABA | CAS 比较的值"先 A 后 B 又回 A"，误判为未变化（见 03 篇） |

## 3. 原理详解

### 3.1 为什么需要内存模型：两重重排

```text
源代码顺序 →（编译器重排：优化/指令调度）→ 汇编顺序 →（CPU 重排：乱序执行/存储缓冲）→ 实际可见顺序
```

- **编译器**：只要不影响单线程可观察行为，可以把 `store` 挪前/挪后、把循环提升到寄存器（数据竞争场景甚至会把共享变量"缓存"进寄存器——见实验 B 的前车之鉴）。
- **CPU**：x86 是 TSO（总存储序）：除 `StoreLoad` 外，load/load、store/store 不重排；ARM/POWER 是弱序：任意读写都可能重排。
- C++ 内存模型给出**跨平台抽象**：只要按规则使用 `memory_order`，在任何平台语义一致；违反规则（数据竞争）就是未定义行为，连"看起来能跑"都不可依赖。

### 3.2 happens-before 与释放序列

两条核心规则：

1. **同一线程内**：`sequenced-before` 构成 happens-before。
2. **release/acquire 配对**：线程 A 的 `x.store(v, release)` 与线程 B 的 `x.load(acquire)` 读到该值时，**A 在 store 之前的所有写（含普通变量）对 B 在 load 之后的所有读可见**。

```text
线程 A                         线程 B
data = 42;                    while (!flag.load(acquire)) {}
flag.store(true, release);     use(data);   // data==42 有保证
```

这就是 message passing：`flag` 是"栅栏变量"，`data` 是"载荷"。若把 `flag` 写成 relaxed 或普通 `bool`，B 可能看到 `flag==true` 而 `data` 还是旧值（弱序平台），或数据竞争 UB（普通变量）。

### 3.3 六种 memory_order 语义与用途

| order | 保证 | 典型用途 | x86 上的指令代价 |
| --- | --- | --- | --- |
| `relaxed` | 仅原子性 | 统计计数、单调计数器、引用计数（无配套数据时） | 普通 load/store；RMW 仍是 `lock` 前缀 |
| `consume` | release 链的依赖序 | 指针发布（理论）；无编译器实现增强，实际按 relaxed 处理 | 同 relaxed |
| `acquire` | 该 load 之后的读/写不越过它 | 消费标志、读锁 | 普通 load（x86） |
| `release` | 该 store 之前的写不越过它 | 发布标志、写锁 | 普通 store（x86） |
| `acq_rel` | 上两者合并 | RMW 同步（如无锁队列 push/pop） | `lock` 前缀 RMW |
| `seq_cst` | 全局一致序（所有线程看到同一修改序） | 默认；需要"全体一致"的算法 | store 用 `mfence`/`xchg`（x86 略贵）；RMW 同 `lock` |

工程结论：

- **默认 `seq_cst` 没错但常有浪费**：热路径统计计数用 `relaxed` 即可（实验 E 显示 x86 上三者 RMW 代价几乎相同，但弱序平台与编译器优化空间不同）。
- **"数据 + 标志"同步**：写数据用普通写、置标志用 `release`；读标志用 `acquire`、再读数据。这是最高性价比的同步模式。
- **`seq_cst` 的必要场景**：需要"所有线程对多个原子变量的修改顺序一致"（如双重检查单例的某些形式、全局序号）。

### 3.4 CAS 与无锁正确性

```cpp
std::atomic<long long> counter{0};
long long cur = counter.load(std::memory_order_relaxed);
while (!counter.compare_exchange_weak(cur, cur + 1, std::memory_order_relaxed)) {
    // cur 已被更新为最新值，重试
}
```

- `compare_exchange_weak` 允许伪失败（spurious failure），必须循环；`strong` 不允许伪失败但更贵。
- CAS 循环是"乐观并发"：期望值过期就重读重试；竞争激烈时重试次数上升（性能退化，但仍正确）。
- ABA 问题：比较"值"而不是"身份"，值绕回时误判——对策见 03 篇（tagged pointer / hazard pointer / epoch）。

### 3.5 UE 对照（UE5.8 本机源码）

- `TAtomic`：`Engine\Source\Runtime\Core\Public\Templates\Atomic.h`——UE 的原子包装，提供 `Store/Load/Exchange/Increment` 等，底层走平台原子。
- 无锁容器：`Engine\Source\Runtime\Core\Public\Containers\LockFreeList.h`——`TLockFreePointerListLIFO` 等（第 842/899 行附近），任务系统与渲染线程间传递对象的标准工具。
- 内存分配器：`Engine\Source\Runtime\Core\Public\HAL\MallocBinned.h`——`FMallocBinned` 的每线程空闲列表依赖原子操作避免全局锁。
- 实践中 UE 代码大量直接使用 `std::atomic`（C++11 起）与 `FPlatformAtomics` 底层指令；新代码优先 `std::atomic` 并显式指定 `memory_order`。

### 3.6 双重检查锁（DCLP）与静态单例

```cpp
// 错误版本：双检锁的经典坑（非原子指针 + 无内存序）
struct Service {
    static Service* Get() {
        if (!instance_) {                        // 第一次检查：普通读
            lock_guard l(mu_);
            if (!instance_) instance_ = new Service();   // 发布
        }
        return instance_;
    }
    static Service* instance_;
    static std::mutex mu_;
};
```

问题：`instance_` 的普通读写是数据竞争（UB）；即使编译成"看起来对"的代码，另一线程可能在对象构造完成前读到非空指针（弱序平台）或读到未发布的值。

正确做法按强度递增：

1. **局部静态（magic statics，C++11 起）**：`static Service& Get() { static Service s; return s; }`——语言保证线程安全初始化，零手写；
2. **`std::atomic` + acquire/release**：`instance_.load(acquire)` / `store(ptr, release)`；
3. **`std::call_once`**：显式且可读。

工程结论：游戏服务端与 UE 里"全局服务"优先用局部静态或引擎已有单例（`FSubsystem`/`ISubsystem` 生命周期由引擎管理），不要手写 DCLP。

### 3.7 原子误用模式速查

| 误用 | 症状 | 修复 |
| --- | --- | --- |
| 普通变量多线程写 | 丢更新/UB（实验 B） | `std::atomic` 或锁 |
| 消息传递用 relaxed 标志 | 弱序平台读到旧数据 | `release` 发布 / `acquire` 消费 |
| 热路径全部 seq_cst | 弱序平台额外屏障开销 | 计数用 relaxed，同步点用 acquire/release |
| 手写 DCLP | 悬垂/部分构造可见 | 局部静态 / call_once / acquire-release |
| 用 volatile 当原子 | 仍是数据竞争 | 换 `std::atomic` |
| 引用计数递减用 relaxed | 释放顺序错误 | 递减用 `acq_rel`（配合析构的 acquire） |
| 忽视 ABA | 无锁容器误判 | tagged pointer / hazard pointer / epoch |

## 4. 示例：本机实验

实验源码 `evidence/labs/atomic-memory-order/src/atomic_memory_order.cpp`，五组：

- A：8 线程 × 100 万次 `fetch_add`（relaxed）；
- B：8 线程 × 500 万次 × 5 轮普通 `++`（数据竞争）；
- C：release/acquire 消息传递 500 万轮；
- D：CAS 无锁计数 800 万次；
- E：1/8 线程 × relaxed/acq_rel/seq_cst 的 `fetch_add` 耗时。

### 4.1 复现与修改指南

- 直接复现：`powershell -NoProfile -ExecutionPolicy Bypass -File evidence/labs/atomic-memory-order/scripts/build_run.ps1`；
- 修改 B 组轮数/次数：改 `test_plain_race` 的 `perThread` 与轮数，观察丢更新比例随规模的变化（规模越大丢得越多）；
- 修改 C 组把 `ready` 改成 relaxed：在 x86 上大概率仍然 PASS（TSO），把同样代码在 ARM 上跑才能看到失败——这正是"换平台才暴露"的典型案例；
- 修改 E 组线程数：从 1 到 16 观察竞争拐点，配合 03 篇 false-sharing 实验一起解读。

## 5. 实验结果与结论

环境：Windows x64，MSVC 14.44.35207，`cl /O2 /std:c++17 /EHsc`，2026-08-12。完整原始输出见 [evidence/labs/atomic-memory-order/README.md](../../evidence/labs/atomic-memory-order/README.md)。

| 组 | 结果 | 结论 |
| --- | --- | --- |
| A atomic fetch_add 800 万次 | got=expect=8000000，PASS | 原子 RMW 精确累计 |
| B 普通 `++` 4000 万次 × 5 轮 | 5 轮全丢更新（got≈19~22M） | 数据竞争 UB：丢更新是常态而非意外 |
| C release/acquire 消息传递 500 万轮 | PASS | "数据 + 标志"模式正确 |
| D CAS 计数 800 万次 | PASS | CAS 循环正确 |
| E 1 线程 fetch_add（2000 万/线程） | relaxed 81.4 / acq_rel 81.6 / seq_cst 82.1 ms | x86 上三种 order 开销几乎相同 |
| E 8 线程 fetch_add | relaxed 1815 / acq_rel 2739 / seq_cst 2766 ms | 竞争是主成本：约 22~34 倍于单线程 |

定量结论：

1. **原子正确性可靠，普通变量不可依赖**：A/D 与 B 的对比是"原子 vs 数据竞争"最直接的证据。
2. **release/acquire 是消息传递的正确工具**：C 组 500 万轮无失败。
3. **x86 上内存序不是主要开销**：三种 order 差异 <5%；真正的成本是缓存行竞争（8 线程约 22~34x）。这直接引出 03 篇的 false sharing 话题。
4. 实验 B 还展示编译器/硬件对数据竞争的处理差异：换机器、换优化级别结果都会变——这正是"UB 不可依赖"的现场证据。

### 5.1 与 false sharing 实验的联动解读

本实验 E 组的"8 线程 22~34 倍"与 03 篇 `shared_atomic` 的"34 倍"互相印证：**缓存行竞争是 x86 多线程热路径的第一成本**。两实验合起来的完整结论：

```text
单线程原子 RMW：~82ms（2000 万次）
8 线程竞争同一行：~1.8~2.8s（22~34 倍）   ← 原子实验 E / false-sharing 实验 shared_atomic
8 线程独立行原子槽：~115ms（每线程独立行）  ← false-sharing 实验 atomic_aligned
```

优化路径也因此清晰：先消除"共享写"（每线程缓冲/分片），再谈内存序选择。

### 5.2 标准演进备忘

- C++11：内存模型诞生（`atomic`、六种 order、`thread`）；
- C++17：`is_always_lock_free`、`hardware_destructive_interference_size`（缓存行对齐工具）；
- C++20：`std::atomic_ref`（对非 atomic 对象做原子操作）、`std::atomic<T>::wait/notify`（原子等待）、`memory_order` 在 `cpp20` 细化 `seq_cst` 语义（`seq_cst` 栅栏差异）；
- 演进主线：语言把"硬件乱序 + 编译器重排"抽象成可控语义，工程上只需掌握 relaxed / acquire / release / seq_cst 四档即可覆盖绝大多数场景。
- 迁移注意：老代码里的 `volatile` 并发用法（如自旋标志）应迁移为 `std::atomic`；UE 老代码中的 `TAtomic` 与新代码 `std::atomic` 混用时要统一内存序约定。

## 6. 最佳实践

1. **统计/单调计数用 `relaxed`**：命中率、任务数、流量计数不需要同步语义。
2. **状态发布用 release/acquire**：写数据（普通写）→ `store(release)` 标志；读标志 `load(acquire)` → 读数据。
3. **默认 `seq_cst` 只在需要全局一致序时用**；不要因为"想安全"就给热路径全部 `seq_cst`（弱序平台代价真实存在）。
4. **不要手写"无锁"**：优先 `std::atomic` 简单原语与现成无锁容器（UE `TLockFreePointerListLIFO` 等）；手写 CAS 循环必须处理 ABA 与内存序。
5. **引用计数**：`shared_ptr`/`TSharedPtr` 的计数用 `relaxed`（递增）与 `acq_rel`（递减）是标准做法——不要自己发明。
6. **数据竞争零容忍**：任何普通共享变量被多线程写就是 UB，哪怕"看起来没问题"；用 TSan（`/fsanitize=thread` 或 UE 的 TSan 构建）在 CI 常驻。
7. **UE 侧**：跨线程传对象用 `TLockFreePointerListLIFO`/`TQueue`；热路径计数用 `std::atomic<int32>` relaxed；帧数据发布（渲染线程读 GameThread 数据）遵循引擎既有的"双缓冲 + 帧号"模式，不自行加锁。
8. **全局服务初始化**：局部静态或引擎生命周期管理，禁止手写 DCLP（见 3.6）。
9. **性能验收用 P50/P95/P99**：并发优化的收益在长尾（竞争、重试）上体现，平均值会掩盖；基准要标注线程数与机器拓扑（见 03 篇的复验条件）。

## 7. FAQ

**Q1：`atomic<int>` 和 `volatile int` 有什么区别？**
完全不同：`atomic` 保证不可分割与内存序；`volatile` 只阻止编译器优化（强制访问内存），**不提供原子性**，多线程读写仍是数据竞争。

**Q2：`relaxed` 是不是"不安全的 atomic"？**
不是。`relaxed` 保证原子性（单变量读改写不可分割），只是不提供跨变量的顺序保证。计数场景用它完全正确。

**Q3：为什么我的 relaxed 消息传递在 x86 上"碰巧"是正确的？**
因为 x86 是 TSO：普通 load/store 本身不乱序（除 StoreLoad）。在 ARM/POWER 弱序平台上同样代码可能失败。C++ 内存模型存在的意义就是让你不依赖"碰巧"。

**Q4：`consume` 能用吗？**
实践中不要用：目前没有主流编译器实现其语义增强，实际按 relaxed 处理；需要依赖序时用 acquire/release 或明确排序。

**Q5：`compare_exchange_weak` 和 `strong` 怎么选？**
循环重试逻辑用 `weak`（便宜，允许伪失败）；单次判定（如无锁栈的头部更新判定）用 `strong` 减少意外重试。

**Q6：UE 的 `TAtomic` 和 `std::atomic` 怎么选？**
新代码优先 `std::atomic`（标准、可移植、显式 memory_order）；需要与 UE 反射/序列化集成或兼容旧代码时用 `TAtomic`。

**Q7：测试能证明无数据竞争吗？**
不能。测试只能覆盖路径，竞争是概率性的。用 TSan（线程消毒器）做静态/动态检测，并把原子语义的单元测试写成"多轮 + 多线程"压力形态（如实验 A/B）。

**Q8：`std::atomic` 一定无锁（lock-free）吗？**
不一定：对不满足 `is_always_lock_free` 的类型（如大结构体），实现会退化为内部锁。绝大多数整数/指针类型在主流平台是 lock-free，用 `is_always_lock_free` 查询。

**Q9：为什么引用计数递减要用 `acq_rel`？**
`fetch_sub` 递减到 0 意味着本线程"最后持有者"，需要 acquire 语义看到其他线程对该对象的所有写（保证释放安全）；递增只需 relaxed。这是 `shared_ptr` 实现的标准模式，自己实现引用计数时照抄。

**Q10：`memory_order` 能随便混用吗？**
可以，但只有"配对正确"的同步才有意义（release 配 acquire，seq_cst 之间全局一致）。混用时最稳妥的规则：写侧 release、读侧 acquire，不确定就用 seq_cst。

**Q11：`fetch_add` 与 `operator++` 对 atomic 有区别吗？**
对 `std::atomic` 没有：`++` 就是 `fetch_add` 的语法糖，两者都是 RMW。容易混淆的是"`atomic` 的 load 后普通 `+1` 再 store"——那是非原子的读-改-写，会丢更新。

**Q12：为什么单例/全局状态在游戏里要避免？**
全局可写状态是隐式共享：跨线程竞争、初始化顺序、测试隔离都变难。服务端用依赖注入/上下文对象（如 TickContext），UE 用引擎生命周期的 Subsystem——都是"减少隐式共享"的工程手段。

**Q13：自旋锁用 atomic 怎么写？**
`while (flag.exchange(true, acquire)) { /* 自旋或让出 */ }`，解锁 `flag.store(false, release)`。注意：自旋锁在单核/受限环境会饿死其他线程，生产环境优先 `std::mutex`（内部有正确退避），自旋只用于明确可预期的短临界区。

## 8. 验证与基准

- 本机实验：`powershell -NoProfile -ExecutionPolicy Bypass -File evidence/labs/atomic-memory-order/scripts/build_run.ps1`，原始输出 `results/atomic_memory_order_win_x64_msvc.txt`。
- 弱序平台验证计划：在 ARM（如 Apple Silicon / 手机 / ARM 服务器）上用相同源码复跑 A~E；重点观察 relaxed 消息传递是否出现乱序失效（预期：可能偶发，需加大轮次）。
- 消毒器：MSVC 用 `/fsanitize=address`（ASan）+ 未来 TSan 支持；GCC/Clang 用 `-fsanitize=thread` 跑并发用例。
- 标准依据：[cppreference - std::memory_order](https://en.cppreference.com/w/cpp/atomic/memory_order)、[cppreference - std::atomic](https://en.cppreference.com/w/cpp/atomic/atomic)。

消毒器命令（Linux/CI 参考）：

```bash
clang++ -std=c++20 -fsanitize=thread -O1 -g atomic_memory_order.cpp -o atomic_tsan
./atomic_tsan      # TSan 会直接报告 B 组的数据竞争
```

### 8.1 并发代码审查清单

| 检查项 | 判定 |
| --- | --- |
| 共享变量是否都是 atomic 或锁保护 | 普通变量多线程写 = FAIL |
| 消息传递是否 release/acquire 配对 | relaxed 标志发布 = FAIL |
| 引用计数递减是否 acq_rel | 只有 relaxed = FAIL |
| 热路径计数是否 relaxed（非 seq_cst） | seq_cst 浪费但不算错 |
| 是否手写 DCLP / 无锁容器 | 优先现成实现，手写需评审 |
| 是否有 TSan 在 CI 常驻 | 无 = 风险项 |

> 本文档的 L4 证据只覆盖 x86；ARM 弱序验证与 TSan 常驻属于升级 L5 的验收项。

### 8.2 快速决策树

```text
多线程共享一个变量？
├─ 只是统计/单调计数 → atomic relaxed
├─ 传递"数据 + 标志" → 数据普通写 + store(release) / load(acquire)
├─ 需要全局一致序（多变量）→ seq_cst
├─ 复杂数据结构（队列/栈/链表）→ 现成无锁容器或锁；不要手写 CAS 数据结构
└─ 生命周期/所有权 → shared_ptr/TSharedPtr（内部已正确处理内存序）
```

## 9. 关联阅读

- [03-LockFree与FalseSharing](03-LockFree与FalseSharing.md)：CAS/ABA/缓存行竞争的完整展开（含本机 Benchmark）。
- [01-C++对象生命周期与RAII](../01-C++核心/01-C++对象生命周期与RAII.md)：所有权与智能指针的引用计数背景。
- [00-计算机与工程基础](../README.md)：本层规划与门禁。
- [游戏知识/12-引擎源码分析](../../游戏知识/12-引擎源码分析/README.md)：任务图（TaskGraph）与线程模型的 UE 实现。
- [游戏服务端/06-世界模拟与运行时](../../游戏服务端/06-世界模拟与运行时/README.md)：服务器多线程边界与 Tick 调度。
- [游戏服务端/01-架构与网络/05-并发与高性能](../../游戏服务端/01-架构与网络/05-并发与高性能.md)：服务端应用层的并发模型与原子实践（篇级互链；分工：本文=语言层/硬件层）。
- [04-C++并发与内存模型 README](README.md)：本分类导航与规划。
- `01-线程同步与锁`（规划）：mutex/rwlock/condition variable 与原子互补。
- [03-LockFree与FalseSharing](03-LockFree与FalseSharing.md)：CAS/ABA 与缓存竞争的完整展开。
- [游戏算法](../../游戏算法/README.md)：确定性算法与基准工程（与原子语义相关的可复现性要求）。
