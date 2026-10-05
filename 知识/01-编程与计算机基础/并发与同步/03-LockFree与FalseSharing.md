---
type: Mechanism
title: "03-LockFree与FalseSharing"
status: stable
verified: []
maturity: L2
updated: 2026-10-05
sources:
  - id: cpp17-n4659
    title: "C++17 工作草案 N4659（2017-03-21）"
    resource: "https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2017/n4659.pdf"
  - id: linux-false-sharing
    title: "Linux kernel documentation · False Sharing"
    resource: "https://docs.kernel.org/kernel-hacking/false-sharing.html"
  - id: hazard-pointers-proposal
    title: "P2530R3 · Why Hazard Pointers Should be in C++26（2023）"
    resource: "https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2023/p2530r3.pdf"
  - id: epoch-reclamation
    title: "Trevor Brown · Reclaiming Memory for Lock-Free Data Structures（arXiv v1，2017-12-04）"
    resource: "https://arxiv.org/pdf/1712.01044"
  - id: clang-tsan
    title: "Clang ThreadSanitizer documentation"
    resource: "https://clang.llvm.org/docs/ThreadSanitizer.html"
  - id: gcc-atomics
    title: "GCC · Memory Model Aware Atomic Operations"
    resource: "https://gcc.gnu.org/onlinedocs/gcc/_005f_005fatomic-Builtins.html"
---
# 03-LockFree与FalseSharing

> 知识基线：C++17 工作草案 N4659 的内存位置、data race、原子操作与进展条款；缓存一致性按行协调的机制解释。`std::atomic`、CAS、`alignas` 自 C++11，本文不使用 C++20 的 `atomic_ref`。
> 适用范围：多线程热路径的访问分工、布局诊断和无锁容器选型；前置为 [02-Atomic与C++内存模型](02-Atomic与C++内存模型.md)。
> 历史实验：2026-08-12，Windows x64，MSVC 2022（v14.44.35207）；原始数据保留在 [false-sharing 实验](../../../evidence/labs/false-sharing/README.md)。本轮没有重跑旧基准，也没有核验旧 UE5.8 私有安装源码。
> 最后更新：2026-10-05，核对标准、公开资料及旧源码/输出，补充非计时功能与布局短例。
> 知识成熟度：L2。原 L4 将局部耗时当作整篇访问正确性、无锁算法及 UE 实现的证据，现按主要承诺改为静态核对。短例确实运行，但不构成完整容器证明或新的性能基准；`verified: []` 不代表另有人工验证事件。

## 1. 先分清要解决的问题

**Lock-free 是进展性质，false sharing 是硬件层的性能现象。** 不加锁不等于无锁算法；共享缓存行也不等于 C++ 数据竞争。排查时依次问：

1. 哪些线程访问哪个对象、在哪个生命周期内访问？
2. 是否访问同一语言层 memory location，冲突访问间有没有 happens-before？
3. 合法访问的数据实际位于哪些缓存行，访问是否含写？
4. 若要求 lock-free，整个操作的分配、原子实现、重试和回收是否满足进展条件？

先解决前两项，再比较布局性能。否则对齐可能让一个有 UB 的程序“看起来稳定”，或把本来合法的独立槽位误改成不必要的原子 RMW。

## 2. 核心概念与访问合同

| 术语 | 本文含义 | 不能由它直接推出什么 |
| --- | --- | --- |
| Memory location | 标量对象，或一段相邻且均非零宽的位域 | 不等于缓存行，也不等于任意“逻辑字段” |
| Data race | 潜在并发的同位置冲突访问，至少一个非原子，且彼此没有 happens-before | 不能只看两个地址是否同一缓存行 |
| True sharing | 线程确实访问同一份数据，如同一个原子计数器 | 合法的原子访问仍可能竞争；不会自动发布其他普通对象 |
| False sharing | 不同数据因共行而有不必要的相干交互，含写/写或写/读 | 不是“不同变量非法干扰”，也不保证明显变慢 |
| Cache line / RFO | 目标机器的行粒度与获取写权限的请求机制 | 不是 C++ 保证的 64 字节，也不是每写一次就全核广播 |
| Lock-free | 通常算法模型下关注系统整体进展 | 不保证更快、公平、每线程无饥饿或墙钟时限 |
| Wait-free | 在规定模型中，每个操作经过有界步数完成 | 仍需线程能执行步骤，不是操作系统调度的墙钟承诺 |
| CAS / ABA | 条件更新；比较值 A→B→A 后可能漏掉中间状态变化 | 值相等不证明算法依赖的状态、节点关系未变 |

### 2.1 三种写法的差别

- **同一普通对象**：两个线程未同步地执行 `++counter`，或一个写、另一个读 `counter`，满足上述条件就是 data race，行为未定义。此处只作静态反例，不运行，也不以“最后总数刚好对”判正确。
- **同一原子对象**：两个线程对已构造且仍存活的 `std::atomic<unsigned>` 做 `fetch_add`，操作本身合法，这是 true sharing。`relaxed` 可用于仅需原子合计的计数；若计数被用来发布普通载荷，需另外建立同步合同。
- **不同普通槽位**：先创建槽位数组，再让线程 i 独占 `slots[i]`，期间不调整容器结构，主线程等所有线程 `join` 后才读值。这里没有工作线程之间的同位置冲突，不会仅因共行产生 data race；是否发生 false sharing 还要看机器、布局和访问。

同理，线程 A 写 `stats.hits`，线程 B 只读初始化后不再修改的 `stats.name`，两个不同位置也可能共行受影响；全只读没有这种写失效问题。相邻非零位域可能属于同一 memory location，`vector<bool>` 也不享有普通容器不同元素的该项保证，不能套用“不同下标必安全”。依据：[N4659 内存位置](https://timsong-cpp.github.io/cppwp/n4659/intro.memory)、[data race 条件](https://timsong-cpp.github.io/cppwp/n4659/intro.races)、[容器不同元素](https://timsong-cpp.github.io/cppwp/n4659/container.requirements.dataraces)。

### 2.2 完整非计时正例：普通槽位与共享原子计数

将以下整块保存为 `access_contract.cpp`。两个普通 `unsigned` 槽位在线程启动前初始化，每个只有唯一写者；两个线程另外更新同一原子计数。每线程 10000 次，合计 20000，不发生 unsigned 回绕。示例处理线程创建失败时对已启动线程的收尾；正常路径在成功 `join` 后核最终值。

```cpp
#include <array>
#include <atomic>
#include <exception>
#include <iostream>
#include <thread>

int main() {
    constexpr unsigned iterations = 10000;
    std::array<unsigned, 2> slots{{0, 0}};
    std::atomic<unsigned> shared{0};
    std::array<std::thread, 2> workers;
    try {
        for (std::size_t i = 0; i < workers.size(); ++i) {
            workers[i] = std::thread([&, i] {
                for (unsigned n = 0; n < iterations; ++n) {
                    ++slots[i]; // Only worker i accesses this ordinary slot.
                    shared.fetch_add(1, std::memory_order_relaxed);
                }
            });
        }
    } catch (const std::exception& e) {
        for (auto& worker : workers) if (worker.joinable()) worker.join();
        std::cerr << "thread creation failed: " << e.what() << '\n';
        return 2;
    }
    for (auto& worker : workers) worker.join();
    const unsigned total = shared.load(std::memory_order_relaxed);
    std::cout << "slots=" << slots[0] << ',' << slots[1]
              << " shared=" << total << '\n';
    if (slots[0] != iterations || slots[1] != iterations ||
        total != 2 * iterations) return 1;
    std::cout << "access-contract checks passed\n";
}
```

预期值为 `slots=10000,10000 shared=20000`；任一不符返回 1，线程创建失败返回 2。这两个访问合同来自所有权与同步推理，**不是从一次输出正确倒推“没有 race”**。[成功 join 的同步效果](https://timsong-cpp.github.io/cppwp/n4659/thread.thread.member) 使工作线程完成先于主线程之后的读值；工作线程互相之间仍靠独立槽位和原子操作各自满足合同。复现命令与本次运行范围见第 8 节。

## 3. 为什么合法的独立访问仍可能慢

### 3.1 缓存行交互：不是每次写都转移归属

下面是**可能的权限交替**，不是逐条 store 的固定总线时序；假设 A、B 位于会发生跨缓存相干交互的核，访问同一行内的不同槽位：

```text
A 写 slot[0]，缺少该行写权限 → 获取权限，可能使其他共享副本失效
A 已持有合适行状态，继续写   → 不必每次再发 RFO 或全核失效
B 随后写同一行的 slot[1]    → 可能需要获取权限、发生行传输/失效
A 再次需要写权限            → 可能重新竞争该行
若 B 只读另一个不变字段      → A 的写也可能使 B 为读该字段重新取行
```

MESI/MOESI 等协议以状态和共享副本决定交互；“每次写都全核失效”“每轮原子 RMW 都发 RFO”都过强。相同位置的原子 RMW 需要满足原子语义，但具体缓存事务、指令与代价取决于实现和已有状态。[Linux 的 false-sharing 说明](https://docs.kernel.org/kernel-hacking/false-sharing.html) 给出写/读例子，也强调用布局和工具定位后再权衡空间成本。

### 3.2 `volatile`、优化与原子化各自改变了什么

`volatile` 约束相应可观察访问，**不提供线程同步或原子性**；它不意味着绕过缓存直达 DRAM，也不保证每个 `++` 恰好对应一条内存 RMW 指令，更不是禁用全部优化。普通私有/独占计数循环，只要保持可观察结果，编译器可以累加到寄存器甚至化简循环；无需借助 data-race UB 才能优化。依据：[N4659 抽象机与可观察行为](https://timsong-cpp.github.io/cppwp/n4659/intro.execution)。

把普通槽位替换为 atomic RMW 会改变操作种类、编译约束与工作量。它可以成为一个单独的对照，不能靠“换原子以后更慢”排他地认证 false sharing，更不能保证任何机器上都复现同样倍数。普通和原子版本都可能受落核、频率、NUMA、访问强度和编译产物影响。旧实验没有这些观测，不能把“线程大多落在同一物理核心”当作已查明的原因。

### 3.3 布局：对齐起点不等于隔离每个元素

常见候选包括每线程计数数组、对象内由不同线程访问的热字段、队列头尾。它们只应先标为**性能调查项**，不能直接判正确性失败：

- `alignas(64) unsigned counters[2]` 只对齐数组起点，元素间距仍为 `sizeof(unsigned)`。
- 将 `alignas(64)` 加在 `Slot` 类型上，才让数组元素的步距包含该类型的尾部填充；还需核字段偏移与实际地址。
- 把两个字段拆到分别分配的对象，也不能保证分配器把它们放在不同行。
- 起点按 64 对齐、每槽大小为 64 时，8 字节热值加 56 字节填充可以在**64 字节行假设下**隔离相邻槽。旧文说这种布局依然必然互踩，结论相反。

下面保留 Stats 的教学目的，并补齐可编译的布局检查。保存为 `layout_contract.cpp`；它使用当前目标提供的 `uintptr_t` 查看地址，**64 是显式假设而非硬件探测结果**。静态断言有意拒绝不满足本例尺寸前提的目标。

```cpp
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <type_traits>

constexpr std::size_t assumed_line = 64;
struct alignas(assumed_line) Slot { unsigned value = 0; };
struct Stats {
    alignas(assumed_line) unsigned hits = 0;
    alignas(assumed_line) unsigned misses = 0;
};
static_assert(std::is_standard_layout<Slot>::value);
static_assert(std::is_standard_layout<Stats>::value);
static_assert(2 * sizeof(unsigned) <= assumed_line);
static_assert(alignof(Slot) >= assumed_line);
static_assert(sizeof(Slot) % assumed_line == 0);
static_assert(offsetof(Slot, value) == 0);
static_assert(alignof(Stats) >= assumed_line);
static_assert(offsetof(Stats, hits) == 0);
static_assert(offsetof(Stats, misses) >= assumed_line);
static_assert(offsetof(Stats, misses) % assumed_line == 0);
static_assert(sizeof(Stats) % assumed_line == 0);

int main() {
    alignas(assumed_line) unsigned base_only[2]{};
    Slot isolated[2]{};
    Stats stats{};
    const auto address = [](const void* p) {
        return reinterpret_cast<std::uintptr_t>(p);
    };
    const auto a = address(&base_only[0]), b = address(&base_only[1]);
    const auto c = address(&isolated[0].value), d = address(&isolated[1].value);
    const auto h = address(&stats.hits), m = address(&stats.misses);
    std::cout << "assumed_line=" << assumed_line
              << " sizeof(unsigned)=" << sizeof(unsigned) << '\n';
    std::cout << "Slot alignof=" << alignof(Slot) << " sizeof=" << sizeof(Slot)
              << " value offset=" << offsetof(Slot, value) << '\n';
    std::cout << "Stats alignof=" << alignof(Stats) << " sizeof=" << sizeof(Stats)
              << " hits offset=" << offsetof(Stats, hits)
              << " misses offset=" << offsetof(Stats, misses) << '\n';
    std::cout << "base_only addresses=" << a << ',' << b << " stride=" << b-a << '\n';
    std::cout << "isolated addresses=" << c << ',' << d << " stride=" << d-c << '\n';
    std::cout << "Stats addresses=" << h << ',' << m << " spacing=" << m-h << '\n';
    if (a % assumed_line || c % assumed_line || d % assumed_line ||
        h % assumed_line || m % assumed_line || b-a != sizeof(unsigned) ||
        d-c != sizeof(Slot) || a/assumed_line != b/assumed_line ||
        c/assumed_line == d/assumed_line || h/assumed_line == m/assumed_line)
        return 1;
    std::cout << "64-byte-assumption layout checks passed\n";
}
```

本次 x86_64-linux-gnu 下 `unsigned` 为 4 字节；普通数组步距 4，`Slot` 对齐/大小均为 64，`Stats` 大小 128、字段偏移 0/64。程序还打印并核了实际地址，地址本身每次可不同。它证明本次编译布局符合假设，不证明所有 CPU 的相干粒度为 64，也不测量性能。

C++17 的 [hardware interference size](https://timsong-cpp.github.io/cppwp/n4659/hardware.interference) 是实现定义的推荐间隔/同用尺寸。`hardware_destructive_interference_size` 用于减少并发对象相互干扰，`hardware_constructive_interference_size` 用于具有时间局部性的共同访问，不仅限于只读数据；二者都不是运行时缓存行探测或跨平台常数。

### 3.4 缓存参数应怎样使用

旧版的容量/延迟表保留作量级示意，**不是当前机器测量，也不是 x86/ARM 的统一规格**：

| 层级 | 旧示意容量 | 旧示意延迟 | 表中假设的行大小 |
| --- | ---: | ---: | --- |
| L1 | 32~64KB/核 | ~4 cycles | 64 字节行 |
| L2 | 0.5~2MB/核 | ~12 cycles | 64 字节行 |
| L3 | 8~64MB 共享 | ~40 cycles | 64 字节行 |

实际值需查目标型号和测量条件；缓存容量查询（旧入口 `wmic cpu get L2CacheSize,L3CacheSize`）不能据此证明行大小。相干交互也不能一律表述为“整行在所有核失效”。优化时先记录目标参数和字段地址，再比较布局相同、工作量相同的访问。

## 4. CAS 发布、ABA 与无锁进展

### 4.1 Treiber 风格 push：只展示发布片段

下列节选需要 `<atomic>`、`<utility>`，只解释私有节点准备到发布的 CAS 路径，**不是可直接使用的完整栈**。假设这里只并发 push，节点在所有访问结束前一直存活；没有 pop、析构清理或回收协议，本轮未把它作为完整程序运行。

```cpp
template <typename T>
class PushPublicationSketch {
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
    // 教学片段：无 pop、析构清理、退休/回收实现，不作为完整容器。
};
```

状态变化是：`n` 先在线程私有状态下构造；每次尝试将它连到观察到的头 `h`；CAS 成功把 `head_` 改为 `n`，可作为该 push 的线性化点。CAS 失败会把读到的当前头写回 `h`，下一轮重新设置 `n->next`；weak 还允许伪失败。单一 `release` 参数在失败路径等价于 `relaxed`，**失败读取不是 acquire**。[N4659 原子操作条款](https://timsong-cpp.github.io/cppwp/n4659/atomics.types.operations) 是这些规则的来源。

片段不解引用旧 `h`。将来若消费者读取节点载荷，需要匹配的发布/获取与存活保护；不能随手补一段 pop 就得到正确容器。`new`、`T` 的构造/移动可能分配、阻塞或抛异常，目标 `atomic<Node*>` 也不保证处处 lock-free。因此 CAS 循环或类名都不足以认证整个 `push` 的无锁进展，缺少的资源/异常合同也须单独设计。

### 4.2 ABA：值恢复与内存回收不是同一问题

只作状态推演，不运行下列错误算法。为排除悬垂和字段访问 race 的干扰，假设节点一直存活，节点关系的访问另外使用原子操作或适当同步；P 已合法保存 A 及旧后继 B，之后不再解引用 A。这不是给上面的普通 `next` 片段直接补 pop：

```text
初态 head=A，链为 A→B→C；P 保存 expected=A、desired=B，随后暂停
Q 摘下 A、摘下 B，再将仍存活的 A 以 next=C 放回；当前链为 A→C
P 只比较 head 的值，CAS(A→B) 可能成功
结果重新把已摘下的 B 作为头；“head 又是 A”并不证明旧后继仍有效
```

这个逻辑反例不需要释放或地址复用。释放后复用同址是另一常见来源，而且还引入悬垂/生命周期风险；不能像旧图那样暗示先释放后随意读取 `A.next`。

- **Tagged pointer**：比较指针和代次的整体，需证明计数回绕不会越过允许的观察窗口、表示合法、整组比较确实原子。不能假定所有平台都有足够的指针空闲位。
- **Hazard pointer**：先取得候选指针并发布保护，再重新验证共享指针仍匹配，验证成功后才解引用；摘下的节点先退休，在满足协议的安全扫描后回收。仅“登记一下”不足以完成协议。参考 [P2530R3 的保护/退休说明和 try_protect 顺序](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2023/p2530r3.pdf)；这是 2023 年提案资料，不是 C++17 已提供的 API。
- **Epoch 类回收**：依赖参与者进入/退出临界区、公布状态或静止点等具体协议；未越过安全回收边界不能释放，停顿参与者可能延迟回收；普通 EBR 的这一限制可见 [Brown 的原论文](https://arxiv.org/pdf/1712.01044) 对 epoch 推进的解释，具体变体可能另有容错策略。回收安全不自动排除上面“活节点重新插入”的逻辑 ABA，仍要审查算法是否允许这种状态变化。

### 4.3 进展性质、工具与 UE 选型

算法通常所说的 lock-free，是在明确执行模型和持续执行的前提下系统整体能完成操作；某线程仍可能反复失败或饥饿。wait-free 要求更强的逐操作步数界，也不是操作系统调度的墙钟承诺。C++17 [原子进展条款](https://timsong-cpp.github.io/cppwp/n4659/intro.progress) 区分强制与推荐措辞，不能把其中的 “should” 提升为所有平台无条件保证。用 `is_always_lock_free` / `is_lock_free()` 查看目标类型/对象能力，再审分配、载荷与回收；[非 lock-free 原子操作可能阻塞](https://timsong-cpp.github.io/cppwp/n4659/atomics.lockfree)。

TSan 是动态 data-race 检测工具；压力测试有助于找到执行中的错误，但无告警、未崩溃或计数正确都不能证明线性化、无 ABA、无饥饿或 lock-free。还需逐操作不变量、线性化点、生命周期与进展分析，必要时做模型/历史检查。[Clang 文档](https://clang.llvm.org/docs/ThreadSanitizer.html) 还说明未插桩代码等检测限制。

UE 场景应先选 FIFO/LIFO、生产者/消费者数量、所有权转移和销毁时机，再看 API：

- [TLockFreePointerListLIFO 公开入口](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Core/TLockFreePointerListLIFO) 与 [TQueue 公开入口](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Core/TQueue) 是所用版本的核对起点，LIFO 不能直接当 FIFO；`TQueue` 模式也要按生产消费合同选。
- 旧文的 UE5.8 源码定位 `Engine/Source/Runtime/Core/Public/Containers/LockFreeList.h`（旧行号 842/899 附近）及 `Engine/Source/Runtime/Core/Public/HAL/MallocBinned.h`（旧行号 109 附近）只保留为历史阅读入口。本次未访问对应安装/CL，**不认证**前者内部采用 epoch，或后者特定线程缓存/空闲列表机制。
- GameThread/RenderThread 双缓冲或原子发布仍需保证被读缓冲存活、在消费者结束前不重用；服务器 Zone/线程分片也需规定迁移与合并同步。现成容器值得优先评估，但不能因此跳过使用合同与完整操作的性能比较。

## 5. 历史 Benchmark：保存观察，限制归因

### 5.1 实现、输入和旧复现入口

[现存源码](../../../evidence/labs/false-sharing/src/false_sharing.cpp) 的四线程配置为每线程 2000 万次、3 轮，五组加局部对照：

- `aligned` / `packed`：独立对齐槽位与相邻 `volatile long long` 槽位。
- `shared_atomic`：多线程对单原子计数器做 relaxed RMW，属于真实共享。
- `atomic_packed` / `atomic_aligned`：每线程独占各自的原子槽位，改变布局。
- `run_padded`：局部累加后写回一次；源码内“不是局部、直接操作槽位”的旧注释与实现不符。

静态阅读能确认：`vector<T> slots(threads)` 先构造；lambda 按值捕获各自 `i`；线程仅操作 `slots[i].v`；容器不并发扩容，函数等待所有线程 join。**在合法输入、成功构造/启动且无算术溢出等前提下，这种独立槽位访问不因同行而发生 race。** 原源码“数据竞争 UB 允许优化”“volatile 每次内存 RMW”“原子必定竞争”等注释不是本页采用的结论，历史文件本轮保留不改；这也不代表整个旧程序对任意命令行输入/资源失败都经过质量验证。

旧 Windows 入口：`powershell -NoProfile -ExecutionPolicy Bypass -File evidence/labs/false-sharing/scripts/build_run.ps1`；参数例：`false_sharing.exe 10000000 8`。这是历史复现入口，**本轮未执行**；旧 runner 会覆盖四线程 results 文件，后续实验应先保留原始文件并将新输出存到独立位置，不能用新数据改写旧记录。

### 5.2 四线程历史结果

原记录环境：Windows x64（16 逻辑处理器），MSVC 14.44.35207，`cl /O2 /std:c++17 /EHsc`，2026-08-12；旧文还标记“虚拟化/受限调度”，本轮仅保留这个历史环境注记，未以拓扑/调度记录核实其具体影响。完整值见 [四线程 raw](../../../evidence/labs/false-sharing/results/false_sharing_win_x64_msvc.txt)。

| 版本 | run1 | run2 | run3 | 能支持的说明 |
| --- | ---: | ---: | ---: | --- |
| aligned（volatile，独立行） | 38.5 | 37.1 | 35.3 ms | 旧样本基线 |
| packed（volatile，同行） | 48.1 | 50.0 | 53.1 ms | 此样本约 1.2~1.5x，不认证落核原因 |
| shared_atomic（单计数器） | 1288.9 | 1284.0 | 1270.4 ms | 对 aligned 约 34x，但同时改变共享方式和原子性 |
| atomic_packed（同行原子槽） | 798.6 | 388.0 | 923.6 ms | 对 atomic_aligned 约 3.4~7.0x，限这组历史观察 |
| atomic_aligned（独立行原子槽） | 119.7 | 114.9 | 131.9 ms | 相同原子操作的布局对照 |
| 局部累加对照 | ~6.3 ms | ~6.3 ms | —— | 原摘要近似值，不是理论绝对下界 |

版本列沿用旧实验标签，其中“同行/独立行”不是本次实测地址结论。表中保留旧舍入值。raw 局部对照实际两轮为 aligned 6.63/6.27 ms、packed 6.32/6.37 ms；不是每轮都精确 6.3。raw 标题的“3 次取平均”是历史误标签，下面实际打印三轮独立值，未输出其算术平均；原文件不据此重写。

### 5.3 十六线程记录与因果边界

[十六线程 raw](../../../evidence/labs/false-sharing/results/false_sharing_16t_win_x64_msvc.txt) 为 16 线程 × 500 万次：aligned 32.09/30.58/32.58 ms，packed 38.48/39.01/38.71 ms，比值逐轮 **1.2、1.3、1.2x**；shared_atomic 1103.99/1106.31/1148.56 ms（原摘要 1103~1149ms）。局部对照两轮为 aligned 25.84/26.90 ms、packed 26.80/24.45 ms。

这份输出**没有 atomic_packed / atomic_aligned**，不能声称它们“同四线程结论”。其输出格式与现存源码不同，现有材料没有确认二者属于同一程序修订版的构建对应关系。它的“3次取平均”同样只是旧标签，不能补造缺失组或均值。

如何读这些数值：

1. 四线程原子槽布局对照呈现了明显时间差，是值得继续调查的历史样本；没有地址/PMU 记录，不能宣布已排除所有混杂因素，更不能推出原子必复现或跨平台固定收益。
2. `shared_atomic` 对 `volatile aligned` 的约 34x 同时改变操作与共享模式，不能当作单一 true-sharing 因果代价。后续应尽量在相同原子工作量下比较 shared/packed/aligned。
3. atomic_packed 三轮约 799/388/924 ms 的波动是观察；线程调度、行交互、频率等只是待验证解释。shared_atomic 约 1280 ms 较接近不证明它不受调度影响，也不证明某一指令或串行化原因。
4. 局部累加可减少频繁写槽并被优化，约 6ms 只是旧实现结果；不能当所有实现的绝对下界，局部写回本身也不能一概称“没有任何缓存行交互”。
5. 旧程序只打印耗时，没有核最终计数、线程拓扑、实际地址、PMU 或尾分位。三轮不能支撑 P99，不能据此认定 packed 差异“被低估”；本次撤回 10~30x 的复验收益门槛。

## 6. 实践选择：先访问正确，再优化

1. **先画所有权和生命周期**：谁写、谁读、何时交接、何时可销毁。不同普通槽位的唯一写者可以成立；同对象的冲突访问必须有适当同步。
2. **先减少共享频率，再权衡布局**：每线程缓冲、分片统计、批量合并可降低争用，但会增加内存、合并工作和可见性延迟。不能把后台合并线程直接读正在写的普通槽位。
3. **布局优化保持工作量可比**：同一种访问下比较 packed 与 padded，记录地址、步距和硬件。原子化是另一维改变，应单列，不作为快速确诊手段。
4. **不要到处加 64 字节 padding**：收益需抵过内存、缓存/TLB 占用与数据局部性成本；常数和 ABI 都要配合目标实现。
5. **锁、现成容器与手写 CAS 都需合同**：先满足 FIFO/LIFO、生产消费数、资源/回收与异常要求，再按真实负载比较吞吐和延迟；无锁并非游戏线程间传递的必选答案。
6. **分开功能、算法和性能验收**：断言检查输入/输出，TSan 尝试发现 race，算法审查检查线性化/ABA/进展，基准测吞吐或延迟分布。任何单项都不能替代其他项。

## 7. FAQ

**`volatile` 为什么不能修并发？**
它不建立线程间 happens-before，也不使普通 `++` 原子化。本例无 race 的依据是独占槽位和 join，不是 `volatile`；它仍可能访问缓存，并非每次到 DRAM。

**`alignas(64)` 一定消除 false sharing 吗？**
不一定。先看对齐施于数组起点还是元素类型、字段跨度和真实地址，再看目标行粒度与访问。第 3.3 节只验证显式 64 字节假设。

**没测到差异，是不是 false sharing 不存在？**
不能这样判断。可能未实际共行、未跨相关缓存访问、写强度低、循环被合法优化，或测量噪声占比大。先核访问合同、编译产物和布局；没有证据就不要指定“同物理核”为原因，也不预定绑核后一定更慢/更快。

**Release 才有正确的性能结论吗？**
O0/O2 都是特定配置的有效观察；它们工作量和生成代码可能不同，应按最终部署配置解释。优化等级不会把有 race 的源码变正确，也不能用 Debug/Release 名称代替真实编译选项。

**`fetch_add` 就是 `lock xadd`，不同 order 成本和语义都一样吗？**
不是。目标、宽度、是否使用返回旧值、优化会影响指令选择或库调用；不能对 MSVC/Clang/GCC 一概指定唯一指令。[GCC 原子操作文档](https://gcc.gnu.org/onlinedocs/gcc/_005f_005fatomic-Builtins.html) 说明目标相关展开与运行库回退。即使一次编译中几种 order 指令相同，[C++ 的发布/获取和顺序语义](https://timsong-cpp.github.io/cppwp/n4659/atomics.order) 仍不同。旧实验源码只用了 `relaxed`，没有对比三种 order；本轮也未做汇编或内存序性能实验。

**内存安全了就不会 ABA 吗？**
不一定。回收协议防止过早销毁；算法还要处理值恢复或活节点重插导致的旧状态失效。第 4.2 节特意使用仍存活节点，区分这两个问题。

**lock-free 一定比锁快、让每个线程及时完成吗？**
都不保证。CAS 重试、相干竞争、分配与回收成本都可能更高。进展性质和业务延迟、公平性是不同要求，必须分别选型和验证。

## 8. 验证范围与后续基准

### 8.1 本次确实运行的窄例

2026-10-05，Linux 6.18.44 x86_64，g++（Debian 14.2.0-19）14.2.0，target `x86_64-linux-gnu`。第 2.2、3.3 节完整代码逐字作为两个翻译单元运行；基础选项均为 `-std=c++17 -Wall -Wextra -Wpedantic -pthread`：

| 对象 | O0 / O2 | O0 / O2 加 UBSan | 实际断言和边界 |
| --- | --- | --- | --- |
| access_contract.cpp | 编译/运行退出 0 | 编译/运行退出 0，无诊断 | 两槽均 10000、原子合计 20000；不计时，不证明任意程序无 race |
| layout_contract.cpp | 编译/运行退出 0 | 编译/运行退出 0，无诊断 | alignof/sizeof/offsetof 及地址符合 64 假设；不认证硬件行大小 |

UBSan 选项为 `-fsanitize=undefined -fno-sanitize-recover=all`，不是 TSan。将两块代码分别保存到**仓库外实验目录**，可按以下命令重现本次四种配置，运行结果仍以自己的输出为准：

```bash
set -eu
for source in access_contract layout_contract; do
  for opt in O0 O2; do
    g++ -std=c++17 -Wall -Wextra -Wpedantic -pthread -$opt "$source.cpp" -o "$source-$opt"
    "./$source-$opt"
    g++ -std=c++17 -Wall -Wextra -Wpedantic -pthread -$opt -fsanitize=undefined -fno-sanitize-recover=all "$source.cpp" -o "$source-$opt-ubsan"
    "./$source-$opt-ubsan"
  done
done
```

本次未运行：同对象未同步反例、位域冲突、旧 MSVC 性能基准、TSan 压力测试、perf/VTune/PMU、物理亲和控制、UE 或完整无锁栈。未添加可运行 pop 或替换历史源/runner/原始结果。完整短例不能认证旧 benchmark 的最终值，因为它们不是同一程序。

### 8.2 后续诊断入口（计划，不是已完成验证）

原工具围栏保留为用途示例。以下 `lockfree_test.cpp` 是需自行准备和评审的测试入口，**仓库没有随文提供该文件**；`false_sharing` 也需另行构建且保护旧输出。perf 需要目标硬件/内核支持、采样权限及可用 PMU，不能仅由命令存在推定可执行：

```bash
# 参考：已有完整测试程序时做动态 race 检测；本轮未运行
clang++ -std=c++17 -fsanitize=thread -pthread -O1 -g lockfree_test.cpp -o lockfree_tsan
# 参考：目标机器支持时调查缓存行访问；本轮未运行
perf c2c record ./false_sharing 20000000 4
perf c2c report
```

下一轮性能实验应保存机器型号/拓扑、线程实际落核、地址、编译选项与代码 revision；先核最终值，再控制工作量、预热和计时范围，多轮保留原值。Windows `SetThreadAffinityMask` 或 Linux `taskset` 可作为控制变量的工具，但逻辑 CPU 不自动等于不同物理核，绑核也不保证收益方向。若目标是尾延迟，需设计足够样本的延迟采集；当前三轮整体耗时不能补算成可信 P95/P99。必要时结合 perf c2c 或 VTune 解释现象，不设 10~30x 等固定通过线。

### 8.3 审查清单与快速决策

| 层面 | 应检查什么 | 不足以通过的替代证据 |
| --- | --- | --- |
| 访问正确性 | 同 memory location 的冲突、同步和对象存活 | 仅对齐，或最终计数偶然相同 |
| 算法正确性 | 每个操作的不变量、线性化点、ABA 与回收协议 | 仅有 CAS / hazard / epoch 名称 |
| 进展 | 原子能力、分配、载荷、重试、回收的整体条件 | 类名叫 LockFree，或没有显式 mutex |
| 性能 | 工作量、布局、调度和硬件的受控对照 | 相邻热槽直接 FAIL，或原子化后更慢 |
| 测试 | 功能断言、动态检测、模型/历史检查各自覆盖 | TSan 无告警或压测未崩即全证明 |

```text
先查同位置冲突与生命周期
├─ 不满足访问合同 → 修同步、所有权和回收，再谈性能
└─ 访问合法
   ├─ 独立热数据可能共行 → 查真实布局，比较隔离/分片的成本
   ├─ 真实共享计数 → 保持原子语义，评估减少更新或批量合并
   ├─ 需要并发容器 → 先选顺序/生产消费合同，再核进展与回收
   └─ 没有性能证据 → 保留简单正确实现，测量后再优化
```

## 9. 来源范围与关联阅读

本次按 N4659 固定版条文镜像核对 memory location、races、container dataraces、join、execution、atomic operations/order、progress/lockfree、hardware interference；[官方 N4659 PDF](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2017/n4659.pdf) 标明 2017-03-21 草案身份。Linux、Clang、GCC 和 Epic 在线资料核对日期为 2026-10-05；它们不是旧实验构建环境。P2530R3 只用于危险指针协议背景，不冒充 C++17 API 或 UE 内部实现。

- [02-Atomic与C++内存模型](02-Atomic与C++内存模型.md)：发布/获取、原子对象与语言内存序。
- [01-C++对象生命周期与RAII](../C%2B%2B语言与对象模型/01-C%2B%2B对象生命周期与RAII.md)：所有权与存活背景。
- [实验 README](../../../evidence/labs/false-sharing/README.md)：历史输入、原始结果、解释和局限。
- [编程与计算机基础路线](../../../00_Index/学习路线/编程与计算机基础.md) 与 [C++并发与内存模型导航](../../../00-计算机与工程基础/04-C%2B%2B并发与内存模型/README.md)：本层入口；`01-线程同步与锁` 仍为规划主题。
- [引擎源码分析入口](../../../游戏知识/12-引擎源码分析/README.md)：UE 线程/任务系统的继续阅读，不作为本页已核私有实现证据。
- [网络与游戏服务端路线](../../../00_Index/学习路线/网络与游戏服务端.md) 与 [服务端并发与高性能](../../07-网络与游戏服务端/运行调度与过载保护/05-并发与高性能.md)：应用层分片、Tick 和缓存竞争；本文负责语言/硬件机制边界。
