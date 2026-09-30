---
type: Reference
title: "解耦模式（Decoupling Patterns）"
description: "切断代码模块之间的脆弱纠缠，让各子系统能够独立迭代、测试与复用，防止代码膨胀为不可收拾的巨型毛线球。"
tags:
  - game-programming-patterns
  - design-patterns
  - cpp
status: stable
verified: []
maturity: L2
updated: 2026-09-14
---

# 解耦模式（Decoupling Patterns）

> **章节**：解耦模式（Decoupling Patterns）  
> **原著**：Robert Nystrom (Bob Nystrom) · 《Game Programming Patterns》  
> **导航**：[目录](README.md) ｜ [上一章：类型对象](type-object.md) ｜ [下一章：组件模式](component.md)

---

一旦你摸到了编程语言的门道，写出你想要的代码实际上相当容易。
困难的是编写在需求*变化*时易于适应的代码。我们很少能奢侈地在打开编辑器之前就拥有完美的功能集。

让修改更轻松的强大工具是*解耦*。
当我们说两块代码“解耦”时，是指修改一块代码一般不需要修改另一块代码。
当我们修改游戏中的特性时，需要改动的代码位置越少，就越容易。

[组件模式](component.md)在单个实体内部将游戏中的不同领域彼此解耦，这个实体同时包含所有这些领域的特性。
[事件队列](event-queue.md)解耦两个相互通信的对象，无论在静态层面还是在*时间*层面。
[服务定位器](service-locator.md)让代码访问服务而无需绑定到提供该服务的代码。

## 模式

* [组件模式](component.md)
* [事件队列](event-queue.md)
* [服务定位器](service-locator.md)

---

> **导航**：[目录](README.md) ｜ [上一章：类型对象](type-object.md) ｜ [下一章：组件模式](component.md)
