---
type: Evidence
title: "Evidence · cpp-move：Copy/Move 计数实验"
status: stable
verified: []
maturity: L2
updated: 2026-10-05
sources:
  - id: cpp17-n4659
    title: "C++17 工作草案 N4659（2017-03-21）"
    resource: "https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2017/n4659.pdf"
---
# Evidence · cpp-move：Copy/Move 计数实验

> 历史状态：已执行（2026-08-12，Windows x64 / MSVC）；下方输出是保留的原记录，本次未重跑。
> 本次校订：2026-10-05，静态读取原源码、runner、raw 及固定版 C++17 条款，纠正解释；[知识正文](../../../知识/01-编程与计算机基础/C%2B%2B语言与对象模型/02-Copy-Move与值语义.md)另有完整新短例与实际结果。
> 知识成熟度：L2。原 L0 收集入口现已完成“历史材料静态核对与解释”这一主要承诺；不是旧基准重新执行、全源码认证或跨平台性能结论。`verified: []` 保留，没有新增人工验证事件。

## 问题与实验能回答的范围

1. 给定类型及工具链的 `std::vector` 插入与扩容分别产生多少次 copy/move？为什么仅看 `noexcept` 不足以判断所有类型？
2. 提前 `reserve(N)` 后，在不超容量的这组追加中，能否避免旧元素重分配？
3. 同型 prvalue 的 C++17 保证与可选 NRVO 怎样区分？这里 `return std::move(t)` 选中了什么？
4. 本次被移动 string 实际打印什么？哪些是类型保证，哪些仅是样本状态？

旧源码只比较 `Moveable`（copy + noexcept move）与 `CopyOnly`（用户声明 copy，因此没有隐式 move）。它没有“可 copy 且 move 可能抛”和“不可 copy 且 move 可能抛”的对照，也没有抛异常后的状态检查。因此不能由两组计数证明“只有 noexcept 才允许 vector 移动”。

## 读数前先区分规则与观察

- `push_back(T&&)` 构造新元素与重分配迁移旧元素是不同路径。库须满足具体操作的异常保证，标准不强制调用 `move_if_noexcept`；copy 不可用时仍可移动潜在抛异常的类型，但该 move 真抛时保证有例外
- `reserve(n)` 成功后容量至少为 n，不必精确相等；不超容量的后续插入不重分配旧元素，仍要构造新元素、可能分配其载荷。累计搬移按各次重分配的旧 size 求和，本程序容量耗尽才扩容时旧 size 等于旧 capacity
- `T f(){ return T{}; }` 这种 C++17 同型 prvalue 直接构造结果对象；NRVO 可选。`return std::move(t)` 排除这里的 NRVO 资格，但对别的类型可 copy 或编译失败，不普遍保证一次 move
- 标准库 moved-from 默认有效但未指定，除另有规定；string 的 size/empty/clear/赋值可按合同使用，front/索引访问仍有前置条件。本历史记录的源为空不是通用断言

依据：[vector.capacity](https://timsong-cpp.github.io/cppwp/n4659/vector.capacity)、[vector.modifiers](https://timsong-cpp.github.io/cppwp/n4659/vector.modifiers)、[move_if_noexcept](https://timsong-cpp.github.io/cppwp/n4659/forward)、[copy elision](https://timsong-cpp.github.io/cppwp/n4659/class.copy.elision)、[同型 prvalue 初始化](https://timsong-cpp.github.io/cppwp/n4659/dcl.init)。版本为 2017-03-21 N4659 工作草案，2026-10-05 核对。

## 历史环境

- 系统：Windows，x64
- 编译器：MSVC 2022 Build Tools（`VC\Tools\MSVC\14.44.35207`），`cl /O2 /std:c++17 /EHsc /utf-8 /W4`
- 优化：`/O2`（Release）；历史记录未开 LTCG/AVX
- 原运行日期：2026-08-12；本次没有访问该 Windows 工具链或认证另一台机器的安装

## 历史运行方式

以下命令原样保留，应在实验目录执行。原 [runner](scripts/build_run.ps1) 使用固定的 Windows Build Tools 路径，并覆盖已有结果文件；若需再现，请先使用单独副本并另存新输出，避免覆盖历史原件。本次没有执行该命令。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build_run.ps1
```

## 输入与指标

- [源码](src/move_counter.cpp)：计数器结构体 + 5 组测试
- 测试规模：`push_back` 10000 次，计数类型元素为 64 字节 `std::string` 载荷
- 测试 1、3 实际先 `reserve(1)`；测试 2 先 `reserve(10000)`
- 构造次数（ctors）、拷贝次数（copies）、移动次数（moves）、最终 capacity；这些不是耗时、内存分配数或帧延迟
- string 测试打印目标与源的 size，没有缓冲区地址/SSO 分界测量

## 原始结果（2026-08-12）

[move_counter_win_x64_msvc.txt](results/move_counter_win_x64_msvc.txt)（MSVC 14.44.35207 x64 /O2 /std:c++17），以下围栏保持历史字节不变。

**紧邻纠错：** 块内“超过 SSO 长度，移动才会真正转移缓冲区”和“实现定义”是旧标签，不是本次认可的标准规则。短字符串也能调用移动构造；仅凭两个 size 不能证明缓冲区归属。源 string 的默认合同是有效但未指定，并非“读取合法查询是实现定义/非法”。“反模式”一行的一次 move 也只属于这个类型/构建。[标准库 moved-from](https://timsong-cpp.github.io/cppwp/n4659/lib.types.movedfrom)、[有效状态](https://timsong-cpp.github.io/cppwp/n4659/defns.valid)

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

## 这份历史输出支持的结论

1. 测试 1 记录 `ctors=10000, copies=0, moves=34284`：本程序的 10000 次新元素移动与 24284 次累计旧元素迁移；不是每次扩容 24284 次
2. 测试 2 记录 `capacity=10000, moves=10000`，这组追加没有后续旧元素重分配。不是容量必须等于请求，也不是总成本变为常数
3. CopyOnly 记录 34284 次 copy，包含右值新元素插入与旧元素迁移。复制 string 载荷不等于每次一定有特定堆分配，更不能用计数宣称“显著更慢”；本实验未计时
4. NRVO 与同型 prvalue 均记录 0 copy/0 move，但前者为可选省略，后者是所示 C++17 上下文的规则；`return std::move(t)` 对 Moveable 记录 1 move，换成 CopyOnly 可复制
5. 原样本 `t.size()==59, s.size()==0` 保留。源可继续作满足前置条件的查询/操作，并不限于销毁或重新赋值；不能把“必须为空”写成验收断言

旧解释中的约 1.5 倍容量序列 `1→2→3→4→6→…→12138` 是实现模型说明，原 raw 只打印最终 capacity，没有记录每次增长。其它标准库/版本也不能机械承诺固定 2.0 倍。

## 保留源码中的旧注释怎样读

源码和 raw 保持原件：其中“noexcept 决定扩容成本”“NRVO 应省略”“return std::move 强制移动”“超 SSO 才真正转移”“实现定义”等标题/注释须按上文限缩；原 README 曾说“只能销毁或重新赋值”，该禁用合法查询的结论现已撤回。源码没有本文新短例中的载荷断言、异常回滚控制或三类元素完整对照。原注释不等于本次对源级实现作出全面认证。

## 本次新短例与未验证项

2026-10-05 的新验证是[正文第 3、8 节](../../../知识/01-编程与计算机基础/C%2B%2B语言与对象模型/02-Copy-Move与值语义.md)内的完整独立程序，而非修改或重跑旧实验：g++ 14.2.0 / x86_64-linux-gnu，C++17、`-Wall -Wextra -Wpedantic`，六个正例各 O0/O2/UBSan 均退出 0，NRVO 禁省略的 O0/O2 对照另退出 0；两个负例各在 O0/O2 因预期 deleted 函数诊断退出 1。NRVO 反例的 `-Wpessimizing-move` 警告保留。正文包含可复现源码、参数、输入、实际观察和标准断言边界；不把次数当成熟度依据。

未运行旧 MSVC 计数程序、自定义 allocator、不可 copy 类型的 move 真抛路径、libc++/其它平台、UE/UHT/PIE、耗时或 P50/P95/P99。O0 和 O2 都可提供记录清楚的观察，Release 不是语义正确性的开关；NRVO 与增长计数的差异不能直接判失败。未来性能实验须单独设计负载与测量，当前不宣称已有后续耗时证据。

## 关联知识文档

- [02-Copy-Move与值语义](../../../知识/01-编程与计算机基础/C%2B%2B语言与对象模型/02-Copy-Move与值语义.md)
- [01-C++对象生命周期与RAII](../../../知识/01-编程与计算机基础/C%2B%2B语言与对象模型/01-C%2B%2B对象生命周期与RAII.md)（异常安全与所有权背景）
