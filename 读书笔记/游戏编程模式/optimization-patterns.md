---
type: Reference
title: "优化模式（Optimization Patterns）"
description: "利用现代硬件物理特性（CPU缓存行、内存对齐、指令流水线）榨取极限算力，保障丝滑的实时帧率。"
tags:
  - game-programming-patterns
  - design-patterns
  - cpp
status: stable
verified: []
maturity: L2
updated: 2026-09-14
---

# 优化模式（Optimization Patterns）

> **章节**：优化模式（Optimization Patterns）  
> **原著**：Robert Nystrom (Bob Nystrom) · 《Game Programming Patterns》  
> **导航**：[目录](README.md) ｜ [上一章：服务定位器](service-locator.md) ｜ [下一章：数据局部性](data-locality.md)

---

虽然越来越快的硬件浪潮已把大部分软件托举到无需为性能担忧的高度，游戏却是少数例外之一。
玩家总是想要更丰富、更真实、更激动人心的体验。
屏幕上挤满了争抢玩家注意力——还有金钱——的游戏，而把硬件推到极限的游戏往往获胜。

为性能而优化是一门高深的艺术，触及软件的方方面面。
底层程序员精通硬件架构中数不清的怪癖。同时，算法研究者们争相用数学证明谁的方法最高效。

这里，我简要介绍几个常用于加速游戏的中间层模式。
[数据局部性](data-locality.md)带你了解现代计算机的内存层次结构，以及如何利用它获得优势。
[脏标识](dirty-flag.md)帮你避开不必要的计算。
[对象池](object-pool.md)帮你避开不必要的内存分配。
[空间分区](spatial-partition.md)加速虚拟世界及其居民在空间中的布局。

## 模式

* [数据局部性](data-locality.md)
* [脏标识](dirty-flag.md)
* [对象池](object-pool.md)
* [空间分区](spatial-partition.md)

---

> **导航**：[目录](README.md) ｜ [上一章：服务定位器](service-locator.md) ｜ [下一章：数据局部性](data-locality.md)
