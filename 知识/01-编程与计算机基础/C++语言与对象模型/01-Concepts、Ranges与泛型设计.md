---
type: Concept
title: "01-Concepts、Ranges与泛型设计"
status: stable
verified: []
maturity: L2
updated: 2026-10-06
---

# 01-Concepts、Ranges与泛型设计

> 知识成熟度仍为 L2。本篇讲 C++20 可表达的接口约束、范围能力与同步消费合同；约束成立不等于业务语义、对象寿命或线程安全已经得到证明。版本依据分别是 N4861（C++20 时期工作草案）与 N4950（C++23 工作草案），不是出版标准全文或所有实现的兼容性承诺。

## 1. 泛型编程的演进与核心痛点

未约束模板仍处于 C++ 静态类型体系中。“按操作使用类型”不等于弱类型：不合法的表达式仍会报错，只是一些错误要等到模板函数体实例化才暴露。约束把一部分要求放在候选接口上，便于拒绝不适用的调用、复用能力名称和选择重载；函数体、库概念的语义要求及运行期前置条件仍需另外成立。

```text
早期模板 / SFINAE       C++11 / 14 / 17            C++20 及后续演进
按操作实例化与候选排除 → std::enable_if 等库工具 → Concepts 显式接口、Ranges 组合
静态类型检查一直存在    不同写法有维护成本          诊断与构建成本仍需实测
```

SFINAE 机制早于 C++11；C++11 标准库加入 `std::enable_if`，不能把两者的起点混为一谈。Concepts 可以改善错误归属，但不会保证所有诊断简短、所有编译更快，也不自动消除运行时派发。历史区保留原来的学习记录，现行结论以本节及下列版本说明为准。

## 2. Concepts 核心语法与约束体系

### 2.1 requires 检查什么，未检查什么

`requires-clause` 为声明附加约束；`requires-expression` 是产生 `bool` 的表达式，常用于定义命名 concept。`requires requires(T x) { ... }` 中，前一个引入子句，后一个引入表达式。

下例保留网络消息情境，但只规定接口形状和 **1400 字节的本地对象大小预算**。它没有发送网络数据，也没有证明编码正确。`SendPacket` 用 `const T&` 消费，检查也采用 `const T&`：具名的 `msg` 是 const 左值。若写成 `requires(T msg)`，检查的是非 const 具名左值，可能错误接纳只有非 const 成员的类型。

```cpp
#include <concepts>
#include <cstddef>

template <typename T>
concept NetworkMessage = requires(const T& msg) {
    msg.GetPacketID();                         // simple：表达式合法
    typename T::HeaderType;                     // type：类型名合法
    { msg.Serialize() } noexcept -> std::same_as<std::size_t>;
                                                // compound：非潜在抛出、精确类型
    requires sizeof(T) <= 1400;                 // nested：本地对象预算为真
};

template <typename T>
concept SimpleSizeCheck = requires { sizeof(T) == 0; };
template <typename T>
concept NestedSizeCheck = requires { requires sizeof(T) == 0; };
static_assert(SimpleSizeCheck<int>);             // 合法表达式，不要求其值为真
static_assert(!NestedSizeCheck<int>);            // sizeof(int) 不会是 0
```

- simple requirement 检查表达式能否形成，不执行 `GetPacketID()`，也不检查其返回值非空、唯一或有效
- type requirement 检查类型名存在；一般不要求所命名的类已经完整定义。本例另有 `sizeof(T)`，因此也要求消息类型可用于该操作
- compound requirement 的 `{ E } -> C` 检查的是 `C<decltype((E))>`，保留值类别和引用信息。本例返回 `std::size_t&` 就不满足 `same_as<std::size_t>`。`noexcept` 检查表达式是否潜在抛出，不能证明实现无 UB、调用必成功；在 `noexcept` 函数中实际抛出还可能终止程序
- nested requirement 才要求替换后的布尔约束成立。局部参数只是描述操作的记号，没有存储、寿命或构造动作；以上成员调用处于未求值语境

模板中依赖参数的 requirement 替换失败可使该 requires-expression 为 false；这不是对任意错误的捕获机制。模板外的非法表达式、函数体实例化中的错误，以及对所有替换都不合法的病态要求，不能一律当作可移植的 false 或必有某种诊断。

`sizeof(T)` 计算对象表示大小，不包含对象通过指针或容器另行拥有的 payload。编码长度还由字段、压缩、版本、长度前缀等决定；传输预算还要考虑协议头和实际链路。真正发包时必须对编码后的字节数做运行时守卫，再按传输层合同处理，不能由 `sizeof(T) <= 1400` 推出满足 MTU 或发包成功。以下 `SendPacket` 只调用接口示范约束位置；第 4.2 节另外展示明确的字节编码。

### 2.2 四种函数约束写法

以下是四种独立替代方案，分别放在命名空间中，不是同一函数可混用的重声明方式。它们依赖上一围栏的 `NetworkMessage`；四种位置并非都适用于类模板。

```cpp
namespace clause_after_template {
template <typename T> requires NetworkMessage<T>
void SendPacket(const T& msg) { (void)msg.Serialize(); }
}
namespace constrained_parameter {
template <NetworkMessage T>
void SendPacket(const T& msg) { (void)msg.Serialize(); }
}
namespace trailing_clause {
template <typename T>
void SendPacket(const T& msg) requires NetworkMessage<T> {
    (void)msg.Serialize();
}
}
namespace abbreviated_function {
void SendPacket(const NetworkMessage auto& msg) { (void)msg.Serialize(); }
}
```

### 2.3 命名原子约束与函数重载偏序

编译器按约束正规化及原子约束身份判断 subsumption，不负责证明任意数学逻辑蕴含。原子约束身份与 **同一表达式在源码中的出现位置、相应参数映射** 有关。复用命名 `Shape<T>` 会复用它的约束来源；在另一处重新拼写相同成员检查，不会因此自动得到相同原子。

下例用返回标签代表渲染策略，方便观察选择结果，没有真正绘图。两个重载形参形状相同、推导和转换条件相同，`SolidShape` 复用了 `Shape` 后增加体积接口，因而选择标签 2。不能由此推出任意重载集合都是“看起来更严格者胜出”。这是函数模板重载，不是函数模板偏特化。

```cpp
template <typename T>
concept Shape = requires(const T& s) { s.Draw(); };

template <typename T>
concept SolidShape = Shape<T> && requires(const T& s) { s.GetVolume(); };

constexpr int Render(const Shape auto&) { return 1; }
constexpr int Render(const SolidShape auto&) { return 2; }

struct Cube {
    void Draw() const {}
    int GetVolume() const { return 1; }
};
static_assert(Render(Cube{}) == 2);
```

只作编译负例：在独立翻译单元中，把上例 `SolidShape` 定义替换为 `requires(const T& s) { s.Draw(); s.GetVolume(); }`，其余保持不变，`Render(Cube{})` 会歧义。新的 requires-expression 是另一个原子来源，不能用“它显然包括 Draw”代替约束偏序规则。这个对照不运行，也不依赖无需诊断的非法程序来证明编译器行为。

## 3. Ranges 体系架构与视图机制

### 3.1 从 iterator/sentinel 到能力选择

range 把遍历入口组合在一个对象接口中；`begin` 提供 iterator，`end` 提供 sentinel，两者不必同型。算法仍要求它们描述一个有效范围，不会因接口换成 range 就自动修复失效迭代器。

```text
range                 有有效的 iterator / sentinel 入口
  └─ input_range      可以读取；可能只能单遍
       └─ forward_range          多遍保证
            └─ bidirectional_range    可后退
                 └─ random_access_range    可随机定位
                      └─ contiguous_range  连续元素与地址关系
独立检查：sized_range / common_range / borrowed_range / view
```

`common_range` 要求 iterator 与 sentinel 同型；`sized_range` 提供相应的摊销常数时间大小能力；`borrowed_range` 关注 iterator 与 range 外壳寿命的关系；`view` 规定适于组合的范围类型及语义。它们不是阶梯上的同一条轴。`std::vector<int>`、`std::span<int>` 是连续范围的例子，`std::vector<bool>` 的代理元素不能沿用这个结论。

从消费操作反推最小能力：一次读出只需相应 input 能力；要重复遍历才要求 forward；需要随机定位才加 random access。经过 filter 后，即使基底是 vector，结果也通常不再有随机访问能力，因为第 k 个匹配元素需要搜索。

### 3.2 view 不等于不拥有，也不等于可以复制

两份固定草案中，view 都要求 range、movable 及 `enable_view`，均未强制 copyable；符合表达式检查也不代表编译器证明了复杂度语义。

| 固定版本 | 定义与复杂度区别 | 所有权含义 |
| --- | --- | --- |
| N4861，C++20 时期草案 | 还要求 `default_initializable`；移动构造、移动赋值、析构为 O(1)，若支持复制则相应复制也为 O(1) | 草案已有共享拥有型范围示例，不能概括成“只持指针、不拥有元素” |
| N4950，C++23 草案 | 去掉默认构造要求；移动构造仍 O(1)，移动赋值不比先析构再移动构造更复杂；从含 M 个元素的对象产生 N 次复制/移动后，那 N 个对象的总析构成本为 O(N+M)；可复制时复制构造 O(1)，复制赋值不比析构后复制构造更复杂 | 明确提供 move-only 的 `owning_view`，允许拥有底层范围；析构不再一律独立于元素数 |

在 N4950 的 `views::all` 规则中，适用路径可把右值普通 vector 移入 owning_view；N4861 的原始 `viewable_range`/all 规则没有这条路径。现代标准库可能在 C++20 模式提供后续修订，不能反推原始 N4861 或历史工具链下限也支持。本文可运行主例全部从命名的左值容器借用；没有把 owning_view 示例计入本次运行矩阵。P2415R2 只用于理解演变背景，不能单凭提案断言采纳、DR 归类或某版本回移。

### 3.3 惰性管道：输入顺序前五个匹配者

下面的 `Player` 查询保留两个 filter、一个 transform 与 `take(5)`。容器及元素在整个同步调用期间存活，遍历期间不增删元素、不使迭代器失效、不改变查询相关状态。`players` 为 const，读取到的 Player 不可经该引用修改；`targets` 自身不必是 const。

```cpp
#include <iostream>
#include <ranges>
#include <vector>

struct Player {
    int id;
    int hp;
    float distance;
};

void ProcessNearbyEnemies(const std::vector<Player>& players) {
    auto targets = players
        | std::views::filter([](const Player& p) { return p.hp > 0; })
        | std::views::filter([](const Player& p) { return p.distance < 50.0f; })
        | std::views::transform([](const Player& p) { return p.id; })
        | std::views::take(5);

    for (int id : targets) {
        std::cout << "Target Locked: " << id << "\n";
    }
}
```

输入依次为 `{1,10,45}`、`{2,0,1}`、`{3,10,50}`、`{4,10,5}`、`{5,10,40}`、`{6,10,10}`、`{7,10,20}`、`{8,10,0}` 时，输出 ID 为 **1、4、5、6、7**。hp 为 0 或距离恰为 50 的项排除；距离为 0 的第六个匹配者不输出。这个操作选的是输入顺序前五个，若要距离最近的五个，需要额外的排序/Top-K 选择及并列规则。

构造管道主要保存基底与可调用对象，不生成全部结果。filter 的首次 `begin()` 就可能扫描到第一个匹配者；forward 基底场景会缓存这个起点。后续推进再搜索，transform 通常在解引用时调用映射，不缓存全部映射结果。重复遍历是否成立先看范围能力，不能把“惰性”当作快照。

`take(5)` 只限制结果数量，不能保证最多检查五个候选；为了找匹配者仍可能扫描全部输入。循环输出第五个结果之后的迭代器推进，也可能引发上游搜索。不要把谓词的精确调用次数、第五次输出后的零工作量或 I/O 耗时当作可移植的复杂度合同。

filter 的相关谓词要求和 transform 的 `regular_invocable` 有语义部分：调用需 equality-preserving（相同输入得到相等结果），且不能修改被要求的函数对象及参数。编译器检查“能调用、类型可用”不足以认证全部语义。这里使用无副作用纯查询；不要把递增计数、随机抽样或不停变化的外部状态塞进回调后，仍按纯查询管道推理。

在本文固定的 N4861/N4950 filter iterator 条款中，修改它指向的元素，使新值不再满足过滤谓词，会产生未定义行为。改变谓词捕获的外部状态则还涉及语义要求、缓存一致性等问题，不能笼统称为同一条“元素修改 UB”。业务更新后重新建立 view，并保持一次消费期间状态稳定；不要期待旧的 cached begin 自动刷新。上述违约只静态说明，不作运行演示。

### 3.4 borrowed、const 与四个寿命

下面是可运行的类型/合法路径对照；临时 vector 的查找结果只检查类型，不解引用。`std::ranges::dangling` 是部分算法返回路径的保护，不是全程序寿命检查器。

```cpp
#include <algorithm>
#include <cassert>
#include <concepts>
#include <iterator>
#include <ranges>
#include <span>
#include <vector>

static_assert(std::ranges::range<std::vector<int>>);
static_assert(!std::ranges::view<std::vector<int>>);
static_assert(!std::ranges::borrowed_range<std::vector<int>>);
static_assert(std::ranges::borrowed_range<std::vector<int>&>);
static_assert(std::ranges::borrowed_range<std::span<int>>);

void CheckRangeTypes() {
    auto result = std::ranges::find(std::vector<int>{1, 2, 3}, 2);
    static_assert(std::same_as<decltype(result), std::ranges::dangling>);

    std::vector<int> values{1, 2, 3};
    auto it = std::ranges::find(values, 2);
    assert(it != values.end() && *it == 2);

    const auto refs = std::views::all(values);
    static_assert(std::same_as<
        std::ranges::range_reference_t<decltype(refs)>, int&>);
    *refs.begin() = 4;                 // const 外壳仍引用可变元素
    assert(values.front() == 4);

    const std::vector<int> fixed{1, 2, 3};
    auto filtered = fixed | std::views::filter([](int n) { return n > 0; });
    using V = decltype(filtered);
    static_assert(std::ranges::view<V>);
    static_assert(!std::ranges::range<const V>);
    static_assert(!std::ranges::borrowed_range<V>);
    static_assert(!std::ranges::random_access_range<V>);
    static_assert(std::same_as<std::ranges::range_reference_t<V>, const int&>);
    static_assert(!std::indirectly_writable<std::ranges::iterator_t<V>, int>);
    assert(*filtered.begin() == 1);
}
```

`const ref_view<vector<int>>` 的 const 只约束外壳，元素仍可变；非 const `filter_view` 可以遍历 const vector，元素不可写。本文两个固定草案的 filter_view 没有 const `begin()`，因此给它加 `const` 甚至可能无法遍历。判断可遍历性用 `range<const V>`，判断元素访问看 `range_reference_t<V>` 和写入要求，不能用一个 const 覆盖所有问题。

| 对象 | 活期/失效需要检查什么 |
| --- | --- |
| 元素所有者 | vector、拥有型 view 等必须活着；erase、重新分配等还可能使 iterator/reference 失效。borrowed 不延长所有者寿命 |
| view 外壳 | filter/transform iterator 保存 parent 指针；filter 的递增使用 parent 的谓词和基底终点，transform 的解引用调用 parent 的映射对象。只保住 vector 不够 |
| 闭包及其捕获目标 | view 持有闭包，不代表闭包引用的对象被拥有。值捕获一个指针或 string_view 也不会获得其所指元素所有权 |
| iterator | 自身尚在作用域内不代表前三者有效；必须同时满足所有者、parent、捕获对象的寿命及修改规则 |

filter 的单次解引用主要使用底层 iterator，也不能据此认为之后的递增可以脱离 parent。不要返回局部 filter/transform 的 iterator，并期待保存底层容器就安全；拥有 vector 的 owning_view 销毁时，其元素 iterator 也不能继续使用。

`borrowed_range<vector<int>&>` 为 true 是左值引用这一参数类型的借用判断，不允许销毁实际 vector 后继续用结果。span/string_view 的 borrowed 属性同样不延寿。`find` 从左值 vector 返回普通 iterator，只证明返回时的类型选择，后续的容器存活和失效性修改仍由调用方管理。

## 4. 现代泛型设计模式实战

### 4.1 CRTP 的实现复用与受约束接口

CRTP 可以让基类复用算法并向派生类静态转发。下面的公开入口只可用于真实 Derived 对象内的 Base 子对象；不能单独构造一个 Base 后把它冒充 Derived。

```cpp
template <typename Derived>
class Base {
public:
    void Action() { static_cast<Derived*>(this)->Impl(); }
protected:
    Base() = default;
};

struct ActionActor : Base<ActionActor> {
    int calls = 0;
    void Impl() { ++calls; }
};
```

如果需求只是“接受能承受伤害的对象”，受约束函数可以面向互不继承的类型；这不替代 CRTP 的全部实现复用，也不自动移除类型中已有的虚调用。下面按实际消费的非 const 左值检查接口，伤害是否合理、对象是否存活仍是业务合同。

```cpp
#include <concepts>

template <typename T>
concept CombatEntity = requires(T& entity, float damage) {
    { entity.TakeDamage(damage) } -> std::same_as<void>;
    { entity.IsAlive() } -> std::convertible_to<bool>;
};

void ApplyAreaDamage(CombatEntity auto& target, float damage) {
    if (target.IsAlive()) {
        target.TakeDamage(damage);
    }
}
```

这里的 `IsAlive()` 在调用时才运行；concept 不判断当前是否活着，也不验证伤害范围、权限、组件存活或并发访问。若这个成员是虚函数，该调用仍遵循虚派发规则。

### 4.2 if constexpr：自定义协议优先的序列化选路

`if constexpr` 在外围模板实例化、条件不再依赖模板参数时，不实例化被舍弃的分支；分支仍要被解析，非依赖错误不能靠它全部隐藏。它适合在同一接口中选择实现，不代替所有类模板形态变化、全特化或偏特化。

以下完整片段只支持两类线格式：类型自定义编码，或者显式的 16 位无符号整数大端编码。**自定义协议优先**，即使类型也 trivially copyable。示例要求实现提供 `std::uint16_t` 且一个字节为 8 位；不把任意 trivial 对象的内存布局当作协议。`StreamWriter` 在内存中积累字节，可能分配并抛异常，不实际发包。

```cpp
#include <climits>
#include <concepts>
#include <cstdint>
#include <type_traits>
#include <vector>

static_assert(CHAR_BIT == 8);

struct StreamWriter {
    std::vector<unsigned char> bytes;
    void WriteU16BE(std::uint16_t value) {
        bytes.push_back(static_cast<unsigned char>(value >> 8));
        bytes.push_back(static_cast<unsigned char>(value & 0xffu));
    }
};

template <typename T>
concept CustomWireSerializable = requires(const T& value, StreamWriter& writer) {
    { value.CustomSerialize(writer) } -> std::same_as<void>;
};

template <typename>
inline constexpr bool unsupported_serialization = false;

template <typename T>
void SerializeData(StreamWriter& writer, const T& value) {
    if constexpr (CustomWireSerializable<T>) {
        value.CustomSerialize(writer);
    } else if constexpr (std::same_as<T, std::uint16_t>) {
        writer.WriteU16BE(value);
    } else {
        static_assert(unsupported_serialization<T>,
                      "Type does not support serialization!");
    }
}

struct PacketCode {
    std::uint16_t code;
    void CustomSerialize(StreamWriter& writer) const {
        writer.WriteU16BE(0xcafe);       // 示例协议的固定标记
        writer.WriteU16BE(code);
    }
};
static_assert(std::is_trivially_copyable_v<PacketCode>);
static_assert(CustomWireSerializable<PacketCode>);
```

`PacketCode{0x1234}` 编码为四个八位字节 `ca fe 12 34`；独立的 `std::uint16_t{0x1234}` 编码为 `12 34`。这证明两种明确选路的示例合同，不证明任意自定义编码器正确，也不提供反序列化、版本协商或长度校验。若后续写入失败，writer 可能已有部分结果；调用方应决定如何丢弃/重试，不能假设自动回滚。

原先“trivially copyable 就 WriteRawBytes”混淆了两件事。受限的本地对象表示快照可以使用字节复制保证，但不能自然成为网络/磁盘协议：字节序、padding、指针/句柄及 ABI 都没有稳定线格式含义。若另写本地快照，需限定非潜在重叠子对象、正确类型且存活的源/目标对象、足够的 char/unsigned char/std::byte 缓冲和合法复制范围；复制回同一对象，或复制到同类型的另一对象，分别按对应条款使用。对泛型对象取真实地址应使用 `std::addressof`。不比较 padding 的固定值、不由复制指针获得新所有权、不恢复任意输入字节为对象。本文序列化接口没有隐式 raw fallback。

## 5. 游戏研发与工程落地对接

### 5.1 游戏移动能力：UE 风格伪代码

以下仅保留 UE 风格的能力示意。`FTransform`、`FVector`、`PhysicsSubsystem` 没有固定引擎版本、真实类声明与工程包含，**不属于可直接构建的 UE 示例**。这里按计划从 const 左值读取位置/速度；若实际实现还修改 actor，要对真实调用的 cv/ref 和能力另加要求。

```cpp
// UE 风格伪代码；依赖项目提供类型与 PhysicsSubsystem 声明。
template <typename T>
concept MovableActor = requires(const T& actor) {
    { actor.GetTransform() } -> std::convertible_to<FTransform>;
    { actor.GetVelocity() } -> std::convertible_to<FVector>;
};

void PhysicsSubsystem::UpdateMovement(MovableActor auto& actor, float deltaTime) {
    // 示意：读取 actor 的变换/速度，按项目规则更新运动。
    // 真实写入操作、deltaTime 合法性和线程条件须另定义。
}
```

类型存在这些表达式，不代表对象/组件此时存在、指针可用、状态已同步或当前线程允许访问。UHT、模块、真实 API 及引擎运行行为均未在本篇验证。

### 5.2 AOI 的同步消费与跨帧边界

稳定候选集可以经过 filter/transform，同步交给编码器逐项消费，避免显式创建一个中间结果 vector。候选所有者、view 及捕获目标要活到消费结束，期间不能发生破坏查询语义或迭代器有效性的修改。编码缓冲、源容器、闭包、日志与 I/O 仍可能分配，不能把省一个中间容器说成整条链零分配。

若任务进入跨帧/跨线程队列，应交付拥有的结果快照，或采用项目明确的实体 ID/代际、所有权与同步合同；存下借用 view 不能替代这些保证。顺序来自候选顺序或显式排序，take 只限制数量。此处是设计边界，未运行真实 AOI、网络或性能压力测试。

## 6. 工程检查与完整同步事件示例

1. 先从消费端决定所需操作与 cv/ref，再把可复用能力命名；不要让巨大约束链替代业务验证
2. 同时检查元素所有者、view、捕获目标、iterator；值捕获借用句柄不等于拥有元素
3. 测量构建耗时、代码体积、分配和热点，按项目选择优化选项。`-O2`/`-O3` 可帮助内联，但不能保证任意 view 都变成理想平铺循环

### 6.1 适配器成本与写入条件

下表 N 是有限输入元素数，K 为非负数量，J 是外层范围数，M_i 为第 i 个内层长度。成本描述以底层单步、比较及回调成本另计；有昂贵 iterator/谓词/映射时，必须乘入或单列这些成本。构造还包含实际基底、闭包的构造/复制/移动，不能对任意输入统一写 O(1)。

| 适配器 | 发生工作的时点与操作量 | 元素修改条件 | 游戏用途 |
| --- | --- | --- | --- |
| filter | begin 搜首个匹配，递增搜索后续；完整一次扫描有 O(N) 级候选检查，forward 起点可缓存 | 先看引用是否可写；不得把被其 iterator 指向的值改成不满足谓词 | 存活/视距筛选 |
| transform | 解引用调用映射；读 N 个结果涉及 N 次该层读取，重复读取可重算 | 取决于返回值/引用/proxy；映射自身仍需满足语义要求 | ID/坐标投影 |
| take | 输出至多 min(K,N) 项；基底 begin、推进与上游搜索仍计费 | 继承底层引用及失效约束 | 已按业务顺序排列的目标限额 |
| drop | 首次定位要跳过至多 min(K,N) 项；随机访问并可得边界时可直接定位，其他路径可能逐步推进；再遍历剩余项 | 继承底层引用及失效约束 | 顺序分页，不能忽略跳过成本 |
| join | 完整遍历要经过外层及内层；普通常数单步条件下 O(J + ΣM_i)，空内层也要跳过；内层按需生成成本另计 | 看内层引用/代理及其活期；不统一保证可写 | 展平网格内实体列表 |

### 6.2 完整 EventBus：编译期入口约束、运行时类型擦除派发

这个教学总线用 `GameEvent` 约束模板入口，运行时以 `type_index` 查桶，用 `std::function` 擦除 handler 类型。名称只是描述信息，不参与索引。`T::GetEventName()` 仅检查**可经类型名调用的名称表达式**及到 `const char*` 的转换：它不能认证成员一定是静态函数（也可能是可调用静态数据成员），不证明返回非空或所指字符串存活。下面两种事件恰好使用静态函数。

调用方合同刻意保持最窄：单线程；在发布前完成订阅；Publish 期间不订阅、清空、替换、移动/移走或销毁 bus，不重入 Publish，也不销毁事件。事件及回调引用捕获的目标必须在每次实际同步回调期间存活；不得把 eventPtr 或借用的事件引用交给异步任务或保留到其寿命之外。没有自动解绑。除空 handler 拒绝外，这些是调用方前置条件，代码没有运行时防护。

```cpp
#include <concepts>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <utility>
#include <vector>

template <typename T>
concept GameEvent = requires {
    { T::GetEventName() } -> std::convertible_to<const char*>;
};

struct PlayerDeathEvent {
    int killerId;
    int victimId;
    static constexpr const char* GetEventName() { return "PlayerDeath"; }
};

struct ItemLootedEvent {
    int playerId;
    int itemId;
    int count;
    static constexpr const char* GetEventName() { return "ItemLooted"; }
};

class EventBus {
public:
    template <GameEvent E>
    using Handler = std::function<void(const E&)>;

    template <GameEvent E>
    void Subscribe(Handler<E> handler) {
        if (!handler) {
            throw std::invalid_argument("empty event handler");
        }
        const auto typeId = std::type_index(typeid(E));
        m_handlers[typeId].push_back([h = std::move(handler)](const void* eventPtr) {
            h(*static_cast<const E*>(eventPtr));
        });
    }

    template <GameEvent E>
    void Publish(const E& event) {
        const auto typeId = std::type_index(typeid(E));
        const auto it = m_handlers.find(typeId);
        if (it != m_handlers.end()) {
            for (const auto& rawHandler : it->second) {
                rawHandler(std::addressof(event));
            }
        }
    }

private:
    using TypeErasedHandler = std::function<void(const void*)>;
    std::unordered_map<std::type_index, std::vector<TypeErasedHandler>> m_handlers;
};

int main() {
    EventBus bus;
    bus.Subscribe<PlayerDeathEvent>([](const PlayerDeathEvent& e) {
        std::cout << "Event: Player " << e.victimId << " killed by " << e.killerId << "\n";
    });
    bus.Subscribe<ItemLootedEvent>([](const ItemLootedEvent& e) {
        std::cout << "Loot: Player " << e.playerId << " item " << e.itemId
                  << " count " << e.count << "\n";
    });

    PlayerDeathEvent deathEvt{.killerId = 101, .victimId = 202};
    ItemLootedEvent lootEvt{.playerId = 202, .itemId = 7, .count = 2};
    bus.Publish(deathEvt);
    bus.Publish(lootEvt);
    return 0;
}
```

输出依次为 `Event: Player 202 killed by 101` 与 `Loot: Player 202 item 7 count 2`。对于同一事件类型，按订阅顺序同步调用；没有对应订阅时不做事，不向基类或其他类型自动广播。

类型擦除安全链是：`Subscribe<E>` 用 E 建桶，wrapper 按同一 E 恢复指针，`Publish<E>` 查同一类型桶，`std::addressof(event)` 取得实际事件地址，再在存活期内同步调用。普通 `&event` 可能调用 E 重载的 `operator&`，不能代替这里的真实取址。约束表达式本身不证明任意 `void*` 转换安全。

空 `std::function` 在 Subscribe 入口被 `invalid_argument` 拒绝，尚未查桶；若直接调用空 std::function，其定义行为是抛 `bad_function_call`，不是 UB。构造/复制 callable、std::function 分配和容器扩容仍可能抛异常；std::function 目标有复制要求，不能随意容纳 move-only 捕获，也不能承诺零开销。

回调抛异常时，Publish 不捕获，异常向调用方传播，本轮后续 handler 不再执行；已完成回调及已发生副作用不回滚。若调用方处理了异常、恢复其业务条件并继续满足上述合同，可再次 Publish，仍从第一个 handler 开始；这不等于自动事务恢复。

发布中向同一桶追加 handler 会破坏正在遍历的 vector 的 iterator/end 合同；即使预先 reserve，push_back 也会使旧 end 失效。不能把“未扩容”当作允许动态订阅。这里没有声称往任意其他桶插入都必然使既有 vector 引用失效，而是统一禁止发布中修改，以保持示例合同简单。重入也不是在所有情形都必然 UB，只是不在本例支持范围。哈希查找、vector 遍历及 std::function 间接调用都在运行时发生；这是教学实现，不能代替项目事件框架的并发、解绑、生命周期和错误策略设计。

### 6.3 证据版本与验证边界

本篇采用以下固定一手来源的相关条款，阅读范围限于表中主题，不宣称通读完整标准。N4861/N4950 都是工作草案；原有 cppreference 链接作为二手参考保留在历史区。

| 固定来源 | 本篇对应条款/用途 |
| --- | --- |
| [N4861 expressions](https://github.com/cplusplus/draft/blob/6aea7f6be0895b9dd361c6562bdee2f3809e4fa0/source/expressions.tex) | `[expr.prim.req]` 及四类 requirement、局部参数、未求值与替换边界 |
| [N4861 templates](https://github.com/cplusplus/draft/blob/6aea7f6be0895b9dd361c6562bdee2f3809e4fa0/source/templates.tex) | `[temp.constr.atomic]`、`[temp.constr.normal]`、`[temp.constr.order]` 的原子身份、参数映射及偏序 |
| [N4861 ranges](https://github.com/cplusplus/draft/blob/6aea7f6be0895b9dd361c6562bdee2f3809e4fa0/source/ranges.tex) | range/view/refinements、borrowed、filter 的缓存、parent 依赖与修改条款 |
| [N4950 ranges](https://github.com/cplusplus/draft/blob/4e4de1df8ee941255b653b61d0a62050b34cf8c9/source/ranges.tex) | view 新语义、ref/owning_view、filter/transform/take/drop/join 的相关定义 |
| [N4950 statements](https://github.com/cplusplus/draft/blob/4e4de1df8ee941255b653b61d0a62050b34cf8c9/source/statements.tex) 与 [basic](https://github.com/cplusplus/draft/blob/4e4de1df8ee941255b653b61d0a62050b34cf8c9/source/basic.tex) | `[stmt.if]`、`[basic.types.general]`：舍弃分支与受限对象表示复制 |
| [N4950 utilities](https://github.com/cplusplus/draft/blob/4e4de1df8ee941255b653b61d0a62050b34cf8c9/source/utilities.tex) 与 [containers](https://github.com/cplusplus/draft/blob/4e4de1df8ee941255b653b61d0a62050b34cf8c9/source/containers.tex) | std::function 的构造/调用、vector 的 reserve/插入失效 |
| [N4861 官方 PDF](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2020/n4861.pdf) | §18.7.3–4 的 regular_invocable/predicate 语义，§20.10.11 的 addressof |
| [N1696](https://www.open-std.org/jtc1/sc22/WG21/docs/papers/2004/n1696.html)、[P2415R2](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2021/p2415r2.html) | 前者是早于 C++11 的 SFINAE 讨论；后者仅作 view 演变背景，不单独证明采纳或回移 |

2026-10-06 的有限自测环境为 Linux x86_64、g++ 14.2.0（Debian 14.2.0-19）、libstdc++ 14（`__GLIBCXX__=20250315`），语言模式 `-std=c++20`（`__cplusplus=202002`、`__cpp_lib_ranges=202110`）。从本篇围栏原样提取定义，独立 harness 只补输入、断言和入口；UE 伪代码不参与编译。

- 使用 `-Wall -Wextra -Wpedantic -Werror`，核心例子、完整 EventBus main、合法事件边界分别在 `-O0`、`-O2` 编译运行通过；相同三组在 `-O1 -g -fsanitize=undefined -fno-sanitize-recover=all` 及 `UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1` 下通过，未报告 UBSan 诊断
- 核心检查覆盖 const/返回引用/noexcept/缺少嵌套类型/本地预算的静态断言、四种函数写法、偏序正例、四种 Player 输入、borrowed/const 类型、CRTP/伤害调用及上述三条编码路径（含非 trivial 自定义类型）
- EventBus 检查覆盖原 main 的两行输出、同类两个 handler 顺序、第二类型独立分桶、无订阅发布、空 handler 拒绝、回调异常中断与下一次正常发布；另以禁用普通 `operator&` 的事件验证最终 addressof 路径
- 两个独立编译负例实际命中重载歧义和不支持序列化的目标 static_assert；有对应成功编译/运行对照，不以缺头文件等无关失败充当拦截证据

这些是有限合法实例的功能与类型检查，不是性能、任意类型或全工具链证明。未运行 ASan/LSan、C++23 owning_view、MSVC/Clang、UE、真实网络、跨帧/并发派发；悬垂、filter 修改违约、发布中修改/重入/销毁等禁止路径只作静态分析。UBSan 无诊断也不证明所有未定义行为都不存在。

### 6.4 2026-08-20 原记录（原文保留，非本次验证结果）

下列旧记录按原字节保留。其“官方参考”实际链接的是二手 cppreference；旧工具链下限与“使用某模式测试”的意图不代表本次已验证的矩阵，也不覆盖后来的标准库修订。

> 验证与基准：使用 `-std=c++20` 编译测试样例并检验编译期诊断清晰度与测试矩阵。

> 知识成熟度：L2（现代 C++ 泛型范式、C++20 Concepts 约束、Ranges 管道与高性能工程落地）。
> 知识基线：ISO C++20 标准（Constraints & Ranges）；C++23 流式扩展；主流工具链（MSVC 19.30+ / Clang 13+ / GCC 11+）。
> 官方参考：[cppreference constraints](https://en.cppreference.com/w/cpp/language/constraints)、[cppreference ranges](https://en.cppreference.com/w/cpp/ranges)。
> 最后更新：2026-08-20。

---

## 7. 关联知识与工程落地

- **前置依赖**：
  - [01-C++核心/01-C++对象生命周期与RAII](01-C%2B%2B对象生命周期与RAII.md)：对象生命周期与值所有权。
  - [01-C++核心/02-Copy-Move与值语义](02-Copy-Move与值语义.md)：右值引用、完美转发与移动语义。
- **同分类与进阶专题**：
  - [02-异常、类型系统与标准库实现](02-异常、类型系统与标准库实现.md)：无异常模式下的现代类型系统与预期返回（std::expected）。
  - [02-C++对象模型与内存/01-对象布局、虚函数与内存分配](01-对象布局、虚函数与内存分配.md)：静态多态与动态虚函数多态的内存与时延对比。
  - [10-编译链接与ABI/01-编译链接与ABI全流程](../编译链接与ABI/01-编译链接与ABI全流程.md)：模板实例化与符号重命名机制。
- **游戏工程与算法落地**：
  - [游戏知识/03-游戏玩法编程/README](../../../游戏知识/03-游戏玩法编程/README.md)：玩法系统组件化设计。
  - [游戏服务端/06-世界模拟与运行时/05-AOI与InterestManagement](../../07-网络与游戏服务端/状态复制与兴趣管理/05-AOI与InterestManagement.md)：流式空间范围查询。
- **分类与领域入口**：
  - [03-现代C++与泛型 README](../../../00_Index/学习路线/编程与计算机基础.md)
  - [计算机与工程基础 Domain MOC](../../../00_Index/学习路线/编程与计算机基础.md)
