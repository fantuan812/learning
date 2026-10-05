---
type: Concept
title: "02-Copy-Move与值语义"
status: stable
verified: []
maturity: L2
updated: 2026-10-05
sources:
  - id: cpp17-n4659
    title: "C++17 工作草案 N4659（2017-03-21）"
    resource: "https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2017/n4659.pdf"
  - id: epic-tarray
    title: "Epic TArray 公开 API（2026-10-05 核对）"
    resource: "https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Core/TArray?lang=en-US"
---
# 02-Copy-Move与值语义

> 知识基线：C++17，按 N4659 工作草案的语言及标准库合同解释；C++11 引入移动语义的历史背景另列。N4659 是固定版本工作草案，并非正式 ISO 出版物。
> 适用范围：对象初始化/赋值、转发、返回值和 `std::vector` 的指定操作；UE 对照仅依据本次核对的公开 API，不把标准库异常策略外推给 `TArray`。
> 历史实验：2026-08-12，Windows x64，MSVC 2022 v14.44.35207，`/O2 /std:c++17`；原数值及原始日志保留于 [Evidence](../../../evidence/labs/cpp-move/README.md)，本次没有重跑。
> 最后更新：2026-10-05，修订语义、来源边界与历史结果解释；新增正文窄例在 g++ 14.2.0 / x86_64-linux-gnu 实际编译运行，详见第 8 节。
> 知识成熟度：L2。原 L3 将局部计数实验扩展为整篇语言/UE 实现证据，现按主要承诺重标为原始资料静态核对；新短例不覆盖所有类型、allocator、标准库实现或 UE。`verified: []` 不代表新增人工验证事件。

## 1. 先分清四个问题

面对 `T b = std::move(a)`，按下面顺序分析，比记“左值复制、右值移动”可靠：

1. **表达式是什么值类别、带什么 cv 限定？** `a` 是具名对象表达式，通常是左值；`std::move(a)` 给出相应 xvalue，但保留 `const`。
2. **有哪些可用重载？** 有匹配的移动构造、只有 `const T&` 复制构造，或选中 deleted 函数，会得到不同结果。
3. **是否根本不需要中间对象？** C++17 同类型 prvalue 直接初始化结果对象；NRVO 则另有条件而且可不采用。
4. **所选操作承诺什么？** 是否转移资源、源对象还能做什么、是否抛异常、复杂度多大，都要看类型和具体操作。

值语义是一种类型设计：对象代表一个值，复制应符合该类型的值模型。C++ 不自动替任意类提供“深拷贝、无别名、无悬垂”。初始化、赋值、传参、返回也并非默认一律复制。本文面向需要维护容器、网络、渲染和服务端热路径的工程师；前置是 [对象生命周期与 RAII](01-C++对象生命周期与RAII.md)。

## 2. 核心概念

| 概念 | 需要保留的区别 |
| --- | --- |
| 值对象与引用/指针 | 两个对象可独立存在，但其指针、视图或共享句柄仍可能引用同一资源 |
| 拷贝构造与赋值 | 前者建立新对象，后者改变已存在对象；能力、异常保证和代价不一定相同 |
| 移动构造 | 类 X 的非模板构造函数，首参为 `X&&`、`const X&&`、`volatile X&&` 或 `const volatile X&&`，其余参数（如有）均须有默认实参；可以转移资源，也可以逐成员复制，不以“偷指针”或 O(1) 定义 |
| `std::move` | cast 工具，本身不调用对象的移动操作，也不去除 `const` |
| Copy elision / NRVO | 部分初始化没有中间对象；具名返回值优化是可选省略，不能混为一条保证 |
| moved-from | 某个移动操作后的源状态；标准库默认保证、具体类型的更强保证、自定义合同分开读 |
| `noexcept` | 可兑现的不抛承诺；有助于某些容器的异常安全选择，不是所有移动的准入条件 |
| 转发引用 / `std::forward` | 特殊推导条件下的右值引用形式 / 按模板实参恢复值类别；不保证发生复制或移动 |
| Rule of 0/3/5 | 检查资源语义的一组设计建议，允许显式 default/delete，不要求五个函数全部可用 |

## 3. 原理与最小正反例

本节标明文件名的 C++ 围栏均为独立完整程序，可保存成同名文件复现。正例断言只核规定的状态、值和自定义合同；观察计数不等于跨实现标准保证。两个负例只编译，不执行。

### 3.1 值成员复制、别名与真正的移动

`Player` 的 `string` 和 `vector<int>` 值成员复制后可以独立修改；`View` 的裸指针复制后仍指向原来的 `int`。后者不能因“对象按值复制”就获得被指对象的副本或延长其生命周期。资源管理应通过 RAII 与明确所有权解决，而不是从语法猜测深浅复制。

保存为 `values.cpp`：

```cpp
#include <cassert>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>
struct Player { std::string name; std::vector<int> buffs; };
struct View { int* p; }; // 非拥有指针，复制它不会复制被指对象
struct Ticket {
    int id;
    explicit Ticket(int n) : id(n) {}
    Ticket(const Ticket&) = delete;
    Ticket(Ticket&& other) noexcept : id(std::exchange(other.id, -1)) {}
    // 本类型明确约定：移动构造后源 id == -1；可查询 id，可析构
};
int main() {
    Player a{"Ada", {1, 2}}, b = a;
    b.name = "Bo"; b.buffs[0] = 9;
    assert(a.name == "Ada" && a.buffs[0] == 1);
    int n = 3; View v{&n}, w = v; *w.p = 4;
    assert(*v.p == 4); // 两个 View 对象仍指向同一个 n
    std::string s = "payload";
    auto&& r = std::move(s); // 只绑定引用；尚未调用 string 移动构造
    assert(&r == &s && s == "payload");
    std::string t = std::move(s);
    assert(t == "payload");
    assert(s.size() == s.length() && s.empty() == (s.size() == 0));
    std::cout << "string target=" << t << " source_size=" << s.size() << '\n';
    s.clear(); assert(s.empty()); s = "reuse"; assert(s == "reuse");
    auto p = std::make_unique<int>(7); auto q = std::move(p);
    assert(p.get() == nullptr && q && *q == 7);
    Ticket x{42}; Ticket y = std::move(x);
    assert(x.id == -1 && y.id == 42);
    std::cout << "Player original=" << a.name << "," << a.buffs[0]
              << " alias_value=" << *v.p << " unique=" << *q
              << " Ticket=" << x.id << "," << y.id << '\n';
}
```

可追踪的结果是：`a` 仍为 `Ada,1`，两个 `View` 看到同一个值 4；仅绑定 `r` 时 `s` 仍为 `payload`。构造 `t` 才真正进入 `string` 的移动构造。成功移动后的 `p` 为空、`q` 拥有 7；`Ticket` 的源值 -1 来自这里手写的合同，并非 C++ 对全部自定义类强加的状态。

反例：把 `View` 指向的局部变量提前销毁，再通过它访问，会出现悬垂问题；本程序没有执行这种访问。给拥有裸指针的类默认复制而让两个析构都释放它，也可能双重释放。移动操作同样可能做线性工作、分配或抛出，不能仅从 `&&` 推出廉价资源转移。默认复制/移动按子对象执行的依据是 [class.copy.ctor](https://timsong-cpp.github.io/cppwp/n4659/class.copy.ctor)。

### 3.2 特殊成员：未声明、默认化与 deleted 是三回事

**隐式声明**决定候选是否存在；**隐式定义**通常在需要该操作时形成函数体；**抑制**表示不隐式声明；**定义为 deleted**表示有声明但不能正常调用。还要检查访问控制、子对象能力和重载选择。

| 特殊成员 | 隐式声明条件与典型删除边界（C++17） |
| --- | --- |
| 默认构造 | 没有用户声明的构造函数时隐式声明；缺初始化的引用成员、不能默认构造的子对象等可使默认化函数 deleted |
| 析构 | 没有用户声明析构时隐式声明；子对象析构不可访问或 deleted 等可使默认化析构 deleted，并非“总是可用” |
| 拷贝构造 / 拷贝赋值 | 未显式声明相应 copy 时仍会隐式声明；若声明了 move 构造或 move 赋值，隐式 copy 定义为 deleted；子对象也可使其删除 |
| 移动构造 | 未声明 move 构造，且没有用户声明 copy 构造、copy 赋值、move 赋值、析构时才隐式声明 |
| 移动赋值 | 未声明 move 赋值，且没有用户声明 copy 构造、move 构造、copy 赋值、析构时才隐式声明 |

用户声明包括类内 `~T() = default`，并不要求有手写函数体。默认化 move 仍可能因子对象不可移动而删除；默认化赋值还可能因引用成员、`const` 非类成员等删除。C++17 对某些“声明了析构/copy 另一项，却依赖隐式 copy 定义”的情况另有弃用规则，不能把可编译当成完整资源语义设计。

来源：[默认构造](https://timsong-cpp.github.io/cppwp/n4659/class.ctor)、[析构](https://timsong-cpp.github.io/cppwp/n4659/class.dtor)、[copy/move 构造](https://timsong-cpp.github.io/cppwp/n4659/class.copy.ctor)、[copy/move 赋值](https://timsong-cpp.github.io/cppwp/n4659/class.copy.assign)。

下面三个分支尤其容易混淆：

- **根本没有 move，copy 可用**：右值可绑定到 `const T&` 并复制
- **显式写 `T(T&&) = delete`**：仍参与重载；若它是最佳匹配，就报错，不自动退回 copy
- **默认化 move 因子对象而被定义为 deleted**：这种 move 被重载决议忽略；可用 copy 才能接手。下面 `Member` 的显式 deleted move 正是使 `Outer` 默认 move 删除的原因

保存为 `special.cpp`：

```cpp
#include <cassert>
#include <iostream>
#include <type_traits>
#include <utility>
struct CopyOnly {
    inline static int copies = 0;
    int value = 7;
    CopyOnly() = default;
    CopyOnly(const CopyOnly& x) noexcept : value(x.value) { ++copies; }
};
struct HasDtor { CopyOnly member; ~HasDtor() = default; };
struct Member {
    inline static int copies = 0;
    int value = 8;
    Member() = default;
    Member(const Member& x) : value(x.value) { ++copies; }
    Member(Member&&) = delete;
};
struct Outer {
    Member member;
    Outer() = default;
    Outer(const Outer&) = default;
    Outer(Outer&&) = default; // Member 的显式 deleted move 使此默认 move 删除
};
struct MoveCtorOnly {
    int value = 9;
    MoveCtorOnly() = default;
    MoveCtorOnly(MoveCtorOnly&&) = default;
};
struct CopyAssigned {
    int value = 10;
    CopyAssigned() = default;
    CopyAssigned(CopyAssigned&&) = default;
    CopyAssigned& operator=(const CopyAssigned&) = default;
};
int main() {
    static_assert(std::is_move_constructible_v<CopyOnly>);
    static_assert(std::is_nothrow_move_constructible_v<CopyOnly>);
    static_assert(std::is_move_constructible_v<HasDtor>);
    static_assert(!std::is_move_constructible_v<Member>);
    static_assert(std::is_move_constructible_v<Outer>);
    static_assert(!std::is_copy_assignable_v<MoveCtorOnly>);
    static_assert(!std::is_move_assignable_v<MoveCtorOnly>);
    CopyOnly a; CopyOnly b(std::move(a));
    const CopyOnly c; CopyOnly d(std::move(c));
    HasDtor h; HasDtor j(std::move(h));
    Outer o; Outer z(std::move(o));
    CopyAssigned k, m; m.value = 11; k = std::move(m);
    assert(b.value == 7 && d.value == 7 && j.member.value == 7);
    assert(z.member.value == 8 && k.value == 11);
    std::cout << "CopyOnly copies=" << CopyOnly::copies
              << " Outer member_copies=" << Member::copies
              << " right_value_assignment=" << k.value << '\n';
}
```

此例的 `CopyOnly` 没有 move 构造，但 `is_move_constructible_v` 和 `is_nothrow_move_constructible_v` 都为 true，因为从 `T&&` 构造时可调用它的 `noexcept` copy。这些 traits 判断表达式能力，不是反射“是否声明了某个 move 函数”，更不证明资源转移。`HasDtor` 不隐式声明 move，仍可经隐式 copy 复制成员。`Outer` 从右值复制其成员；`Member` 自身从右值却不行。依据：[类型性质 traits](https://timsong-cpp.github.io/cppwp/n4659/meta.unary.prop)。

负例 `negative_deleted_move.cpp`：预期编译失败，诊断选中 `X::X(X&&)`，不是头文件或工具缺失。

```cpp
#include <utility>
struct X {
    X() = default;
    X(const X&) = default;
    X(X&&) = delete;
};
int main() { X x; X y(std::move(x)); }
```

负例 `negative_assignment.cpp`：预期编译失败，诊断隐式 copy assignment deleted。用户声明 move ctor 抑制了隐式 move assignment，同时使隐式 copy assignment 删除，不能说“只写 move ctor，赋值总能退回复制”。

```cpp
#include <utility>
struct X {
    X() = default;
    X(X&&) = default;
};
int main() { X x, y; y = std::move(x); }
```

`special.cpp` 的 `CopyAssigned` 则显式提供可用的 copy assignment，从而可接收右值赋值。Rule of 0 优先让 RAII 成员承担资源管理；必须自定义时检查整组操作该保留、默认化还是删除。不要为了凑 Rule of 5 让本不该复制的资源变得可复制。设计依据：[Core Guidelines C.20/C.21](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#Rc-zero)。

### 3.3 `vector`：新元素构造与旧元素迁移分别推理

一次尾插可能包含两种工作：用实参构造**新元素**，以及容量不足时把**原有元素**迁到新缓冲区。`push_back(std::move(x))` 可为新元素选择潜在抛异常的移动构造，即使该实现迁移旧元素时选复制。

迁移旧元素时，复制可以保留原值，在新缓冲区构造失败后销毁已构造部分；破坏源值的移动若中途抛出，就未必有办法还原。标准规定操作要求、效果和异常保证，不要求实现实际调用 `std::move_if_noexcept`。

下面是理解常见策略的决策图，前提是类型满足当前容器操作的 Insertable 等要求：

```text
旧元素需要迁移
  能从右值不抛地构造 → 可以用这一操作维持异常保证（也可能实际调用 copy）
  从右值构造可能抛，且可复制插入 → 常见实现复制，保留旧元素供失败回滚
  从右值构造可能抛，且不可复制插入 → 仍可移动；若该 move 真抛，保证有例外
```

helper 本身的精确规则是：`!is_nothrow_move_constructible_v<T> && is_copy_constructible_v<T>` 时 `std::move_if_noexcept(x)` 返回 `const T&`，否则返回 `T&&`。它只选引用类型，也不执行构造。容器的 **CopyInsertable** 则涉及 allocator 构造表达式及语义后置条件，不等同一个 trait。[forward 条款](https://timsong-cpp.github.io/cppwp/n4659/forward)、[allocator-aware container 要求](https://timsong-cpp.github.io/cppwp/n4659/container.requirements.general)。

下面 `vector.cpp` 用默认 `std::allocator`、两个整数载荷，依次隔离两步。两个 `Both` 类型都有 copy；`MoveOnly` 删除 copy，其 move 声明为可能抛，但本轮执行路径不抛。

```cpp
#include <cassert>
#include <iostream>
#include <type_traits>
#include <utility>
#include <vector>
template<bool Nothrow> struct Both {
    inline static int copies = 0, moves = 0;
    int value;
    explicit Both(int n) : value(n) {}
    Both(const Both& x) : value(x.value) { ++copies; }
    Both(Both&& x) noexcept(Nothrow) : value(x.value) {
        ++moves; x.value = -1;
    }
};
struct MoveOnly {
    inline static int copies = 0, moves = 0;
    int value;
    explicit MoveOnly(int n) : value(n) {}
    MoveOnly(const MoveOnly&) = delete;
    MoveOnly(MoveOnly&& x) noexcept(false) : value(x.value) {
        ++moves; x.value = -1; // 本次不抛；类型签名允许抛
    }
};
template<class T> void observe(const char* label) {
    std::vector<T> v; // 默认 std::allocator<T>
    v.reserve(2); v.emplace_back(10);
    T x{20}; const auto old_capacity = v.capacity();
    T::copies = T::moves = 0;
    v.push_back(std::move(x)); // 容量足够：隔离新元素构造
    assert(v.size() == 2 && v.capacity() == old_capacity);
    assert(v[0].value == 10 && v[1].value == 20);
    std::cout << label << " append copies=" << T::copies
              << " moves=" << T::moves << '\n';
    const auto requested = v.capacity() + 1;
    T::copies = T::moves = 0;
    v.reserve(requested); // 不插入新元素：隔离旧元素迁移
    assert(v.size() == 2 && v.capacity() >= requested);
    assert(v[0].value == 10 && v[1].value == 20);
    std::cout << label << " reserve copies=" << T::copies
              << " moves=" << T::moves << " size=" << v.size()
              << " capacity=" << v.capacity()
              << " values=" << v[0].value << "," << v[1].value << '\n';
}
int main() {
    static_assert(std::is_same_v<decltype(std::move_if_noexcept(
        std::declval<Both<true>&>())), Both<true>&&>);
    static_assert(std::is_same_v<decltype(std::move_if_noexcept(
        std::declval<Both<false>&>())), const Both<false>&>);
    static_assert(std::is_same_v<decltype(std::move_if_noexcept(
        std::declval<MoveOnly&>())), MoveOnly&&>);
    observe<Both<true>>("nothrow-move");
    observe<Both<false>>("copyable-throwing-move");
    observe<MoveOnly>("move-only-throwing-move");
}
```

先 `reserve(2)` 再尾插时，三类在本次 libstdc++ 都记录 1 次 move；随后独立 `reserve(capacity()+1)` 时，记录分别为 move 2 次、copy 2 次、move 2 次。这说明“有潜在抛出的 move 就禁止移动”不成立。测试只断言 size、载荷和 `capacity >= requested`，不锁死计数或容量增长策略，也不认证自定义 allocator。

具体操作的边界必须逐条看：

| 操作 | C++17 保证及例外 |
| --- | --- |
| `reserve(n)` | 大于当前容量时才重分配；成功后容量至少为 n，size 不变；重分配会使旧元素引用、指针和迭代器失效 |
| `reserve` 抛异常 | 除不可 CopyInsertable 类型的 move 构造抛出这一例外外，容器没有效果；例外情况下不承诺旧元素原值回滚 |
| 在末尾插入单个元素 | 若 T 可 CopyInsertable 或 `is_nothrow_move_constructible_v<T>` 为 true，发生异常时容器没有效果；否则，不可 CopyInsertable 类型的 move 抛出时效果未指定 |
| 中间插入、erase、赋值 | 另查各自对构造、赋值和异常的要求；不能套用尾插或 reserve 的结论 |

“容器没有效果”也不表示撤销用户构造函数对计数器等外部状态的副作用，或保证传入的外部实参没有变化。上述依据分别为 [vector.capacity](https://timsong-cpp.github.io/cppwp/n4659/vector.capacity) 与 [vector.modifiers](https://timsong-cpp.github.io/cppwp/n4659/vector.modifiers)。

为实际验证一次失败回滚，`reserve_exception.cpp` 使用只有 copy 的 `CopyBomb`，使迁移确定触发 copy 抛异常。它正常复制时保留源值，符合本例默认 allocator 下的 CopyInsertable 要求。

```cpp
#include <cassert>
#include <iostream>
#include <stdexcept>
#include <vector>
struct CopyBomb {
    inline static bool fail = false;
    inline static int attempts = 0;
    int value;
    explicit CopyBomb(int n) : value(n) {}
    CopyBomb(const CopyBomb& x) : value(x.value) {
        ++attempts;
        if (fail) throw std::runtime_error("copy probe");
    } // 无 move；const& copy 可接收右值，默认 allocator 下可 CopyInsertable
};
int main() {
    std::vector<CopyBomb> v;
    v.reserve(2); v.emplace_back(10); v.emplace_back(20);
    const auto capacity = v.capacity(); const auto* data = v.data();
    CopyBomb::fail = true;
    bool caught = false;
    try { v.reserve(capacity + 1); }
    catch (const std::runtime_error&) { caught = true; }
    assert(caught && CopyBomb::attempts > 0);
    assert(v.size() == 2 && v.capacity() == capacity && v.data() == data);
    assert(v[0].value == 10 && v[1].value == 20);
    std::cout << "copy_exception=" << caught << " size=" << v.size()
              << " capacity=" << v.capacity()
              << " values=" << v[0].value << "," << v[1].value << '\n';
}
```

预期捕获 `copy probe`，容量、地址、size 与值仍为原状；本次输出 `copy_exception=1 size=2 capacity=2 values=10,20`。这不能证明不可复制且 move 真抛时也有同样保证，该路径未运行。

容量够用可免旧元素重分配，但新元素构造、载荷分配、实参求值仍有成本且可抛；`reserve` 本身也可能分配、迁移或抛出。给确实不抛的移动操作声明 `noexcept` 有价值；会抛的操作不能为了“让 vector 更快”虚标不抛，否则异常逃逸时会终止程序。

### 3.4 返回值：同型 prvalue 保证与可选 NRVO

| 场景与前提 | C++11/14 | C++17 |
| --- | --- | --- |
| `T f() { return T{}; }`，返回同类型 prvalue | 可省略，仍需满足当时的 copy/move 可用性要求 | 直接构造结果对象，不先产生另一个待移动对象 |
| `T x = T{};`，普通同型对象初始化 | 可省略 | 同型 prvalue 直接初始化目的对象；不能不加条件套给潜在重叠子对象 |
| `T t; return t;`，同型非 volatile 自动局部、非形参等 NRVO 条件满足 | 可选 NRVO | 仍可选 NRVO |
| 同一局部改为 `return std::move(t);` | 不符合上述 NRVO 形式 | 同样失去该 NRVO 资格；结果仍要看重载，不保证一次 move |

省略不等于没有构造：最终对象仍要构造，析构也须可访问且未删除。C++17 同型 prvalue 例可在 copy/move 均 deleted 时良构；反之，把不可移动也不可复制的 `Fixed` 改为具名局部再 `return t`，不能依赖可选 NRVO 挽救程序。

在满足 C++17 相应返回规则的局部对象场景，未实施省略时先按右值进行构造函数重载决议；若失败，或选中的构造函数首参不是对象类型的右值引用，再按左值决议。此过程不采用 vector 式的 `noexcept` 复制回退。来源：[class.copy.elision](https://timsong-cpp.github.io/cppwp/n4659/class.copy.elision)、[dcl.init](https://timsong-cpp.github.io/cppwp/n4659/dcl.init)。

保存为 `elision.cpp`，特意保留 `return std::move(t)` 作为反例对照：

```cpp
#include <cassert>
#include <iostream>
#include <utility>
struct Fixed {
    int value = 42;
    Fixed() = default;
    Fixed(const Fixed&) = delete;
    Fixed(Fixed&&) = delete;
    ~Fixed() = default;
};
Fixed direct() { return Fixed{}; }
struct Count {
    inline static int copies = 0, moves = 0;
    int value = 7;
    Count() = default;
    Count(const Count& x) : value(x.value) { ++copies; }
    Count(Count&& x) noexcept(false) : value(x.value) { ++moves; }
};
Count named() { Count t; return t; }
struct OnlyCopy {
    inline static int copies = 0;
    int value = 8;
    OnlyCopy() = default;
    OnlyCopy(const OnlyCopy& x) : value(x.value) { ++copies; }
};
OnlyCopy cast_return() { OnlyCopy t; return std::move(t); }
int main() {
    Fixed a = direct(), b = Fixed{};
    Count c = named(); OnlyCopy d = cast_return();
    assert(a.value == 42 && b.value == 42 && c.value == 7 && d.value == 8);
    std::cout << "prvalue=" << a.value << "," << b.value
              << " NRVO copies=" << Count::copies << " moves=" << Count::moves
              << " cast_return copies=" << OnlyCopy::copies << '\n';
}
```

预期四个对象的值不变；不要断言普通构建中 NRVO 计数恒为 0。本次 O0/O2 中 `Count` 都为 copies=0、moves=0，加入 `-fno-elide-constructors` 后都为 copies=0、moves=1，即使其 move 可能抛，也没有因 `noexcept(false)` 改选 copy。`Fixed` 的两个同型 prvalue 构造在两种模式下都成立；该开关不关闭 C++17 的这条语言规则。

`OnlyCopy` 没有 move，所以 `return std::move(t)` 本次记录 copy=1，编译器另给出 `-Wpessimizing-move` 警告。若某类型有最佳匹配但显式 deleted 的 move，该写法还可能编译失败。一般返回符合条件的局部直接 `return t`；这是保留 NRVO 机会的建议，不是未测量就能宣称“所有返回 std::move 都恰好多一次 move、绝对更慢”。

### 3.5 moved-from：默认保证、强保证和自定义合同

1. **标准库定义的类型**：除另有规定，成功移动后的源处于有效但未指定状态。不变量成立，满足前置条件的操作仍按合同工作。[lib.types.movedfrom](https://timsong-cpp.github.io/cppwp/n4659/lib.types.movedfrom)、[valid but unspecified](https://timsong-cpp.github.io/cppwp/n4659/defns.valid)
2. **具体操作的更强保证**：例如本例 `unique_ptr` 成功移动构造后源 `get()==nullptr`，目的指针接管所有权；仍须满足该构造函数对 deleter 等的要求。[unique.ptr.single.ctor](https://timsong-cpp.github.io/cppwp/n4659/unique.ptr.single.ctor)
3. **自定义类型**：由它自己的移动操作、不变量与支持操作约定负责；不能自动套用标准库的总括条款。被某个泛型组件使用时，还须满足该组件的类型要求。`Ticket` 的 -1 后置条件就是一个明确设计

对 moved-from `std::string`，可以调用 `size()`、`empty()`、`clear()`，也可以重新赋值。调用 `front()` 先确保非空，元素访问满足相应索引合同。读取合法查询不是 UB，也不是“只许析构/赋值”；不能依赖源一定为空、一定保留原内容，或一定仍指向某块缓冲区。

**未指定（unspecified）不等于实现定义（implementation-defined）**：后者要求实现记录其选择，前者不要求逐项文档化该状态。[未指定定义](https://timsong-cpp.github.io/cppwp/n4659/defns.unspecified)、[实现定义定义](https://timsong-cpp.github.io/cppwp/n4659/defns.impl.defined)。SSO、缓冲区布局和观察到空字符串是实现问题；短字符串也可调用移动构造，不能说“超过 SSO 才真正移动”。历史输出的旧标签保留但不作为现行规则。

### 3.6 UE 对照：复制、移动与 relocation 是不同能力

Epic 的公开 [TArray API](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Core/TArray?lang=en-US) 说明它假设元素可平凡搬移（trivially relocatable）：对象可以透明地转移至新内存，不需要调用 copy 构造。这不是“类型必须可以复制”，也不是 `std::is_trivially_copyable` 的同义词。移动构造能力与这种 relocation 能力同样需要分开判断。[TIsTriviallyRelocatable API](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Core/TIsTriviallyRelocatable)

| 维度 | `std::vector` | UE `TArray` 公开 API 可支持的结论 |
| --- | --- | --- |
| 元素能力 | 看具体操作的 Insertable / Assignable 等要求和异常条款 | 看版本、allocator、relocation 要求及具体成员操作；不移植 vector 异常策略 |
| move-only | 例如本文默认 allocator 的 `MoveOnly` 可完成尾插与 reserve | 不能仅凭 move-only 判定一概非法；复制整个数组或调用复制路径还需元素 copy 能力 |
| 容量管理 | `size()` / `capacity()` / `reserve()` | `Num()` / `Max()` / `Reserve()`；空闲量为 `Max()-Num()`，可用 `Shrink()` 请求收缩 |
| cast 工具 | `std::move` 保留 cv，接受符合其模板规则的实参 | `MoveTemp` 也是 cast，公开合同额外拒绝右值和 const 对象 |

[TUniquePtr](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Core/TUniquePtr) 的独占/移动语义不能推出 `TArray<TUniquePtr<T>>` 一定编译失败，也不能反向保证它的所有操作可用。先看报错究竟是复制元素、反射属性支持、allocator 还是 relocation 约束；不要为修复未经证实的类型限制，把独占所有权改成共享指针或裸指针。普通 `USTRUCT` 的 C++ 复制仍遵守成员的 C++ 语义，UObject 属性复制和反射接口是另一个问题，不能归因于 `CopyPropertiesForUnrealObjects` 自动生成这种复制。

[MoveTemp API](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Core/MoveTemp) 的 cast 本身也不转移资源；只有后续操作才可能改变源。在 UE 代码中按所用版本及项目规范选择工具，源对象后续用法仍按类型合同判断。

历史文章曾引用 UE5.8 的 `Engine/Source/Runtime/Core/Public/Containers/Array.h`，含 `TIsTriviallyRelocatable_V`、`RelocateConstructItems`、`CalculateSlackReserve`，以及 1982/1996/2159/2290/2307 附近行号；还引用 `Engine/Source/Runtime/Core/Public/Templates/UnrealTemplate.h` 的 530 附近行号。它们现仅是待复核线索：本次未访问该安装或 CL，不能认证历史静态断言原句、这些行号、固定 1.5x 增长策略，或“不平凡搬移时必有安全 fallback”。公开页面当前显示的版本也不能替代私有 checkout 的版本证据；没有运行 UE 编译、UHT、PIE 或真实 `TArray` 实验。

### 3.7 转发：保留值类别，不承诺构造

函数模板中针对同一个未 cv 限定类型参数进行推导的 `T&&`，是转发引用这种特殊右值引用形式。左值实参可使 T 推导为引用，经过引用折叠得到左值引用；右值实参可保留右值引用。不是所有写着 `T&&` 的参数都适用，例如已固定 T 的类模板成员参数不能仅凭这几个字符认定为转发引用。[temp.deduct.call](https://timsong-cpp.github.io/cppwp/n4659/temp.deduct.call)

保存为 `forward.cpp`：

```cpp
#include <cassert>
#include <iostream>
#include <utility>
struct Item {
    inline static int copies = 0, moves = 0;
    Item() = default;
    Item(const Item&) { ++copies; }
    Item(Item&&) noexcept { ++moves; }
};
int inspect(Item&) { return 1; }
int inspect(Item&&) { return 2; }
template<class T> int relay(T&& v) {
    // v 这个具名表达式是左值；forward<T> 恢复按 T 决定的值类别
    return inspect(std::forward<T>(v));
}
int main() {
    Item x;
    const int a = relay(x), b = relay(std::move(x));
    assert(a == 1 && b == 2 && Item::copies == 0 && Item::moves == 0);
    const Item c; Item d(std::move(c)); // const Item&& 不能绑定 Item&&，选 copy
    assert(Item::copies == 1 && Item::moves == 0);
    assert(inspect(std::forward<Item>(x)) == 2); // 显式 T 也可以合法
    std::cout << "reference_overloads=" << a << "," << b
              << " const_source_copies=" << Item::copies
              << " moves=" << Item::moves << '\n';
}
```

`relay` 的两个调用只绑定 `inspect` 的引用参数，copies/moves 都为 0。`std::forward<T>(v)` 没有强制“左值 copy、右值 move”。实际接到 `push_back` 时，值类别参与重载，构造还取决于元素类型的能力与容器操作。

反例是把 `relay` 中的 `forward` 无条件换成 `move`：左值也会送入右值路径；下游若消费资源，就可能意外修改调用方对象。但 cast 本身仍不会修改它。`std::forward<Item>(x)` 这种显式实参用法可以合法，不能说“模板外或 T 未推导就必错”。`std::move(c)` 保留 const，本例 `Item&&` 无法绑定，因而 copy；具体类型若另有 `const T&&` 等重载还需另判。[move / forward](https://timsong-cpp.github.io/cppwp/n4659/forward)

## 4. 如何定位热路径里的误复制

旧实验的 [move_counter.cpp](../../../evidence/labs/cpp-move/src/move_counter.cpp) 用五组场景观察：不预留足够容量的 vector、预先 reserve、CopyOnly、返回值、长字符串移动。它只有 `noexcept` Moveable 与 CopyOnly 两类元素，没有 throwing-move、异常回滚、自定义 allocator 或 UE 对照，不能单独解释所有选择分支。

在 AOI 更新、技能结算、移动同步、背包排序等路径，可以按下面顺序排查：

1. 区分新对象构造、旧对象赋值、旧元素迁移及引用绑定；先确认是哪一种成本
2. 在可恢复的实验分支临时删除 copy 构造/赋值，让编译诊断暴露依赖复制的实例化路径；它不保证枚举运行中的全部 copy 点，也会改变重载集合
3. 给所选测试类型记录 copy/move 计数，同时断言载荷值与所有权正确。多线程计数不能引入新的数据竞争；埋点自身也会影响测量
4. 对容量足够的尾插与独立 reserve 分别清计数，不把两种工作合并成“扩容一次多少 move”
5. 只有明确目标操作承诺零 copy，才断言 `copies==0`；继续测实际耗时、分配与延迟，别把调用数当 P99
6. UE 的 trait 检查只回答该版本对应的类型约束；不能替代操作正确性、生命周期或性能验证

## 5. 2026-08-12 历史结果及其真实含义

历史环境：Windows x64，MSVC 14.44.35207，`cl /O2 /std:c++17 /EHsc /utf-8`（runner 另含 `/W4`）。以下保留原场景标签与三个计数列；第一行旧标签“无 reserve”实际指没有预留全部规模，源码先执行了 `reserve(1)`。本次仅静态读取 [原始结果](../../../evidence/labs/cpp-move/results/move_counter_win_x64_msvc.txt)，不是重新运行该工具链。

| 场景 | ctors | copies | moves | 本次校订后的解释 |
| --- | ---: | ---: | ---: | --- |
| `push_back`×10000，无 `reserve`（noexcept 移动） | 10000 | 0 | 34284 | 本次历史样本的 10000 次新元素移动加 24284 次累计旧元素迁移；不是单次扩容次数 |
| 相同操作 + `reserve(10000)` | 10000 | 0 | 10000 | 此运行没有后续旧元素重分配，仍有新元素移动 |
| `push_back`×10000，仅拷贝类型 | 10000 | 34284 | 0 | 此 CopyOnly 连插入右值也会复制，计数含插入及累计迁移；并非所有 copy 都等同深拷贝 |
| NRVO 返回具名对象 | 1 | 0 | 0 | 此类型/选项实施了可选 NRVO |
| C++17 返回临时对象 | 1 | 0 | 0 | 这里是满足条件的同类型 prvalue 结果对象构造 |
| `return std::move(t)` | 1 | 0 | 1 | 此 Moveable 重载选择产生 1 次移动；不能泛化给 CopyOnly 或显式 deleted move |
| 移动超 SSO 长字符串 | —— | —— | —— | 原观察 `t.size()=59, s.size()=0` 保留；不靠 size 数据认证缓冲区转移，不要求其它运行源为空 |

旧报告的 `capacity=12138` 与预留组 `capacity=10000` 是样本值。旧说明中的约 1.5 倍序列 `1,2,3,4,6,…,12138` 是该实现增长模型的解释；raw 只打印最终容量，没有逐次容量日志。不能据此承诺其它版本也如此，或把 libstdc++/libc++ 固定为 2.0。`reserve(10000)` 的标准断言应为容量至少 10000。

34284−10000=24284 是旧运行追加过程中累计的旧元素构造数；在这个每次容量耗尽再尾插的工作负载下，可按扩容前各 size 求和解释。旧结果里的“moves > 10000”“CopyOnly copies 等于该 Moveable moves”“NRVO moves=0”“反模式 moves=1”都只是本程序/工具链观察，不作为跨平台验收门槛。

### 5.1 工程换算：计数不能直接换成帧预算

设一个 MMO 服务器要向待同步列表追加 10000 个对象，合理推理是：先确认列表是否复用旧容量、元素 copy/move 的真实工作和是否分配，再判断重分配是不是热点。**没有 `noexcept` 不等于 CopyOnly**；24284 更不是每次扩容或每 Tick 必付的成本。

已知规模时，提前预留、复用容量可能减少旧元素迁移；不会消除新元素及其载荷构造，也不会让处理 10000 项的总工作变成 O(1)。本实验没计时、没分配统计、没帧延迟分布，不能推出“慢一个数量级”“消除 P99 尖峰”或收益倍数。与服务端时间预算、AOI 批量更新的联系是待测优化假设，而非性能认证。

## 6. 最佳实践与反模式

优先 Rule of 0；必须自定义资源管理时，分别设计复制、移动、赋值、析构和源状态。先保证语义正确，再量化成本。对真实不抛的操作使用合适的 `noexcept`，不要为容器选择掩盖异常。返回局部通常直接 `return t`；调用 `move` 表示允许后续操作消费源，后续仍按该类型合同使用源。

| 常见做法或误判 | 风险 | 更可靠的处理 |
| --- | --- | --- |
| 对 NRVO 候选写 `return std::move(t)` | 失去该 NRVO 机会，也可能 copy 或编译失败 | 通常直接 `return t`；不同返回类型等场景另判 |
| 给会抛的 move 硬加 `noexcept` | 异常逃逸时终止程序 | 修好实现/合同再承诺不抛 |
| 声明析构后认为仍有隐式 move | move 声明被抑制，右值可能 copy 或不可构造 | 检查完整特殊成员集合与 traits 的实际含义 |
| 移动后无条件读 `front()` 或断言空 | 可能违反前置条件或依赖未指定值 | 合法查询、先检查前置条件，必要时重设状态 |
| 在转发层一律 `std::move(v)` | 左值也走消费路径 | 按原意使用 `std::forward<T>(v)` |
| 大对象一律按 `const T&` 或一律按值 | 前者未必满足所有权需要，后者可能增加复制 | 按借用/接管语义选接口，再测实参路径 |
| 热路径每轮机械 reserve 或不断 `reserve(size()+1)` | 预留本身有成本，可能破坏原增长策略 | 结合规模和复用方式预留；不超当前容量就无须为“移动”再预留 |
| 裸指针被误当多所有者 | 双重释放或悬垂 | 区分非拥有观察与独占/共享所有权 |
| 因 TArray 元素 move-only 就改为共享/裸指针 | 改坏所有权模型且可能没修到真正报错 | 按具体元素、版本、allocator 和操作排查 |
| 只写 move ctor 就假设能赋值 | copy assignment 可 deleted，move assignment 可未声明 | 单独检查 assignment 能力，明确 default/delete |
| 只相信 Release 的复制计数 | 漏掉可选省略及配置差异 | O0/O2 都记录选项与观测；两者都不能代替标准合同 |

`emplace_back(args...)` 可直接在元素存储里用参数构造，避免某个待插入临时对象；仍可能重分配、分配载荷或抛异常。`push_back(T{})` 也可用于满足要求的 move-only 类型。哪种接口更快、读起来更清楚，需要具体类型与调用点，不能只看函数名。

## 7. FAQ

**Q1：有 move 构造，vector 为什么仍可能复制？**
看具体操作、复制是否可用及异常保证。可能抛的 move 加可用 copy 时，常见实现迁移旧元素用 copy；新元素仍可 move。标准没有强制调用 helper，也不能靠无条件加 `noexcept` 修复。

**Q2：`return std::move(t)` 为什么常是反模式？**
对符合 NRVO 条件的局部，它破坏这一省略机会；但最终可能 move、copy 或不合法，不保证一次 move，也没有未测的绝对速度结论。

**Q3：moved-from string 还能读吗？**
可以按合同查询 size/empty；`front` 等仍要满足前置条件。不要依赖它一定空或仍保留旧值，未指定状态不等于读本身非法。

**Q4：C++17 保证省略是什么？**
如第 3.4 节的同类型 prvalue 直接初始化结果对象，不需要一个中间对象再复制/移动；析构等要求仍存在。NRVO 另列且可选。

**Q5：TArray 扩容和 vector 一样吗？**
不能套同一异常/搬移策略。TArray 的公开 relocation 假设与 vector 指定操作的 Insertable 要求是不同合同，细节须核对实际版本和 allocator。

**Q6：有 string 成员就一定有 noexcept move 吗？**
不一定。先看自己的用户声明是否抑制隐式 move，再看所有子对象是否可构造及异常说明；有一个 string 成员不足以证明整个类型的能力。符合条件时 Rule of 0 可以直接利用成员操作。

**Q7：历史 capacity 为什么是 12138 而非 16384？**
它反映该历史实现和输入的增长结果，标准不规定统一增长因子。把最终容量、累计计数与逐次容量轨迹分开，后者原日志没记录。

**Q8：emplace 一定更快，move-only 只能 emplace 吗？**
都不是。emplace 能免去某些临时对象，不能保证免重分配或更快；`push_back(T{})` 和合适的 `push_back(std::move(x))` 也能接收 move-only 值。

**Q9：`TArray<TUniquePtr<T>>` 报错怎么查？**
先定位究竟实例化了哪个操作，是否要求复制元素，以及所用版本的 trait、allocator 与反射支持；不能仅凭 move-only 推出容器一概非法，也不在本文保证所有操作可用。

**Q10：`std::move` 与 MoveTemp 有什么区别？**
都是 cast；`std::move` 保留 const，MoveTemp 公开合同另限制 const/右值实参。它们本身都不调用移动构造。

**Q11：移动构造与移动赋值都要手写吗？**
不必手写，也不是都必须可用。明确设计各项；第 3.2 节展示了只声明 move ctor 导致赋值失败，以及显式可用 copy assignment 的不同结果。

**Q12：Debug/Release 的计数不同说明谁错了？**
不由优化开关单独判对错。NRVO 可选，工具链和库配置也可不同；记录 O0/O2 的真实选项，并把语言保证与观察计数分开。

**Q13：TArray 的容量接口是什么？**
公开接口为 `Max()`，元素数 `Num()`，空闲容量 `Max()-Num()`；`Reserve` 与 `Shrink` 用于容量管理。本文不认证历史 `CalculateSlackReserve` 实现或固定增长倍数。

## 8. 可复现验证记录、来源与未覆盖项

### 8.1 本次窄例：2026-10-05

实际工具链为 `g++ (Debian 14.2.0-19) 14.2.0`，target `x86_64-linux-gnu`，系统 libstdc++。把上文每个完整围栏保存为对应文件，在独立临时目录执行；下列命令示范单例的相同参数组合（六个正例分别运行，不编译进同一个程序）：

```bash
g++ -std=c++17 -Wall -Wextra -Wpedantic -O0 values.cpp -o values_O0
./values_O0
g++ -std=c++17 -Wall -Wextra -Wpedantic -O2 values.cpp -o values_O2
./values_O2
g++ -std=c++17 -Wall -Wextra -Wpedantic -O2 -fsanitize=undefined -fno-sanitize-recover=undefined values.cpp -o values_ubsan
./values_ubsan
# NRVO 对照；另以 -O0 执行同一组合
g++ -std=c++17 -Wall -Wextra -Wpedantic -O2 -fno-elide-constructors elision.cpp -o elision_noelide
./elision_noelide
# 两个负例分别 -c 编译，不运行；另以 -O0 重复
g++ -std=c++17 -Wall -Wextra -Wpedantic -O2 -c negative_deleted_move.cpp
g++ -std=c++17 -Wall -Wextra -Wpedantic -O2 -c negative_assignment.cpp
```

| 对象与输入 | 本次实际结果 | 断言与边界 |
| --- | --- | --- |
| values：两个 Player、同指针 View、string/unique_ptr/Ticket | O0/O2/UBSan 退出 0；string 目标 payload，源 size=0；Player=Ada,1，alias=4，unique=7，Ticket=-1,42 | 源 string size=0 只打印；只断言合法关系、clear/赋值与其它明确合同 |
| special：三种 copy/move 候选、析构抑制、赋值对照 | 三模式退出 0；CopyOnly copies=3，Outer member_copies=1，右值赋值结果 11 | traits 的 true 不等于有 move；默认化删除与显式删除区别已编译验证 |
| forward：引用重载及 const 源 | 三模式退出 0；引用重载为 1,2；const 源 copies=1，moves=0 | 引用绑定未发生构造；没有将 forward 强解为移动 |
| vector：三类各两个整数载荷，默认 allocator | 三模式退出 0；尾插均记录 1 move；reserve 分别为 2 move / 2 copy / 2 move；size=2、capacity=3、values=10,20 | 只核值、size 与容量下界，计数/精确容量为该实现观察 |
| reserve_exception：两元素，令 copy 抛出 | 三模式退出 0；捕获异常，size=2、capacity=2、values=10,20，data 未变 | 覆盖此 reserve 的 no-effects，不覆盖不可 copy 的 move 真抛 |
| elision：Fixed、Count、OnlyCopy | O0/O2/UBSan 退出 0；NRVO 0 copy/0 move；O0/O2 禁 NRVO 均退出 0并记录 0 copy/1 move；cast_return copy=1 | 不断言 NRVO 恒 0；`-Wpessimizing-move` 警告保留，未当作零告警 |
| 两个独立负编译 | 各在 O0/O2 退出 1；分别诊断 deleted move ctor、deleted copy assignment | 没有执行负例；不是把基础设施错误当预期失败 |

六个正例共 18 次运行，NRVO 对照另 2 次；六个 UBSan 运行没有报告诊断。它们是有限输入的语义观察，不证明任意类型/平台无缺陷。全文主要承诺仍按标准/API静态核对标为 L2。

### 8.2 历史实验复现入口与保护

历史 Windows 运行入口见 [README](../../../evidence/labs/cpp-move/README.md)。旧 runner 会写入已有 results 文件；如要复现，应在另建的可写副本执行并把新结果另存，不覆盖 2026-08-12 原件。本次未执行旧 runner 或旧计数大程序。

旧 Linux 入口保留为**未执行参考命令**，在副本仓库根运行，删除原来对固定增长倍数/跨实现计数的预期：

```bash
g++ -O2 -std=c++17 -o move_counter evidence/labs/cpp-move/src/move_counter.cpp
./move_counter   # 记录本次工具链与实际输出；不以历史容量、NRVO 或 moved-from 空值判通过
```

原源码、脚本、日志不改。源码中“NRVO 应省略”“return std::move 强制移动”“超 SSO 才转移”“实现定义”等旧注释/标签不是本次静态核对后的通用规则；按第 3 节及 README 的块外纠正阅读。旧实验打印计数而没有本文这些断言，也没有 throwing-move 三类和异常回滚控制。

### 8.3 一手来源与覆盖范围

2026-10-05 核对 [WG21 N4659 版本入口](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2017/n4659.pdf) 对应的固定版 HTML 条款，以及下列公开 API；来源支持具体合同，不把链接存在当运行证据。

| 定位 | 支持的范围 |
| --- | --- |
| [class.copy.ctor](https://timsong-cpp.github.io/cppwp/n4659/class.copy.ctor)、[class.copy.assign](https://timsong-cpp.github.io/cppwp/n4659/class.copy.assign)、[meta.unary.prop](https://timsong-cpp.github.io/cppwp/n4659/meta.unary.prop) | 特殊成员候选、删除/抑制、traits 表达式能力 |
| [forward](https://timsong-cpp.github.io/cppwp/n4659/forward)、[temp.deduct.call](https://timsong-cpp.github.io/cppwp/n4659/temp.deduct.call) | cast、move_if_noexcept 引用类型及转发推导 |
| [vector.capacity](https://timsong-cpp.github.io/cppwp/n4659/vector.capacity)、[vector.modifiers](https://timsong-cpp.github.io/cppwp/n4659/vector.modifiers) | 指定 reserve/尾插效果与异常例外，不是通用容器性能模型 |
| [class.copy.elision](https://timsong-cpp.github.io/cppwp/n4659/class.copy.elision)、[dcl.init](https://timsong-cpp.github.io/cppwp/n4659/dcl.init) | 同型 prvalue、NRVO、C++17 返回时的重载规则 |
| [lib.types.movedfrom](https://timsong-cpp.github.io/cppwp/n4659/lib.types.movedfrom)、[defns.valid](https://timsong-cpp.github.io/cppwp/n4659/defns.valid)、[unique.ptr.single.ctor](https://timsong-cpp.github.io/cppwp/n4659/unique.ptr.single.ctor) | 标准库默认源状态、有效状态含义与 unique_ptr 的更强后置条件 |
| [TArray](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Core/TArray?lang=en-US)、[MoveTemp](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Core/MoveTemp) | 当前公开 API 的有限对照，不认证原 UE5.8 安装、CL、源码行号或运行 |

未运行：旧 MSVC 基准、libc++/其它编译器/ARM、不可 copy 类型的 move 真抛路径、自定义 allocator、UE/UHT/PIE、性能与 P50/P95/P99。后续若研究性能，应另设计耗时与分配测量、负载分布、重复轮次和 UE 对照，再按实际覆盖定级；不能靠本次机械门禁数量升级。

## 9. 关联阅读

- [01-C++对象生命周期与RAII](01-C++对象生命周期与RAII.md)：所有权、异常安全与智能指针边界。
- [Evidence · cpp-move](../../../evidence/labs/cpp-move/README.md)：历史设计、原始数据与本次解释边界。
- [00-计算机与工程基础](../../../00_Index/学习路线/编程与计算机基础.md)：本层规划；后续模板与完美转发、容器与 allocator、对象布局专题。
- [游戏知识/12-引擎源码分析](../../../游戏知识/12-引擎源码分析/README.md)：容器内存源码的后续复核入口。
- [游戏服务端/01-架构与网络](../../../00_Index/学习路线/网络与游戏服务端.md)：服务端热路径的使用背景。
