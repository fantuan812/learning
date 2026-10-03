---
type: Evidence
title: "Evidence · cpp-move：Copy/Move 计数实验"
status: stable
verified: []
maturity: L0
---
# Evidence · cpp-move：Copy/Move 计数实验

> 状态：已执行（2026-08-12，Windows x64 / MSVC）

## 问题

1. `std::vector` 扩容时元素是复制还是移动？为什么 `noexcept` 移动决定扩容成本？
2. `reserve(N)` 能否消除扩容期的复制/移动？
3. C++17 保证的 copy elision（RVO/NRVO）在什么条件下发生？`return std::move(t)` 为什么是反模式？
4. moved-from 对象处于什么状态？

## 假设

- 提供 `noexcept` 移动构造时，`vector` 扩容会移动旧元素；没有移动构造（或移动可能抛异常）时退化为逐元素复制。
- 预分配容量可消除扩容搬移；扩容搬移总量 = 扩容时旧容量之和。
- NRVO 是优化（非保证）；返回临时对象从 C++17 起是保证省略；`return std::move(t)` 会破坏 NRVO 产生一次移动。
- moved-from 状态有效但未指定：MSVC 下长字符串被移动后 `size()==0`。

## 环境

- 系统：Windows，x64
- 编译器：MSVC 2022 Build Tools（`VC\Tools\MSVC\14.44.35207`），`cl /O2 /std:c++17 /EHsc /utf-8 /W4`
- 优化：`/O2`（Release）；未开 LTCG/AVX

## 运行方式

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build_run.ps1
```

## 输入

- 源码：`src/move_counter.cpp`（计数器结构体 + 5 组测试）
- 测试规模：`push_back` 10000 次，元素为 64 字节 `std::string` 载荷

## 指标

- 构造次数（ctors）、拷贝次数（copies）、移动次数（moves）、最终 `capacity`

## 原始结果

`results/move_counter_win_x64_msvc.txt`（2026-08-12，MSVC 14.44.35207 x64 /O2 /std:c++17）：

```text
== 1) vector 扩容：noexcept 移动类型（先 reserve(1)，再 push_back 10000 次）==
push_back x10000: capacity=12138, ctors=10000, copies=0, moves=34284

== 2) 相同操作但先 reserve(N)：扩容消失 ==
push_back x10000 (reserve): capacity=10000, ctors=10000, copies=0, moves=10000

== 3) 只有拷贝的类型（无移动构造）：扩容退化为逐元素复制 ==
push_back x10000 (CopyOnly): capacity=12138, ctors=10000, copies=34284

== 4) 返回值优化 ==
NRVO 返回具名对象: ctors=1, copies=0, moves=0
C++17 返回临时对象: ctors=1, copies=0, moves=0
反模式 return std::move(t): ctors=1, copies=0, moves=1

== 5) moved-from 状态（长字符串超过 SSO 长度，移动才会真正转移缓冲区）==
move 后: t.size()=59, s.size()=0（实现定义：MSVC 下通常为空，但必须保持有效、可析构、可赋值）
```

## 结论

1. **noexcept 移动让扩容变成搬移**：测试 1 中 10000 次插入 + 24284 次扩容搬移（`capacity` 按约 1.5 倍增长：1→2→3→4→6→…→12138，扩容搬移总量 = 扩容前容量之和），`copies=0`。
2. **`reserve(N)` 把 moves 从 34284 降到 10000**：消除全部扩容搬移，只剩每次插入的 1 次移动。
3. **没有移动构造的类型扩容退化为复制**：CopyOnly 产生 34284 次深拷贝（含 64 字节字符串），成本显著高于移动。
4. **RVO/NRVO**：返回具名对象（NRVO）与返回临时对象（C++17 保证省略）均为 0 次拷贝/移动；`return std::move(t)` 强制产生 1 次移动，是反模式。
5. **moved-from 状态**：长字符串被移动后 `s.size()==0`，但对象仍有效、可析构、可赋值——移动后的对象只能被"销毁或重新赋值"，读取其值属于未指定行为。

## 局限

- 计数是 Debug 可观测的确定性数据，不是耗时 Benchmark；耗时对比请见后续 `vector vs list` 与池化 allocator 实验。
- MSVC 的扩容因子（约 1.5）与 libstdc++（2.0）/libc++（2.0）不同，搬移总量会随实现变化；结论的定性部分（noexcept 决定 move/copy、reserve 消除搬移）跨实现成立。
- `/O2` 下 NRVO 生效；`/Od` 或复杂控制流下 NRVO 可能失效（C++17 保证省略仅适用于返回临时对象）。

## 关联知识文档

- [02-Copy-Move与值语义](../../../知识/01-编程与计算机基础/C%2B%2B语言与对象模型/02-Copy-Move与值语义.md)
- [01-C++对象生命周期与RAII](../../../知识/01-编程与计算机基础/C%2B%2B语言与对象模型/01-C%2B%2B对象生命周期与RAII.md)（异常安全与所有权背景）
