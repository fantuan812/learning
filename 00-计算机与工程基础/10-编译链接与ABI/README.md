---
type: Index
title: "编译、链接与 ABI"
status: stable
verified: []
maturity: L0
---
# 编译、链接与 ABI

本目录解释源代码如何变成可执行文件，以及模块之间如何稳定调用。正文覆盖编译阶段、链接器、加载器、符号、ODR、动态库和 UE `MODULE_API`。

## 文档

- [01-编译链接与ABI全流程](01-编译链接与ABI全流程.md)：从预处理到运行时装载的系统教程。

## 学习顺序

先掌握翻译单元与符号，再阅读重定位、调用约定和动态库；最后结合 CMake、Build.cs 与崩溃转储练习。
