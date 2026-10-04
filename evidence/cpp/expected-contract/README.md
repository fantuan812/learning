---
type: Evidence
title: std::expected 的内嵌存储与异常边界实验
description: 用 C++23 实验区分包装对象、载荷资源、布局与异常传播。
status: draft
verified: []
maturity: L2
updated: 2026-10-04
sources:
  - title: C++23 Working Draft N4950
    resource: https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2023/n4950.pdf
  - title: WG21 P0323R12 — std::expected
    resource: https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2022/p0323r12.html
---

# std::expected 的内嵌存储与异常边界实验

对应[异常、类型系统与标准库实现](../../../知识/01-编程与计算机基础/C++语言与对象模型/02-异常、类型系统与标准库实现.md) §3.1。内嵌载荷对象、载荷拥有的资源、包装对象的存储期和异常行为必须分别分析。

## 复现

需要 Python 3、支持 C++23 的编译器与标准库，并启用异常。runner 仅使用 Python 标准库，采用 GCC/Clang 风格命令参数。在仓库根运行：

~~~sh
python3 -B evidence/cpp/expected-contract/run_expected_contract.py --cxx g++-14
~~~

默认创建新的系统临时目录并打印位置。也可指定 `--out 新目录路径`；已有目录即使为空也会被拒绝，退出 2。不要将归档目录作为新运行的输出位置。

实验分别以 `-O0`、`-O2` 和 `-Wall -Wextra -Wpedantic -Werror` 编译、执行。仅两次编译和执行全部成功才退出 0；编译或执行失败返回非零，不将缺少编译器或功能当作通过。报告和日志保留，临时二进制会清理。`report.json` 记录真实命令、退出状态、编译器版本、目标平台、Python 版本及源码和 runner 的 SHA-256。

## 检查内容

每种优化级别有 28 项运行期检查：

- 4 项：静态、线程、自动及动态存储期的包装对象
- 10 项：成功值和错误值各使用一个带独立资源计数器的 PMR vector，检查状态、内容、资源申请与释放
- 8 项：`T` / `E` 构造异常的传播、已完成构造对象的清理、未完成构造载荷本体不执行析构
- 6 项：显式业务错误分支、成功状态 `.value()`，以及错误状态 `.value()` 的异常类型、错误码和调用者清理

另有两条编译期断言和三组布局日志。布局数字不作跨实现相等或固定 `+8` 断言。resource 只计数显式采用它的载荷请求，转发给 `std::pmr::new_delete_resource()`，没有替换全局分配器。

## 实际观测

[2026-10-04 原始报告](observed-gcc14-2026-10-04/report.json)、[-O0 输出](observed-gcc14-2026-10-04/O0.stdout.txt)、[-O2 输出](observed-gcc14-2026-10-04/O2.stdout.txt)来自同一次真实调用。相邻编译/运行 stdout、stderr 文件完整保留；报告中的临时路径是原始现场路径。

实测为 Debian g++-14 14.2.0 / libstdc++14，x86_64 Linux；`__cplusplus=202302`、`__cpp_lib_expected=202211`、`__GLIBCXX__=20250315`。两种优化级别各 28 项、零失败，编译与运行 stderr 均为空。

- 三组包装大小为 2、8、128 字节，最大载荷为 1、4、64 字节；差额为 1、4、64 字节
- 各含 1024 个 int 的成功/错误 vector 分别申请一次 4096 字节，随后各释放一次
- 两种构造失败分别使已构造成员和调用者探针在异常传播期间析构，未完成构造的载荷本体析构次数为零
- 显式错误分支读到 17，成功 `.value()` 得到 42；错误状态 `.value()` 抛出 `bad_expected_access<int>` 并清理调用者探针

这些是本次实现的观测，不是统一大小、分配次数或物理栈地址保证。

## 两项 runner 安全回归

~~~sh
python3 -B evidence/cpp/expected-contract/test_runner_safety.py --cxx g++-14
~~~

1. 已有空目录和含标记文件的目录均必须拒绝，退出 2，内容不变
2. 将同字节 runner 复制到独立夹具，配上明确 `#error` 的源码，用真正的 g++ 编译；两种优化级别均须编译失败，runner 退出 1、报告失败且没有执行阶段

回归不修改实验源码，保留诊断并打印目录。缺少真实编译器会失败；没有用不存在的命令或伪造退出码代替编译错误。

## 标准依据与适用边界

- [P0323R12 §5.7](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2022/p0323r12.html)和 [N4950 §22.8.6.1](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2023/n4950.pdf#page=749)给出载荷对象内嵌的约束；载荷拥有的动态资源与该对象自身存储不同。这条标准约束不靠地址测量证明
- [N4950 §22.8.6.2](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2023/n4950.pdf#page=751)允许载荷初始化异常传播；[§22.8.6.6](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2023/n4950.pdf#page=756)规定 `.value()` 错误路径。错误值复制/移动本身也可能抛出。显式检查状态仍适合传递可预期的业务失败
- [对象大小与填充](https://eel.is/c++draft/expr.sizeof#2)、[存储期](https://eel.is/c++draft/basic.stc.general)和[栈展开](https://eel.is/c++draft/except.ctor)分别说明实现布局、创建方式与清理语义。eel.is 是持续更新的 WG21 草案镜像；本实验以固定版本 N4950 的 C++23 语义为基线

仅实测上述 GCC/libstdc++ 组合；未测试 MSVC、libc++、Unreal Engine、禁用异常构建或 sanitizer。源文件要求 `__cpp_lib_expected >= 202202L`，不测试 monadic 操作。64 字节扩展对齐需要目标实现支持。

没有测量延迟、吞吐、分支预测、ABI 指令、异常运行时分配或全进程分配，不能据此承诺实时性或“零开销”。无分配、无抛出路径仍需约束载荷类型及每项实际操作。
