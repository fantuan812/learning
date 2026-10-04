---
type: Index
title: "C++ 语言与标准库契约实验"
status: stable
verified: []
maturity: L0
---

# C++ 语言与标准库契约实验

实验将标准约束、实现观测与未验证边界分开。原始输出保留编译器、参数、源码指纹与执行状态。

- [expected 内嵌存储与异常边界](expected-contract/README.md)：C++23，GCC/libstdc++14，O0/O2 各 28 项功能检查；不作跨实现大小或性能保证

- [原子发布、对象池与类型化解码](foundation-contracts/README.md)：C++23，GCC/libstdc++14，O0/O2/UBSan 功能检查与隔离负例；不作原子性能、任意分配器或生产网络协议保证

[证据总览](../README.md) · [编程与计算机基础](../../知识/01-编程与计算机基础/README.md)
