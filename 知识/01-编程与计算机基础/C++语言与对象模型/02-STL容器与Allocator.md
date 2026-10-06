---
type: Mechanism
title: "02-STL 容器与 Allocator"
status: stable
verified: []
maturity: L2
updated: 2026-10-06
---
# 02-STL 容器与 Allocator

> 知识成熟度：L2

### 2026-08-20 原记录，非本次执行结果

以下六行保留当时的日期、来源与验证目标。它们不是已提供的 benchmark 结果；本次实际范围和运行记录见下文及 §9.3。

> 验证入口：运行文中容器复杂度、分配次数与缓存局部性基准，记录 P50/P95/P99。
> 知识基线：ISO C++20/23 容器需求与 AllocatorAwareContainer。
> 最后更新：2026-08-20。
> 来源：[cppreference containers](https://en.cppreference.com/w/cpp/container)、[C++ draft allocator.requirements](https://eel.is/c++draft/allocator.requirements)。
> 验证入口：使用 GCC/Clang `-std=c++20 -O2 -Wall -Wextra` 编译示例，并以 Sanitizer 检查生命周期。
> 验证与基准：对不同容器和分配器运行基准测试，比较吞吐、分配次数与 P95 延迟。

### 2026-10-06 现行范围与证据

本文以 C++23 工作草案 [N4950](https://github.com/cplusplus/draft/releases/tag/n4950)、固定提交 `4e4de1df8ee941255b653b61d0a62050b34cf8c9` 为文字合同依据；§9 的两个完整程序只使用 C++20 接口。`allocate_at_least`、`flat_map`、`mdspan` 的 C++23 说明不属于本次编译覆盖。主承诺是解释资源归属、具体操作后的句柄效力和失败状态，成熟度仍为 L2。

本次只执行 §9 的有限合法示例，不执行失效句柄访问、不等 allocator 的 swap、释放后使用、故意竞争或压力测试。§10、§18、§19 中的跨平台测量、吞吐/RSS/分位数与故障复现仍是后续目标，不能由本次退出码推出。

主要条款按主题就近链接到固定源：容器要求见 [containers.tex](https://github.com/cplusplus/draft/blob/4e4de1df8ee941255b653b61d0a62050b34cf8c9/source/containers.tex)，allocator 要求见 [lib-intro.tex](https://github.com/cplusplus/draft/blob/4e4de1df8ee941255b653b61d0a62050b34cf8c9/source/lib-intro.tex)，traits/PMR 见 [memory.tex](https://github.com/cplusplus/draft/blob/4e4de1df8ee941255b653b61d0a62050b34cf8c9/source/memory.tex)，范围寿命见 [ranges.tex](https://github.com/cplusplus/draft/blob/4e4de1df8ee941255b653b61d0a62050b34cf8c9/source/ranges.tex)。本次阅读的是相关原源段落，完整取回不等于通读全源。准备阶段 open-std 原 PDF 读取超时，未作为成功读取的依据；未审 libstdc++/libc++/MSVC 实现源码，也未测目标硬件或 UE 平台。

## 1. 总览

STL 容器把所有权、元素生命周期、迭代器和复杂度组合成可复用契约。先描述访问模式，再回答五个问题：存储从哪里来，哪些元素已构造，谁最终能释放，修改后哪些句柄仍有效，抛出异常后能依赖什么状态。最后才比较性能。

例如帧内 `pmr::vector`：`reserve` 为将来的元素准备容量，`emplace_back` 构造元素，外部资源负责供应存储；把容器移动到另一作用域并不延长资源寿命。若帧末先 `release`，即使 vector 已经 `clear`，它仍可能持有旧容量。正确顺序是先结束所有依赖分配的对象/容器，再释放资源存储。这条链贯穿 §5 的操作表、§6 的句柄表和 §16 的订单簿设计。

本文的通用 allocator 与移动/swap 讨论限于经典容器集合：`vector<T>`、`deque`、`list`、`forward_list`、`map/set/multimap/multiset` 和四种 `unordered_*`。下文通用的元素构造、元素地址、句柄及连续性讨论均排除 `vector<bool>` 特化。`basic_string` 虽然 allocator-aware，但 [container.requirements.pre] 明确豁免其通过 allocator `construct/destroy` 构造销毁元素的要求，不能直接套本篇的元素构造与句柄表。`array`、`span/mdspan`、其他 view 和 C++23 `flat_map` 也不套这组表。

对象寿命、对齐和 placement new 的底层规则见 [对象布局与内存分配](01-对象布局、虚函数与内存分配.md)；类型自身的复制/移动与 `noexcept` 见 [Copy-Move 与值语义](02-Copy-Move与值语义.md)。本篇只把这些条件应用到容器操作，不重新认证那两篇的实验。

## 2. 顺序容器

1. `vector<T>`（非 `bool` 特化）提供连续元素存储和摊销常数尾插；一次扩容仍可能搬迁全部元素，摊销复杂度不是单次延迟上界。`vector<bool>` 可能以位和代理引用表示元素，不能当作连续 `bool[]`。
2. vector 扩容的增长因子由实现选择，不能写死为翻倍或 1.5 倍；容量曲线要记录实际编译器和标准库版本。
3. vector 成功重分配使全部元素迭代器、引用、指针及旧 `end()` 失效；地址数值恰好相同也不能替代合同。具体插入/擦除见 §6。
4. `reserve(n)` 不改变 `size()`，只在 `n > capacity()` 时重分配并取得至少 n 的容量。合法下标仍须小于 `size()`，不是小于 `capacity()`。
5. `resize(n)` 改变元素数：缩小时析构尾部元素；增长时无值参数重载追加 default-inserted 元素，经过 `allocator_traits::construct`，要求相应的 DefaultInsertable/MoveInsertable 条件。默认 allocator 下可表现为值初始化，不能推广到任意自定义构造策略；带值重载追加该值的副本。
6. `shrink_to_fit` 是非强制请求；若发生重分配则按全部句柄失效处理，不把调用成功当作一定减小容量或回收 RSS。
7. deque 支持随机访问和两端单元素常数插入。分块缓冲区加索引是常见实现描述，不是标准规定的块大小或内部布局。
8. deque 元素不保证连续，不能用首元素地址加长度构造覆盖整个 deque 的 span。
9. list 在已知合法位置插入单元素为常数，插入不使旧元素句柄失效；查找这个位置仍可能线性。节点分散、指针追踪造成的局部性成本是常见性能风险，需与元素移动成本一起测量。
10. forward_list 提供单向遍历和以位置之后为主的插入/擦除接口，适合不需要反向遍历的简洁节点场景；稳定节点不意味着任意跨资源转移都合法。
11. `array<T,N>` 在对象内嵌固定连续元素，N 是类型的一部分；不使用 allocator，移动和 swap 涉及元素操作，不能套动态容器的常数转移结论。
12. basic_string 提供连续字符存储；小字符串优化（SSO）是可能的实现策略，其阈值、对象布局和实际分配行为不由这里保证。
13. 字符串尾部的空字符可通过规定的字符访问接口读取；它不是 `size()` 个逻辑字符中的一个。本次只保留这项基础事实，不扩展 string 专属失效或异常矩阵。
14. C++20 span 是非拥有视图，底层元素的所有者须继续存活且未使这些地址失效；复制 span 只复制视图，不复制元素。
15. C++23 mdspan 表达多维索引到数据句柄的映射与布局，本身不负责拥有数据；布局和 accessor 也不意味着任意多维数据都连续。本次未编译该接口。

vector 的容量和 resize 依据：[vector.capacity](https://github.com/cplusplus/draft/blob/4e4de1df8ee941255b653b61d0a62050b34cf8c9/source/containers.tex#L8853-L9029)。其余布局选择须保持“标准接口/复杂度”与“常见实现”两层含义。

## 3. 关联与无序容器

1. map 保证按 Compare 定义的键顺序遍历，查找/普通单元素插入为对数复杂度；平衡树、红黑树是常见实现，不是本文核实过的某库源码结构。
2. set 只存键，并按比较器的等价关系保持唯一；这里“等价”不一定是 `operator==`。
3. multimap/multiset 允许等价键重复，有序 multi 容器的插入、擦除保持等价元素之间的相对次序。
4. unordered_map 按哈希桶组织元素，查找平均常数、最坏线性；它不保证键的排序。
5. 恶劣碰撞会使查找退化，应按输入是否由攻击者控制选择哈希策略、容量和退化处置。标准复杂度承诺不等于抗攻击保证。
6. `unordered.reserve(n)` 以预期元素数为参数，相当于按当前最大负载因子请求足够桶；`rehash(n)` 的 n 是桶数下界，还须容纳现有元素。两者都不是严格内存预算，具体桶数不必等于 n。
7. rehash 使迭代器（含旧 end）失效，但保留现存元素的指针/引用；平均复杂度线性、最坏平方。不要把查找的平均常数复杂度套给 rehash。
8. node handle 可在类型兼容的节点容器间转移元素而不复制元素；非空 node 插入前还须满足目的 allocator 与 node allocator 相等。下面三阶段说明引用何时能访问。
9. `try_emplace` 遇到已有键时不构造新的 mapped 对象，但调用前的实参表达式仍会求值；`try_emplace(k, expensive())` 不是自动惰性执行 expensive。
10. `insert_or_assign` 适合“存在则赋值，不存在则插入”；赋值路径会调用 mapped 类型的赋值操作，其异常不能直接套单元素 insert 的无效果保证。
11. 异质查找：有序容器要求 Compare 的 `is_transparent`；无序容器要求 Hash 和 Pred 双方透明，并且对参与的键类型保持一致的哈希/等价语义。仅加一个 typedef 不会自动修正错误比较。
12. Compare 必须形成严格弱序，同一容器内相同键对的比较结果须保持一致；依赖随时变化的全局排序模式会破坏这一前提。
13. Pred 判为等价的键必须有相同哈希值；键在容器中时，Hash/Pred 对它的相关结果须稳定。
14. 不可原地修改影响有序性/哈希归属的键。需要改键时，可在满足前提的情况下 extract，使用 node 接口修改，再插入并处理重复键失败；mapped 值的普通修改与改键是不同操作。
15. C++23 flat_map 是分别存键和值的双底层容器适配器，适合将读多写少的小数据集列为候选；单元素插删线性、失效规则不同于传统 map，且不满足经典 allocator-aware 附加要求。默认底层是 vector，也允许满足约束的 deque，因此不能概括所有实例都连续。本次不编译或测量 flat_map。

### 3.1 node handle 的三阶段访问

以 `source.extract(key)` 后插入 `target` 为例，先核类型兼容，再在 node 非空时核 `node.get_allocator() == target.get_allocator()`。资源不等时不能借“零复制”绕过释放责任。

| 阶段 | 抽出前取得的 iterator / pointer / reference | 通过 node 接口取得的访问 |
| --- | --- | --- |
| 元素仍在 source | 正常依 source 操作合同使用 | 此时还没有 node |
| extract 后由 node 拥有 | 旧 iterator 失效；旧 pointer/reference 虽保留，但经它们访问元素是 UB | 可通过非空 node 的 `key()/mapped()/value()` 等相应接口访问、改键 |
| 成功插入 target | 旧 iterator 不会复活；抽出前保留的 pointer/reference 可再次用于该元素 | node 拥有期新取得的 pointer/reference 此时失效；从返回的 position 重新取得句柄 |

唯一键容器的无 hint `insert(node)` 若因重复键未插入，返回结果中的 `node` 仍拥有该元素；不要只看 `position` 就以为资源已转移。hint 重载失败时的 node 保留方式另按该重载合同。§9 只运行一个成功插入正例，不覆盖重复键、跨资源或所有重载。

依据：[associative.reqmts.general 的 node 插入及 extract 规则](https://github.com/cplusplus/draft/blob/4e4de1df8ee941255b653b61d0a62050b34cf8c9/source/containers.tex#L3298-L3418)、[extract 的引用访问限制](https://github.com/cplusplus/draft/blob/4e4de1df8ee941255b653b61d0a62050b34cf8c9/source/containers.tex#L3906-L3924)、[unord.req.general](https://github.com/cplusplus/draft/blob/4e4de1df8ee941255b653b61d0a62050b34cf8c9/source/containers.tex#L5760-L5869)、[flat.map.overview](https://github.com/cplusplus/draft/blob/4e4de1df8ee941255b653b61d0a62050b34cf8c9/source/containers.tex#L14612-L14717)。

## 4. 迭代器与范围

1. 输入迭代器只保证单遍读取，不能把保存的副本当作可任意重放的游标。
2. 前向迭代器增加多遍保证，可在范围和句柄仍有效时重复遍历。
3. 双向迭代器支持合法位置上的递减，不能递减 begin。
4. 随机访问迭代器支持常数时间的位置算术；算术仍受同一有效范围约束。
5. 连续迭代器在随机访问之上承诺元素地址对应连续存储；deque 的随机访问不蕴含此保证。
6. `begin/end` 定义半开区间，范围为空时两者相等；C++20 范围的 sentinel 不必与 iterator 同类型。
7. end 不指向元素，不能解引用；元素引用仍有效也不表示缓存的 end 仍有效。
8. 失效迭代器不能继续用于解引用、递增、比较或当作下一次操作的位置。擦除循环用合法返回值或按具体规则重新获取，而非检查旧地址是否“还能读”。
9. 范围算法减少 begin/end 来自不同对象的配对错误，但不替调用者保证范围的底层存储存活。
10. `borrowed_range` 表示所得迭代器的有效性不绑定到该 range 变量本身的寿命。span 是 borrowed range，仍可因所指数组销毁而悬垂；`vector<int>&` 满足该概念也不延长 vector 寿命。向范围算法传临时 vector 时，算法可以执行，但某些返回迭代器的结果类型变成 `ranges::dangling`，并非拒绝整个调用。
11. filter_view 的谓词及其引用状态必须仍有效；底层修改也可能使其游标/缓存失效，不能把惰性遍历当作快照。
12. transform_view 不保证缓存转换结果，重复解引用不能据此假定只执行一次转换；转换函数及捕获状态也要满足寿命合同。
13. view 通常提供轻量组合接口，但 view 不是“非拥有”的同义词：span/ref_view 借用底层对象，owning_view 则持有其 range。先判所有者和底层寿命，再谈视图类型。
14. libstdc++ 调试迭代器模式可检测某些跨容器比较、范围和所有权错误；它不能证明所有 raw pointer、PMR 资源寿命或失效访问都安全。单 TU 模式的实际运行见 §9.3。
15. 算法复杂度要区分比较次数和迭代器移动次数；例如二分查找在非随机访问范围上仍可能有线性步进成本，不能只数比较次数。

依据：[range.range](https://github.com/cplusplus/draft/blob/4e4de1df8ee941255b653b61d0a62050b34cf8c9/source/ranges.tex#L1251-L1330)、[range.dangling](https://github.com/cplusplus/draft/blob/4e4de1df8ee941255b653b61d0a62050b34cf8c9/source/ranges.tex#L2133-L2190)、[range.owning.view](https://github.com/cplusplus/draft/blob/4e4de1df8ee941255b653b61d0a62050b34cf8c9/source/ranges.tex#L4399-L4437)。

## 5. Allocator 模型

### 5.1 存储、元素与合法释放

allocator 封装存储分配模型，容器决定何时构造/销毁元素；它不自动拥有元素所引用的业务对象。`allocate(n)` 为 n 个 T 的数组提供存储，并涉及数组对象的创建，但不等于已经构造 n 个元素。C++20/23 的隐式对象创建规则也不能简化成“任何对象寿命都没开始”。vector 只有 `size()` 个可按元素接口访问的对象，容量中的空位不是任意索引许可。

容器通过 `allocator_traits<A>::construct(a,p,args...)` 构造元素，销毁时通过 `destroy` 结束其寿命，然后才能释放对应存储。traits 优先调用有效的 allocator 成员，否则分别回退到 `construct_at` / `destroy_at`；现代 allocator 不必自己提供旧式 construct/destroy。`rebind_alloc<U>` 让容器为内部节点等类型取得对应分配器，不能因此把元素生命周期和整个节点分配混为一件事。特别是 `vector<bool>` 的位值不通过 `allocator_traits::construct` 构造，不适用本段的通用元素构造说明，见 [vector.bool.pspc](https://github.com/cplusplus/draft/blob/4e4de1df8ee941255b653b61d0a62050b34cf8c9/source/containers.tex#L9299-L9305)。

合法释放要求的是 **allocator 相等，不是同一个 allocator 对象**。设 a 与 b 是两个实例且 `a == b`，a 分配的存储可由 b 释放。对于 `allocate(n)` 路径，传回对应指针、匹配的 n，并且只释放一次；PMR 的底层资源还须匹配原来的 bytes 和 alignment。§9 由两个指向同一资源的 polymorphic_allocator 对象完成分配、构造、销毁与交叉释放。C++23 `allocate_at_least(req)` 另允许 deallocate 的数量在 `req <= n <= ret.count` 之间，本次 C++20 示例不使用它。

`is_always_equal=true` 是同类型任意两个实例都相等的编译期承诺；false 只表示没有这项承诺，仍须看本次 `a == b`。运行时相等、trait 和两个对象地址分别回答“本次能交叉释放吗”“所有实例都保证吗”“是否同一对象”，不能替换着用。

分配须满足所请求的尺寸和对齐，包括 over-aligned 类型；失败依接口抛出相应异常，不能把任意 nullptr/业务错误码塞进标准 Allocator 或 memory_resource 的成功返回路径。`null_memory_resource::allocate` 明确抛 `bad_alloc`。deallocate 不抛，释放路径也不能依靠异常补救错误指针、错配尺寸或重复释放。

依据：[allocator.requirements.general](https://github.com/cplusplus/draft/blob/4e4de1df8ee941255b653b61d0a62050b34cf8c9/source/lib-intro.tex#L1934-L2660)、[allocator.traits](https://github.com/cplusplus/draft/blob/4e4de1df8ee941255b653b61d0a62050b34cf8c9/source/memory.tex#L1337-L1654)、[memory_resource 私有虚接口合同](https://github.com/cplusplus/draft/blob/4e4de1df8ee941255b653b61d0a62050b34cf8c9/source/memory.tex#L5526-L5588)。

### 5.2 构造、赋值和 swap 分别选择谁的 allocator

以下 X 是 §1 明列的经典 allocator-aware 容器，a 为目的地、b 为来源、m 为显式传入的 allocator。表中“线性/常数”按标准所计元素操作理解，不是实时耗时或分配次数承诺；类型还须满足具体重载的 CopyInsertable、MoveInsertable、赋值等要求。

| 操作 | allocator 选择/传播 | 元素与释放责任 |
| --- | --- | --- |
| 普通复制构造 `X a(b)` | 调用 `traits::select_on_container_copy_construction(b.get_allocator())`（SOCCC）；成员不存在时 traits 回退返回原 allocator | 在选出的 allocator 下复制构造元素，通常按元素数线性；不由 POCCA 控制 |
| 显式 allocator 复制构造 `X a(b,m)` | 使用 m | 在 m 下复制构造，线性；可明确指定副本所属资源 |
| 普通移动构造 `X a(std::move(b))` | 从来源 allocator 移动构造；allocator 这一移动不抛 | 取得来源原有元素，经典容器该构造为常数复杂度；不需要逐元素按 move-if-noexcept 选支。不能由此推断每种容器的整个构造都声明 noexcept |
| 显式 allocator 移动构造 `X a(std::move(b),m)` | 使用 m | `m == b.get_allocator()` 时常数，否则线性，须满足 MoveInsertable；不等时不能让 m 随意释放来源存储，可能构造新元素。不能一律承诺“偷 buffer”或 noexcept |
| 复制赋值 `a = b` | `propagate_on_container_copy_assignment`（POCCA）为 true 才复制 allocator；否则保留 a 的 allocator | 目的地可能复用/重新分配存储并赋值/构造元素，处理旧元素；若更换为不相等 allocator，旧存储仍须由兼容旧资源释放 |
| 移动赋值 `a = std::move(b)` | `propagate_on_container_move_assignment`（POCMA）为 true 才传播；否则保留 a 的 allocator | 不传播且不等时，需在目的资源下完成元素移动构造/赋值等工作，可能分配、线性且可能抛出；即使可以转移来源存储，也仍要处理目的地旧元素，不能把整次赋值一概称 O(1) |
| `a.swap(b)` | `propagate_on_container_swap`（POCS）为 true 则交换 allocator；false 则 allocator 不交换，并要求运行时相等 | 在合法前提下交换内容，不逐元素调用 move/copy/swap。POCS=false 且 allocator 不等是 UB，没有标准承诺的逐元素兜底 |

这些传播 trait 控制三个不同操作；SOCCC 是复制构造选择机制，不能从某一个 trait 推出其他操作的资源。合规 allocator 若声明对应传播为 true，其相应复制赋值、移动赋值或 swap 也须满足不抛等要求。

句柄另查一层：普通移动构造后，原元素引用/指针/迭代器仍指向移入目的地的元素，旧 end 不在这项元素句柄保证内。带 allocator 的移动构造或移动赋值不能无条件套这个结论，尤其不等且不传播时；必须核具体重载、traits 与实际相等关系。目的地旧元素句柄也不能当作来源句柄来推理。合法 swap 保持指向元素的句柄，但元素现在属于另一个容器，旧 end 可能失效，应重新获取。Compare/Hash/Pred 的 swap 还可能抛异常，见 §6。

移动后的源容器仍是有效但状态未指定的对象（另有明确保证者除外）；可析构或重新赋值，使用带前置条件的操作先核条件，不假定源一定 empty、容量归零或保留原地址。资源选择与元素值移动是两层合同，`std::move` 本身只作值类别转换。

依据：[container.reqmts](https://github.com/cplusplus/draft/blob/4e4de1df8ee941255b653b61d0a62050b34cf8c9/source/containers.tex#L578-L693)、[container.alloc.reqmts](https://github.com/cplusplus/draft/blob/4e4de1df8ee941255b653b61d0a62050b34cf8c9/source/containers.tex#L888-L1214)、[lib.types.movedfrom](https://github.com/cplusplus/draft/blob/4e4de1df8ee941255b653b61d0a62050b34cf8c9/source/lib-intro.tex#L3731-L3743)。

### 5.3 PMR：资源选择不等于资源所有权

`polymorphic_allocator<T>` 用一个 memory_resource 指针在运行时选择分配策略，容器静态类型可以保持不变。它的 POCCA、POCMA、POCS 均为 false，is_always_equal 为 false；两个 PMR allocator 仍可能相等。resource 相等依 `is_equal` 的相互释放兼容合同判断，不要求资源对象地址相同。§9 的 CountingResource 特意只与自身相等，这是本例策略，不是所有 resource 的规则。

| PMR 容器操作 | 所用资源 | 读者应据此作出的决定 |
| --- | --- | --- |
| 普通复制构造 | SOCCC 返回默认构造的 polymorphic_allocator，选取当时 `get_default_resource()` | `auto copy = source` 不保证副本留在 source 的 arena；需要指定资源时使用显式 allocator 重载 |
| 显式 allocator 复制构造 | 传入的资源 | 元素复制到选定资源；不能把普通复制的默认资源规则套过来 |
| 普通移动构造 | 保留来源所带的资源 | 目的容器对该外部资源的寿命依赖随之转移，不会变成默认资源 |
| 显式 allocator 移动构造 | 传入的资源 | 是否相等决定能否按常数路径处理；不等时元素操作的成本和异常仍存在 |
| 复制赋值 / 移动赋值 | 保留目的地资源 | 不等资源间的移动赋值合法，但可能逐元素处理；不能与不等资源 swap 混为一谈 |
| swap | 不交换资源，要求 allocator 相等 | 对不同且不相等资源的两个 PMR 容器调用 swap 是 UB |

PMR allocator **不拥有 resource**，保存指针不会延长资源寿命。`pmr::vector<pmr::string>` 构造内层 pmr::string 时可通过 uses-allocator 把资源传入；换成 `pmr::vector<std::string>`，只改变 vector 的分配策略，内层普通 string 不因此改走同一资源。即使字符串因 SSO 未分配，也不能用“没看到分配”推翻路由合同。

依据：[mem.poly.allocator](https://github.com/cplusplus/draft/blob/4e4de1df8ee941255b653b61d0a62050b34cf8c9/source/memory.tex#L5602-L6014)。本次程序运行普通复制/移动构造、复制/移动赋值、相等资源 swap 及内层 pmr::string 路由；显式 allocator 构造重载在这里纸面说明，未在程序中另测。

### 5.4 arena 的回收顺序与线程边界

声明和所有权设计应形成 `buffer / upstream → resource → 依赖分配的对象与容器` 的长寿依赖。退出时反向处理：先让所有依赖者完成最后访问并析构，再 `release()` 或析构 resource，最后才结束外部 buffer/upstream 的寿命。容器持有 allocator 的值，不代表它引用的外部状态也被拥有。

`vector.clear()` 结束元素寿命却保留 capacity，所以“已清空”不是可以释放 arena 的证明。即使 resource 对象仍活着，过早 release 也会归还上游块或重置 buffer 供后续覆盖，旧容量不能继续依赖。最直观的帧设计是在内层作用域声明容器，退出该作用域后再 release；若有异步任务、外部 span 或索引引用，要先完成它们或把数据转移到更长寿命的所有者。

monotonic resource 的单次 deallocate 无效果，但实参仍须满足合法释放合同；`release` 归还上游存储并重置分配状态，**不调用 T 的析构函数**。需要析构的对象必须先 destroy。初始固定 buffer 用尽时默认可向 upstream 继续分配，§9.1 的 4096 字节数组不是硬上限。显式使用 `null_memory_resource()` 作为 upstream 才使该资源链的额外请求抛 bad_alloc；这仍约束不了元素成员绕过它的独立分配。

pool resource 适合重复尺寸类别的分配/回收；同步策略须另行设计。`unsynchronized_pool_resource` 不允许多个线程同时访问，串行跨线程或外部同步下使用不等于必然竞争。`synchronized_pool_resource` 允许并发资源接口调用，不规定某一种锁，也不自动保护共享 vector 的 size、容量、迭代器或业务对象字段；对同一容器做并发结构修改仍须同步。不同元素并发还要核对应容器规则，`vector<bool>` 的位代理是特别例外。

依据：[mem.res.pool](https://github.com/cplusplus/draft/blob/4e4de1df8ee941255b653b61d0a62050b34cf8c9/source/memory.tex#L6015-L6328)、[mem.res.monotonic.buffer](https://github.com/cplusplus/draft/blob/4e4de1df8ee941255b653b61d0a62050b34cf8c9/source/memory.tex#L6329-L6527)。

## 6. 失效与异常

### 6.1 先确定操作，再判断句柄

下表描述成功完成的指定操作。元素 iterator、reference/pointer 和尾后 end 分开；“保留”只保留本来合法且未被删除的对象，不延长容器或资源寿命。范围必须有效，pop/单位置 erase 须满足非空/可解引用等前提。vector 指普通非 bool 特化，ordered 指传统 map/set/multi，unordered 指四个经典无序容器。

| 容器与操作 | 元素 iterator | 元素 reference / pointer | 旧 end |
| --- | --- | --- | --- |
| vector 成功重分配：reserve/插入/实际缩容 | 全失效 | 全失效 | 失效 |
| vector reserve 未重分配 | 保留 | 保留 | 保留 |
| vector 无重分配的尾插 | 原有元素保留 | 原有元素保留 | 失效 |
| vector 无重分配的中间插入 | 插入点之前保留；点及以后失效 | 同左 | 失效 |
| vector 擦除非空范围 | 擦除点及以后失效 | 擦除点及以后不可据旧元素身份继续使用 | 失效 |
| deque 两端插入 | 全失效 | 既有元素保留 | 失效 |
| deque 中间插入 | 全失效 | 全失效 | 失效 |
| deque 擦除包含最后元素的范围 | 仅被擦元素失效 | 仅被擦元素失效 | 失效 |
| deque 只擦首部、不含最后元素 | 仅被擦元素失效 | 仅被擦元素失效 | 保留 |
| deque 擦除既不含首也不含尾的非空范围 | 全失效 | 全失效 | 失效 |
| list / ordered 插入 | 保留 | 保留 | 保留 |
| list / ordered 擦除 | 仅被擦元素失效 | 仅被擦元素失效 | 保留 |
| unordered 插入且 `(N+n) <= z*B` | 保留 | 保留 | 保留 |
| unordered 插入不能满足上述保证 / 发生 rehash | 不能保留旧 iterator；rehash 使其失效 | 原有元素保留 | 不再沿用 |
| unordered erase | 仅被擦元素失效 | 仅被擦元素失效 | 保留 |

unordered 行中 N 是插入前元素数，n 是实际插入数，B 是原桶数，z 是最大负载因子。条件不成立不表示某次必定重排，但调用者不能据此索取不失效保证。`reserve` 按 rehash 合同处理，不能因为仍是相同 key/value 就复用旧 iterator。

deque 的 `pop_front()` / `pop_back()` 都属于擦除；单元素 deque 的 pop_front 同时擦掉最后元素，因此旧 end 失效。vector 擦除后有些存储地址可能仍存在，但位置上的值/对象关联已发生变化，不把物理地址可读当作原引用合同；可用 erase 返回的合法下一位置继续循环。

### 6.2 失败后能依赖什么

“无效果”是对应容器操作的状态保证，不撤销用户构造函数、Hash/Compare 所做的外部计数、日志或 IO。强异常保证必须指明操作和前提；元素移动不抛有利于实现该保证，但 `std::move_if_noexcept` 是解释常见选择的工具，不是标准强制每个容器实现都调用的函数。

| 指定操作 | 异常保证与关键前提 |
| --- | --- |
| vector `reserve` | 除非异常来自非 CopyInsertable 类型的移动构造，否则无效果；因此本例 `int` + 分配抛 bad_alloc 可核 size/capacity/值保持。上述 throwing-move 例外不能宣称强保证 |
| vector 尾部单元素 insert/emplace/push | 若 T 为 CopyInsertable 或 `is_nothrow_move_constructible_v<T>` 为 true，抛出时无效果。非 CopyInsertable 的 move 抛出可导致效果未指定 |
| vector 中间/范围插入 | 对非元素 copy/move/赋值及非 InputIterator 操作导致的异常有无效果保证；不能把上一行的尾插条件无差别搬到中间插入 |
| vector erase | T 的赋值/移动赋值可抛；不承诺一般回滚至旧序列 |
| deque 两端单元素插入 | 抛出时无效果；中间/多元素插入另按元素操作与具体条款，不由这一行保证 |
| deque erase | T 的赋值可抛；无此类异常才有不抛保证，不概括为全部 erase 都自动强保证 |
| list 单元素插入 / erase | 插入抛出时无效果；erase 不抛。析构/allocator 自身仍须遵守所需合同 |
| ordered 单元素 insert/emplace | 内部操作抛出时插入无效果；按键 erase 可能由 Compare 抛出 |
| unordered 单元素 insert/emplace | 除 Hash 抛出以外的异常，插入无效果；Hash 异常不能套此保证 |
| unordered rehash / 按键 erase | rehash 的无效果保证排除 Hash/Pred 异常；按键 erase 也可能由 Hash/Pred 抛出 |
| 合法 ordered / unordered swap | 分别可能由 Compare 的 swap、Hash/Pred 的 swap 抛出；资源前提合法不等于所有用户函数都不抛 |

分配后若元素构造失败，清理必须只销毁已成功构造的元素并释放相应存储，不能重复 destroy。资源释放和元素析构应设计成不让异常逃逸；默认析构异常规格还取决于基类/成员，不能说语言强制所有析构都 noexcept。细节见 [C++ 对象生命周期与 RAII](01-C++对象生命周期与RAII.md)；CopyInsertable 与 `noexcept` 的类型条件见 [Copy-Move 与值语义](02-Copy-Move与值语义.md)。

依据：[vector.capacity/modifiers](https://github.com/cplusplus/draft/blob/4e4de1df8ee941255b653b61d0a62050b34cf8c9/source/containers.tex#L8853-L9150)、[deque.modifiers](https://github.com/cplusplus/draft/blob/4e4de1df8ee941255b653b61d0a62050b34cf8c9/source/containers.tex#L6789-L6884)、[list.modifiers](https://github.com/cplusplus/draft/blob/4e4de1df8ee941255b653b61d0a62050b34cf8c9/source/containers.tex#L8160-L8230)、[associative.reqmts.except](https://github.com/cplusplus/draft/blob/4e4de1df8ee941255b653b61d0a62050b34cf8c9/source/containers.tex#L3987-L4004)、[unord.req.except](https://github.com/cplusplus/draft/blob/4e4de1df8ee941255b653b61d0a62050b34cf8c9/source/containers.tex#L5891-L5917)。

## 7. 性能设计

1. 连续存储通常有利于顺序读取和预取，这是候选设计依据；实际收益随元素尺寸、访问模式和硬件变化，须测目标工作负载。
2. 节点容器可减少插入时的元素搬迁，却引入节点开销和指针追踪；稳定句柄收益应与分配、缓存成本一起比较。
3. 合理 reserve 可避免容量范围内后续插入的重分配；它本身可能分配，元素成员也可能另外分配，不能称整个热路径“零分配”。
4. 已知批量大小时评估批量插入或一次预留，避免每次 push 前都 reserve 一个增量而破坏原本的摊销行为。
5. SSO、小对象优化和内联阈值须按实际库及输入测量，不能以类型名称推断分配次数。
6. 内存池的测量应包括峰值存储、碎片、回收延迟及上游保留内存；资源接口计数不等于 malloc 次数或 RSS。
7. 调试迭代器可能改变性能和 ABI；libstdc++ debug 模式不得与不兼容模式的容器对象跨边界混用，不能拿 debug 运行耗时当生产性能。
8. PMR 将运行时分配策略从容器静态类型中解耦，但 copy/assignment 的资源路线仍按 §5；比较策略前先保证各方案执行同一业务操作。
9. unsynchronized_pool_resource 适合无并发资源访问或由调用者串行化的场景；线程本地资源也是一种安排，不要求它终身只能在最初线程使用。
10. synchronized_pool_resource 只承诺资源接口可并发访问，锁方式/争用成本依实现；共享容器的数据竞争必须另行解决。

## 8. 工程检查清单

1. 记录元素数量分布、读写比例、存活时长和突发峰值，据此选择可容忍的搬迁与回收时机。
2. 记录哪些索引/外部系统持有稳定引用、指针、iterator 或 end；为每个结构修改写出更新规则。
3. 单独估计排序、Hash/Compare 和元素 copy/move 成本，避免只比较大 O 而忽略昂贵回调。
4. 画清线程访问边界：资源接口、容器结构、不同元素和元素引用的外部对象各由谁同步。
5. 写明分配失败、元素异常和业务失败如何传播；只有具体操作获得无效果保证时才允许依赖自动回滚。
6. 区分资源请求次数、上游请求、实际字节和进程 RSS；内存上限还要计入桶/节点、对齐、元素内部分配和暂时并存的新旧存储。
7. 用静态分析辅助寻找悬空句柄，再按 §6 逐个核操作路径；工具没有警告不等于合同成立。
8. 用 ASan/UBSan 检查所运行合法路径的越界、释放后访问等可检测错误；本次不执行故意 UB，且无报告不证明所有失效均可检出。
9. 用 §19 的 benchmark 比较满足相同正确性需求的候选，而不是把本例断言运行当性能测量。
10. 在目标平台验证 allocator 的尺寸和对齐支持，至少覆盖业务需要的 over-aligned 类型；本次 `alignas(64)` 只代表 §9 的宿主路径。

## 9. 最小验证示例

### 9.1 保留的原最小例

下面围栏保留原字节与示例身份。它只检查所写的整数访问路径：buffer、resource、vector 按声明逆序销毁，`reserve(128)` 后加入 100 个元素再读第 42 个。默认 upstream 仍可 fallback，不能从代码推出无上游分配、固定容量精确值、传播规则、耗尽、并发或性能结论。

```cpp
#include <vector>
#include <memory_resource>
#include <cassert>
int main() {
  std::byte buf[4096];
  std::pmr::monotonic_buffer_resource r(buf, sizeof buf);
  std::pmr::vector<int> v{&r};
  v.reserve(128);
  for (int i=0;i<100;++i) v.push_back(i);
  assert(v[42]==42);
}
```

### 9.2 C++20 有限合同例：container_allocator_contracts.cpp

这是一个独立翻译单元，三个函数分别检查合法句柄、PMR 资源路由与指定分配失败、资源释放顺序。所有必要动作在 assert 外；定义 NDEBUG 会被编译期拒绝，O2 本身不关闭断言。先写定合同再运行，不能靠“不崩溃”判断失效句柄是否合法。

CountingResource 只统计它自己接口的成功分配/释放和注入请求，特意只与同一资源对象相等。它不是生产 allocator，不追踪每个地址的完整账本，也不能独立证明错误尺寸/对齐从未发生。capacity/bucket_count 的加一操作先检查上界；若此前提不满足，本实验不能完成，应报告失败而不是溢出或删除检查。

```cpp
#ifdef NDEBUG
#error "This teaching example requires assertions enabled"
#endif

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <deque>
#include <list>
#include <map>
#include <memory>
#include <memory_resource>
#include <new>
#include <ranges>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>
#include <unordered_map>

// 单线程教学观测器；不为任意第三方资源证明安全，也不是生产分配器。
class CountingResource final : public std::pmr::memory_resource {
    std::pmr::memory_resource* upstream_ = std::pmr::new_delete_resource();
    void* do_allocate(std::size_t bytes, std::size_t alignment) override {
        ++attempts;
        if (reject) throw std::bad_alloc();
        void* p = upstream_->allocate(bytes, alignment);
        ++allocated;
        ++live;
        return p;
    }
    void do_deallocate(void* p, std::size_t bytes,
                       std::size_t alignment) override {
        assert(live != 0);
        upstream_->deallocate(p, bytes, alignment);
        ++freed;
        --live;
    }
    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override {
        return this == &other;
    }
public:
    std::size_t attempts = 0, allocated = 0, freed = 0, live = 0;
    bool reject = false;
};

void valid_handles() {
    std::vector<int> v{1, 2};
    v.reserve(4);
    int* first = &v[0];
    v.push_back(3); // capacity >= 4：既有元素仍有效，旧 end 不使用
    assert(*first == 1);
    auto next = v.erase(v.begin() + 1);
    assert(*first == 1 && next != v.end() && *next == 3);
    assert(v.capacity() < v.max_size());
    const auto requested = v.capacity() + 1;
    v.reserve(requested); // 此后不读、不比较、不解引用旧 first/next
    assert(v.size() == 2 && v.capacity() >= requested);
    assert(v[0] == 1 && v[1] == 3);

    std::deque<int> d{1, 2, 3};
    int& middle = d[1];
    d.push_front(0);
    d.push_back(4); // 两端插入保留引用；不保存或复用旧 iterator/end
    assert(middle == 2);

    std::list<int> l{1, 2};
    auto stable = l.begin();
    l.push_front(0);
    l.erase(--l.end());
    assert(*stable == 1);

    std::unordered_map<int, int> u{{7, 70}};
    int* value = &u.find(7)->second;
    assert(u.bucket_count() < u.max_bucket_count());
    u.rehash(u.bucket_count() + 1);
    assert(*value == 70); // 引用/指针保留；没有复用旧迭代器

    std::map<int, int> source{{7, 70}}, target;
    auto node = source.extract(7);
    assert(node.get_allocator() == target.get_allocator());
    node.key() = 8;
    auto inserted = target.insert(std::move(node));
    assert(inserted.inserted && inserted.position->first == 8);
    assert(inserted.position->second == 70 && source.empty());

    static_assert(std::ranges::borrowed_range<std::span<int>>);
    static_assert(!std::ranges::borrowed_range<std::vector<int>>);
    static_assert(std::ranges::borrowed_range<std::vector<int>&>);
    static_assert(std::is_same_v<
        decltype(std::ranges::find(std::vector<int>{1}, 1)),
        std::ranges::dangling>);
}

void resource_routing_and_failure() {
    using A = std::pmr::polymorphic_allocator<int>;
    using Traits = std::allocator_traits<A>;
    static_assert(!Traits::propagate_on_container_copy_assignment::value);
    static_assert(!Traits::propagate_on_container_move_assignment::value);
    static_assert(!Traits::propagate_on_container_swap::value);
    static_assert(!Traits::is_always_equal::value);
    CountingResource r1, r2;
    {
        A a{&r1}, b{&r1}; // 两个 allocator 对象，相等，可交叉释放
        assert(&a != &b && a == b);
        int* p = a.allocate(1);
        Traits::construct(a, p, 42);
        assert(*p == 42);
        Traits::destroy(b, p);
        b.deallocate(p, 1);

        std::pmr::vector<int> original({1, 2, 3}, &r1);
        auto* default_resource = std::pmr::get_default_resource();
        std::pmr::vector<int> copied(original);
        assert(copied.get_allocator().resource() == default_resource);
        std::pmr::vector<int> moved(std::move(original));
        assert(moved.get_allocator().resource() == &r1);
        assert(moved.size() == 3 && moved[2] == 3);

        std::pmr::vector<int> destination({9}, &r2);
        destination = copied;
        assert(destination.get_allocator().resource() == &r2);
        destination = std::move(moved); // 资源不等且不传播，目的地仍属 r2
        assert(destination.get_allocator().resource() == &r2);
        assert(destination.size() == 3 && destination[0] == 1);
        assert(destination[1] == 2 && destination[2] == 3);
        // 不对两个 moved-from vector 的 size/capacity/地址作假定。

        std::pmr::vector<int> peer({8}, &r2);
        assert(destination.get_allocator() == peer.get_allocator());
        int* old_element = &destination[0];
        destination.swap(peer); // 满足相等前提；旧元素属于 peer
        assert(*old_element == 1 && peer[0] == 1 && destination[0] == 8);
        assert(A{&r1} != A{&r2});
        // 不执行不相等资源的 swap，也不把潜在 UB 当作“预期失败测试”。

        std::pmr::vector<std::pmr::string> words(&r1);
        words.emplace_back("uses-allocator");
        assert(words[0].get_allocator().resource() == &r1);
        // 这里只检资源路由，不以 SSO 或某个确定分配次数为合同。

        const auto old_size = peer.size(), old_capacity = peer.capacity();
        const auto old_live = r2.live;
        assert(old_capacity < peer.max_size());
        r2.reject = true;
        bool failed = false;
        try { peer.reserve(old_capacity + 1); }
        catch (const std::bad_alloc&) { failed = true; }
        r2.reject = false;
        assert(failed && peer.size() == old_size);
        assert(peer.capacity() == old_capacity);
        assert(peer[0] == 1 && peer[1] == 2 && peer[2] == 3);
        assert(r2.live == old_live);
    }
    assert(r1.live == 0 && r2.live == 0);
    assert(r1.allocated == r1.freed && r2.allocated == r2.freed);
}

struct alignas(64) Tracked {
    int* destroyed;
    explicit Tracked(int& n) noexcept : destroyed(&n) {}
    ~Tracked() noexcept { ++*destroyed; }
};

void arena_lifetime_and_limit() {
    CountingResource upstream;
    int destroyed = 0;
    {
        std::pmr::monotonic_buffer_resource arena(&upstream);
        void* raw = arena.allocate(sizeof(Tracked), alignof(Tracked));
        void* aligned = raw;
        std::size_t available = sizeof(Tracked);
        void* alignment_result = std::align(alignof(Tracked), sizeof(Tracked),
                                            aligned, available);
        assert(alignment_result == raw);
        auto* object = ::new (raw) Tracked(destroyed);
        const auto frees_before = upstream.freed;
        std::destroy_at(object); // 明确先结束对象寿命
        arena.deallocate(raw, sizeof(Tracked), alignof(Tracked));
        assert(destroyed == 1 && upstream.freed == frees_before);
        assert(upstream.live != 0); // monotonic deallocate 没有回收上游块
        arena.release(); // 已没有活对象或保留这些分配的容器
        assert(upstream.live == 0);
    }
    assert(upstream.live == 0 && upstream.allocated == upstream.freed);

    alignas(64) std::byte buffer[512];
    std::pmr::monotonic_buffer_resource bounded(
        buffer, sizeof buffer, std::pmr::null_memory_resource());
    void* p = bounded.allocate(64, 64);
    bounded.deallocate(p, 64, 64);
    bool exhausted = false;
    try { (void)bounded.allocate(sizeof buffer + 1, 1); }
    catch (const std::bad_alloc&) { exhausted = true; }
    assert(exhausted);
    bounded.release(); // buffer 此时仍活着；后面不再使用 p
}

int main() {
    valid_handles();
    resource_routing_and_failure();
    arena_lifetime_and_limit();
}
```

### 9.3 本次实际运行与边界

2026-10-06 UTC 从本节两个 cpp 围栏逐字提取后执行。环境：Linux x86_64，g++（Debian 14.2.0-19）14.2.0，目标 `x86_64-linux-gnu`；实际头文件宏为 `__cplusplus=202002L`、`_GLIBCXX_RELEASE=14`、`__GLIBCXX__=20250315`。以下结果只属于该环境与该源码。

| 源码 / 模式 | 编译退出码 | 运行退出码与实际结果 |
| --- | --- | --- |
| 原最小例 C++20 O2 | 0 | 0；原有值断言满足，仅复跑该路径 |
| 新合同例 C++20 O0 | 0 | 0；有限断言满足 |
| 新合同例 C++20 O2 | 0 | 0；有限断言满足 |
| 新合同例 O1 ASan+UBSan，默认运行 | 0 | **1，未通过**；stderr 报 LeakSanitizer fatal 并给出不支持 ptrace 的提示；未进一步确认底层原因，原失败记录保留 |
| 同一 ASan+UBSan 二进制，`ASAN_OPTIONS=detect_leaks=0` | 沿用上一行构建 | 0，无 address/undefined 报告；同一二进制关闭 leak 检测，address/undefined 保持启用，LSan 未验证 |
| 新合同例 libstdc++ 单 TU debug，O0 | 0 | 0；本例合法迭代器路径完成，未混用不同 debug ABI 对象 |

所有 exit0 运行的 stdout/stderr 为空；这是无输出的断言程序，不是性能报告。原例源码（不含围栏）272 bytes，SHA-256 `b5cdb75bee94c12d616cdf179af8098921a76554c6576f189c934d442d4f0b05`；新例 7905 bytes，SHA-256 `85e841a4c3a84e8eed83e226e38cd75ca8aa7e661a0474688987e4b27dcda465`。逐次命令、原始输出、退出码和源码 hash 随本次作者验证记录保存；未把首轮 sanitizer 失败覆盖成成功。

提取文件名分别为 `original_example.cpp` 和 `container_allocator_contracts.cpp`，在已有 build 目录下实际执行的编译/运行命令如下（断言启用，不传 NDEBUG）：

```bash
g++ -std=c++20 -g -Wall -Wextra -Werror -pedantic -O2 original_example.cpp -o build/original-O2
./build/original-O2
g++ -std=c++20 -g -Wall -Wextra -Werror -pedantic -O0 container_allocator_contracts.cpp -o build/contracts-O0
./build/contracts-O0
g++ -std=c++20 -g -Wall -Wextra -Werror -pedantic -O2 container_allocator_contracts.cpp -o build/contracts-O2
./build/contracts-O2
g++ -std=c++20 -g -Wall -Wextra -Werror -pedantic -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all container_allocator_contracts.cpp -o build/contracts-sanitized
./build/contracts-sanitized
env ASAN_OPTIONS=detect_leaks=0 ./build/contracts-sanitized
g++ -std=c++20 -g -Wall -Wextra -Werror -pedantic -O0 -D_GLIBCXX_DEBUG container_allocator_contracts.cpp -o build/contracts-debug
./build/contracts-debug
```

该有限程序实际覆盖：vector 无重分配尾插/擦除返回位置与重分配后新访问、deque 两端插入的保留引用、list 非被删节点、unordered rehash 后保留指针、一次合法 node 转移；两个相等 allocator 实例交叉释放、PMR 普通 copy/move 与不传播赋值、合法 swap、内层 pmr::string 路由；int vector.reserve 注入 bad_alloc 后 size/capacity/元素/资源存活计数不变；alignas(64) 对象先析构再 release，以及 null upstream 的一次明确超限失败。

该例不测试显式 allocator 移动构造、POC=true 的自定义 allocator、throwing move/copy 元素、Hash/Compare 异常、全部 node 重载或重复键失败；对应标准合同来自文字条款，不能标为本机实测。也未运行 MSVC/libc++/ARM、C++23 flat_map/mdspan、UE、TSan、跨线程竞争、容量增长基准或任何吞吐/RSS/P50/P95/P99 测量。资源接口的平衡断言不代表标准强制某个确定分配次数。

## 10. 延伸验证

以下保留后续实验方向，除明确指向 §9 的有限路径外均未在本次执行。

1. 记录 GCC 与 MSVC、各自标准库版本的容量增长曲线；结果只属于所测实现，不能反推标准增长因子。
2. 在相同数据和访问模式下比较 vector、deque、list 遍历吞吐，同时记录元素大小和节点分配策略。
3. 比较默认 new 与 PMR 时分别统计容器资源接口、上游和真正分配后端，避免把多层计数相加或混称 malloc 次数。
4. 原“在 ASan 下执行故意失效迭代器”保留为后续隔离故障教学目标；本次仅纸面分析哪些操作非法，实际程序只走合法正向路径，不能以负例静默通过证明安全。
5. §9 已包含 alignas(64) 分配的有限路径；其他对齐、allocator/fancy pointer 与目标平台组合仍待验证。
6. 多线程资源边界要单独设计有同步的实验及必要的竞争检测；本次没有运行线程竞争负例。
7. 将来若执行 benchmark，应将真实数据和编译命令按项目证据流程保存到 evidence；本次仅保存有限示例证据，没有新建仓内 benchmark/runner/CI。
8. 升级标准或标准库后重查接口版本、traits 和具体操作前提，必要时重新提取正文源码验证；不能沿用旧通过覆盖新代码。

## 11. 结论

容器选择是契约设计问题，不是 API 偏好。先锁定生命周期、失效、复杂度和线程边界，再决定容器与 allocator。相等资源解释谁能释放，传播规则解释资源是否改变，操作规则解释句柄能否继续用；任何性能结论仍须目标平台基准支持。

## 12. 常见陷阱

1. 把 reserve 当元素初始化：它不改变 size，需要 resize/emplace 等真正创建元素。
2. 用 capacity 当下标上界：合法元素区间仍是 `[0,size())`。
3. 保存扩容前的 data 指针：重分配后从容器重新取得；不要比较旧指针来证明它仍有效。
4. 在 range-for 中擦除当前元素：隐式迭代器可能失效；按具体容器使用 erase 返回值或选择合适的 erase_if。
5. 用临时 string 构造 string_view：表达式后所有者销毁，视图不延寿；改为保留拥有者或复制数据。
6. 返回指向局部数组的 span：borrowed_range 也不能挽救已结束的底层寿命。
7. Compare 读取变化的全局状态：同一组键的顺序前提被破坏，应固定比较语义或重建结构。
8. 修改哈希键后不重插：旧桶归属和查找条件冲突；采用合法 extract/改键/重插并处理失败。
9. 把“节点稳定”直接用在 swap：先核 POCS/相等前提；合法时元素句柄转属另一容器，旧 end 另算。
10. 只看临时 allocator 对象寿命：容器复制 allocator 值，真正要长寿的是它引用的 resource、buffer/upstream；中途 release 也会破坏依赖。
11. 跨不相等资源释放 PMR 内存：必须核释放兼容、bytes/alignment 与一次释放，资源名字相似不构成相等证明。
12. 忽略 over-aligned 分配：传递类型所需 alignment，并在对应平台验证上游确能满足。
13. 无同步同时访问 unsynchronized pool：选串行所有权、外部同步或同步资源；后者仍不保护容器结构。
14. 异常路径重复 destroy：只清理已完成构造且仍由本路径拥有的对象，避免和容器析构重复承担责任。
15. 依赖未规定增长因子：用 capacity 下界和实际 size 设计正确性，用测量决定预留策略。

## 13. 设计模板

1. 高频尾插、随机访问：先评估 vector，明确最大批量、预留策略和外部句柄何时重取。
2. 双端队列：评估 deque；选择保存元素引用还是 iterator 时应用 §6 的不同失效规则。
3. 需要稳定节点地址：评估 list，同时量化查找位置、指针追踪和分配成本。
4. 有序范围查询：比较传统 map 与 C++23 flat_map，分别处理节点稳定性和线性插删；不能只按“有序”互换。
5. 精确查找：评估 unordered_map，设定负载因子和 reserve 时机，并对输入碰撞风险单独设计。
6. 临时批处理：PMR 单调资源可集中供给存储，前提是能把所有依赖者的结束点收敛到批末。
7. 帧内分配：容器放在帧作用域中，完成任务/撤销外部借用并析构后再 release；跨帧数据使用更长寿的所有者。
8. 长生命周期对象：评估可逐项回收的池，明确销毁责任和统计；不以定期 release 代替活对象析构。
9. 共享资源：公开线程归属与同步策略，避免资源内部同步掩盖容器结构竞争。
10. ABI 边界：避免暴露具体 allocator/标准库容器类型；PMR 的统一静态类型也不消除不同标准库或 debug ABI 差异。
11. 插件边界：用约定稳定的 C 接口传数组时同时传长度、所有权和释放函数/责任，不能让另一分配域随意 delete。
12. 序列化：先定义字节序、字段格式和对齐/填充处理，不直接转储容器控制对象或把连续存储当跨平台格式。
13. 热点循环：将连续存储、批量算法列为候选，再测真实数据依赖和缓存行为。
14. 大对象：可存稳定句柄减少对象复制，但句柄代次、目标寿命和额外间接访问成本仍需设计。
15. 读多写少：考虑不可变快照，同时安排快照发布同步和旧快照资源的安全回收。

## 14. 评审问题

1. 元素、元素引用的业务对象及底层存储分别由谁拥有，析构/释放责任是否唯一？
2. 哪个 iterator/reference/pointer/end 需要跨哪项操作保留，是否能从 §6 指到具体行？
3. 分配失败如何传播，调用方会重试、丢弃本次工作还是结束任务？
4. 所讨论的是元素移动、普通容器移动构造、带 allocator 构造还是赋值；该操作的 noexcept 条件是什么？
5. Compare 是否严格弱序且在容器使用期间保持稳定？
6. Hash/Pred 是否一致，攻击者可控输入下的碰撞/退化计划是什么？
7. 哪些线程访问资源、容器结构和元素；同步覆盖是否完整？
8. 资源统计统计的是哪一层，是否把接口次数误当 malloc/RSS？
9. 峰值是否包括旧/新存储并存、上游 fallback、桶/节点和元素内部分配；硬上限具体约束哪一条资源链？
10. 空容器路径是否先核前提，避免 front/pop/erase(end) 等非法操作？
11. 重复键时是否正确处理 try_emplace 的返回值以及未插入 node 的所有权？
12. 异常覆盖的是 allocate、元素构造/赋值还是 Hash/Compare；对应失败保证是否逐项核对？
13. 是否纸面覆盖失效边界，且运行测试只使用合法句柄，不以“没崩”判断有效？
14. 是否覆盖业务所需对齐和目标平台；resource/buffer/upstream 是否长于全部依赖者？
15. benchmark 是否固定编译器、标准库、CPU 与配置，并保留输入、原始样本和统计方法？

## 16. 案例：订单簿容器选择

假设订单按价格排序、同价位按时间入队，即同价 FIFO。先确定 P 为活跃价档数、k 为本次实际触及的订单数、R 为离散价格范围；不同量决定不同成本。

`std::map<Price, std::deque<Order>>` 提供有序价档。查找/新建某价档通常走对数复杂度接口，找到已有价档后向其 deque 尾部入队为常数元素操作；比较器执行和真实分配耗时仍另计。最优价可通过有序首/尾入口访问，但应先处理空簿和买卖两侧的排序约定。

已定位非空队列的一次 front/pop_front 是常数操作，**整次撮合不是 O(1)**：它可能消耗 k 个订单、跨多个价档，还要更新剩余数量、移除空价档并寻找下一价档。应按实际使用的 key 查找或 iterator 擦除接口计价档成本，再计队列操作、订单 ID 索引和事件发布成本；不能只将树插入 O(log P) 与一次队首操作拼成整个系统复杂度。

若价格离散且范围有限，`std::vector<std::deque<Order>>` 可用价格到下标映射直接定位队列；下一最优价的发现仍要安排空档扫描、位图或分层索引，不能省略其维护成本。

向量可减少“价档对象定位”的树节点跳转，但连续的是 deque 对象，**Order 载荷并未因此全部连续**。缓存优势是待测假设，不是布局名称直接推出的结果；维护非空价档索引也会增加写入和同步成本。

当范围变大而活跃价档稀疏时，向量至少承担 O(R) 个队列对象的空间。可评估压缩坐标或分段索引，同时写清新价格加入时的映射更新、段分配及最优价查询代价，不能把 O(1) 定位当作没有代价的普适替代。

unordered_map 不维护价格顺序，可用于订单 ID 精确定位等辅助索引，却不能直接替代价格有序结构；它的 rehash 保留元素引用，不保留 iterator，辅助索引的句柄种类必须选对。

每次撮合都扫描整个 unordered_map 找最优价会把价格选择引入线性工作。若需求确实允许扫描，也须纳入整次成本；若保存额外最优价结构，则新增/删除/部分成交时必须一起维护其一致性。

删除任意订单还暴露另一个取舍：deque 中间 erase 会破坏已有句柄，缓存 `deque<Order>::iterator` 的 ID 索引不能只删一个索引项。可重新设计稳定句柄、允许墓碑后批量整理，或评估节点队列并测其局部性成本；无论选择什么，部分成交、撤单、空价档清理都要更新业务索引。PMR 仅改变存储来源，不能自动修复这些索引或改变订单生命期。

未来基准应分别测入队、撤单、最优价读取和整批撮合，并包含同价集中、稀疏价格、跨档成交等输入分布；区分只测容器原语和含索引/事件处理的业务路径。

记录 P50、P95、P99 与吞吐、峰值内存；均值可能掩盖 rehash、扩容、回收等长尾。本次没有这组测量结果。

## 17. 实验：迭代器失效

实验一保留“保存 vector 首元素句柄、观察尾插与容量变化”的学习目的。§9.2 先 reserve，再在容量内尾插并合法访问首元素，随后擦除其后的元素并使用 erase 返回值；最后以 `capacity()+1` 强制请求重分配。

成功重分配后旧元素句柄和旧 end 失效；程序不再读取、比较或解引用它们。这个正例验证新容器状态，失效本身依标准判定，不通过故意访问 UB 来证明。

`reserve(N)` 自身可能分配，只保证后续插入的新 size 不超过当前 capacity 时不再重分配；它不阻止 erase 引起的移动，也不保留尾插前的 end。

实验二在 deque 两端插入后只读取原有中间引用；引用保留是指定操作的保证，而旧 iterator/end 不再使用。中间插入与单元素 pop_front 的不同规则在 §6 纸面核对，未额外运行负例。

实验三在 list 插入及擦除另一个节点后读取保留的 iterator；erase 只使被擦节点句柄失效。§9 另增加 unordered rehash 后读取保留的 mapped-value 指针，明确它与 iterator 的区别。

ASan 可以检测某些悬空内存访问，但未必能检测仍落在已分配存储中的失效迭代器。原故障观察目标仍可用于未来隔离教学，本次 sanitizer 只运行合法程序；静默通过不扩大标准许可。

可用 libstdc++ 单 TU `_GLIBCXX_DEBUG` 核对本例的合法位置/容器归属，不混链不同模式对象；它与 ASan 的检测范围不同，不能代替 PMR 寿命审阅。

每次修改后先判哪类句柄失效，再重新获取所需 iterator/end，或使用操作返回的合法位置。并非所有修改都使所有句柄失效，精确合同比一律重取更能解释为什么代码安全。

## 18. 实验：Allocator 与 PMR

原目标是为短生命周期消息提供 monotonic 存储。§9.2 将其缩成可审计的资源路由和寿命正例：不相等资源的移动赋值合法、目的资源不传播；同资源 swap 合法；已构造对象先析构再 release。

固定栈 buffer 超出后统计 upstream 次数仍是未来实验；原 §9.1 没有计数。新例分别用无初始 buffer 的 arena 观察上游块归还，以及固定 buffer + null upstream 观察一个超限请求抛 bad_alloc，不混称完整 fallback 性能测试。

默认 allocator、unsynchronized_pool_resource、monotonic 的比较仍待按 §19 设计；不同资源的回收语义不同，必须把相同业务寿命和清理工作计入比较。

后续可在相同对象数量、尺寸分布和合法同步的线程数下测分配吞吐、峰值 RSS、释放耗时。当前 CountingResource 的接口平衡计数不能提供这些指标。

monotonic 的 deallocate 不逐块回收；release 才归还/重置存储而不析构 T。新例 Tracked 的析构计数先增加，再 release 清空上游存活块；不能先 release 再依赖对象的析构或容器旧容量。

pool 按尺寸类别管理块，适合大量重复尺寸请求；减少上游调用或 malloc 元数据开销的收益应实测，不保证任意负载都更省内存或更快。

unsynchronized resource 的边界是不能同时从多个线程访问；可用线程归属、外部同步或 synchronized resource，后者仍须搭配容器结构同步。本次没有线程程序或竞争检测。

所有容器/对象先结束，resource 再 release/析构，buffer/upstream 最后结束。`clear` 保留 vector 容量，因此“资源仍活着”和“容器 size 为零”都不足以允许中途 release；帧内练习应以完整依赖者作用域结束为回收点。

## 19. Benchmark 设计模板

本节仍是未来性能测量模板，本次未运行 benchmark，也没有未提供的旧结果可引用。

固定编译器版本、标准库实现、优化选项、CPU 型号和频率策略；记录调试迭代器、sanitizer、线程绑定等配置，不混比不同执行条件。

预热缓存和分支预测，分别报告冷启动与稳态；容器构建、分配和回收是否计时须事先定义。

保存输入生成规则、随机种子和原始样本；重复运行，避免仅选择最快一轮，也避免优化器删掉没有结果依赖的计算。

通过 `benchmark::DoNotOptimize` 或等价方法保留结果依赖，并确认计时范围涵盖需要比较的业务工作；这不是允许把错误或 UB 程序用于比较。

分别报告吞吐（ops/s）和延迟（ns/op），解释批次统计和单操作统计的区别；需要长尾决策时补 P50/P95/P99。

改变数据规模，观察从 L1、L2 到 LLC、内存工作集的变化；缓存层次解释应由采样支持，不能只凭曲线拐点猜原因。

改变负载分布，至少比较均匀、热点和顺序访问，保留插入/删除比例与突发规模，使读者能判断与业务是否同类。

在目标平台用 perf、VTune 或 ETW 等适用工具采样 cache-miss、分支失误和 cycles；本次未安装或使用这些性能工具。

报告重复样本、离散程度和置信区间。当差异小于测量不确定性时，不宣称某容器更快；即使平均更快，也要检查最坏输入和回收长尾。

## 20. 常见故障排查

vector 抖动先对齐时间线与 capacity 变化，检查是否频繁重分配、每次 reserve 小增量或元素自身有昂贵移动/分配；这些是待检假设，不能仅看到抖动便归因扩容。

map 长尾可排查节点分配、allocator 争用、比较器成本与 NUMA 远端访问；先采样/记录触发条件再判断，没有本机证据不称已经测出锁竞争或 NUMA 原因。

unordered_map 退化先看碰撞分布、负载因子和是否在 rehash，区分查询退化与重建桶的峰值；reserve 可以安排重建时机，但不能修正敌对碰撞或错误 Hash/Pred。

list 遍历慢可核节点分散与缓存未命中，同时检查遍历次数、额外查找和业务回调。即使都是线性遍历，局部性仍可能不同；不能在未测时断言唯一原因。

自定义 allocator 崩溃时，从 allocate 的尺寸/对齐、相等关系、construct 成功数、destroy 次数、deallocate 参数和 resource 寿命顺序逐项追踪；同一 allocator 类型或同名 arena 不足以证明可交叉释放。

移动后句柄异常先分普通移动构造、显式 allocator 构造和移动赋值，再核相等/传播条件及来源还是目的地旧句柄。普通移动构造不能一律判原元素句柄失效，带 allocator/赋值也不能一律判有效；旧 end 始终另核，moved-from 的 size/capacity 不凭直觉猜。

## 21. 复习与验收

能按具体操作解释 vector、deque、list 的布局与 iterator/reference/pointer/end 差异，并把 §9 的每次合法访问对应到 §6；分清常见布局和标准要求。

能从有序查询、稳定句柄、写入比例和碰撞风险选择 map、unordered_map 或 flat 容器，并解释订单簿的 FIFO、价格定位、跨档撮合与索引维护成本。

“实现计数 allocator”仍是后续能力目标。本次实现的是 memory_resource 接口观测器，应能解释成功调用平衡、不相等资源移动赋值、SOCCC 和传播 traits，而不能把该例当作任意 allocator 的完整认证。

能为按帧释放的 PMR 临时对象安排 buffer/upstream、resource、对象/容器的寿命链，证明先结束依赖者再 release；本例演示有限顺序，不另实现固定槽对象池或异步引擎系统。

能按 §19 设计包含预热、重复运行与置信区间的 benchmark 是后续目标；当前没有性能数据，不把断言通过当作性能验收。

能用 sanitizer 定位越界、UAF、双重释放仍保留为后续隔离故障练习目标。本次验收只针对标准合同、信息保全和 §9 指定合法程序；不要求执行故意 UB，也不因有限通过提升全文成熟度或补造 verified 事件。

---

## 关联知识与工程落地

- **前置依赖**：
  - [01-对象布局、虚函数与内存分配](01-对象布局、虚函数与内存分配.md)：结构体对齐、填充与内存物理分布。
  - [01-C++核心/02-Copy-Move与值语义](02-Copy-Move与值语义.md)：特殊成员函数与移动语义。
- **同分类与进阶专题**：
  - [05-数据结构与复杂度/01-数据结构复杂度与容器选型](../../02-数学与游戏算法/数据结构与编码/01-数据结构复杂度与容器选型.md)：容器复杂度与缓存感知选型。
  - [08-计算机体系结构与性能/01-处理器存储层次与性能工程](../硬件体系结构与性能/01-处理器存储层次与性能工程.md)：CPU 缓存行与预取器局部性。
- **游戏引擎与业务落地**：
  - [游戏知识/12-引擎源码分析/08-FMallocBinned与内存池源码分析](../../03-引擎架构与资源系统/对象模型与生命周期/07-容器与内存管理源码.md)：UE 垃圾箱分配器与内存碎片治理。
  - [游戏服务端/01-架构与网络/05-并发与高性能](../../07-网络与游戏服务端/运行调度与过载保护/05-并发与高性能.md)：服务端线程专属缓冲与高性能分配。
- **分类与领域入口**：
  - [02-C++对象模型与内存 README](../../../00_Index/学习路线/编程与计算机基础.md)
  - [计算机与工程基础 Domain MOC](../../../00_Index/学习路线/编程与计算机基础.md)
