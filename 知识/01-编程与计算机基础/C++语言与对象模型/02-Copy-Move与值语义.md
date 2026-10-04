---
type: Concept
title: "02-Copy-Move与值语义"
status: stable
verified: []
maturity: L3
---
# 02-Copy-Move与值语义

> 知识基线：C++11 移动语义、C++17 保证省略（guaranteed copy elision）、`std::vector` 扩容语义（`move_if_noexcept`）；编译器基准 MSVC 2022（v14.44.35207）/ x64，`/O2 /std:c++17`；UE 对照以 UE5.8 `TArray` 源码为准。
> 版本基准：C++11 引入移动语义与 `noexcept` 移动；C++17 起"返回临时对象"为强制省略；NRVO 始终是优化而非保证。
> 适用范围：所有 C++ 开发；容器扩容相关结论同时适用于 UE `TArray`（机制同源，实现见 12-引擎源码分析）。
> 官方参考：[cppreference - Copy elision](https://en.cppreference.com/w/cpp/language/copy_elision)、[cppreference - Move constructor](https://en.cppreference.com/w/cpp/language/move_constructor)、[cppreference - Rule of three/five/zero](https://en.cppreference.com/w/cpp/language/rule_of_three)。
> 最后更新：2026-08-12（首版，含本机实验）。
> 知识成熟度：L3（可运行 Demo + 原始结果，见 [evidence/labs/cpp-move](../../../evidence/labs/cpp-move/README.md)）。

## 1. 概述

"值语义"是 C++ 区别于 Java/C# 的核心：**变量就是对象本身，赋值/传参/返回默认都是复制**。复制成本高时引入移动语义，移动语义又带来 `noexcept`、省略（elision）、moved-from 状态等一系列规则。不理解这套规则，就解释不了三个最常见的工程现象：

1. `TArray`/`vector` 扩容为什么有时是"搬移"、有时是"深拷贝"，差一个数量级；
2. `return std::move(t)` 为什么被普遍视为反模式；
3. 为什么 C++17 之后"返回临时对象"不再产生任何拷贝/移动。

本文给出完整规则 + 一个可复现的本机实验（[evidence/labs/cpp-move](../../../evidence/labs/cpp-move/README.md)），用计数数据验证结论。

目标读者：需要写或维护 C++ 容器/网络/渲染热路径的客户端与服务端工程师；前置：掌握 01 篇的生命周期与所有权概念。

## 2. 核心概念

| 术语 | 中文 | 一句话定义 |
| --- | --- | --- |
| Value semantics | 值语义 | 变量直接持有对象；复制产生独立副本，修改互不影响 |
| Reference semantics | 引用语义 | 变量持有引用/指针；多个名字指向同一对象 |
| Copy constructor | 拷贝构造 | 从同类型对象复制构造新对象 |
| Move constructor | 移动构造 | 从右值"偷走"资源构造新对象，源对象进入 moved-from 状态 |
| Copy elision | 拷贝省略 | 编译器/标准省略拷贝或移动构造（C++17 部分为强制） |
| RVO / NRVO | 返回值优化 | 返回临时对象（RVO）/ 返回具名局部对象（NRVO）的省略 |
| Moved-from state | 移动后状态 | 被移动对象必须"有效但未指定"，只能销毁或重新赋值 |
| Rule of 0/3/5 | 三五零规则 | 特殊成员函数的显式声明规则，防止资源管理错误 |
| `noexcept` move | 不抛移动 | 移动构造/赋值声明不抛，是容器扩容选择移动的前提 |
| Forwarding reference | 转发引用 | 模板参数 `T&&` 在推导语境下按实参值类别折叠，配合 `std::forward` 转发 |
| `std::forward` | 条件转发 | 按推导类型保持实参值类别（左值→拷贝、右值→移动），与 `std::move` 不同 |

## 3. 原理详解

### 3.1 值语义与"复制"的代价

```cpp
struct Player { std::string name; std::vector<int> buffs; };

Player a = load();
Player b = a;            // 值语义：深拷贝 name 与 buffs（可能触发多次堆分配）
```

值语义的好处是安全（无别名、无悬垂），代价是复制昂贵。C++11 之前的优化手段只有"省略"与"引用传递"；移动语义出现后，"把资源转移而不是复制"成为标准答案。

### 3.2 特殊成员函数：何时隐式生成、何时被删除

| 特殊成员 | 隐式生成条件 | 被删除/抑制的典型条件 |
| --- | --- | --- |
| 默认构造 | 无任何用户声明构造 | 声明了任何构造函数 |
| 拷贝构造/赋值 | 未声明移动成员时生成 | 声明了移动构造或移动赋值 |
| 移动构造/赋值 | 未声明拷贝/析构/移动成员时生成 | 声明了拷贝构造、拷贝赋值或析构 |
| 析构 | 总是生成 | —— |

工程推论：

- **声明了析构函数（如释放资源）却没有声明移动成员**：移动被抑制，`vector` 扩容退化为复制（实验测试 3 的直接原因）。
- **声明了移动构造却没有 `noexcept`**：`vector` 扩容时用 `move_if_noexcept` 判定，可能退化为复制。
- Rule of 0/3/5：要么全不声明（Rule of 0，成员用 RAII 类型），要么拷贝/移动/析构成组声明（3/5）；只声明析构是最常见的隐性性能与正确性坑。

### 3.3 `vector` 扩容：为什么 `noexcept` 移动决定一切

`std::vector` 扩容时要把旧元素搬到新内存。标准规定：**只有移动构造保证不抛（`noexcept`）时才允许移动旧元素，否则必须复制**（`move_if_noexcept`）。原因：复制失败可以回滚到旧缓冲区（强保证），移动一旦中途抛异常，旧缓冲区已被破坏，无法恢复。

```text
扩容搬移决策：
  元素移动构造 noexcept ?  → 逐元素移动（O(1) 资源转移）
                        :  → 逐元素复制（深拷贝，可能大量堆分配）
```

这与 UE `TArray` 同源：`TArray` 的 `RelocateConstructItems` 在元素类型可平凡搬移（`TIsTriviallyRelocatable`）时直接内存搬运，否则调用移动/拷贝构造；`noexcept`/平凡性决定搬移成本。

### 3.4 拷贝省略：C++17 前后分界

| 场景 | C++11/14 | C++17 起 |
| --- | --- | --- |
| `return T();`（返回临时） | 允许省略（非保证） | **强制省略**（语言保证） |
| `T t = T();`（初始化临时） | 允许省略 | 强制省略 |
| NRVO（返回具名局部） | 允许省略（优化） | 仍是非保证优化 |
| `return std::move(t);` | 阻止省略，产生移动 | 同样阻止省略，产生移动 |

注意：**省略的是"复制/移动构造调用"，不是"构造"**。`return T()` 仍会构造那个临时对象一次，只是直接在目标地址构造。

### 3.5 moved-from 状态契约

移动构造/赋值后，源对象进入 moved-from 状态：

- **必须有效**：可析构、可赋值、可调用不依赖内部值的成员；
- **值未指定**：读取其内容属于实现定义（MSVC 下长字符串 `size()==0`，但标准不保证）；
- 因此：被移动后不要读值；要么重新赋值，要么销毁。

### 3.6 UE 对照：`TArray` 与 `std::vector` 的搬移差异（UE5.8 源码）

UE 的 `TArray` 与 `std::vector` 机制同源、策略不同，本机 UE 5.8 源码可验证三点：

1. **`TArray` 要求元素"可平凡搬移"**：`Engine/Source/Runtime/Core/Public/Containers/Array.h` 中 `UE_STATIC_ASSERT_WARN(TIsTriviallyRelocatable_V<InElementType>, "TArray can only be used with trivially relocatable types")`——`TArray` 只接受可平凡搬移类型，因此 `TArray<TUniquePtr<T>>` 这类 move-only 元素不合法；而 `std::vector` 通过 `noexcept` 移动支持 move-only 类型。代价权衡：UE 用"限制元素类型"换取"批量内存搬移"的高性能路径。
2. **批量搬移统一走 `RelocateConstructItems`**：`Array.h` 中 `Insert`/`RemoveAt`/`SetNum` 等批量操作（第 1982/1996/2159/2290/2307 行附近）都调用 `RelocateConstructItems<ElementType>`——对可平凡搬移类型退化为 `memmove` 级别的内存搬运，不逐个调用构造/移动。
3. **`MoveTemp` 是 UE 的 `std::move`**：定义于 `Engine/Source/Runtime/Core/Public/Templates/UnrealTemplate.h`（第 530 行附近）：`constexpr std::remove_reference_t<T>&& MoveTemp(T&& Obj) noexcept`，且带 `static_assert` 禁止对右值调用——防止 `MoveTemp(MoveTemp(x))` 之类的冗余转移。

与实验结论的映射：

| 维度 | `std::vector` | UE `TArray` |
| --- | --- | --- |
| 搬移条件 | 移动构造 `noexcept`（`move_if_noexcept`） | 元素可平凡搬移（编译期约束） |
| 搬移方式 | 逐元素移动构造 | 可平凡搬移时批量内存搬运 |
| move-only 元素 | 支持 | 不支持（静态断言） |
| 预分配 | `reserve(N)` | `Reserve(N)`（`Array.h` 中增长经 `CalculateSlackReserve`，约 1.5x slack） |
| 移动工具 | `std::move` | `MoveTemp`（禁止对右值调用） |

> 验证命令：`rg -n "TIsTriviallyRelocatable_V|RelocateConstructItems" "C:\Program Files\Epic Games\UE_5.8\Engine\Source\Runtime\Core\Public\Containers\Array.h"`。

### 3.7 转发引用与完美转发（与移动语义配套）

模板参数 `T&&` 在推导语境下是**转发引用（forwarding reference）**，不是右值引用：

```cpp
template <typename T>
void push(T&& v) {            // T&& 是转发引用：传左值时 T=T&，传右值时 T=T
    vec_.push_back(std::forward<T>(v));   // 保持"左值→拷贝、右值→移动"
}
```

区分 `std::move` 与 `std::forward`：

| 工具 | 作用 | 典型场景 |
| --- | --- | --- |
| `std::move(x)` | 无条件转成右值引用 | 转移所有权、入容器 |
| `std::forward<T>(x)` | 按推导类型转发（保持值类别） | 完美转发参数、工厂函数 |
| UE `MoveTemp` | 等价 `std::move` + 静态断言 | UE 代码中的移动 |

常见错误：

- 在转发函数里用 `std::move(v)` 代替 `std::forward<T>(v)`：左值实参也会被移动，破坏调用方对象；
- 对非模板参数用 `std::forward`（`T` 未推导）导致编译错误；
- 转发引用与普通 `const T&` 混淆，丢失移动优化机会。

> 模板推导细节（引用折叠、`auto&&`）在 W1-04 模板与完美转发专题展开；本处只建立"移动语义如何穿过模板层"的直觉。

## 4. 示例：本机计数实验

实验源码 `evidence/labs/cpp-move/src/move_counter.cpp`，用带计数器的类型观察 5 组场景：

1. `vector` 扩容（`noexcept` 移动类型，不预分配）；
2. 相同操作但 `reserve(N)`；
3. 只有拷贝、无移动的类型；
4. 返回值优化（NRVO / 强制省略 / `return std::move`）；
5. moved-from 状态（超 SSO 长度的字符串）。

运行方式与完整原始输出见 [evidence/labs/cpp-move/README.md](../../../evidence/labs/cpp-move/README.md)。

### 4.1 预期输出与断言

实验的验收断言（运行 `build_run.ps1` 后核对）：

| 断言 | 预期 | 验证内容 |
| --- | --- | --- |
| 测试 1 `copies == 0` | 成立 | `noexcept` 移动类型扩容全程零拷贝 |
| 测试 1 `moves > 10000` | 成立（34284） | 扩容产生额外搬移 |
| 测试 2 `moves == 10000` 且 `capacity == 10000` | 成立 | `reserve` 消除扩容搬移 |
| 测试 3 `copies == moves(测试1)` | 成立（34284） | 无移动类型扩容退化为复制 |
| 测试 4 NRVO/临时对象 `moves == 0` | 成立 | 省略生效 |
| 测试 4 反模式 `moves == 1` | 成立 | `return std::move(t)` 多一次移动 |
| 测试 5 `t.size()==59, s.size()==0` | 成立（MSVC） | moved-from 状态可观测 |

### 4.2 热路径误拷贝检测

生产代码里"哪里发生了隐式拷贝"比实验更难发现，三种实用手段：

1. **临时删除拷贝构造**：怀疑某类型在热路径被拷贝时，把拷贝构造/赋值 `= delete`，编译错误会列出所有拷贝点（改完恢复）。
2. **计数器埋点（Debug 专用）**：像实验的 `Moveable` 一样给类型加 `static int copies`，用 `assert(copies == 0)` 放在帧末/Tick 末检查。
3. **UE 静态断言**：`TArray` 元素要求可平凡搬移（编译期约束）；对不可搬移类型使用 `static_assert(TIsTriviallyRelocatable_V<T>)` 提前暴露容器误用。

适用场景：AOI 更新、技能结算、移动同步、背包排序等每帧/每 Tick 执行的代码路径。

## 5. 实验结果与结论

环境：Windows x64，MSVC 14.44.35207，`cl /O2 /std:c++17 /EHsc /utf-8`，2026-08-12。

| 场景 | ctors | copies | moves | 结论 |
| --- | ---: | ---: | ---: | --- |
| `push_back`×10000，无 `reserve`（noexcept 移动） | 10000 | 0 | 34284 | 扩容全部走移动：10000 次插入 + 24284 次扩容搬移（容量按约 1.5 倍增长） |
| 相同操作 + `reserve(10000)` | 10000 | 0 | 10000 | 扩容搬移归零，只剩插入移动 |
| `push_back`×10000，仅拷贝类型 | 10000 | 34284 | 0 | 无移动构造 → 扩容退化为 24284 次深拷贝 + 10000 次插入拷贝 |
| NRVO 返回具名对象 | 1 | 0 | 0 | 优化生效，0 拷贝 0 移动 |
| C++17 返回临时对象 | 1 | 0 | 0 | 强制省略 |
| `return std::move(t)` | 1 | 0 | 1 | 反模式：破坏 NRVO，多 1 次移动 |
| 移动超 SSO 长字符串 | —— | —— | —— | `t.size()=59, s.size()=0`：缓冲区被转移，源对象有效但为空 |

定量结论：

1. **`noexcept` 移动使 34284 次操作中 0 次深拷贝**；去掉移动构造后同样操作产生 34284 次深拷贝（每元素含 64 字节字符串）。扩容搬移总量 = 扩容前容量之和（`capacity` 序列 1,2,3,4,6,…,12138）。
2. **`reserve(N)` 是消除扩容成本最直接的手段**：搬移从 24284 降到 0。
3. **RVO/NRVO 真实生效**（`/O2`）：返回路径 0 拷贝 0 移动；`return std::move(t)` 凭空增加 1 次移动。
4. **moved-from 状态可观测**：长字符串移动后 `size()==0` 但对象有效。

> 原始数据文件：`evidence/labs/cpp-move/results/move_counter_win_x64_msvc.txt`。

### 5.1 工程换算：扩容成本在服务器帧预算里的含义

把实验数据放到实时服务器场景（示意算例，非本机测量）：

- 假设 MMO 服务器每 Tick 向"待同步列表"追加 10000 个实体包对象，且对象无 `noexcept` 移动（等同实验测试 3）：每次扩容发生 24284 次深拷贝（含堆分配），P99 帧时间出现尖峰；
- 同对象声明 `noexcept` 移动（测试 1）：扩容退化为搬移，深拷贝归零；
- 提前 `Reserve(10000)`（测试 2）：搬移也归零，成本变为可预期的常数。

结论：**热路径容器"先 Reserve 后使用 + 元素可平凡搬移/`noexcept` 移动"是消除帧尖峰的必备手段**；这与 W2-01 Server Main Loop 的时间预算、W2-04 AOI 的更新列表批量发送相互印证。

## 6. 最佳实践

1. **类型设计**：优先 Rule of 0（成员全是 RAII 类型，不写任何特殊成员）；必须手写资源管理时按 Rule of 3/5 成组声明，并给移动构造/赋值加 `noexcept`。
2. **容器使用**：能预估规模就 `reserve`/`TArray::Reserve`；热路径容器元素优先可平凡搬移或 `noexcept` 移动类型。
3. **返回值**：直接 `return 局部对象;`（NRVO）或 `return T(...);`（强制省略）；**不要**写 `return std::move(t);`。
4. **显式移动**只在真正需要转移所有权时使用（`std::move` 进容器、交换、转移资源），并立即停止使用源对象的值。
5. **UE 侧**：`TArray` 元素同理；`USTRUCT` 的复制由 `CopyPropertiesForUnrealObjects` 等生成，注意 `TArray<TUniquePtr>` 之类不可复制成员的删除规则；热路径（每帧、每 Tick、AOI 更新）避免隐式深拷贝。
6. **不要把"省略"当依赖**：只有 C++17 返回临时对象是保证；NRVO 失效时用 `noexcept` 移动兜底，确保不会退回深拷贝。
7. **UE 侧元素类型**：`TArray` 元素必须是可平凡搬移类型；需要 move-only 语义时改用 `TUniquePtr` 的容器方案或指针容器，并明确所有权（关联 01 篇）。
8. **`MoveTemp` 纪律**：UE 代码用 `MoveTemp` 而非 `std::move`；不要对右值再 `MoveTemp`（有静态断言拦截），不要 `MoveTemp` 后继续读源对象。

### 6.1 反模式速查表

| 反模式 | 后果 | 正确做法 |
| --- | --- | --- |
| `return std::move(t);` | 破坏 NRVO，多一次移动 | `return t;` |
| 移动构造/赋值不标 `noexcept` | `vector` 扩容退回逐元素复制 | 加 `noexcept` |
| 声明了析构却不写移动成员 | 移动被抑制（隐式删除） | 按 Rule of 0/5 成组声明 |
| 移动后继续读源对象的值 | 未指定值（实现定义） | 销毁或重新赋值后再用 |
| 热路径 `push_back` 不 `Reserve` | 扩容期反复搬移/深拷贝 | 预估规模后 `Reserve` |
| 转发函数里 `std::move` 代替 `std::forward` | 左值实参被意外移动 | 按 `T&&` 转发用 `std::forward` |
| 大对象按值传参 | 每次调用一次深拷贝 | `const T&` 或"按值 + 移动"双路径 |
| 裸指针多所有者 | 双重释放/悬垂 | `unique_ptr`（独占）或 `shared_ptr`（共享） |
| `TArray` 存 move-only 类型 | 编译期静态断言失败 | 换 `TArray<TSharedPtr<T>>` 或指针 + 明确所有权 |
| 只写移动构造不写移动赋值 | `v[i] = std::move(x)` 退化为拷贝 | Rule of 5 成组补齐 |
| Debug 下验证拷贝数 | Release 优化不同，结论失真 | 在 `/O2` 下复测 |

## 7. FAQ

**Q1：为什么 `vector` 扩容在有移动构造时仍可能复制？**
因为标准要求 `move_if_noexcept`：移动构造未声明 `noexcept` 时，为保持强异常保证只能复制。修复：给移动构造/赋值加 `noexcept`。

**Q2：`return std::move(t)` 为什么是反模式？**
`t` 是具名局部对象，本可被 NRVO 省略；`std::move` 把它变成右值后，编译器必须走移动构造（NRVO 被抑制）。结果多一次移动，且绝不比直接 `return t;` 快。

**Q3：moved-from 的 `std::string` 还能用吗？**
能用但值未指定：只能销毁或重新赋值。读取其内容是实现定义行为，不要依赖。

**Q4：C++17 的强制省略是什么意思？**
`T f() { return T(); }` 与 `T t = T();` 中，临时对象的复制/移动构造调用被语言保证省略，直接在目标存储构造。注意 NRVO（返回具名对象）仍然不是保证。

**Q5：UE 的 `TArray` 扩容和 `std::vector` 一样吗？**
机制同源但实现不同：`TArray` 用 `RelocateConstructItems`，对可平凡搬移类型（`TIsTriviallyRelocatable`）直接内存搬运，否则调用移动/拷贝构造；`Reserve` 同样消除扩容搬移。

**Q6：自定义类型有 `std::string` 成员，需要自己写移动构造吗？**
不需要：成员移动构造自动生成且为 `noexcept`（若成员移动是 `noexcept`）。这就是 Rule of 0 的价值。

**Q7：为什么实验里 `capacity=12138` 而不是 16384？**
MSVC 的 `vector` 扩容因子约为 1.5（1,2,3,4,6,9,13,…），不是 2.0；libstdc++/libc++ 为 2.0。搬移总量随实现变化，但"`noexcept` 决定 move/copy、`reserve` 消除搬移"的定性结论跨实现成立。

**Q8：`emplace_back` 比 `push_back` 快在哪？**
`push_back(T{...})` 先构造临时对象再移动进容器（多 1 次构造 + 1 次移动）；`emplace_back(args...)` 在容器内存里直接构造（少 1 次临时对象构造与移动）。move-only 类型只能用 `emplace_back`/`push_back(std::move(x))`。

**Q9：为什么 `TArray<TUniquePtr<T>>` 编译不过？**
UE5.8 `Array.h` 静态断言要求元素可平凡搬移（`TIsTriviallyRelocatable_V`），而 `TUniquePtr` 不是。需要 move-only 语义时用 `TArray<T*>` + 明确所有权，或 `TArray<TSharedPtr<T>>` 等引用语义容器。

**Q10：`std::move` 和 UE `MoveTemp` 有区别吗？**
语义相同（都产生右值引用）；`MoveTemp` 额外带 `static_assert` 拒绝右值参数，并在编译错误信息中提示"const 对象上 MoveTemp 无效"，工程上更早暴露误用。

**Q11：移动赋值与移动构造都要写吗？**
Rule of 5 要求成组：拷贝构造/拷贝赋值/移动构造/移动赋值/析构。只写移动构造不写移动赋值，`v[i] = std::move(x)` 会退化为拷贝赋值（若可拷贝）或编译失败。

**Q12：为什么 Debug 和 Release 看到的拷贝次数不一样？**
省略（尤其 NRVO）是优化，`/Od` 与 `/O2` 下编译器决策可能不同；且 Debug 容器可能带检查逻辑。因此拷贝/移动计数实验应在 `/O2` 下执行并记录编译选项，否则结论不可复现。

**Q13：UE `TArray` 有 `capacity()` 吗？**
没有；对应概念是 `Max()`（容量）与 `Slack`（`Max()-Num()`，空闲槽位）。`Reserve(N)` 预分配、`Shrink()` 收缩；增长路径经 `CalculateSlackReserve` 计算 slack（约 1.5 倍）。allocator 与 slack 策略在 W1-05 容器与 allocator 专题展开。

## 8. 验证与基准

- 本机实验：`powershell -NoProfile -ExecutionPolicy Bypass -File evidence/labs/cpp-move/scripts/build_run.ps1`，原始输出 `results/move_counter_win_x64_msvc.txt`（2026-08-12，MSVC 14.44.35207 x64 `/O2`）。
- 标准依据：[cppreference - Copy elision](https://en.cppreference.com/w/cpp/language/copy_elision)、[cppreference - Move constructor](https://en.cppreference.com/w/cpp/language/move_constructor)、[Rule of three/five/zero](https://en.cppreference.com/w/cpp/language/rule_of_three)。
- UE 侧验证入口（本机已核对）：`Engine\Source\Runtime\Core\Public\Containers\Array.h`（`TIsTriviallyRelocatable_V` 静态断言、`RelocateConstructItems` 批量搬移、`CalculateSlackReserve` 增长）；`Engine\Source\Runtime\Core\Public\Templates\UnrealTemplate.h`（`MoveTemp` 定义）。源码深度分析见 [12-引擎源码分析](../../../游戏知识/12-引擎源码分析/README.md) 容器内存专题。
- 升级 L4 计划：将实验扩展为耗时 Benchmark（`reserve` 前后、move vs copy 类型的 `push_back` 吞吐与 P50/P95/P99），并与 UE `TArray` 对照。

跨平台验证计划（Linux 环境）：

```bash
g++ -O2 -std=c++17 -o move_counter evidence/labs/cpp-move/src/move_counter.cpp
./move_counter   # 预期：定性结论一致；capacity 序列不同（libstdc++ 因子 2.0）
```

实验源码只依赖标准库，可在 libstdc++/libc++ 直接编译；`-fno-elide-constructors` 可关闭非强制省略，用于观察 NRVO 失效时的拷贝/移动差异。

## 9. 关联阅读

- [01-C++对象生命周期与RAII](01-C++对象生命周期与RAII.md)：所有权、异常安全与智能指针边界。
- [Evidence · cpp-move](../../../evidence/labs/cpp-move/README.md)：本实验的完整设计与原始数据。
- [00-计算机与工程基础](../../../00_Index/学习路线/编程与计算机基础.md)：本层规划；后续 W1-05 容器与 allocator、W1-03 对象布局。
- [游戏知识/12-引擎源码分析](../../../游戏知识/12-引擎源码分析/README.md)：`TArray`/容器内存源码深度。
- [游戏服务端/01-架构与网络](../../../00_Index/学习路线/网络与游戏服务端.md)：服务端热路径避免隐式拷贝的实践。
