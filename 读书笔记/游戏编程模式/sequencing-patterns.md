---
type: Reference
title: "序列模式（Sequencing Patterns）"
description: "游戏时间、帧率与模拟步长的掌控：让离散计算机模拟真实世界中连续流淌的时间与瞬时交互。"
tags:
  - game-programming-patterns
  - design-patterns
  - cpp
status: stable
verified: []
maturity: L2
updated: 2026-09-14
---

# 序列模式（Sequencing Patterns）

> **章节**：序列模式（Sequencing Patterns）  
> **原著**：Robert Nystrom (Bob Nystrom) · 《Game Programming Patterns》  
> **导航**：[目录](README.md) ｜ [上一章：状态模式](state.md) ｜ [下一章：双缓冲模式](double-buffer.md)

---

电子游戏之所以令人兴奋，很大程度上是因为它们把我们带到别的地方。
在几分钟里（或者，咱们对自己诚实点，会长得多），我们成为虚拟世界的居民。
创造这些世界是游戏程序员最顶级的乐趣之一。

大多数游戏世界都具有的一个特征是*时间*——人造世界以自己的节奏生活、呼吸。
作为世界的构建者，我们必须发明时间，打造驱动游戏这座大钟的齿轮。

本篇的模式正是做这件事的工具。
[游戏循环](game-loop.md)是时钟围绕其旋转的中心轴。
对象通过[更新方法](update-method.md)来聆听时钟的滴答声。
我们可以用[双缓冲模式](double-buffer.md)把计算机的顺序本质隐藏在瞬间快照构成的假象之后，这样世界看起来就在同步更新。

## 模式

* [双缓冲模式](double-buffer.md)
* [游戏循环](game-loop.md)
* [更新方法](update-method.md)

---

> **导航**：[目录](README.md) ｜ [上一章：状态模式](state.md) ｜ [下一章：双缓冲模式](double-buffer.md)
